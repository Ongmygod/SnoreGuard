/*******************************************************************************
 * File Name: snore_flash_log.h
 *
 * Description: Dual-tier snore event log for SnoreGuard.
 *
 * Tier 1 – SRAM circular buffer:
 *   Up to SNORE_LOG_MAX_EVENTS (200) events held in RAM.
 *   Fastest access; lost on power cycle unless flushed.
 *
 * Tier 2 – Non-volatile flash (kv-store):
 *   Buffer flushed to kv-store after each event.
 *   Restored on boot so data survives power cycles.
 *   KV keys:
 *     "sg_count"  uint16 – number of valid events
 *     "sg_head"   uint16 – write head index
 *     "sg_data"   byte[200*7] – packed event array
 *
 * Morning Sync streaming:
 *   snore_log_get_event(index, &event) is called by the BLE GATT handler
 *   to send events one-by-one as BLE notifications.
 *******************************************************************************/

#ifndef SOURCE_SNORE_SNORE_FLASH_LOG_H_
#define SOURCE_SNORE_SNORE_FLASH_LOG_H_

#include "snore_detect.h"
#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * Configuration
 *******************************************************************************/
#define SNORE_LOG_MAX_EVENTS    200u

/* kv-store keys */
#define KV_KEY_LOG_COUNT   "sg_count"
#define KV_KEY_LOG_HEAD    "sg_head"
#define KV_KEY_LOG_DATA    "sg_data"

/*******************************************************************************
 * Function Prototypes
 *******************************************************************************/

/**
 * @brief Initialise flash log module. Loads persisted data from kv-store.
 *        Must be called after kv-store is ready (after app_kv_store_init).
 */
void snore_log_init(void);

/**
 * @brief Add a snore event to the SRAM buffer and schedule a flash flush.
 *        Overwrites oldest event when the buffer is full (circular).
 */
void snore_log_add_event(const snore_event_t *event);

/**
 * @brief Return the number of valid events currently in the log.
 */
uint16_t snore_log_get_count(void);

/**
 * @brief Copy event at position index (0 = oldest) into *out.
 * @return true if index is valid, false if out of range.
 */
bool snore_log_get_event(uint16_t index, snore_event_t *out);

/**
 * @brief Clear all events from SRAM and kv-store.
 *        Called after a successful Morning Sync transfer.
 */
void snore_log_clear(void);

/**
 * @brief Flush the SRAM buffer to kv-store (non-volatile flash).
 *        Called internally after each event; also exposed for forced flush.
 */
void snore_log_flush_to_flash(void);

/**
 * @brief Print a formatted UART report of all events in the current log.
 *        Only compiled/printed when SNOREGUARD_DEBUG_LOG is defined.
 *        Reads from the existing SRAM buffer – no flash access needed.
 */
void snore_log_print_report(void);

/**
 * @brief Rewrite any fallback-timestamped events in the SRAM buffer to real
 *        calendar time by adding a constant offset, then flush to flash.
 *        Called from snore_set_epoch_base() the moment the phone writes
 *        Time Sync so that episodes logged before sync land on the correct
 *        day in the Flutter app timeline instead of ~Nov 2023.
 *
 *        Events whose timestamp is already outside the fallback range
 *        (real calendar time, or sync'd previously) are left unchanged.
 *
 * @param offset  Signed seconds to add:
 *                epoch_at_sync - uptime_at_sync - SNORE_FALLBACK_EPOCH_BASE
 * @return Number of events whose timestamp was rewritten.
 */
uint16_t snore_log_rebase_fallback_timestamps(int32_t offset);

#endif /* SOURCE_SNORE_SNORE_FLASH_LOG_H_ */
