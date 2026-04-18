/*******************************************************************************
 * File Name: app_bt_event_handler.h
 *
 * Description: Public interface of app_bt_event_handler.c for SnoreGuard.
 *******************************************************************************/

#ifndef SOURCE_APP_BT_APP_BT_EVENT_HANDLER_H_
#define SOURCE_APP_BT_APP_BT_EVENT_HANDLER_H_

#include "wiced_bt_dev.h"
#include "wiced_bt_types.h"

/*******************************************************************************
 * Structures
 *******************************************************************************/
typedef struct
{
    wiced_bt_device_address_t remote_addr;   /* remote peer device address  */
    uint32_t  timer_count_s;                 /* seconds since start         */
    uint32_t  timer_count_ms;                /* milliseconds since start    */
    uint16_t  conn_id;                       /* BLE connection ID (0=none)  */
    uint16_t  peer_mtu;                      /* negotiated MTU              */
    uint8_t   flag_indication_sent;          /* waiting for indication ack  */
    uint8_t   num_to_send;                   /* pending messages counter    */
} hello_sensor_state_t;

/*******************************************************************************
 * Variable Declarations
 *******************************************************************************/
extern hello_sensor_state_t hello_sensor_state;
extern uint8_t bondindex;

/*******************************************************************************
 * Function Prototypes
 *******************************************************************************/
wiced_result_t app_bt_management_callback(
    wiced_bt_management_evt_t event,
    wiced_bt_management_evt_data_t *p_event_data);

void app_bt_application_init(void);
void app_bt_adv_stop_handler(void);

/* Called from app_bt_gatt_handler.c on BLE connection events */
void app_bt_on_connection_up(void);
void app_bt_on_connection_down(void);
void app_bt_on_time_sync_received(void);

#endif /* SOURCE_APP_BT_APP_BT_EVENT_HANDLER_H_ */
