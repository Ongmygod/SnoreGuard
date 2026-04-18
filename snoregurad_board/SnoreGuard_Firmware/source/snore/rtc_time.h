/*******************************************************************************
 * File Name: rtc_time.h
 *
 * Description: Hardware RTC wrapper for SnoreGuard.
 *              Provides real calendar-time Unix timestamps when seeded by BLE
 *              Time Sync. Falls back gracefully before any sync occurs.
 *
 * Ported from: code/RTC.c (original project)
 *******************************************************************************/

#ifndef SOURCE_SNORE_RTC_TIME_H_
#define SOURCE_SNORE_RTC_TIME_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialise the hardware RTC peripheral. Call once in main().
 */
void rtc_time_init(void);

/**
 * @brief Seed the hardware RTC with a Unix epoch received from the phone.
 *        After this call rtc_time_is_valid() returns true.
 * @param epoch  Unix timestamp (seconds since 1970-01-01 UTC)
 */
void rtc_time_set_unix(uint32_t epoch);

/**
 * @brief Read current Unix time from the hardware RTC.
 * @return Unix timestamp in seconds, or 0 on read failure.
 */
uint32_t rtc_time_get_unix(void);

/**
 * @brief Returns true once the RTC has been seeded via rtc_time_set_unix().
 */
bool rtc_time_is_valid(void);

#endif /* SOURCE_SNORE_RTC_TIME_H_ */
