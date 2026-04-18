/*******************************************************************************
 * File Name: rtc_time.c
 *
 * Description: Hardware RTC wrapper for SnoreGuard.
 *              Uses cyhal_rtc to keep accurate calendar time once seeded by
 *              the BLE Time Sync characteristic. Before seeding, callers
 *              should use the uptime-based fallback in snore_detect.c.
 *
 * Ported from: code/RTC.c (original project)
 *******************************************************************************/

#include "rtc_time.h"
#include "cy_pdl.h"
#include "cybsp.h"
#include "cyhal_rtc.h"
#include "cy_retarget_io.h"
#include <time.h>
#include <string.h>

/*******************************************************************************
 * Private State
 *******************************************************************************/

static cyhal_rtc_t s_rtc_obj;
static bool        s_rtc_valid = false;   /* true after first rtc_time_set_unix() */

/*******************************************************************************
 * Public API
 *******************************************************************************/

void rtc_time_init(void)
{
    cy_rslt_t rslt = cyhal_rtc_init(&s_rtc_obj);
    if (CY_RSLT_SUCCESS == rslt)
    {
        printf("[RTC] Initialized.\r\n");
    }
    else
    {
        printf("[RTC] ERROR: Init failed (0x%08lX). Uptime fallback active.\r\n",
               (unsigned long)rslt);
    }
}

void rtc_time_set_unix(uint32_t epoch)
{
    time_t    raw  = (time_t)epoch;
    struct tm *info = gmtime(&raw);
    if (!info) return;

    cy_rslt_t rslt = cyhal_rtc_write(&s_rtc_obj, info);
    if (CY_RSLT_SUCCESS == rslt)
    {
        s_rtc_valid = true;
        printf("[RTC] Time set to epoch=%lu\r\n", (unsigned long)epoch);
    }
    else
    {
        printf("[RTC] ERROR: Write failed (0x%08lX)\r\n", (unsigned long)rslt);
    }
}

uint32_t rtc_time_get_unix(void)
{
    struct tm current;
    memset(&current, 0, sizeof(current));
    if (cyhal_rtc_read(&s_rtc_obj, &current) == CY_RSLT_SUCCESS)
    {
        return (uint32_t)mktime(&current);
    }
    return 0u;
}

bool rtc_time_is_valid(void)
{
    return s_rtc_valid;
}
