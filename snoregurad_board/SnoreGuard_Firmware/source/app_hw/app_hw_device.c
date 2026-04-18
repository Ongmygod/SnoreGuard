/*******************************************************************************
 * File Name: app_hw_device.c
 *
 * Description: Hardware interface for SnoreGuard.
 *              - LED blink timers (unchanged from Hello Sensor template)
 *              - Button task:
 *                    short press (50-250 ms) → trigger Morning Sync (BLE log stream)
 *                    long  press (5-10 s)    → enter pairing mode
 *              - Haptic motor PWM:
 *                    6-round pulse/shake sequence for effective wake stimulus
 *                    5 intensity levels: 20%, 40%, 60%, 80%, 100%
 *              - Optional level-adjustment button (P9_2):
 *                    enable with SNOREGUARD_ENABLE_LEVEL_BUTTON in Makefile DEFINES
 *
 * NOTE: HAPTIC_MOTOR_PIN defaults to CYBSP_USER_LED1 for dev-kit visual
 *       feedback during development.  Replace with the actual motor driver
 *       GPIO before deploying hardware.
 *
 * Shake sequence ported from: code/Motor_Controll.c::MOTOR_PLUSE
 *******************************************************************************/

#include "inttypes.h"
#include <FreeRTOS.h>
#include <task.h>
#include "timers.h"
#include "app_bt_bonding.h"
#include "app_flash_common.h"
#include "cycfg_gap.h"
#include "app_bt_utils.h"
#include "app_bt_event_handler.h"
#include "app_bt_gatt_handler.h"
#include "app_hw_device.h"
#include "snore_detect.h"           /* snore_on_haptic_complete(), snore_haptic_level_increment() */
#include "cyhal.h"
#include "cy_retarget_io.h"
#ifdef ENABLE_BT_SPY_LOG
#include "cybt_debug_uart.h"
#endif

/*******************************************************************************
 * Macro Definitions
 *******************************************************************************/
#define APP_BTN_PRESS_SHORT_MIN     (50)
#define APP_BTN_PRESS_SHORT_MAX     (250)
#define APP_BTN_PRESS_5S            (5000)
#define APP_BTN_PRESS_10S           (10000)
#define APP_TIMEOUT_MS_BTN          (1)
#define APP_TIMEOUT_LED_INDICATE    (500)
#define APP_TIMEOUT_LED_BLINK       (250)
#define MAXIMUM_LED_BLINK_COUNT     (11)

#define GPIO_INTERRUPT_PRIORITY     (4)
#define BTN_TASK_STACK_SIZE         (2048u)
#define BTN_TASK_PRIORITY           (5)

#ifndef CYBSP_USER_LED2
#define CYBSP_USER_LED2  CYBSP_USER_LED
#endif

/*------------------------------------------------------------------------------
 * Haptic motor configuration
 *   Replace HAPTIC_MOTOR_PIN with the real motor-driver GPIO.
 *   For development, LED1 gives a visual indication of haptic firing.
 *
 * Shake sequence (ported from code/Motor_Controll.c):
 *   HAPTIC_SHAKE_ROUNDS: number of on/off pulses per intervention
 *   HAPTIC_SHAKE_ON_MS:  motor-on duration per pulse
 *   HAPTIC_SHAKE_OFF_MS: motor-off gap between pulses
 *----------------------------------------------------------------------------*/
#define HAPTIC_MOTOR_PIN            CYBSP_USER_LED1
#define HAPTIC_PWM_FREQUENCY_HZ     (200u)          /* 200 Hz ERM drive       */
#define HAPTIC_SHAKE_ROUNDS         (6u)            /* pulses per intervention */
#define HAPTIC_SHAKE_ON_MS          (500u)          /* motor on per pulse      */
#define HAPTIC_SHAKE_OFF_MS         (500u)          /* gap between pulses      */

/* Duty cycle (%) per intensity level 0-4 */
static const uint32_t HAPTIC_DUTY_PERCENT[5] = {20u, 40u, 60u, 80u, 100u};

/*------------------------------------------------------------------------------
 * Optional level-adjustment button (P9_2)
 *----------------------------------------------------------------------------*/
#ifdef SNOREGUARD_ENABLE_LEVEL_BUTTON
#define LEVEL_BTN_PIN               P9_2
#define LEVEL_BTN_IRQ_PRIORITY      (3u)
#define LEVEL_BTN_TASK_STACK        (512u)
#define LEVEL_BTN_TASK_PRIORITY     (4u)
#endif

/*******************************************************************************
 * Variable Definitions
 *******************************************************************************/

TimerHandle_t timer_led_blink;
TimerHandle_t ms_timer_led_indicate;
TimerHandle_t ms_timer_btn;

uint8_t led_blink_count;
TaskHandle_t button_handle;
static uint8_t  led_indicate_count;
static bool     is_btn_pressed;
bool            pairing_mode = FALSE;
static uint32_t btn_press_start;

/* Haptic PWM object */
static cyhal_pwm_t  s_haptic_pwm;
static bool         s_haptic_pwm_inited = false;

/* Shake sequence state */
typedef enum { SHAKE_IDLE = 0, SHAKE_ON, SHAKE_OFF } shake_state_t;
static shake_state_t s_shake_state = SHAKE_IDLE;
static uint8_t       s_shake_count = 0;
static uint8_t       s_shake_level = 0;
static TimerHandle_t haptic_shake_timer;

static cyhal_gpio_callback_data_t btn_cb_data =
{
    .callback     = app_bt_gpio_interrupt_handler,
    .callback_arg = NULL
};

#ifdef SNOREGUARD_ENABLE_LEVEL_BUTTON
static SemaphoreHandle_t          s_level_btn_sem;
static cyhal_gpio_callback_data_t level_btn_cb_data;
#endif

/*******************************************************************************
 * Private: Raw motor on/off helpers
 *******************************************************************************/

static void motor_set_on(uint8_t level)
{
    if (s_haptic_pwm_inited)
    {
        cyhal_pwm_set_duty_cycle(&s_haptic_pwm,
                                 (float)HAPTIC_DUTY_PERCENT[level],
                                 HAPTIC_PWM_FREQUENCY_HZ);
    }
    else
    {
        cyhal_gpio_write(HAPTIC_MOTOR_PIN, true);
    }
}

static void motor_set_off(void)
{
    if (s_haptic_pwm_inited)
    {
        cyhal_pwm_set_duty_cycle(&s_haptic_pwm, 0.0f, HAPTIC_PWM_FREQUENCY_HZ);
    }
    else
    {
        cyhal_gpio_write(HAPTIC_MOTOR_PIN, false);
    }
}

/*******************************************************************************
 * Haptic Shake Timer Callback
 *
 * State machine: SHAKE_ON → SHAKE_OFF → SHAKE_ON → ... (HAPTIC_SHAKE_ROUNDS)
 * After all rounds, calls snore_on_haptic_complete() and returns to SHAKE_IDLE.
 *******************************************************************************/

static void haptic_shake_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    if (s_shake_state == SHAKE_ON)
    {
        /* End of ON phase – turn motor off */
        motor_set_off();
        s_shake_count++;

        if (s_shake_count >= HAPTIC_SHAKE_ROUNDS)
        {
            /* All rounds complete */
            s_shake_state = SHAKE_IDLE;
            printf("[Haptic] Shake sequence done (%u rounds).\r\n",
                   HAPTIC_SHAKE_ROUNDS);
            snore_on_haptic_complete();
            return;
        }

        /* Schedule OFF phase */
        s_shake_state = SHAKE_OFF;
        xTimerChangePeriod(haptic_shake_timer,
                           pdMS_TO_TICKS(HAPTIC_SHAKE_OFF_MS), 0);
        xTimerStart(haptic_shake_timer, 0);
    }
    else if (s_shake_state == SHAKE_OFF)
    {
        /* End of OFF phase – start next ON pulse */
        s_shake_state = SHAKE_ON;
        motor_set_on(s_shake_level);

#ifdef SNOREGUARD_DEBUG_LOG
        printf("[Haptic] Shake %u/%u\r\n",
               s_shake_count + 1u, HAPTIC_SHAKE_ROUNDS);
#endif

        xTimerChangePeriod(haptic_shake_timer,
                           pdMS_TO_TICKS(HAPTIC_SHAKE_ON_MS), 0);
        xTimerStart(haptic_shake_timer, 0);
    }
}

/*******************************************************************************
 * Haptic Motor Implementation
 *******************************************************************************/

void haptic_motor_init(void)
{
    cy_rslt_t rslt;
    rslt = cyhal_pwm_init(&s_haptic_pwm, HAPTIC_MOTOR_PIN, NULL);
    if (CY_RSLT_SUCCESS == rslt)
    {
        s_haptic_pwm_inited = true;
        /* Start at 0% duty (motor off) */
        cyhal_pwm_set_duty_cycle(&s_haptic_pwm, 0.0f, HAPTIC_PWM_FREQUENCY_HZ);
        cyhal_pwm_start(&s_haptic_pwm);
        printf("[Haptic] PWM init OK.\r\n");
    }
    else
    {
        /* Fallback: plain GPIO if PWM init fails (e.g., pin conflict) */
        s_haptic_pwm_inited = false;
        cyhal_gpio_init(HAPTIC_MOTOR_PIN,
                        CYHAL_GPIO_DIR_OUTPUT,
                        CYHAL_GPIO_DRIVE_STRONG,
                        false);
        printf("[Haptic] PWM unavailable – GPIO fallback mode.\r\n");
    }

    /* Create one-shot shake timer (period re-armed in callback) */
    haptic_shake_timer = xTimerCreate("haptic_shake",
                                      pdMS_TO_TICKS(HAPTIC_SHAKE_ON_MS),
                                      pdFALSE,   /* one-shot; re-armed by callback */
                                      NULL,
                                      haptic_shake_timer_cb);
}

void haptic_motor_fire(uint8_t level)
{
    if (level > 4u) level = 4u;

    /* Abort any running sequence first */
    if (s_shake_state != SHAKE_IDLE)
    {
        xTimerStop(haptic_shake_timer, 0);
        motor_set_off();
    }

    s_shake_level = level;
    s_shake_count = 0;
    s_shake_state = SHAKE_ON;

    printf("[Haptic] FIRE level=%u (%lu%% duty) – %u shake rounds\r\n",
           level, (unsigned long)HAPTIC_DUTY_PERCENT[level], HAPTIC_SHAKE_ROUNDS);

    /* Start first ON pulse */
    motor_set_on(level);

#ifdef SNOREGUARD_DEBUG_LOG
    printf("[Haptic] Shake 1/%u\r\n", HAPTIC_SHAKE_ROUNDS);
#endif

    xTimerChangePeriod(haptic_shake_timer,
                       pdMS_TO_TICKS(HAPTIC_SHAKE_ON_MS), 0);
    xTimerStart(haptic_shake_timer, 0);
}

void haptic_motor_stop(void)
{
    xTimerStop(haptic_shake_timer, 0);
    s_shake_state = SHAKE_IDLE;
    s_shake_count = 0;
    motor_set_off();
#ifdef SNOREGUARD_DEBUG_LOG
    printf("[Haptic] Motor stopped.\r\n");
#endif
}

/*******************************************************************************
 * LED / Timer Functions (unchanged from Hello Sensor)
 *******************************************************************************/

void app_bt_led_blink(uint8_t num_of_blinks)
{
    if (num_of_blinks)
    {
        led_blink_count = num_of_blinks;
        /* Only blink LED2 since LED1 is used for haptic indication */
        cyhal_gpio_write(CYBSP_USER_LED2, CYBSP_LED_STATE_ON);
        xTimerStart(timer_led_blink, 0);
    }
}

void app_bt_timeout_ms_btn(TimerHandle_t timer_handle)
{
    hello_sensor_state.timer_count_ms++;
    if (APP_BTN_PRESS_5S == (hello_sensor_state.timer_count_ms - btn_press_start) &&
        is_btn_pressed)
    {
        cyhal_gpio_write(CYBSP_USER_LED2, CYBSP_LED_STATE_ON);
        xTimerStart(ms_timer_led_indicate, 0);
    }
}

void app_bt_timeout_led_indicate(TimerHandle_t timer_handle)
{
    led_indicate_count++;
    cyhal_gpio_toggle(CYBSP_USER_LED2);
    if (led_indicate_count == MAXIMUM_LED_BLINK_COUNT)
    {
        xTimerStop(ms_timer_led_indicate, 0);
        cyhal_gpio_write(CYBSP_USER_LED2, CYBSP_LED_STATE_OFF);
        led_indicate_count = 0;
    }
}

void app_bt_timeout_led_blink(TimerHandle_t timer_handle)
{
    static wiced_bool_t led_on = WICED_TRUE;
    if (led_on)
    {
        cyhal_gpio_write(CYBSP_USER_LED2, CYBSP_LED_STATE_OFF);
        if (--led_blink_count)
        {
            led_on = WICED_FALSE;
            xTimerStart(timer_led_blink, 0);
        }
    }
    else
    {
        led_on = WICED_TRUE;
        cyhal_gpio_write(CYBSP_USER_LED2, CYBSP_LED_STATE_ON);
        xTimerStart(timer_led_blink, 0);
    }
}

void app_bt_interrupt_config(void)
{
    cyhal_gpio_init(CYBSP_USER_BTN,
                    CYHAL_GPIO_DIR_INPUT,
                    CYHAL_GPIO_DRIVE_PULLUP,
                    CYBSP_BTN_OFF);
    cyhal_gpio_register_callback(CYBSP_USER_BTN, &btn_cb_data);
    cyhal_gpio_enable_event(CYBSP_USER_BTN,
                            CYHAL_GPIO_IRQ_BOTH,
                            GPIO_INTERRUPT_PRIORITY,
                            true);
}

void app_bt_gpio_interrupt_handler(void *handler_arg, cyhal_gpio_event_t event)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (CYHAL_GPIO_IRQ_FALL == event)
    {
        xTimerStartFromISR(ms_timer_btn, &xHigherPriorityTaskWoken);
    }
    if ((CYHAL_GPIO_IRQ_RISE == event) || (CYHAL_GPIO_IRQ_FALL == event))
    {
        vTaskNotifyGiveFromISR(button_handle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/*******************************************************************************
 * Optional Level-Adjustment Button (P9_2)
 *******************************************************************************/

#ifdef SNOREGUARD_ENABLE_LEVEL_BUTTON
static void level_btn_isr(void *handler_arg, cyhal_gpio_event_t event)
{
    (void)handler_arg;
    (void)event;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(s_level_btn_sem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void level_btn_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        if (xSemaphoreTake(s_level_btn_sem, portMAX_DELAY) == pdTRUE)
        {
            snore_haptic_level_increment();
            printf("[LevelBtn] Manual level bump via P9_2.\r\n");
            vTaskDelay(pdMS_TO_TICKS(200u)); /* debounce */
        }
    }
}
#endif /* SNOREGUARD_ENABLE_LEVEL_BUTTON */

/*******************************************************************************
 * Hardware Init
 *******************************************************************************/

void app_bt_hw_init(void)
{
    /* LED2 for status; LED1 managed by haptic PWM init */
    cyhal_gpio_init(CYBSP_USER_LED2, CYHAL_GPIO_DIR_OUTPUT,
                    CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);

    /* Haptic motor PWM + shake timer */
    haptic_motor_init();

    /* Button millisecond timer */
    ms_timer_btn = xTimerCreate("ms_timer_btn",
                                pdMS_TO_TICKS(APP_TIMEOUT_MS_BTN),
                                pdTRUE,
                                NULL,
                                app_bt_timeout_ms_btn);
    xTimerStart(ms_timer_btn, 0);

    ms_timer_led_indicate = xTimerCreate("ms_timer_led_indicate",
                                         pdMS_TO_TICKS(APP_TIMEOUT_LED_INDICATE),
                                         pdTRUE,
                                         NULL,
                                         app_bt_timeout_led_indicate);

    timer_led_blink = xTimerCreate("led_timer",
                                   pdMS_TO_TICKS(APP_TIMEOUT_LED_BLINK),
                                   pdFALSE,
                                   NULL,
                                   app_bt_timeout_led_blink);

    /* Main button task – priority 5 (must exceed audio task at 4) */
    xTaskCreate(button_task,
                "Button task",
                BTN_TASK_STACK_SIZE,
                NULL,
                BTN_TASK_PRIORITY,
                &button_handle);

#ifdef SNOREGUARD_ENABLE_LEVEL_BUTTON
    /* Optional level-adjustment button on P9_2 */
    s_level_btn_sem = xSemaphoreCreateBinary();
    cyhal_gpio_init(LEVEL_BTN_PIN,
                    CYHAL_GPIO_DIR_INPUT,
                    CYHAL_GPIO_DRIVE_PULLUP,
                    1);
    level_btn_cb_data.callback     = level_btn_isr;
    level_btn_cb_data.callback_arg = NULL;
    cyhal_gpio_register_callback(LEVEL_BTN_PIN, &level_btn_cb_data);
    cyhal_gpio_enable_event(LEVEL_BTN_PIN,
                            CYHAL_GPIO_IRQ_FALL,
                            LEVEL_BTN_IRQ_PRIORITY,
                            true);
    xTaskCreate(level_btn_task,
                "LevelBtn",
                LEVEL_BTN_TASK_STACK,
                NULL,
                LEVEL_BTN_TASK_PRIORITY,
                NULL);
    printf("[LevelBtn] P9_2 level-adjust button enabled.\r\n");
#endif
}

/*******************************************************************************
 * Button Task
 *
 * Short press (50-250 ms): trigger Morning Sync – streams flash log via BLE
 * Long press  (5-10 s)   : enter pairing mode
 *******************************************************************************/

void button_task(void *arg)
{
    static uint32_t btn_press_duration;
    wiced_result_t  result;

    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (CYBSP_BTN_PRESSED == cyhal_gpio_read(CYBSP_USER_BTN))
        {
            is_btn_pressed  = TRUE;
            btn_press_start = hello_sensor_state.timer_count_ms;
            cyhal_gpio_write(CYBSP_USER_LED2, CYBSP_LED_STATE_ON);
        }
        else if (0 != btn_press_start)
        {
            is_btn_pressed  = FALSE;
            cyhal_gpio_write(CYBSP_USER_LED2, CYBSP_LED_STATE_OFF);
            btn_press_duration = hello_sensor_state.timer_count_ms - btn_press_start;

            /* ---- SHORT PRESS: Morning Sync ---- */
            if ((btn_press_duration > APP_BTN_PRESS_SHORT_MIN) &&
                (btn_press_duration <= APP_BTN_PRESS_SHORT_MAX))
            {
                printf("[Button] Short press – Morning Sync requested.\r\n");
                if (0 == hello_sensor_state.conn_id)
                {
                    printf("[Button] No BLE connection – starting advertisement.\r\n");
                    result = wiced_bt_start_advertisements(
                        BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, NULL);
                    (void)result;
                }
                else
                {
                    /* Signal the GATT handler to start streaming the log */
                    app_bt_morning_sync_start();
                }
            }
            /* ---- LONG PRESS (5-10 s): Pairing Mode ---- */
            else if ((btn_press_duration > APP_BTN_PRESS_5S) &&
                     (btn_press_duration < APP_BTN_PRESS_10S))
            {
                printf("[Button] Long press – entering pairing mode.\r\n");
#ifdef PSOC6_BLE
                pairing_mode = TRUE;
                wiced_bt_start_advertisements(BTM_BLE_ADVERT_OFF, 0, NULL);
                wiced_bt_ble_address_resolution_list_clear_and_disable();
#endif
                result = wiced_bt_start_advertisements(
                    BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, NULL);
                (void)result;

                xTimerStop(ms_timer_led_indicate, 0);
                led_indicate_count = 0;
                cyhal_gpio_write(CYBSP_USER_LED2, CYBSP_LED_STATE_OFF);
            }

            btn_press_start = 0;
        }
    }
}
