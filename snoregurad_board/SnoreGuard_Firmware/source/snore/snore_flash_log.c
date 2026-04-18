/*******************************************************************************
 * File Name: snore_flash_log.c
 *
 * Description: Dual-tier (SRAM + kv-store flash) event log for SnoreGuard.
 *******************************************************************************/

#include "snore_flash_log.h"
#include "app_flash_common.h"
#include "cy_retarget_io.h"

#include <FreeRTOS.h>
#include <task.h>
#include <string.h>

/*******************************************************************************
 * Private SRAM Circular Buffer
 * Oldest event is at index s_tail; newest at (s_tail + s_count - 1) % MAX
 *******************************************************************************/

static snore_event_t s_buf[SNORE_LOG_MAX_EVENTS];
static uint16_t      s_head  = 0;   /* next write position            */
static uint16_t      s_count = 0;   /* valid events (0..MAX_EVENTS)   */


/*******************************************************************************
 * Private: kv-store helpers
 *******************************************************************************/

extern mtb_kvstore_t kvstore_obj;   /* defined in app_bt_bonding.c */

static void flash_save(void)
{
    cy_rslt_t rslt;

    if (s_count == 0) return;

    /* Save count (head is derived from linearized layout on reload) */
    rslt = mtb_kvstore_write(&kvstore_obj, KV_KEY_LOG_COUNT,
                             (const uint8_t *)&s_count, sizeof(s_count));
    if (CY_RSLT_SUCCESS != rslt)
    {
        printf("[FlashLog] ERROR: Failed to write count (%lu)\r\n",
               (unsigned long)rslt);
        return;
    }

    /* Linearize the circular buffer (oldest event first) into a static scratch
     * buffer, then write only the s_count active events.  This keeps the write
     * size small (s_count * 7 bytes) and avoids the 1400-byte fixed-size write
     * that would overflow the kv-store partition.                              */
    static snore_event_t s_linear[SNORE_LOG_MAX_EVENTS];
    uint16_t tail = (uint16_t)((s_head + SNORE_LOG_MAX_EVENTS - s_count)
                               % SNORE_LOG_MAX_EVENTS);
    for (uint16_t i = 0; i < s_count; i++)
    {
        s_linear[i] = s_buf[(tail + i) % SNORE_LOG_MAX_EVENTS];
    }

    rslt = mtb_kvstore_write(&kvstore_obj, KV_KEY_LOG_DATA,
                             (const uint8_t *)s_linear,
                             (uint32_t)s_count * sizeof(snore_event_t));
    if (CY_RSLT_SUCCESS != rslt)
    {
        printf("[FlashLog] ERROR: Failed to write data (%lu)\r\n",
               (unsigned long)rslt);
    }
#ifdef SNOREGUARD_DEBUG_LOG
    else
    {
        printf("[FlashLog] Flushed %u events to flash.\r\n", s_count);
    }
#endif
}

static void flash_load(void)
{
    cy_rslt_t rslt;
    uint32_t  read_len;

    /* Load count */
    read_len = sizeof(s_count);
    rslt = mtb_kvstore_read(&kvstore_obj, KV_KEY_LOG_COUNT,
                            (uint8_t *)&s_count, &read_len);
    if (CY_RSLT_SUCCESS != rslt || s_count > SNORE_LOG_MAX_EVENTS)
    {
        s_count = 0;
        s_head  = 0;
#ifdef SNOREGUARD_DEBUG_LOG
        printf("[FlashLog] No valid log in flash – starting fresh.\r\n");
#endif
        return;
    }

    /* Load linearized event data (only the s_count active events) */
    read_len = (uint32_t)s_count * sizeof(snore_event_t);
    rslt = mtb_kvstore_read(&kvstore_obj, KV_KEY_LOG_DATA,
                            (uint8_t *)s_buf, &read_len);
    if (CY_RSLT_SUCCESS != rslt)
    {
        s_count = 0;
        s_head  = 0;
        memset(s_buf, 0, sizeof(s_buf));
        return;
    }

    /* Events are stored linearized (oldest at [0]); restore head pointer */
    s_head = s_count % SNORE_LOG_MAX_EVENTS;

    printf("[FlashLog] Restored %u events from flash.\r\n", s_count);
}

/*******************************************************************************
 * Public API
 *******************************************************************************/

void snore_log_init(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    s_head  = 0;
    s_count = 0;
    flash_load();
}

void snore_log_add_event(const snore_event_t *event)
{
    if (!event) return;

    /* Disable interrupts around buffer modification (FreeRTOS critical section) */
    taskENTER_CRITICAL();
    {
        s_buf[s_head] = *event;
        s_head = (s_head + 1u) % SNORE_LOG_MAX_EVENTS;
        if (s_count < SNORE_LOG_MAX_EVENTS)
        {
            s_count++;
        }
        /* When full, s_head has wrapped past the oldest event –
         * the oldest is implicitly overwritten (circular semantics).        */
    }
    taskEXIT_CRITICAL();

    /* Persist to flash asynchronously (done inline here for simplicity;
     * a production design would offload to a low-priority flash task).      */
    snore_log_flush_to_flash();

#ifdef SNOREGUARD_DEBUG_LOG
    printf("[FlashLog] Event added: ts=%lu dur=%us haptic=%u success=%u. "
           "Total events: %u\r\n",
           (unsigned long)event->timestamp,
           event->duration_s,
           event->haptic_flag,
           event->haptic_success,
           s_count);
#endif
}

uint16_t snore_log_get_count(void)
{
    return s_count;
}

bool snore_log_get_event(uint16_t index, snore_event_t *out)
{
    if (!out || index >= s_count) return false;

    /* Translate logical index to physical ring-buffer index.
     * Oldest event is at: (s_head - s_count + SNORE_LOG_MAX_EVENTS) % MAX  */
    uint16_t tail = (uint16_t)((s_head + SNORE_LOG_MAX_EVENTS - s_count)
                               % SNORE_LOG_MAX_EVENTS);
    uint16_t phys = (uint16_t)((tail + index) % SNORE_LOG_MAX_EVENTS);

    *out = s_buf[phys];
    return true;
}

void snore_log_clear(void)
{
    taskENTER_CRITICAL();
    {
        memset(s_buf, 0, sizeof(s_buf));
        s_head  = 0;
        s_count = 0;
    }
    taskEXIT_CRITICAL();

    /* Erase persisted log */
    mtb_kvstore_delete(&kvstore_obj, KV_KEY_LOG_COUNT);
    mtb_kvstore_delete(&kvstore_obj, KV_KEY_LOG_DATA);

    printf("[FlashLog] Log cleared.\r\n");
}

void snore_log_flush_to_flash(void)
{
    flash_save();
}

void snore_log_print_report(void)
{
#ifdef SNOREGUARD_DEBUG_LOG
    uint16_t count = snore_log_get_count();
    printf("\r\n--- SNORE REPORT TABLE (%u events) ---\r\n", count);
    printf("Unix Timestamp | Duration(s) | Haptic | Success\r\n");
    printf("--------------------------------------------------\r\n");

    for (uint16_t i = 0; i < count; i++)
    {
        snore_event_t ev;
        if (snore_log_get_event(i, &ev))
        {
            printf("%14lu | %11u | %6u | %7u\r\n",
                   (unsigned long)ev.timestamp,
                   ev.duration_s,
                   ev.haptic_flag,
                   ev.haptic_success);
        }
    }
    printf("--------------------------------------------------\r\n");
#endif
}
