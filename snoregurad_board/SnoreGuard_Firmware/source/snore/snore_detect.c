/*******************************************************************************
 * File Name: snore_detect.c
 *
 * Description: Snore detection, sliding-window haptic logic, and timestamp
 *              management for SnoreGuard firmware.
 *
 * Algorithm overview:
 *   1. Each inference result is checked against SNORE_CONFIDENCE_THRESHOLD.
 *   2. Confirmed snore events are timestamped and pushed into a ring buffer.
 *   3. The Sliding Window counter counts events in the last 60 s.
 *   4. When the counter reaches 5, the haptic motor fires (if not in cooldown).
 *   5. After firing, a 15-minute post-haptic window monitors whether snoring
 *      stops (Success) or continues (Unsuccessful).
 *   6. A fast 10-second posture validation layer (posture_validation.c) runs
 *      concurrently and auto-escalates haptic level on failure.
 *   7. Each event is persisted via snore_flash_log.
 *
 * Timestamp strategy (priority order):
 *   1. Hardware RTC (rtc_time.c)  – accurate after BLE Time Sync seeds it
 *   2. BLE epoch base + uptime    – if RTC not yet seeded but epoch received
 *   3. Fallback uptime epoch      – if no BLE connection or no Time Sync
 *******************************************************************************/

#include "snore_detect.h"
#include "snore_flash_log.h"
#include "posture_validation.h"
#include "rtc_time.h"
#include "app_hw_device.h"   /* haptic_motor_fire() */
#include "cy_retarget_io.h"

#include <FreeRTOS.h>
#include <task.h>
#include <string.h>

/*******************************************************************************
 * Private Sliding-Window State
 *******************************************************************************/

/* Ring buffer of recent snore event timestamps (seconds) */
static uint32_t s_window_buf[SNORE_SLIDING_WINDOW_COUNT * 4];  /* 4× oversized ring */
static uint8_t  s_window_head = 0;
static uint8_t  s_window_size = 0;
#define WINDOW_BUF_LEN  (SNORE_SLIDING_WINDOW_COUNT * 4u)

/* Active snore event tracking (per-breath edge detection) */
static bool     s_snore_active      = false;
static uint32_t s_snore_start_ts    = 0;

/* Episode state: merges consecutive breaths into one logged event.
 * Uptime is used for gap/duration math (monotonic); the wall-clock ts is
 * captured at episode start for the packet payload. */
static bool     s_episode_open            = false;
static uint32_t s_episode_start_ts        = 0;  /* wall-clock at episode start */
static uint32_t s_episode_start_up        = 0;  /* uptime at episode start    */
static uint32_t s_episode_last_snore_up   = 0;  /* uptime at last falling edge */

/* Haptic / cooldown state */
static bool     s_haptic_cooldown   = false;
static uint32_t s_haptic_fired_at_s = 0;
static bool     s_post_haptic_open  = false;  /* 15-min window open */
static uint32_t s_post_haptic_ts    = 0;      /* time haptic fired */
static snore_event_t s_pending_haptic_event;  /* event that triggered haptic */

/* Current haptic level set by phone or auto-incremented */
static uint8_t  s_haptic_level = 2;  /* default 60% */

/* Haptic enable flag: motor will not fire when false */
static bool     s_haptic_enabled = true;

/*******************************************************************************
 * Private Timestamp State
 *******************************************************************************/

static time_sync_state_t s_time_sync_state   = TIME_SYNC_NONE;
static uint32_t          s_epoch_base        = 0;
static uint32_t          s_uptime_at_sync_s  = 0;

static uint32_t uptime_seconds(void)
{
    return (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
}

/*******************************************************************************
 * Public API: Initialisation
 *******************************************************************************/

void snore_detect_init(void)
{
    memset(s_window_buf, 0, sizeof(s_window_buf));
    s_window_head      = 0;
    s_window_size      = 0;
    s_snore_active          = false;
    s_episode_open          = false;
    s_haptic_cooldown       = false;
    s_post_haptic_open      = false;
    s_time_sync_state       = TIME_SYNC_NONE;
    s_haptic_enabled        = true;

#ifdef SNOREGUARD_DEBUG_LOG
    printf("[SnoreDetect] Initialized.\r\n");
#endif
}

/*******************************************************************************
 * Public API: Timestamp / Time Sync
 *******************************************************************************/

uint32_t snore_get_timestamp(void)
{
    /* Priority 1: hardware RTC (accurate after BLE Time Sync seeds it) */
    if (rtc_time_is_valid())
    {
        return rtc_time_get_unix();
    }

    /* Priority 2 & 3: uptime-based */
    uint32_t uptime = uptime_seconds();

    switch (s_time_sync_state)
    {
        case TIME_SYNC_OK:
            /* epoch synced with phone but RTC not yet seeded */
            return s_epoch_base + (uptime - s_uptime_at_sync_s);

        case TIME_SYNC_FALLBACK:
        case TIME_SYNC_STANDALONE:
        default:
            /* No valid epoch – use uptime relative to a fixed fallback base.
             * The mobile app will see that the year is ~2023 and know these
             * are relative timestamps, not absolute calendar times.           */
            return SNORE_FALLBACK_EPOCH_BASE + uptime;
    }
}

void snore_set_epoch_base(uint32_t epoch)
{
    uint32_t uptime = uptime_seconds();

    /* Retroactively rebase any fallback-timestamped data to real calendar time.
     * Events logged while the device was running without a synced clock carry
     * timestamp = SNORE_FALLBACK_EPOCH_BASE + uptime_at_event. At sync time:
     *     real_ts = epoch - (uptime - uptime_at_event)
     *             = fallback_ts + (epoch - uptime - SNORE_FALLBACK_EPOCH_BASE)
     * So a single constant offset fixes every fallback stamp at once. Without
     * this, a session mixing pre-sync and post-sync episodes would span ~2.5
     * years on the app's timeline chart and render as unreadable slivers.     */
    int32_t offset = (int32_t)((int64_t)epoch
                               - (int64_t)uptime
                               - (int64_t)SNORE_FALLBACK_EPOCH_BASE);

    /* 1. Rewrite persisted events in the SRAM ring buffer + flash. */
    uint16_t rewritten = snore_log_rebase_fallback_timestamps(offset);

    /* 2. Rewrite in-flight episode state so the next snore_log_add_event()
     *    (either a gap-close or a 15-min post-haptic evaluation) carries the
     *    correct real timestamp rather than a fallback one. */
    if (s_episode_start_ts >= SNORE_FALLBACK_EPOCH_BASE &&
        s_episode_start_ts <  SNORE_FALLBACK_EPOCH_BASE + 31536000UL)
    {
        s_episode_start_ts = (uint32_t)((int64_t)s_episode_start_ts + offset);
    }
    if (s_pending_haptic_event.timestamp >= SNORE_FALLBACK_EPOCH_BASE &&
        s_pending_haptic_event.timestamp <  SNORE_FALLBACK_EPOCH_BASE + 31536000UL)
    {
        s_pending_haptic_event.timestamp =
            (uint32_t)((int64_t)s_pending_haptic_event.timestamp + offset);
    }

    s_epoch_base       = epoch;
    s_uptime_at_sync_s = uptime;
    s_time_sync_state  = TIME_SYNC_OK;

    /* Also seed hardware RTC so timestamps survive disconnects accurately */
    rtc_time_set_unix(epoch);

#ifdef SNOREGUARD_DEBUG_LOG
    printf("[SnoreDetect] Time sync OK. epoch=%lu, rebased %u logged event(s)\r\n",
           (unsigned long)epoch, rewritten);
#else
    (void)rewritten;
#endif
}

void snore_on_ble_connected(void)
{
    if (s_time_sync_state == TIME_SYNC_NONE ||
        s_time_sync_state == TIME_SYNC_STANDALONE)
    {
        s_time_sync_state = TIME_SYNC_CONNECTED;
#ifdef SNOREGUARD_DEBUG_LOG
        printf("[SnoreDetect] BLE connected – waiting for Time Sync.\r\n");
#endif
    }
}

void snore_on_ble_disconnected(void)
{
    /* Keep epoch base if already synced so logs stay consistent */
    if (s_time_sync_state == TIME_SYNC_CONNECTED)
    {
        /* Disconnected before Time Sync was received */
        s_time_sync_state = TIME_SYNC_FALLBACK;
#ifdef SNOREGUARD_DEBUG_LOG
        printf("[SnoreDetect] BLE disconnected before Time Sync – fallback mode.\r\n");
#endif
    }
}

void snore_on_time_sync_timeout(void)
{
    if (s_time_sync_state == TIME_SYNC_CONNECTED)
    {
        /* Still connected but phone did not write Time Sync in 30 s */
        s_time_sync_state = TIME_SYNC_FALLBACK;
        printf("[SnoreDetect] WARNING: Time Sync timeout (30 s). "
               "Using uptime-based timestamps (fallback epoch %lu).\r\n",
               (unsigned long)SNORE_FALLBACK_EPOCH_BASE);
    }
}

void snore_on_connect_timeout(void)
{
    if (s_time_sync_state == TIME_SYNC_NONE ||
        s_time_sync_state == TIME_SYNC_CONNECTED)
    {
        /* 5 minutes passed with no BLE connection – enter standalone mode */
        s_time_sync_state = TIME_SYNC_STANDALONE;
        printf("[SnoreDetect] WARNING: No BLE connection after %u s. "
               "Standalone mode – using uptime timestamps.\r\n",
               BLE_CONNECT_TIMEOUT_S);
    }
}

time_sync_state_t snore_get_time_sync_state(void)
{
    return s_time_sync_state;
}

/*******************************************************************************
 * Private: Sliding-Window helpers
 *******************************************************************************/

static void window_push(uint32_t ts)
{
    s_window_buf[s_window_head] = ts;
    s_window_head = (s_window_head + 1u) % WINDOW_BUF_LEN;
    if (s_window_size < WINDOW_BUF_LEN)
    {
        s_window_size++;
    }
}

/* Count events within the last SNORE_SLIDING_WINDOW_S seconds */
static uint8_t window_count_recent(uint32_t now_s)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < s_window_size; i++)
    {
        uint32_t ts = s_window_buf[i];
        if (ts > 0 && (now_s - ts) <= SNORE_SLIDING_WINDOW_S)
        {
            count++;
        }
    }
    return count;
}

/*******************************************************************************
 * Private: Haptic motor helpers
 *******************************************************************************/

static void trigger_haptic(void)
{
    if (!s_haptic_enabled)
    {
#ifdef SNOREGUARD_DEBUG_LOG
        printf("[SnoreDetect] Haptic suppressed (disabled by app).\r\n");
#endif
        return;
    }
    haptic_motor_fire(s_haptic_level);
    s_haptic_cooldown   = true;
    s_haptic_fired_at_s = uptime_seconds();
    s_post_haptic_open  = true;
    s_post_haptic_ts    = s_haptic_fired_at_s;

    /* Start fast 10-s posture validation (auto-escalation layer) */
    posture_validation_start();

#ifdef SNOREGUARD_DEBUG_LOG
    printf("[SnoreDetect] Haptic triggered! level=%u cooldown starts.\r\n",
           s_haptic_level);
#endif
}

/* Evaluate success/fail after SNORE_POST_HAPTIC_SUCCESS_S.
 * Returns true if success (no snore during window), false if unsuccessful.
 * Also updates the pending event and logs it.                               */
static void evaluate_post_haptic(bool snore_detected_in_window)
{
    if (!s_post_haptic_open) return;

    s_post_haptic_open = false;
    s_pending_haptic_event.haptic_success = snore_detected_in_window ? 0x00 : 0x01;

    snore_log_add_event(&s_pending_haptic_event);

#ifdef SNOREGUARD_DEBUG_LOG
    printf("[SnoreDetect] Post-haptic evaluation: %s\r\n",
           s_pending_haptic_event.haptic_success ? "SUCCESS" : "UNSUCCESSFUL");
#endif
}

/*******************************************************************************
 * Private: Core frame processor
 *
 * Shared by snore_detect_on_inference() and snore_detect_on_decision().
 * Handles cooldown, post-haptic window, posture validation, and event logging.
 *******************************************************************************/

static void process_snore_frame(bool snore_now)
{
    uint32_t now_s = uptime_seconds();
    uint32_t ts    = snore_get_timestamp();

    /* --- Update cooldown state --- */
    if (s_haptic_cooldown)
    {
        uint32_t elapsed = now_s - s_haptic_fired_at_s;

        /* Check 15-min post-haptic window */
        if (s_post_haptic_open && elapsed >= SNORE_POST_HAPTIC_SUCCESS_S)
        {
            /* Window closed without snore in it → SUCCESS */
            evaluate_post_haptic(false);
        }

        /* Release cooldown after 10 min */
        if (elapsed >= SNORE_HAPTIC_COOLDOWN_S)
        {
            s_haptic_cooldown = false;
#ifdef SNOREGUARD_DEBUG_LOG
            printf("[SnoreDetect] Haptic cooldown ended.\r\n");
#endif
        }
    }

    /* --- Post-haptic window: track snore occurrence (15-min window) --- */
    if (s_post_haptic_open && snore_now)
    {
        evaluate_post_haptic(true);   /* snore resumed → UNSUCCESSFUL */
    }

    /* --- Fast posture validation (10-s window) --- */
    posture_validation_on_frame(snore_now, now_s);

    /* --- Per-breath edge tracking (drives haptic; does NOT log directly) --- */
    if (snore_now && !s_snore_active)
    {
        /* Rising edge: a new breath starts */
        s_snore_active   = true;
        s_snore_start_ts = ts;

        /* Open an episode if none is currently open */
        if (!s_episode_open)
        {
            s_episode_open         = true;
            s_episode_start_ts     = ts;
            s_episode_start_up     = now_s;
#ifdef SNOREGUARD_DEBUG_LOG
            printf("[SnoreDetect] Episode OPEN ts=%lu\r\n", (unsigned long)ts);
#endif
        }

        window_push(now_s);
    }
    else if (!snore_now && s_snore_active)
    {
        /* Falling edge: breath ends. Mark last-snore uptime but DO NOT log;
         * the gap-close tick below is what closes and logs the episode. */
        s_snore_active           = false;
        s_episode_last_snore_up  = now_s;

        /* Evaluate haptic trigger as before (per-breath cadence). If it
         * fires, the pending event is queued for post-haptic evaluation
         * and the episode is considered handled via that path. */
        uint8_t recent_count = window_count_recent(now_s);
        if (recent_count >= SNORE_SLIDING_WINDOW_COUNT && !s_haptic_cooldown)
        {
            uint32_t dur = (s_episode_open)
                         ? (now_s - s_episode_start_up)
                         : 0u;
            if (dur > SNORE_EPISODE_MAX_DUR_S) dur = SNORE_EPISODE_MAX_DUR_S;

            s_pending_haptic_event.timestamp      = s_episode_start_ts;
            s_pending_haptic_event.duration_s     = (uint8_t)dur;
            s_pending_haptic_event.haptic_flag    = 0x01;
            s_pending_haptic_event.haptic_success = 0x00; /* filled later */

            trigger_haptic();

            /* Episode is now owned by the post-haptic path — close it here
             * so the gap-tick logger does not double-log it.            */
            s_episode_open         = false;
#ifdef SNOREGUARD_DEBUG_LOG
            printf("[SnoreDetect] Episode closed by haptic dur=%lus window=%u\r\n",
                   (unsigned long)dur, recent_count);
#endif
        }
    }

    /* --- Gap-close tick: close an open episode after SNORE_EPISODE_GAP_S
     *     seconds of silence and log it as a single event.                */
    if (s_episode_open && !s_snore_active &&
        (now_s - s_episode_last_snore_up) >= SNORE_EPISODE_GAP_S)
    {
        uint32_t dur = s_episode_last_snore_up - s_episode_start_up;
        if (dur > SNORE_EPISODE_MAX_DUR_S) dur = SNORE_EPISODE_MAX_DUR_S;

        if (dur >= SNORE_EPISODE_MIN_DUR_S)
        {
            snore_event_t ev = {
                .timestamp      = s_episode_start_ts,
                .duration_s     = (uint8_t)dur,
                .haptic_flag    = 0x00,
                .haptic_success = 0x00,
            };
            snore_log_add_event(&ev);

#ifdef SNOREGUARD_DEBUG_LOG
            printf("[SnoreDetect] Episode CLOSED ts=%lu dur=%lus (gap=%us)\r\n",
                   (unsigned long)s_episode_start_ts,
                   (unsigned long)dur,
                   SNORE_EPISODE_GAP_S);
#endif
        }
#ifdef SNOREGUARD_DEBUG_LOG
        else
        {
            printf("[SnoreDetect] Episode discarded (dur=%lus < min=%us)\r\n",
                   (unsigned long)dur, SNORE_EPISODE_MIN_DUR_S);
        }
#endif
        s_episode_open = false;
    }
}

/*******************************************************************************
 * Public API: Process Inference Result (from label scores array)
 *
 * @note Model label indexing for the MOCKUP (speech demo model):
 *   Index 0 = "unlabelled" (treat as NOT snore)
 *   Any other label with score > SNORE_CONFIDENCE_THRESHOLD = treat as snore
 *
 *   When integrated with the real snore model:
 *   - Set snore_label_idx to the index of the "snore" class
 *   - Labels such as "noise" and "unlabelled" will be false if score < threshold
 *******************************************************************************/

void snore_detect_on_inference(const float *label_scores,
                               int          label_count,
                               int          snore_label_idx)
{
    if (!label_scores || label_count <= 0) return;

    /* --- Determine if snore is detected this frame --- */
    bool snore_now = false;

    /* Bounds-check snore_label_idx */
    if (snore_label_idx >= 0 && snore_label_idx < label_count)
    {
        snore_now = (label_scores[snore_label_idx] >= SNORE_CONFIDENCE_THRESHOLD);
    }
    else
    {
        /* Fallback for mockup: any non-zero label with sufficient confidence */
        float best_score = label_scores[0];
        int   best_idx   = 0;
        for (int i = 1; i < label_count; i++)
        {
            if (label_scores[i] > best_score)
            {
                best_score = label_scores[i];
                best_idx   = i;
            }
        }
        snore_now = (best_idx != 0 && best_score >= SNORE_CONFIDENCE_THRESHOLD);
    }

    process_snore_frame(snore_now);
}

/*******************************************************************************
 * Public API: Process Pre-Decided Snore Boolean
 *
 * Use when the audio task has applied hysteresis (label stickiness) and
 * already determined a binary is_snore result. Skips internal score processing.
 *******************************************************************************/

void snore_detect_on_decision(bool is_snore)
{
    process_snore_frame(is_snore);
}

/*******************************************************************************
 * Public API: Haptic Level
 *******************************************************************************/

void snore_set_haptic_level(uint8_t level)
{
    if (level > 4u) level = 4u;
    s_haptic_level = level;
#ifdef SNOREGUARD_DEBUG_LOG
    printf("[SnoreDetect] Haptic level set to %u (%u%%)\r\n",
           level, 20u + level * 20u);
#endif
}

void snore_set_haptic_enabled(bool enabled)
{
    s_haptic_enabled = enabled;
    printf("[SnoreDetect] Haptic motor %s.\r\n", enabled ? "ENABLED" : "DISABLED");
}

bool snore_get_haptic_enabled(void)
{
    return s_haptic_enabled;
}

void snore_haptic_level_increment(void)
{
    if (s_haptic_level < 4u)
    {
        s_haptic_level++;
    }
    /* Always print – this is an important event regardless of debug flag */
    printf("[SnoreDetect] Haptic level bumped to %u (%u%%)\r\n",
           s_haptic_level, 20u + s_haptic_level * 20u);
}

void snore_on_haptic_complete(void)
{
    /* Called by haptic driver when the full shake sequence finishes.
     * The 15-min post-haptic window (s_post_haptic_open) is already running.
     * Reserved for any future logic that should start only after motor stops. */
#ifdef SNOREGUARD_DEBUG_LOG
    printf("[SnoreDetect] Haptic shake sequence complete. Post-haptic window active.\r\n");
#endif
}

void snore_detect_flush_open_episode(void)
{
    if (!s_episode_open) return;

    uint32_t now_s  = uptime_seconds();
    /* If a breath is still active (no falling edge yet), clamp end to now. */
    uint32_t end_up = s_snore_active ? now_s : s_episode_last_snore_up;
    uint32_t dur    = (end_up >= s_episode_start_up)
                      ? (end_up - s_episode_start_up)
                      : 0u;
    if (dur > SNORE_EPISODE_MAX_DUR_S) dur = SNORE_EPISODE_MAX_DUR_S;

    if (dur >= SNORE_EPISODE_MIN_DUR_S)
    {
        snore_event_t ev = {
            .timestamp      = s_episode_start_ts,
            .duration_s     = (uint8_t)dur,
            .haptic_flag    = 0x00,
            .haptic_success = 0x00,
        };
        snore_log_add_event(&ev);
#ifdef SNOREGUARD_DEBUG_LOG
        printf("[SnoreDetect] Episode FLUSHED (pre-sync) ts=%lu dur=%lus\r\n",
               (unsigned long)s_episode_start_ts, (unsigned long)dur);
#endif
    }
    s_episode_open = false;
    s_snore_active = false;
}
