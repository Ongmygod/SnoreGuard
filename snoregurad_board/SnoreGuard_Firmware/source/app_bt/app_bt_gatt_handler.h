/*******************************************************************************
 * File Name: app_bt_gatt_handler.h
 *
 * Description: Public interface of app_bt_gatt_handler.c for SnoreGuard.
 *******************************************************************************/

#ifndef SOURCE_APP_BT_APP_BT_GATT_HANDLER_H_
#define SOURCE_APP_BT_APP_BT_GATT_HANDLER_H_

#include "cycfg_gatt_db.h"
#include "wiced_bt_gatt.h"

/* Type for buffer-free callback */
typedef void (*pfn_free_buffer_t)(uint8_t *);

/*******************************************************************************
 * Function Prototypes
 *******************************************************************************/

/* Main GATT event callback (registered with wiced_bt_gatt_register) */
wiced_bt_gatt_status_t app_bt_gatt_callback(
    wiced_bt_gatt_evt_t event,
    wiced_bt_gatt_event_data_t *p_event_data);

/* Called from button_task to start streaming the flash log via BLE notify */
void app_bt_morning_sync_start(void);

/* Send one event notification (called repeatedly until log is exhausted) */
void app_bt_morning_sync_send_next(void);

/* Buffer helpers */
void  app_bt_free_buffer(uint8_t *p_buf);
void *app_bt_alloc_buffer(int len);

/* Internal handlers (used within this file) */
wiced_bt_gatt_status_t app_bt_gatt_req_cb(
    wiced_bt_gatt_attribute_request_t *p_attr_req,
    uint16_t *p_error_handle);

wiced_bt_gatt_status_t app_bt_gatt_conn_status_cb(
    wiced_bt_gatt_connection_status_t *p_conn_status);

wiced_bt_gatt_status_t app_bt_gatt_req_read_handler(
    uint16_t conn_id,
    wiced_bt_gatt_opcode_t opcode,
    wiced_bt_gatt_read_t *p_read_req,
    uint16_t len_req,
    uint16_t *p_error_handle);

wiced_bt_gatt_status_t app_bt_gatt_req_write_handler(
    uint16_t conn_id,
    wiced_bt_gatt_opcode_t opcode,
    wiced_bt_gatt_write_req_t *p_write_req,
    uint16_t len_req,
    uint16_t *p_error_handle);

wiced_bt_gatt_status_t app_bt_gatt_req_read_by_type_handler(
    uint16_t conn_id,
    wiced_bt_gatt_opcode_t opcode,
    wiced_bt_gatt_read_by_type_t *p_read_req,
    uint16_t len_requested,
    uint16_t *p_error_handle);

wiced_bt_gatt_status_t app_bt_gatt_connection_up(
    wiced_bt_gatt_connection_status_t *p_status);

wiced_bt_gatt_status_t app_bt_gatt_connection_down(
    wiced_bt_gatt_connection_status_t *p_status);

gatt_db_lookup_table_t *app_bt_find_by_handle(uint16_t handle);

wiced_bt_gatt_status_t app_bt_set_value(
    uint16_t attr_handle,
    uint8_t *p_val,
    uint16_t len);

#endif /* SOURCE_APP_BT_APP_BT_GATT_HANDLER_H_ */
