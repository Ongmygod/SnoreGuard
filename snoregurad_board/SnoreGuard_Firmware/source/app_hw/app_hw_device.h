/*******************************************************************************
 * File Name: app_hw_device.h
 *
 * Description: Hardware interface for SnoreGuard.
 *              LED, button, haptic motor PWM.
 *******************************************************************************/

#ifndef SOURCE_APP_HW_APP_HW_DEVICE_H_
#define SOURCE_APP_HW_APP_HW_DEVICE_H_

#include <FreeRTOS.h>
#include <timers.h>
#include "cyhal.h"
#include "app_bt_event_handler.h"
#include "wiced_bt_gatt.h"

extern bool pairing_mode;

/*******************************************************************************
 * Haptic Motor API
 *
 * Pin:   HAPTIC_MOTOR_PIN (default CYBSP_USER_LED1 for dev kit visual feedback;
 *        replace with actual motor driver GPIO in hardware)
 * Levels: 0-4 → PWM duty 20%-100% in 20% steps
 *******************************************************************************/

/** Initialize PWM peripheral for haptic motor. */
void haptic_motor_init(void);

/**
 * @brief Fire the haptic motor for HAPTIC_FIRE_DURATION_MS at the given level.
 * @param level  0-4  (0 = 20% duty, 4 = 100% duty)
 */
void haptic_motor_fire(uint8_t level);

/** Stop the haptic motor immediately. */
void haptic_motor_stop(void);

/*******************************************************************************
 * LED / Button API (unchanged from Hello Sensor)
 *******************************************************************************/
void app_bt_led_blink(uint8_t num_of_blinks);
void app_bt_timeout_ms(TimerHandle_t timer_handle);
void app_bt_timeout_led_indicate(TimerHandle_t timer_handle);
void app_bt_timeout_led_blink(TimerHandle_t timer_handle);
void app_bt_interrupt_config(void);
void app_bt_gpio_interrupt_handler(void *handler_arg, cyhal_gpio_event_t event);
void app_bt_hw_init(void);
void button_task(void *arg);

/*******************************************************************************
 * Optional Level-Adjustment Button (P9_2)
 *
 * Enable by adding SNOREGUARD_ENABLE_LEVEL_BUTTON to Makefile DEFINES.
 * Creates a small FreeRTOS task that increments haptic level on each press.
 *******************************************************************************/
#ifdef SNOREGUARD_ENABLE_LEVEL_BUTTON
void level_btn_task(void *arg);
#endif

#endif /* SOURCE_APP_HW_APP_HW_DEVICE_H_ */
