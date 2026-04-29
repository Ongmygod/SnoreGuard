/*******************************************************************************
 * File Name: app_bt_gatt_handler.c
 *
 * Description: GATT event handler for SnoreGuard Sleep Monitor service.
 *
 * Handles:
 *   - Connection up/down (delegates to event_handler for timeout logic)
 *   - Time Sync Write  → parses uint32 epoch, calls snore_set_epoch_base()
 *   - Log Transfer CCCD Write → enable/disable notifications
 *   - Haptic Intensity Write  → validates range [0-4], calls snore_set_haptic_level()
 *   - Morning Sync streaming  → sends flash log one 7-byte event per notify
 *******************************************************************************/

#include "inttypes.h"
#include <FreeRTOS.h>
#include <task.h>
#include <stdlib.h>
#include <string.h>
#include "wiced_memory.h"
#include "wiced_bt_stack.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_ble.h"
#include "app_bt_bonding.h"
#include "app_flash_common.h"
#include "cycfg_gap.h"
#include "app_bt_utils.h"
#include "app_bt_event_handler.h"
#include "app_bt_gatt_handler.h"
#include "app_hw_device.h"
#include "snore_detect.h"
#include "snore_flash_log.h"
#ifdef ENABLE_BT_SPY_LOG
#include "cybt_debug_uart.h"
#endif

/*******************************************************************************
 * Morning Sync State
 * Streams snore_log events one-by-one as BLE notifications after button press.
 *******************************************************************************/

static bool     s_morning_sync_active = false;
static uint16_t s_morning_sync_idx    = 0;   /* next event index to send */
static uint16_t s_morning_sync_total  = 0;   /* snapshot of count at sync start (MUST NOT change during sync) */
static bool     s_pending_ack         = false; /* true = all events sent, awaiting app ack */

/*******************************************************************************
 * Helper: find attribute in lookup table
 *******************************************************************************/

gatt_db_lookup_table_t *app_bt_find_by_handle(uint16_t handle)
{
    for (int i = 0; i < app_gatt_db_ext_attr_tbl_size; i++)
    {
        if (app_gatt_db_ext_attr_tbl[i].handle == handle)
        {
            return &app_gatt_db_ext_attr_tbl[i];
        }
    }
    return NULL;
}

/*******************************************************************************
 * Buffer helpers
 *******************************************************************************/

void app_bt_free_buffer(uint8_t *p_buf)
{
    vPortFree(p_buf);
}

void *app_bt_alloc_buffer(int len)
{
    return pvPortMalloc(len);
}

/*******************************************************************************
 * Main GATT callback
 *******************************************************************************/

wiced_bt_gatt_status_t
app_bt_gatt_callback(wiced_bt_gatt_evt_t event,
                     wiced_bt_gatt_event_data_t *p_event_data)
{
    wiced_bt_gatt_status_t          gatt_status = WICED_BT_SUCCESS;
    uint16_t                        error_handle = 0;
    wiced_bt_gatt_attribute_request_t *p_attr_req =
        &p_event_data->attribute_request;

    switch (event)
    {
        case GATT_CONNECTION_STATUS_EVT:
            gatt_status = app_bt_gatt_conn_status_cb(
                &p_event_data->connection_status);
            break;

        case GATT_ATTRIBUTE_REQUEST_EVT:
            gatt_status = app_bt_gatt_req_cb(p_attr_req, &error_handle);
            if (gatt_status != WICED_BT_GATT_SUCCESS)
            {
                wiced_bt_gatt_server_send_error_rsp(p_attr_req->conn_id,
                                                    p_attr_req->opcode,
                                                    error_handle,
                                                    gatt_status);
            }
            break;

        case GATT_GET_RESPONSE_BUFFER_EVT:
            p_event_data->buffer_request.buffer.p_app_rsp_buffer =
                app_bt_alloc_buffer(p_event_data->buffer_request.len_requested);
            p_event_data->buffer_request.buffer.p_app_ctxt =
                (void *)app_bt_free_buffer;
            gatt_status = WICED_BT_GATT_SUCCESS;
            break;

        case GATT_APP_BUFFER_TRANSMITTED_EVT:
        {
            pfn_free_buffer_t pfn_free =
                (pfn_free_buffer_t)p_event_data->buffer_xmitted.p_app_ctxt;
            if (pfn_free)
                pfn_free(p_event_data->buffer_xmitted.p_app_data);
            gatt_status = WICED_BT_GATT_SUCCESS;

            /* After each successful transmission, send next event if syncing */
            if (s_morning_sync_active)
            {
                app_bt_morning_sync_send_next();
            }
        }
            break;

        default:
            gatt_status = WICED_BT_GATT_ERROR;
            break;
    }
    return gatt_status;
}

/*******************************************************************************
 * GATT request dispatcher
 *******************************************************************************/

wiced_bt_gatt_status_t
app_bt_gatt_req_cb(wiced_bt_gatt_attribute_request_t *p_attr_req,
                   uint16_t *p_error_handle)
{
    wiced_bt_gatt_status_t gatt_status = WICED_BT_SUCCESS;

    switch (p_attr_req->opcode)
    {
        case GATT_REQ_READ:
        case GATT_REQ_READ_BLOB:
            gatt_status = app_bt_gatt_req_read_handler(
                p_attr_req->conn_id,
                p_attr_req->opcode,
                &p_attr_req->data.read_req,
                p_attr_req->len_requested,
                p_error_handle);
            break;

        case GATT_REQ_WRITE:
        case GATT_CMD_WRITE:
            gatt_status = app_bt_gatt_req_write_handler(
                p_attr_req->conn_id,
                p_attr_req->opcode,
                &p_attr_req->data.write_req,
                p_attr_req->len_requested,
                p_error_handle);
            if ((GATT_REQ_WRITE == p_attr_req->opcode) &&
                (WICED_BT_GATT_SUCCESS == gatt_status))
            {
                wiced_bt_gatt_server_send_write_rsp(
                    p_attr_req->conn_id,
                    p_attr_req->opcode,
                    p_attr_req->data.write_req.handle);
            }
            break;

        case GATT_REQ_MTU:
            gatt_status = wiced_bt_gatt_server_send_mtu_rsp(
                p_attr_req->conn_id,
                p_attr_req->data.remote_mtu,
                CY_BT_MTU_SIZE);
            break;

        case GATT_HANDLE_VALUE_NOTIF:
            printf("[GATT] Notification sent.\n");
            break;

        case GATT_REQ_READ_BY_TYPE:
            gatt_status = app_bt_gatt_req_read_by_type_handler(
                p_attr_req->conn_id,
                p_attr_req->opcode,
                &p_attr_req->data.read_by_type,
                p_attr_req->len_requested,
                p_error_handle);
            break;

        case GATT_HANDLE_VALUE_CONF:
            printf("[GATT] Indication confirmed.\n");
            hello_sensor_state.flag_indication_sent = FALSE;
            break;

        default:
            printf("[GATT] Unhandled opcode: %d\n", p_attr_req->opcode);
            gatt_status = WICED_BT_GATT_ERROR;
            break;
    }
    return gatt_status;
}

/*******************************************************************************
 * Connection callbacks
 *******************************************************************************/

wiced_bt_gatt_status_t
app_bt_gatt_conn_status_cb(wiced_bt_gatt_connection_status_t *p_conn_status)
{
    if (p_conn_status->connected)
        return app_bt_gatt_connection_up(p_conn_status);
    else
        return app_bt_gatt_connection_down(p_conn_status);
}

wiced_bt_gatt_status_t
app_bt_gatt_connection_up(wiced_bt_gatt_connection_status_t *p_status)
{
    printf("[GATT] Connected to: ");
    print_bd_address(p_status->bd_addr);

    hello_sensor_state.conn_id = p_status->conn_id;
    memcpy(hello_sensor_state.remote_addr, p_status->bd_addr,
           sizeof(wiced_bt_device_address_t));

    /* Notify event handler to start Time Sync and connect-timeout timers */
    app_bt_on_connection_up();

#ifdef PSOC6_BLE
    if (pairing_mode == TRUE)
    {
        app_bt_add_devices_to_address_resolution_db();
        pairing_mode = FALSE;
    }
#endif
    return WICED_BT_GATT_SUCCESS;
}

wiced_bt_gatt_status_t
app_bt_gatt_connection_down(wiced_bt_gatt_connection_status_t *p_status)
{
    wiced_result_t result;

    printf("[GATT] Disconnected from: ");
    print_bd_address(p_status->bd_addr);
    printf("reason: %s\n", get_bt_gatt_disconn_reason_name(p_status->reason));

    memset(hello_sensor_state.remote_addr, 0, BD_ADDR_LEN);
    hello_sensor_state.conn_id = 0;

    /* Cancel any ongoing morning sync; log is preserved if app never acked */
    s_morning_sync_active = false;
    s_morning_sync_idx    = 0;
    s_morning_sync_total  = 0;
    s_pending_ack         = false;

    /* Notify event handler */
    app_bt_on_connection_down();

    /* Restart advertising */
    result = wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH,
                                           0, NULL);
    (void)result;
    return WICED_BT_GATT_SUCCESS;
}

/*******************************************************************************
 * Read Handler
 *******************************************************************************/

wiced_bt_gatt_status_t
app_bt_gatt_req_read_handler(uint16_t conn_id,
                             wiced_bt_gatt_opcode_t opcode,
                             wiced_bt_gatt_read_t *p_read_req,
                             uint16_t len_req,
                             uint16_t *p_error_handle)
{
    gatt_db_lookup_table_t *puAttribute;
    int   attr_len_to_copy;
    uint8_t *from;
    int   to_send;

    *p_error_handle = p_read_req->handle;

    puAttribute = app_bt_find_by_handle(p_read_req->handle);
    if (!puAttribute)
        return WICED_BT_GATT_INVALID_HANDLE;

    attr_len_to_copy = puAttribute->cur_len;
    if (p_read_req->offset >= puAttribute->cur_len)
        return WICED_BT_GATT_INVALID_OFFSET;

    to_send = MIN(len_req, attr_len_to_copy - p_read_req->offset);
    from    = ((uint8_t *)puAttribute->p_data) + p_read_req->offset;

    return wiced_bt_gatt_server_send_read_handle_rsp(conn_id, opcode,
                                                     to_send, from, NULL);
}

/*******************************************************************************
 * Write Handler – core application logic
 *******************************************************************************/

wiced_bt_gatt_status_t
app_bt_gatt_req_write_handler(uint16_t conn_id,
                              wiced_bt_gatt_opcode_t opcode,
                              wiced_bt_gatt_write_req_t *p_write_req,
                              uint16_t len_req,
                              uint16_t *p_error_handle)
{
    *p_error_handle = p_write_req->handle;
    return app_bt_set_value(p_write_req->handle,
                            p_write_req->p_val,
                            p_write_req->val_len);
}

/*******************************************************************************
 * app_bt_set_value – attribute write logic for each characteristic
 *******************************************************************************/

wiced_bt_gatt_status_t
app_bt_set_value(uint16_t attr_handle, uint8_t *p_val, uint16_t len)
{
    wiced_bt_gatt_status_t gatt_status   = WICED_BT_GATT_INVALID_HANDLE;
    wiced_bool_t           isInTable     = WICED_FALSE;
    cy_rslt_t              rslt;

    /* Locate attribute in lookup table first to bounds-check length */
    for (int i = 0; i < app_gatt_db_ext_attr_tbl_size; i++)
    {
        if (app_gatt_db_ext_attr_tbl[i].handle != attr_handle) continue;

        isInTable = WICED_TRUE;

        if (app_gatt_db_ext_attr_tbl[i].max_len < len)
        {
            return WICED_BT_GATT_INVALID_ATTR_LEN;
        }
        /* Copy raw value to the buffer (attribute-specific logic below) */
        app_gatt_db_ext_attr_tbl[i].cur_len = len;
        memcpy(app_gatt_db_ext_attr_tbl[i].p_data, p_val, len);
        gatt_status = WICED_BT_GATT_SUCCESS;

        switch (attr_handle)
        {
            /* ----------------------------------------------------------------
             * Time Sync Write
             * Phone sends current Unix epoch (uint32, little-endian, 4 bytes)
             * at the start of each sleep session.
             * ---------------------------------------------------------------- */
            case HDLC_SLEEP_MONITOR_TIME_SYNC_VALUE:
                if (len != 4)
                    return WICED_BT_GATT_INVALID_ATTR_LEN;
                {
                    uint32_t epoch = (uint32_t)p_val[0]
                                   | ((uint32_t)p_val[1] << 8u)
                                   | ((uint32_t)p_val[2] << 16u)
                                   | ((uint32_t)p_val[3] << 24u);
                    snore_set_epoch_base(epoch);
                    app_bt_on_time_sync_received();
                    printf("[GATT] Time Sync received: epoch=%lu\r\n",
                           (unsigned long)epoch);
                }
                break;

            /* ----------------------------------------------------------------
             * Log Transfer CCCD Write
             * Phone enables/disables notifications for the log stream.
             * ---------------------------------------------------------------- */
            case HDLD_SLEEP_MONITOR_LOG_TRANSFER_CLIENT_CHAR_CONFIG:
                if (len != 2)
                    return WICED_BT_GATT_INVALID_ATTR_LEN;
                app_log_transfer_cccd[0] = p_val[0];
                /* Persist CCCD for bonded device */
                peer_cccd_data[bondindex] = p_val[0] | ((uint16_t)p_val[1] << 8u);
                rslt = app_bt_update_cccd(peer_cccd_data[bondindex], bondindex);
                if (CY_RSLT_SUCCESS != rslt)
                {
                    printf("[GATT] WARN: Failed to save CCCD to flash.\r\n");
                }
                printf("[GATT] Log Transfer CCCD = 0x%02X\r\n", p_val[0]);
                break;

            /* ----------------------------------------------------------------
             * Haptic Intensity Write
             * Phone sets intensity level: 0=20%, 1=40%, 2=60%, 3=80%, 4=100%
             * ---------------------------------------------------------------- */
            case HDLC_SLEEP_MONITOR_HAPTIC_INTENSITY_VALUE:
                if (len != 1)
                    return WICED_BT_GATT_INVALID_ATTR_LEN;
                {
                    uint8_t level = p_val[0];
                    if (level > 4u)
                    {
                        printf("[GATT] WARN: Invalid haptic level %u (max 4).\r\n",
                               level);
                        return WICED_BT_GATT_ILLEGAL_PARAMETER;
                    }
                    app_haptic_intensity_value[0] = level;
                    snore_set_haptic_level(level);
                    printf("[GATT] Haptic intensity set to level %u (%u%%)\r\n",
                           level, 20u + level * 20u);
                }
                break;

            /* ----------------------------------------------------------------
             * Sync Ack Write
             * App writes 0x01 after successfully inserting all events into
             * its local SQLite database.  Only then is the on-device log cleared.
             * App writes 0x00 (or omits the write) on failure; log is preserved.
             * ---------------------------------------------------------------- */
            case HDLC_SLEEP_MONITOR_SYNC_ACK_VALUE:
                if (len != 1)
                    return WICED_BT_GATT_INVALID_ATTR_LEN;
                if (p_val[0] == 0x01 && s_pending_ack)
                {
                    printf("[MorningSync] Sync Ack received – clearing log.\r\n");
                    s_pending_ack = false;
                    snore_log_clear();
                }
                else if (p_val[0] == 0x00)
                {
                    printf("[MorningSync] Sync Nack received – log preserved.\r\n");
                    s_pending_ack = false;
                }
                else
                {
                    printf("[MorningSync] WARN: Ack received but not in pending state "
                           "(val=%u).\r\n", p_val[0]);
                }
                break;

            /* ----------------------------------------------------------------
             * Haptic Enable Write
             * App writes 0x01 to enable or 0x00 to disable the haptic motor.
             * ---------------------------------------------------------------- */
            case HDLC_SLEEP_MONITOR_HAPTIC_ENABLE_VALUE:
                if (len != 1)
                    return WICED_BT_GATT_INVALID_ATTR_LEN;
                {
                    bool enabled = (p_val[0] != 0x00);
                    app_haptic_enable_value[0] = enabled ? 0x01 : 0x00;
                    snore_set_haptic_enabled(enabled);
                    printf("[GATT] Haptic motor %s by app.\r\n",
                           enabled ? "ENABLED" : "DISABLED");
                }
                break;

            /* ----------------------------------------------------------------
             * Generic Attribute service-changed CCCD (required by BLE spec)
             * ---------------------------------------------------------------- */
            case HDLD_GATT_SERVICE_CHANGED_CLIENT_CHAR_CONFIG:
                gatt_status = WICED_BT_GATT_SUCCESS;
                break;

            default:
                gatt_status = WICED_BT_GATT_INVALID_HANDLE;
                break;
        }
        break;
    }

    if (!isInTable)
    {
        printf("[GATT] Write to unknown handle: 0x%x\n", attr_handle);
        gatt_status = WICED_BT_GATT_WRITE_NOT_PERMIT;
    }
    return gatt_status;
}

/*******************************************************************************
 * Read-by-type handler (required for GATT service discovery)
 *******************************************************************************/

wiced_bt_gatt_status_t
app_bt_gatt_req_read_by_type_handler(uint16_t conn_id,
                                     wiced_bt_gatt_opcode_t opcode,
                                     wiced_bt_gatt_read_by_type_t *p_read_req,
                                     uint16_t len_requested,
                                     uint16_t *p_error_handle)
{
    gatt_db_lookup_table_t *puAttribute;
    uint16_t last_handle  = 0;
    uint16_t attr_handle  = p_read_req->s_handle;
    uint8_t *p_rsp        = app_bt_alloc_buffer(len_requested);
    uint8_t  pair_len     = 0;
    int      used_len     = 0;

    if (!p_rsp)
        return WICED_BT_GATT_INSUF_RESOURCE;

    while (WICED_TRUE)
    {
        *p_error_handle = attr_handle;
        last_handle     = attr_handle;
        attr_handle     = wiced_bt_gatt_find_handle_by_type(attr_handle,
                                                             p_read_req->e_handle,
                                                             &p_read_req->uuid);
        if (0 == attr_handle) break;

        puAttribute = app_bt_find_by_handle(attr_handle);
        if (!puAttribute)
        {
            app_bt_free_buffer(p_rsp);
            return WICED_BT_GATT_INVALID_HANDLE;
        }

        int filled = wiced_bt_gatt_put_read_by_type_rsp_in_stream(
            p_rsp + used_len,
            len_requested - used_len,
            &pair_len,
            attr_handle,
            puAttribute->cur_len,
            puAttribute->p_data);

        if (0 == filled) break;
        used_len += filled;
        attr_handle++;
    }

    if (0 == used_len)
    {
        app_bt_free_buffer(p_rsp);
        *p_error_handle = p_read_req->s_handle;
        return WICED_BT_GATT_INVALID_HANDLE;
    }

    wiced_bt_gatt_server_send_read_by_type_rsp(conn_id, opcode, pair_len,
                                               used_len, p_rsp,
                                               (void *)app_bt_free_buffer);
    return WICED_BT_GATT_SUCCESS;
}

/*******************************************************************************
 * Morning Sync: stream flash log events as BLE notifications
 *******************************************************************************/

void app_bt_morning_sync_start(void)
{
    if (0 == hello_sensor_state.conn_id)
    {
        printf("[MorningSync] No BLE connection.\r\n");
        return;
    }
    if (!(app_log_transfer_cccd[0] & GATT_CLIENT_CONFIG_NOTIFICATION))
    {
        printf("[MorningSync] Notifications not enabled by client.\r\n");
        return;
    }

    /* Close any in-progress snore episode so it gets streamed this session. */
    snore_detect_flush_open_episode();

    /* Snapshot the count NOW. New events added by the audio task during
     * streaming must not be included — they belong to the next sync session. */
    uint16_t count = snore_log_get_count();
    printf("[MorningSync] Starting – %u events to send.\r\n", count);

    if (count == 0)
    {
        printf("[MorningSync] No events to send.\r\n");
        return;
    }

    s_morning_sync_active = true;
    s_morning_sync_idx    = 0;
    s_morning_sync_total  = count;   /* fixed upper bound for this session */

    app_bt_morning_sync_send_next();
}

void app_bt_morning_sync_send_next(void)
{
    if (!s_morning_sync_active) return;

    /* Use the snapshotted total so events added by the audio task during
     * streaming do not extend this sync session. */
    if (s_morning_sync_idx >= s_morning_sync_total)
    {
        /* All events sent — wait for app to confirm DB save before clearing */
        printf("[MorningSync] Complete. %u events sent. Awaiting app ack.\r\n",
               s_morning_sync_idx);
        s_morning_sync_active = false;
        s_morning_sync_idx    = 0;
        s_morning_sync_total  = 0;
        s_pending_ack         = true;
        /* snore_log_clear() is now called only from the Sync Ack write handler */
        return;
    }

    snore_event_t event;
    if (!snore_log_get_event(s_morning_sync_idx, &event))
    {
        /* Defensive: index is within total but get_event failed (should not happen) */
        printf("[MorningSync] WARN: get_event(%u) failed unexpectedly – aborting.\r\n",
               s_morning_sync_idx);
        s_morning_sync_active = false;
        s_morning_sync_idx    = 0;
        s_morning_sync_total  = 0;
        s_pending_ack         = true;
        return;
    }

    /* Pack 7-byte event into the GATT buffer */
    app_log_transfer_event[0] = (uint8_t)(event.timestamp        & 0xFF);
    app_log_transfer_event[1] = (uint8_t)((event.timestamp >> 8)  & 0xFF);
    app_log_transfer_event[2] = (uint8_t)((event.timestamp >> 16) & 0xFF);
    app_log_transfer_event[3] = (uint8_t)((event.timestamp >> 24) & 0xFF);
    app_log_transfer_event[4] = event.duration_s;
    app_log_transfer_event[5] = event.haptic_success;
    app_log_transfer_event[6] = event.haptic_flag;

    /* Pre-increment idx BEFORE send. The WICED stack fires
     * GATT_APP_BUFFER_TRANSMITTED_EVT synchronously inside
     * wiced_bt_gatt_server_send_notification(), which re-enters this
     * function via the handler at line ~124. If we increment AFTER send,
     * the recursive entries see stale idx, miss the cap check, and keep
     * sending until the BLE controller TX queue fills (typically 9 deep) —
     * the original "always 9 events" bug. Pre-incrementing makes the
     * cap check correct under recursion; rollback on send failure
     * preserves the retry semantics. */
    s_morning_sync_idx++;

    wiced_bt_gatt_status_t status =
        wiced_bt_gatt_server_send_notification(
            hello_sensor_state.conn_id,
            HDLC_SLEEP_MONITOR_LOG_TRANSFER_VALUE,
            app_log_transfer_event_len,
            app_log_transfer_event,
            NULL);

    if (WICED_BT_GATT_SUCCESS != status)
    {
        /* Roll back so the next buffer-transmitted callback retries this index */
        s_morning_sync_idx--;
        printf("[MorningSync] WARN: notify status %d – will retry.\r\n", status);
        if (0 == hello_sensor_state.conn_id)
        {
            s_morning_sync_active = false;
        }
    }
}
