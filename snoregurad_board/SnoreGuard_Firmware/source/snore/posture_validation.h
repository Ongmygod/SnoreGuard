/*******************************************************************************
 * File Name: posture_validation.h
 *
 * Description: Fast posture-change validation for SnoreGuard.
 *
 * After the haptic motor fires, this module monitors whether the user stops
 * snoring within FAST_POSTURE_VALIDATION_S seconds (10 s default):
 *
 *   SUCCESS: no snore detected for 10 consecutive seconds
 *            → no haptic level change
 *
 *   FAIL:    snore resumes before the 10-s window closes
 *            → calls snore_haptic_level_increment() to escalate intensity
 *
 * This is a faster feedback layer on top of the 15-minute post-haptic window
 * in snore_detect.c. Both run concurrently: this one drives auto-escalation,
 * the 15-minute window drives the flash-log haptic_success field.
 *
 * Ported from: code/Detection_Logic.c::POSTURE_CONFIRMATION
 *******************************************************************************/

#ifndef SOURCE_SNORE_POSTURE_VALIDATION_H_
#define SOURCE_SNORE_POSTURE_VALIDATION_H_

#include <stdint.h>
#include <stdbool.h>

/** Seconds of continuous silence after haptic = posture success */
#define FAST_POSTURE_VALIDATION_S   10u

/**
 * @brief Start the posture-validation window. Call when haptic fires.
 */
void posture_validation_start(void);

/**
 * @brief Feed each inference frame into the validator.
 *        Call from snore_detect_on_inference() / snore_detect_on_decision().
 *
 * @param snore_now  true if snore was detected in this frame
 * @param now_s      current uptime in seconds (from xTaskGetTickCount)
 */
void posture_validation_on_frame(bool snore_now, uint32_t now_s);

#endif /* SOURCE_SNORE_POSTURE_VALIDATION_H_ */
