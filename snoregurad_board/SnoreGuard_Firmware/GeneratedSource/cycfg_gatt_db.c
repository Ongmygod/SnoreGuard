/*******************************************************************************
 * File Name: cycfg_gatt_db.c
 *
 * Description: GATT database for SnoreGuard Sleep Monitor service.
 *
 * Services:
 *   1. Generic Access (0x1800)
 *   2. Generic Attribute (0x1801)
 *   3. Sleep Monitor (128-bit custom UUID)
 *      - Time Sync       (Write, 4 bytes)  : phone sets epoch at session start
 *      - Log Transfer    (Notify, 7 bytes) : streams snore events (morning sync)
 *      - Haptic Intensity(R/W, 1 byte)     : 0-4 maps to 20%-100% PWM duty
 *******************************************************************************/

#include "cycfg_gatt_db.h"
#include "wiced_bt_uuid.h"
#include "wiced_bt_gatt.h"

/******************************************************************************
 * GATT Server Attribute Database
 ******************************************************************************/

const uint8_t gatt_database[] =
{
    /* ---- Generic Access Service ---- */
    PRIMARY_SERVICE_UUID16(
        HDLS_GAP,
        __UUID_SERVICE_GENERIC_ACCESS),

        CHARACTERISTIC_UUID16(
            HDLC_GAP_DEVICE_NAME,
            HDLC_GAP_DEVICE_NAME_VALUE,
            __UUID_CHARACTERISTIC_DEVICE_NAME,
            GATTDB_CHAR_PROP_READ,
            GATTDB_PERM_READABLE),

        CHARACTERISTIC_UUID16(
            HDLC_GAP_APPEARANCE,
            HDLC_GAP_APPEARANCE_VALUE,
            __UUID_CHARACTERISTIC_APPEARANCE,
            GATTDB_CHAR_PROP_READ,
            GATTDB_PERM_READABLE),

    /* ---- Generic Attribute Service ---- */
    PRIMARY_SERVICE_UUID16(
        HDLS_GATT,
        __UUID_SERVICE_GENERIC_ATTRIBUTE),

        CHARACTERISTIC_UUID16(
            HDLC_GATT_SERVICE_CHANGED,
            HDLC_GATT_SERVICE_CHANGED_VALUE,
            __UUID_CHARACTERISTIC_SERVICE_CHANGED,
            GATTDB_CHAR_PROP_INDICATE,
            GATTDB_PERM_NONE),

            CHAR_DESCRIPTOR_UUID16_WRITABLE(
                HDLD_GATT_SERVICE_CHANGED_CLIENT_CHAR_CONFIG,
                __UUID_DESCRIPTOR_CLIENT_CHARACTERISTIC_CONFIGURATION,
                GATTDB_PERM_READABLE | GATTDB_PERM_WRITE_REQ),

    /* ---- Sleep Monitor Service (128-bit UUID) ---- */
    PRIMARY_SERVICE_UUID128(
        HDLS_SLEEP_MONITOR,
        __UUID_SERVICE_SLEEP_MONITOR),

        /* Time Sync: Write-only, phone sends uint32 epoch at session start */
        CHARACTERISTIC_UUID128_WRITABLE(
            HDLC_SLEEP_MONITOR_TIME_SYNC,
            HDLC_SLEEP_MONITOR_TIME_SYNC_VALUE,
            __UUID_CHARACTERISTIC_SLEEP_MONITOR_TIME_SYNC,
            GATTDB_CHAR_PROP_WRITE | GATTDB_CHAR_PROP_WRITE_NO_RESPONSE,
            GATTDB_PERM_WRITE_REQ | GATTDB_PERM_WRITE_CMD),

        /* Log Transfer: Notify, device sends 7-byte event packets on morning sync */
        CHARACTERISTIC_UUID128(
            HDLC_SLEEP_MONITOR_LOG_TRANSFER,
            HDLC_SLEEP_MONITOR_LOG_TRANSFER_VALUE,
            __UUID_CHARACTERISTIC_SLEEP_MONITOR_LOG_TRANSFER,
            GATTDB_CHAR_PROP_READ | GATTDB_CHAR_PROP_NOTIFY,
            GATTDB_PERM_READABLE),

            CHAR_DESCRIPTOR_UUID16_WRITABLE(
                HDLD_SLEEP_MONITOR_LOG_TRANSFER_CLIENT_CHAR_CONFIG,
                __UUID_DESCRIPTOR_CLIENT_CHARACTERISTIC_CONFIGURATION,
                GATTDB_PERM_READABLE | GATTDB_PERM_WRITE_REQ),

        /* Haptic Intensity: Read/Write, 0=20%, 1=40%, 2=60%, 3=80%, 4=100% */
        CHARACTERISTIC_UUID128_WRITABLE(
            HDLC_SLEEP_MONITOR_HAPTIC_INTENSITY,
            HDLC_SLEEP_MONITOR_HAPTIC_INTENSITY_VALUE,
            __UUID_CHARACTERISTIC_SLEEP_MONITOR_HAPTIC_INTENSITY,
            GATTDB_CHAR_PROP_READ | GATTDB_CHAR_PROP_WRITE | GATTDB_CHAR_PROP_WRITE_NO_RESPONSE,
            GATTDB_PERM_READABLE | GATTDB_PERM_WRITE_REQ | GATTDB_PERM_WRITE_CMD),
};

const uint16_t gatt_database_len = sizeof(gatt_database);

/******************************************************************************
 * GATT Attribute Initial Values
 ******************************************************************************/

uint8_t app_gap_device_name[]    = {'S','n','o','r','e','G','u','a','r','d','\0'};
uint8_t app_gap_appearance[]     = {0x00, 0x00};
uint8_t app_gatt_service_changed[]                 = {0x00, 0x00, 0x00, 0x00};
uint8_t app_gatt_service_changed_client_char_config[] = {0x00, 0x00};

/* Time Sync: 4-byte epoch placeholder (overwritten by phone write) */
uint8_t app_time_sync_value[4] = {0x00, 0x00, 0x00, 0x00};

/* Log Transfer: 7-byte event packet buffer
 * Layout: [timestamp:4][duration_s:1][haptic_success:1][haptic_flag:1]
 */
uint8_t app_log_transfer_event[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
uint8_t app_log_transfer_cccd[2]  = {0x00, 0x00};

/* Haptic Intensity: default level 2 = 60% */
uint8_t app_haptic_intensity_value[1] = {0x02};

/******************************************************************************
 * GATT Attribute Lookup Table
 ******************************************************************************/

gatt_db_lookup_table_t app_gatt_db_ext_attr_tbl[] =
{
    /* GAP */
    { HDLC_GAP_DEVICE_NAME_VALUE,
      MAX_LEN_GAP_DEVICE_NAME,   10,  app_gap_device_name },

    { HDLC_GAP_APPEARANCE_VALUE,
      MAX_LEN_GAP_APPEARANCE,     2,  app_gap_appearance },

    /* GATT Service Changed */
    { HDLC_GATT_SERVICE_CHANGED_VALUE,
      MAX_LEN_GATT_SERVICE_CHANGED, 4, app_gatt_service_changed },

    { HDLD_GATT_SERVICE_CHANGED_CLIENT_CHAR_CONFIG,
      MAX_LEN_GATT_SERVICE_CHANGED_CLIENT_CHAR_CONFIG, 2,
      app_gatt_service_changed_client_char_config },

    /* Time Sync */
    { HDLC_SLEEP_MONITOR_TIME_SYNC_VALUE,
      MAX_LEN_SLEEP_MONITOR_TIME_SYNC, 4, app_time_sync_value },

    /* Log Transfer value + CCCD */
    { HDLC_SLEEP_MONITOR_LOG_TRANSFER_VALUE,
      MAX_LEN_SLEEP_MONITOR_LOG_TRANSFER, 7, app_log_transfer_event },

    { HDLD_SLEEP_MONITOR_LOG_TRANSFER_CLIENT_CHAR_CONFIG,
      MAX_LEN_SLEEP_MONITOR_LOG_TRANSFER_CCCD, 2, app_log_transfer_cccd },

    /* Haptic Intensity */
    { HDLC_SLEEP_MONITOR_HAPTIC_INTENSITY_VALUE,
      MAX_LEN_SLEEP_MONITOR_HAPTIC_INTENSITY, 1, app_haptic_intensity_value },
};

const uint16_t app_gatt_db_ext_attr_tbl_size =
    (sizeof(app_gatt_db_ext_attr_tbl) / sizeof(gatt_db_lookup_table_t));

/* Characteristic length constants */
const uint16_t app_gap_device_name_len         = 10;
const uint16_t app_gap_appearance_len          = sizeof(app_gap_appearance);
const uint16_t app_gatt_service_changed_len    = sizeof(app_gatt_service_changed);
const uint16_t app_gatt_service_changed_client_char_config_len =
    sizeof(app_gatt_service_changed_client_char_config);
const uint16_t app_time_sync_value_len         = sizeof(app_time_sync_value);
const uint16_t app_log_transfer_event_len      = sizeof(app_log_transfer_event);
const uint16_t app_log_transfer_cccd_len       = sizeof(app_log_transfer_cccd);
const uint16_t app_haptic_intensity_value_len  = sizeof(app_haptic_intensity_value);
