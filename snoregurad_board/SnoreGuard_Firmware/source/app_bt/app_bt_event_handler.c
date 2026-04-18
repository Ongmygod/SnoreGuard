/*******************************************************************************
 * File Name: app_bt_event_handler.c
 *
 * Description: BLE management event handler for SnoreGuard.
 *
 * Key additions vs the Hello Sensor template:
 *   - BLE connect/disconnect triggers snore_on_ble_connected/disconnected()
 *   - On BTM_ENABLED_EVT, start BLE_CONNECT_TIMEOUT_MS timer (5 min):
 *       if no connection before it fires → standalone mode
 *   - On connection, start TIME_SYNC_TIMEOUT_MS timer (30 s):
 *       if no Time Sync write before it fires → fallback uptime timestamps
 *******************************************************************************/

#include "inttypes.h"
#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include "app_bt_bonding.h"
#include "app_flash_common.h"
#include "cycfg_gap.h"
#include "app_bt_utils.h"
#include "app_hw_device.h"
#include "app_bt_event_handler.h"
#include "app_bt_gatt_handler.h"
#include "snore_detect.h"
#include "snore_flash_log.h"
#include "wiced_bt_stack.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_ble.h"
#ifdef ENABLE_BT_SPY_LOG
#include "cybt_debug_uart.h"
#endif

/*******************************************************************************
 * Variable Definitions
 *******************************************************************************/

hello_sensor_state_t hello_sensor_state;
uint8_t              bondindex = 0;

/* Software timers for BLE / Time Sync timeouts */
static TimerHandle_t s_ble_connect_timeout;   /* 5 min – no BLE connection    */
static TimerHandle_t s_time_sync_timeout;     /* 30 s  – no Time Sync write   */

/*******************************************************************************
 * Private: Timeout Callbacks
 *******************************************************************************/

static void ble_connect_timeout_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    /* 5 minutes elapsed, still no BLE connection */
    snore_on_connect_timeout();

    /* Enter power-saving advertisement pattern: 30 s on / 30 s off */
    printf("[BLE] Connect timeout. Switching to slow-duty advertisement.\r\n");
    /* (advertisement is already running; just log the mode change here) */
}

static void time_sync_timeout_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    /* Connected but phone did not write Time Sync within 30 s */
    snore_on_time_sync_timeout();
}

/*******************************************************************************
 * app_bt_management_callback
 *******************************************************************************/

wiced_result_t
app_bt_management_callback(wiced_bt_management_evt_t event,
                            wiced_bt_management_evt_data_t *p_event_data)
{
    wiced_result_t result = WICED_BT_SUCCESS;
    cy_rslt_t      rslt;
    wiced_bt_dev_encryption_status_t *p_status;
    wiced_bt_ble_advert_mode_t       *p_mode;
    wiced_bt_dev_ble_pairing_info_t  *p_info;
    wiced_bt_device_address_t local_bda = {0x00, 0xA0, 0x50, 0x01, 0x44, 0x55};

    printf("Event:%s\n", get_btm_event_name(event));

    switch (event)
    {
        case BTM_ENABLED_EVT:
            if (WICED_BT_SUCCESS == p_event_data->enabled.status)
            {
                wiced_bt_set_local_bdaddr(local_bda, BLE_ADDR_PUBLIC);
                wiced_bt_dev_read_local_addr(local_bda);
                printf("Local BT Address: ");
                print_bd_address(local_bda);
                app_bt_application_init();
            }
            else
            {
                printf("Bluetooth enable failed.\n");
            }
            break;

        case BTM_DISABLED_EVT:
            break;

        case BTM_PAIRING_IO_CAPABILITIES_BLE_REQUEST_EVT:
            p_event_data->pairing_io_capabilities_ble_request.local_io_cap =
                BTM_IO_CAPABILITIES_NONE;
            p_event_data->pairing_io_capabilities_ble_request.oob_data =
                BTM_OOB_NONE;
            p_event_data->pairing_io_capabilities_ble_request.auth_req =
                BTM_LE_AUTH_REQ_SC_BOND;
            p_event_data->pairing_io_capabilities_ble_request.max_key_size = 0x10;
            p_event_data->pairing_io_capabilities_ble_request.init_keys =
                BTM_LE_KEY_PENC | BTM_LE_KEY_PID | BTM_LE_KEY_PCSRK | BTM_LE_KEY_LENC;
            p_event_data->pairing_io_capabilities_ble_request.resp_keys =
                BTM_LE_KEY_PENC | BTM_LE_KEY_PID | BTM_LE_KEY_PCSRK | BTM_LE_KEY_LENC;
            break;

        case BTM_PAIRING_COMPLETE_EVT:
            p_info = &p_event_data->pairing_complete.pairing_complete_info.ble;
            printf("Pairing Complete: %s\n",
                   get_bt_smp_status_name((wiced_bt_smp_status_t)p_info->reason));
            rslt = app_bt_update_slot_data();
            (void)rslt;
            break;

        case BTM_BLE_ADVERT_STATE_CHANGED_EVT:
            p_mode = &p_event_data->ble_advert_state_changed;
            printf("Advert State: %s\n", get_bt_advert_mode_name(*p_mode));
            if (BTM_BLE_ADVERT_OFF == *p_mode)
            {
                app_bt_adv_stop_handler();
            }
            break;

        case BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT:
            rslt = app_bt_save_device_link_keys(
                &(p_event_data->paired_device_link_keys_update));
            if (CY_RSLT_SUCCESS == rslt)
            {
                printf("Bonded to ");
                print_bd_address(p_event_data->paired_device_link_keys_update.bd_addr);
            }
            else
            {
                printf("Failed to bond!\n");
            }
            break;

        case BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT:
            result = WICED_BT_ERROR;
            bondindex = app_bt_find_device_in_flash(
                p_event_data->paired_device_link_keys_request.bd_addr);
            if (BOND_INDEX_MAX > bondindex)
            {
                memcpy(&(p_event_data->paired_device_link_keys_request),
                       &bond_info.link_keys[bondindex],
                       sizeof(wiced_bt_device_link_keys_t));
                result = WICED_BT_SUCCESS;
            }
            break;

        case BTM_LOCAL_IDENTITY_KEYS_UPDATE_EVT:
            rslt = app_bt_save_local_identity_key(
                p_event_data->local_identity_keys_update);
            if (CY_RSLT_SUCCESS != rslt) result = WICED_BT_ERROR;
            break;

        case BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT:
            app_kv_store_init();
            rslt = app_bt_read_local_identity_keys();
            if (CY_RSLT_SUCCESS == rslt)
            {
                memcpy(&(p_event_data->local_identity_keys_request),
                       &identity_keys, sizeof(wiced_bt_local_identity_keys_t));
                result = WICED_BT_SUCCESS;
            }
            else
            {
                result = WICED_BT_ERROR;
            }
            break;

        case BTM_ENCRYPTION_STATUS_EVT:
            p_status = &p_event_data->encryption_status;
            printf("Encryption status BDA:");
            print_bd_address(p_status->bd_addr);
            bondindex = app_bt_find_device_in_flash(
                p_event_data->encryption_status.bd_addr);
            if (bondindex < BOND_INDEX_MAX)
            {
                app_bt_restore_bond_data();
                app_bt_restore_cccd();
                /* Restore Log Transfer CCCD from flash */
                app_log_transfer_cccd[0] = peer_cccd_data[bondindex];
            }
            break;

        case BTM_SECURITY_REQUEST_EVT:
            wiced_bt_ble_security_grant(p_event_data->security_request.bd_addr,
                                        WICED_BT_SUCCESS);
            break;

        case BTM_BLE_CONNECTION_PARAM_UPDATE:
            printf("Conn param: interval=%d latency=%d timeout=%d\n",
                   p_event_data->ble_connection_param_update.conn_interval,
                   p_event_data->ble_connection_param_update.conn_latency,
                   p_event_data->ble_connection_param_update.supervision_timeout);
            break;

        case BTM_BLE_PHY_UPDATE_EVT:
            printf("PHY TX=%dM RX=%dM\n",
                   p_event_data->ble_phy_update_event.tx_phy,
                   p_event_data->ble_phy_update_event.rx_phy);
            break;

        default:
            printf("Unhandled BT Event: 0x%x %s\n", event,
                   get_btm_event_name(event));
            break;
    }
    return result;
}

/*******************************************************************************
 * app_bt_application_init – called once when BT stack is ready
 *******************************************************************************/

void app_bt_application_init(void)
{
    wiced_result_t result;

    /* Configure button GPIO interrupt */
    app_bt_interrupt_config();

    /* Init LEDs, haptic PWM, timers, button task */
    app_bt_hw_init();

    /* Restore bonded device info */
    app_bt_restore_bond_data();

    /* Load persisted snore event log from flash.
     * Called here (not in main) because app_kv_store_init() runs first,
     * inside BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT before this function. */
    snore_log_init();

    /* Create BLE connect-timeout timer (5 min one-shot) */
    s_ble_connect_timeout = xTimerCreate(
        "ble_conn_timeout",
        pdMS_TO_TICKS((uint32_t)BLE_CONNECT_TIMEOUT_S * 1000u),
        pdFALSE,
        NULL,
        ble_connect_timeout_cb);
    xTimerStart(s_ble_connect_timeout, 0);

    /* Create Time Sync timeout timer (30 s one-shot; started on connection) */
    s_time_sync_timeout = xTimerCreate(
        "time_sync_timeout",
        pdMS_TO_TICKS((uint32_t)BLE_TIME_SYNC_TIMEOUT_S * 1000u),
        pdFALSE,
        NULL,
        time_sync_timeout_cb);

    /* Enable pairing */
    wiced_bt_set_pairable_mode(WICED_TRUE, FALSE);

    /* Set advertisement data */
    wiced_bt_ble_set_raw_advertisement_data(CY_BT_ADV_PACKET_DATA_SIZE,
                                            cy_bt_adv_packet_data);

    /* Register GATT callback */
    wiced_bt_gatt_register(app_bt_gatt_callback);

    /* Init GATT database */
    wiced_bt_gatt_db_init(gatt_database, gatt_database_len, NULL);

    /* Start advertising */
    result = wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH,
                                           0, NULL);
    if (WICED_BT_SUCCESS != result)
    {
        printf("ERROR: Failed to start advertisements: %d\n", result);
    }

    printf("[App] SnoreGuard firmware initialized. Advertising as 'SnoreGuard'.\r\n");
}

/*******************************************************************************
 * app_bt_adv_stop_handler
 *******************************************************************************/

void app_bt_adv_stop_handler(void)
{
    wiced_result_t result;

    /* If not connected, restart low-duty advertising */
    if (0 == hello_sensor_state.conn_id)
    {
        result = wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_LOW,
                                               0, NULL);
        printf("Restarted low-duty advertisements. Result: %d\n", result);
        (void)result;
    }
}

/*******************************************************************************
 * Connection up/down – also called from gatt_handler.c
 *******************************************************************************/

void app_bt_on_connection_up(void)
{
    /* Stop the "no connection" timeout – we're connected now */
    if (s_ble_connect_timeout)
    {
        xTimerStop(s_ble_connect_timeout, 0);
    }

    /* Start 30-s Time Sync timeout */
    if (s_time_sync_timeout)
    {
        xTimerStart(s_time_sync_timeout, 0);
    }

    /* Notify snore module */
    snore_on_ble_connected();

    printf("[BLE] Connected. Waiting for Time Sync (30 s window).\r\n");
}

void app_bt_on_connection_down(void)
{
    /* Stop Time Sync timer if it's still running */
    if (s_time_sync_timeout)
    {
        xTimerStop(s_time_sync_timeout, 0);
    }

    /* Notify snore module */
    snore_on_ble_disconnected();

    /* Restart BLE connect timeout for next advertising cycle */
    if (s_ble_connect_timeout)
    {
        xTimerStart(s_ble_connect_timeout, 0);
    }

    printf("[BLE] Disconnected.\r\n");
}

/*******************************************************************************
 * Time Sync acknowledged (called by gatt_handler when phone writes epoch)
 *******************************************************************************/

void app_bt_on_time_sync_received(void)
{
    /* Stop the 30-s Time Sync timeout */
    if (s_time_sync_timeout)
    {
        xTimerStop(s_time_sync_timeout, 0);
    }
}
