/*******************************************************************************
 * File Name: snore_detect.h
 *
 * Description: Snore detection logic for SnoreGuard.
 *
 * Responsibilities:
 *   - Receives inference results from the audio task
 *   - Implements Sliding Window haptic intervention:
 *       5 snore events within 60 s → fire haptic motor
 *       10-minute cooldown after haptic fires
 *       15-minute post-haptic window for success/fail evaluation
 *   - Manages Unix timestamp (synced via BLE Time Sync characteristic)
 *   - Handles BLE timeout cases:
 *       Case 1: BLE connected, Time Sync received  → use synced epoch
 *       Case 2: BLE connected, no Time Sync ≥ 30 s → fallback to uptime epoch
 *       Case 3: No BLE connection ≥ 5 min           → standalone mode, uptime epoch
 *******************************************************************************/

#ifndef SOURCE_SNORE_SNORE_DETECT_H_
#define SOURCE_SNORE_SNORE_DETECT_H_

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * Algorithm Parameters  (all values in seconds unless noted)
 *******************************************************************************/
#define SNORE_CONFIDENCE_THRESHOLD    0.50f   /* min inference score for snore */
#define SNORE_SLIDING_WINDOW_S        60u     /* sliding window width */
#define SNORE_SLIDING_WINDOW_COUNT    5u      /* events in window to trigger haptic */
#define SNORE_HAPTIC_COOLDOWN_S       600u    /* 10-minute post-haptic cooldown */
#define SNORE_POST_HAPTIC_SUCCESS_S   900u    /* 15-min silence = success */

/* Episode merging (debounce): consecutive snore breaths within
 * SNORE_EPISODE_GAP_S seconds are collapsed into a single logged event.
 * Without this, continuous snoring produces >200 events/night (one per breath).
 */
#define SNORE_EPISODE_GAP_S            15u   /* silence needed to close an episode */
#define SNORE_EPISODE_MIN_DUR_S         3u   /* drop episodes shorter than this */
#define SNORE_EPISODE_MAX_DUR_S       240u   /* safety cap below uint8_t 255 clamp */

/* BLE timeout parameters */
#define BLE_CONNECT_TIMEOUT_S         300u    /* 5 min: enter standalone if no BLE */
#define BLE_TIME_SYNC_TIMEOUT_S       30u     /* 30 s: fallback if no epoch write */

/* Fallback epoch (Nov 2023 – clearly a fallback, not a real timestamp)
 * Will be offset by device uptime so events are ordered correctly.         */
#define SNORE_FALLBACK_EPOCH_BASE     1700000000UL

/*******************************************************************************
 * Snore Event Packet (7 bytes, little-endian, matches proposal BLE protocol)
 *******************************************************************************/
typedef struct __attribute__((packed))
{
    uint32_t timestamp;       /* Unix epoch of event start               */
    uint8_t  duration_s;      /* Snore duration in seconds (0-255)       */
    uint8_t  haptic_success;  /* 1 = snoring ceased within 15 min window */
    uint8_t  haptic_flag;     /* 1 = haptic fired during this event      */
} snore_event_t;

/*******************************************************************************
 * Time Sync State
 *******************************************************************************/
typedef enum
{
    TIME_SYNC_NONE       = 0,   /* no BLE connection yet                    */
    TIME_SYNC_CONNECTED  = 1,   /* BLE connected, waiting for Time Sync     */
    TIME_SYNC_OK         = 2,   /* epoch received from phone                */
    TIME_SYNC_FALLBACK   = 3,   /* timeout – using uptime-based timestamps  */
    TIME_SYNC_STANDALONE = 4,   /* no BLE after 5 min – standalone mode     */
} time_sync_state_t;

/*******************************************************************************
 * Function Prototypes
 *******************************************************************************/

/**
 * @brief Initialise snore detection state. Call once before the audio task.
 */
void snore_detect_init(void);

/**
 * @brief Called by audio inference task with each model output.
 *
 * @param label_scores  Float array of per-class scores (IMAI_DATA_OUT_COUNT elements)
 * @param label_count   Number of elements in label_scores
 * @param snore_label_idx  Index of the "snore" class in the scores array
 *                         (0 in the current demo model means "unlabelled";
 *                          for the real snore model set this to the snore class index)
 */
void snore_detect_on_inference(const float *label_scores,
                               int          label_count,
                               int          snore_label_idx);

/**
 * @brief Simplified entry point for pre-decided snore detection.
 *        Call this when the audio task has already applied hysteresis and
 *        determined a binary is_snore decision (e.g. via audio label stickiness).
 *        Bypasses internal label_scores processing.
 *
 * @param is_snore  true if snore was detected in this inference frame
 */
void snore_detect_on_decision(bool is_snore);

/* ---- Time Sync API (called from BLE GATT write handler) ---- */

/** Call when BLE connects. Starts the Time Sync timeout timer. */
void snore_on_ble_connected(void);

/** Call when BLE disconnects. */
void snore_on_ble_disconnected(void);

/** Call when phone writes Time Sync characteristic. epoch = Unix timestamp. */
void snore_set_epoch_base(uint32_t epoch);

/** Called from a FreeRTOS software timer after BLE_TIME_SYNC_TIMEOUT_S. */
void snore_on_time_sync_timeout(void);

/** Called from a FreeRTOS software timer after BLE_CONNECT_TIMEOUT_S. */
void snore_on_connect_timeout(void);

/** Return current timestamp (synced or fallback). */
uint32_t snore_get_timestamp(void);

/** Return current time sync state. */
time_sync_state_t snore_get_time_sync_state(void);

/* ---- Haptic API (called internally, exposed for testing) ---- */

/**
 * @brief Set haptic intensity level used for the next trigger.
 * @param level  0-4 maps to 20%-100% PWM duty cycle.
 */
void snore_set_haptic_level(uint8_t level);

/**
 * @brief Increment haptic intensity by one level (clamped at 4 = 100%).
 *        Called by posture_validation on posture-check failure.
 */
void snore_haptic_level_increment(void);

/**
 * @brief Called by the haptic driver when the full shake sequence completes.
 *        Signals that the motor has stopped and the post-haptic window is active.
 */
void snore_on_haptic_complete(void);

/**
 * @brief Force-close any open snore episode and flush it to the log.
 *        Call before morning sync streaming so an in-progress episode
 *        (e.g. user presses the sync button while still snoring) is not lost.
 */
void snore_detect_flush_open_episode(void);

#endif /* SOURCE_SNORE_SNORE_DETECT_H_ */
