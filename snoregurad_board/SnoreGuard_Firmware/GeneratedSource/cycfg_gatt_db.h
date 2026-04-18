/*******************************************************************************
 * File Name: cycfg_gatt_db.h
 *
 * Description: GATT database definitions for SnoreGuard Sleep Monitor service.
 *
 * Service: Sleep Monitor (custom 128-bit UUID)
 *   - Time Sync       (Write Only, 4 bytes) : phone → device epoch timestamp
 *   - Log Transfer    (Notify,     7 bytes) : device → phone, 1 event/notify
 *   - Haptic Intensity(Read/Write, 1 byte)  : haptic motor duty level 0-4
 *
 * Binary event packet (7 bytes, little-endian):
 *   [timestamp:4][duration_s:1][haptic_success:1][haptic_flag:1]
 *******************************************************************************/

#if !defined(CYCFG_GATT_DB_H)
#define CYCFG_GATT_DB_H

#include "stdint.h"

/* Standard BLE 16-bit UUIDs */
#define __UUID_SERVICE_GENERIC_ACCESS                   0x1800
#define __UUID_CHARACTERISTIC_DEVICE_NAME               0x2A00
#define __UUID_CHARACTERISTIC_APPEARANCE                0x2A01
#define __UUID_SERVICE_GENERIC_ATTRIBUTE                0x1801
#define __UUID_CHARACTERISTIC_SERVICE_CHANGED           0x2A05
#define __UUID_DESCRIPTOR_CLIENT_CHARACTERISTIC_CONFIGURATION  0x2902

/* Sleep Monitor Service (128-bit, little-endian byte array)
 * Base UUID: E0-1F-56-98-4B-21-47-10-A0-F6-00-11-22-33-44-55
 * Time Sync:        first byte 0xE1
 * Log Transfer:     first byte 0xE2
 * Haptic Intensity: first byte 0xE3
 */
#define __UUID_SERVICE_SLEEP_MONITOR \
    0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0xF6, 0xA0, \
    0x10, 0x47, 0x21, 0x4B, 0x98, 0x56, 0x1F, 0xE0

#define __UUID_CHARACTERISTIC_SLEEP_MONITOR_TIME_SYNC \
    0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0xF6, 0xA0, \
    0x10, 0x47, 0x21, 0x4B, 0x98, 0x56, 0x1F, 0xE1

#define __UUID_CHARACTERISTIC_SLEEP_MONITOR_LOG_TRANSFER \
    0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0xF6, 0xA0, \
    0x10, 0x47, 0x21, 0x4B, 0x98, 0x56, 0x1F, 0xE2

#define __UUID_CHARACTERISTIC_SLEEP_MONITOR_HAPTIC_INTENSITY \
    0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0xF6, 0xA0, \
    0x10, 0x47, 0x21, 0x4B, 0x98, 0x56, 0x1F, 0xE3

/* --- Attribute Handles --- */

/* Generic Access Service */
#define HDLS_GAP                                            0x0001
#define HDLC_GAP_DEVICE_NAME                                0x0002
#define HDLC_GAP_DEVICE_NAME_VALUE                          0x0003
#define MAX_LEN_GAP_DEVICE_NAME                             0x000C  /* "SnoreGuard" = 10 chars + null */
#define HDLC_GAP_APPEARANCE                                 0x0004
#define HDLC_GAP_APPEARANCE_VALUE                           0x0005
#define MAX_LEN_GAP_APPEARANCE                              0x0002

/* Generic Attribute Service */
#define HDLS_GATT                                           0x0006
#define HDLC_GATT_SERVICE_CHANGED                           0x0007
#define HDLC_GATT_SERVICE_CHANGED_VALUE                     0x0008
#define MAX_LEN_GATT_SERVICE_CHANGED                        0x0004
#define HDLD_GATT_SERVICE_CHANGED_CLIENT_CHAR_CONFIG        0x0009
#define MAX_LEN_GATT_SERVICE_CHANGED_CLIENT_CHAR_CONFIG     0x0002

/* Sleep Monitor Service */
#define HDLS_SLEEP_MONITOR                                  0x000A

/* Time Sync Characteristic (Write Only, 4 bytes) */
#define HDLC_SLEEP_MONITOR_TIME_SYNC                        0x000B
#define HDLC_SLEEP_MONITOR_TIME_SYNC_VALUE                  0x000C
#define MAX_LEN_SLEEP_MONITOR_TIME_SYNC                     0x0004  /* uint32 epoch */

/* Log Transfer Characteristic (Notify, 7 bytes per event) */
#define HDLC_SLEEP_MONITOR_LOG_TRANSFER                     0x000D
#define HDLC_SLEEP_MONITOR_LOG_TRANSFER_VALUE               0x000E
#define MAX_LEN_SLEEP_MONITOR_LOG_TRANSFER                  0x0007  /* 7-byte event packet */
#define HDLD_SLEEP_MONITOR_LOG_TRANSFER_CLIENT_CHAR_CONFIG  0x000F
#define MAX_LEN_SLEEP_MONITOR_LOG_TRANSFER_CCCD             0x0002

/* Haptic Intensity Characteristic (Read/Write, 1 byte, level 0-4 = 20-100%) */
#define HDLC_SLEEP_MONITOR_HAPTIC_INTENSITY                 0x0010
#define HDLC_SLEEP_MONITOR_HAPTIC_INTENSITY_VALUE           0x0011
#define MAX_LEN_SLEEP_MONITOR_HAPTIC_INTENSITY              0x0001

/* --- Lookup Table Type --- */
typedef struct
{
    uint16_t handle;
    uint16_t max_len;
    uint16_t cur_len;
    uint8_t  *p_data;
} gatt_db_lookup_table_t;

/* --- External Declarations --- */
extern const uint8_t  gatt_database[];
extern const uint16_t gatt_database_len;
extern gatt_db_lookup_table_t app_gatt_db_ext_attr_tbl[];
extern const uint16_t app_gatt_db_ext_attr_tbl_size;

/* GAP */
extern uint8_t  app_gap_device_name[];
extern const uint16_t app_gap_device_name_len;
extern uint8_t  app_gap_appearance[];
extern const uint16_t app_gap_appearance_len;

/* GATT */
extern uint8_t  app_gatt_service_changed[];
extern const uint16_t app_gatt_service_changed_len;
extern uint8_t  app_gatt_service_changed_client_char_config[];
extern const uint16_t app_gatt_service_changed_client_char_config_len;

/* Sleep Monitor: Time Sync */
extern uint8_t  app_time_sync_value[];
extern const uint16_t app_time_sync_value_len;

/* Sleep Monitor: Log Transfer */
extern uint8_t  app_log_transfer_event[];        /* 7-byte event packet buffer */
extern const uint16_t app_log_transfer_event_len;
extern uint8_t  app_log_transfer_cccd[];
extern const uint16_t app_log_transfer_cccd_len;

/* Sleep Monitor: Haptic Intensity */
extern uint8_t  app_haptic_intensity_value[];
extern const uint16_t app_haptic_intensity_value_len;

#endif /* CYCFG_GATT_DB_H */
