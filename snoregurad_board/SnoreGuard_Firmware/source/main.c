/*******************************************************************************
 * File Name: main.c
 *
 * Description: SnoreGuard firmware entry point.
 *
 * Architecture:
 *   CM4 FreeRTOS tasks:
 *     1. audio_inference_task (priority 4)
 *          - PDM/PCM capture at 16 kHz
 *          - IMAI model inference (enqueue/dequeue)
 *          - Calls snore_detect_on_inference() on each model output
 *     2. button_task (priority 2, created by app_bt_hw_init)
 *          - Short press → Morning Sync (BLE log stream)
 *          - Long press  → pairing mode
 *     3. BT stack task (priority set by WICED BTStack internals)
 *
 *   BLE lifecycle:
 *     - Advertising as "SnoreGuard" on boot
 *     - Phone writes Time Sync characteristic to set epoch timestamp
 *     - Phone enables Log Transfer notifications
 *     - User short-presses button → device sends all flash log events
 *
 *   Timestamp fallback:
 *     - If no BLE within 5 min: standalone mode with uptime-based timestamps
 *     - If connected but no Time Sync within 30 s: uptime fallback timestamps
 *
 * NOTE on the AI model:
 *   The included models/model.h/c is the Imagimob speech demo model (36 classes).
 *   For the real SnoreGuard deployment, replace it with the trained snore-detection
 *   model and set SNORE_LABEL_IDX to the correct snore class index.
 *   Currently: any non-"unlabelled" (idx 0) class with score > 0.5 = snore.
 *******************************************************************************/

#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cyhal.h"
#include <float.h>
#include <string.h>

/* BLE / FreeRTOS stack */
#include "app_flash_common.h"
#include "app_bt_bonding.h"
#include <FreeRTOS.h>
#include <task.h>
#include "cycfg_bt_settings.h"
#include "wiced_bt_stack.h"
#include "cybsp_bt_config.h"
#include "cybt_platform_config.h"
#include "app_bt_event_handler.h"
#include "app_bt_gatt_handler.h"
#include "app_hw_device.h"
#include "app_bt_utils.h"

/* Snore detection */
#include "snore_detect.h"
#include "snore_flash_log.h"
#include "rtc_time.h"

/* DEEPCRAFT model */
#include "models/model.h"

#ifdef ENABLE_BT_SPY_LOG
#include "cybt_debug_uart.h"
#endif

/*******************************************************************************
 * Audio Configuration (from DEEPCRAFT template)
 *******************************************************************************/
#define SAMPLE_RATE_HZ          16000u
#define AUDIO_SYS_CLOCK_HZ      24576000u
#define DECIMATION_RATE         64u
#define MICROPHONE_GAIN         20      /* 0.5 dB steps: 20 = +10 dB */
#define DIGITAL_BOOST_FACTOR    10.0f
#define AUDIO_BITS_PER_SAMPLE   16u
#define PDM_DATA                P10_5
#define PDM_CLK                 P10_4
#define AUDIO_BUFFER_SIZE       512u

/* Normalise int16 PCM sample to [-1.0, +1.0] */
#define SAMPLE_NORMALIZE(s)  (((float)(s)) / (float)(1u << (AUDIO_BITS_PER_SAMPLE - 1u)))

/*******************************************************************************
 * Snore Model Label Index
 * 0 = "unlabelled" in the speech demo model.
 * Set to the correct index when the real snore model is deployed.
 * -1 = auto-select: any non-zero label with score > threshold = snore.
 *******************************************************************************/
#define SNORE_LABEL_IDX         (-1)

/*******************************************************************************
 * Audio Label Hysteresis Thresholds
 * Ported from code/Audio.c for reduced classification flicker.
 *
 * ENTRY:  minimum score for a new label to become "active"
 * RETAIN: minimum score for the previous label to remain sticky
 *
 * Effect: once a non-silent label exceeds ENTRY, it stays active as long as
 * its score stays above RETAIN, even if it dips below ENTRY briefly.
 *******************************************************************************/
#define AUDIO_LABEL_ENTRY_THRESHOLD     0.35f
#define AUDIO_LABEL_RETAIN_THRESHOLD    0.20f

/*******************************************************************************
 * Task Configuration
 *******************************************************************************/
#define AUDIO_TASK_STACK_SIZE   (4096u)
#define AUDIO_TASK_PRIORITY     (4u)

/*******************************************************************************
 * Private: Board and Audio Init
 *******************************************************************************/

static cyhal_clock_t s_audio_clock;
static cyhal_clock_t s_pll_clock;

static void pdm_frequency_fix(void)
{
    /* Workaround: write correct divider values to PDM clock register.
     * See DEEPCRAFT Deploy Model Audio example for context.              */
    static uint32_t *pdm_reg = (uint32_t *)(0x40A00010u);
    uint32_t clk1  = 2u;
    uint32_t clk2  = 1u;
    uint32_t clk3  = 8u;
    uint32_t sinc  = AUDIO_SYS_CLOCK_HZ / (clk1 * clk2 * clk3 * 2u * SAMPLE_RATE_HZ);
    uint32_t val   = ((clk1 - 1u) << 0u) |
                     ((clk2 - 1u) << 4u) |
                     ((clk3 - 1u) << 8u) |
                     (sinc        << 16u);
    *pdm_reg = val;
}

static cy_rslt_t init_audio(cyhal_pdm_pcm_t *pdm_pcm)
{
    cy_rslt_t result;

    const cyhal_pdm_pcm_cfg_t pdm_cfg = {
        .sample_rate     = SAMPLE_RATE_HZ,
        .decimation_rate = DECIMATION_RATE,
        .mode            = CYHAL_PDM_PCM_MODE_LEFT,
        .word_length     = (uint8_t)AUDIO_BITS_PER_SAMPLE,
        .left_gain       = MICROPHONE_GAIN,
        .right_gain      = MICROPHONE_GAIN,
    };

    result = cyhal_clock_reserve(&s_pll_clock, &CYHAL_CLOCK_PLL[1]);
    if (CY_RSLT_SUCCESS != result) return result;
    result = cyhal_clock_set_frequency(&s_pll_clock, AUDIO_SYS_CLOCK_HZ, NULL);
    if (CY_RSLT_SUCCESS != result) return result;
    result = cyhal_clock_set_enabled(&s_pll_clock, true, true);
    if (CY_RSLT_SUCCESS != result) return result;

    result = cyhal_clock_reserve(&s_audio_clock, &CYHAL_CLOCK_HF[1]);
    if (CY_RSLT_SUCCESS != result) return result;
    result = cyhal_clock_set_source(&s_audio_clock, &s_pll_clock);
    if (CY_RSLT_SUCCESS != result) return result;
    result = cyhal_clock_set_enabled(&s_audio_clock, true, true);
    if (CY_RSLT_SUCCESS != result) return result;

    result = cyhal_pdm_pcm_init(pdm_pcm, PDM_DATA, PDM_CLK,
                                 &s_audio_clock, &pdm_cfg);
    if (CY_RSLT_SUCCESS != result) return result;

    result = cyhal_pdm_pcm_clear(pdm_pcm);
    if (CY_RSLT_SUCCESS != result) return result;

    pdm_frequency_fix();

    result = cyhal_pdm_pcm_start(pdm_pcm);
    return result;
}

/*******************************************************************************
 * audio_inference_task
 *
 * Continuously captures audio and feeds samples to the IMAI model.
 * On each inference output, calls snore_detect_on_inference().
 *******************************************************************************/

static void audio_inference_task(void *arg)
{
    (void)arg;

    int16_t  audio_buf[AUDIO_BUFFER_SIZE];
    float    label_scores[IMAI_DATA_OUT_COUNT];
    float    sample;
    size_t   audio_count;
    cy_rslt_t result;
    cyhal_pdm_pcm_t pdm_pcm;

    /* Label stickiness state (ported from code/Audio.c) */
    int16_t s_prev_best_label = 0;

    /* Initialise IMAI model */
    result = (cy_rslt_t)IMAI_init();
    if (result != CY_RSLT_SUCCESS)
    {
        printf("[AudioTask] ERROR: IMAI_init failed (%lu)\r\n",
               (unsigned long)result);
        vTaskDelete(NULL);
        return;
    }

    /* Initialise PDM/PCM microphone */
    result = init_audio(&pdm_pcm);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("[AudioTask] ERROR: PDM init failed (%lu)\r\n",
               (unsigned long)result);
        vTaskDelete(NULL);
        return;
    }

    printf("[AudioTask] Audio inference started. Model: %d classes.\r\n",
           IMAI_DATA_OUT_COUNT);

    for (;;)
    {
        audio_count = AUDIO_BUFFER_SIZE;
        memset(audio_buf, 0, sizeof(audio_buf));
        result = cyhal_pdm_pcm_read(&pdm_pcm, (void *)audio_buf, &audio_count);
        if (CY_RSLT_SUCCESS != result) continue;

        for (size_t i = 0; i < audio_count; i++)
        {
            /* Normalise and boost sample */
            sample = SAMPLE_NORMALIZE(audio_buf[i]) * DIGITAL_BOOST_FACTOR;
            if      (sample >  1.0f) sample =  1.0f;
            else if (sample < -1.0f) sample = -1.0f;

            /* Push to model */
            int enq = IMAI_enqueue(&sample);
            if (enq != IMAI_RET_SUCCESS && enq != IMAI_RET_NODATA) continue;

            /* Check for inference output */
            int deq = IMAI_dequeue(label_scores);
            if (IMAI_RET_SUCCESS == deq)
            {
                /* --- Audio label hysteresis (ported from code/Audio.c) ---
                 *
                 * Find the best-scoring label in this frame, then apply
                 * stickiness rules to reduce rapid flicker:
                 *
                 *  1. If the previous non-silent label still scores above
                 *     RETAIN threshold, keep it (sticky).
                 *  2. Otherwise, accept a new non-silent label only if it
                 *     exceeds the ENTRY threshold.
                 *  3. If neither condition holds, revert to silent (label 0).
                 *
                 * The resulting boolean is_snore is passed directly to
                 * snore_detect_on_decision(), bypassing the internal score
                 * processing in snore_detect_on_inference().
                 */
                float   best_score = label_scores[0];
                int16_t best_idx   = 0;
                for (int j = 1; j < IMAI_DATA_OUT_COUNT; j++)
                {
                    if (label_scores[j] > best_score)
                    {
                        best_score = label_scores[j];
                        best_idx   = (int16_t)j;
                    }
                }

                bool is_snore;
                if (s_prev_best_label != 0 &&
                    label_scores[(int)s_prev_best_label] > AUDIO_LABEL_RETAIN_THRESHOLD)
                {
                    /* Sticky: previous label is still plausible */
                    is_snore = true;
                }
                else if (best_idx != 0 && best_score >= AUDIO_LABEL_ENTRY_THRESHOLD)
                {
                    /* New non-silent label crossed the entry threshold */
                    s_prev_best_label = best_idx;
                    is_snore = true;
                }
                else
                {
                    /* Silent / below thresholds */
                    s_prev_best_label = 0;
                    is_snore = false;
                }

                snore_detect_on_decision(is_snore);
            }
        }

        /* Delay 1 ms so lower-priority tasks (button, BLE) get CPU time.
         * taskYIELD() only yields to equal/higher-priority tasks and would
         * starve button_task (priority 5 after fix, was 2).               */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/*******************************************************************************
 * main – entry point
 *******************************************************************************/

int main(void)
{
    cy_rslt_t      cy_result;
    wiced_result_t wiced_result;

    /* BSP and clock init */
    cy_result = cybsp_init();
    CY_ASSERT(CY_RSLT_SUCCESS == cy_result);

    __enable_irq();

#ifdef ENABLE_BT_SPY_LOG
    {
        cybt_debug_uart_config_t config = {
            .uart_tx_pin = CYBSP_DEBUG_UART_TX,
            .uart_rx_pin = CYBSP_DEBUG_UART_RX,
            .uart_cts_pin = CYBSP_DEBUG_UART_CTS,
            .uart_rts_pin = CYBSP_DEBUG_UART_RTS,
            .baud_rate = DEBUG_UART_BAUDRATE,
            .flow_control = TRUE,
        };
        cybt_debug_uart_init(&config, NULL);
    }
#else
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX,
                        CYBSP_DEBUG_UART_RX,
                        CY_RETARGET_IO_BAUDRATE);
#endif

    printf("\r\n");
    printf("============================================================\r\n");
    printf("  SnoreGuard Firmware – Edge AI Sleep Monitor              \r\n");
    printf("  PSoC6 CY8CKIT-062S2-AI                                  \r\n");
    printf("============================================================\r\n");

    /* Initialise snore detection (before BT stack starts timers) */
    snore_detect_init();

    /* Initialise hardware RTC – provides real calendar timestamps once seeded
     * by BLE Time Sync. Until seeded, snore_detect.c uses uptime fallback.  */
    rtc_time_init();

    /* Configure BT platform */
    cybt_platform_config_init(&cybsp_bt_platform_cfg);

    /* Init kv-store block device for bonding + flash log.
     * NOTE: snore_log_init() is NOT called here – the kvstore is not yet
     * fully initialized (app_kv_store_init() runs inside the BT stack
     * callback). snore_log_init() is called from app_bt_application_init(). */
    app_kvstore_bd_config(&block_device);

    /* Create audio inference task BEFORE starting the BT stack so that
     * PDM/PCM and model buffers are allocated before FreeRTOS scheduler runs.
     * The task waits inside init_audio for the clock to stabilise.         */
    xTaskCreate(audio_inference_task,
                "AudioInfer",
                AUDIO_TASK_STACK_SIZE,
                NULL,
                AUDIO_TASK_PRIORITY,
                NULL);

    /* Start BT stack (app_bt_application_init is called from
     * BTM_ENABLED_EVT callback, which creates button_task and timers)     */
    wiced_result = wiced_bt_stack_init(app_bt_management_callback,
                                       &wiced_bt_cfg_settings);
    if (WICED_BT_SUCCESS != wiced_result)
    {
        printf("ERROR: BT stack init failed (%d)\r\n", wiced_result);
        CY_ASSERT(0);
    }

    /* Start FreeRTOS scheduler – never returns */
    vTaskStartScheduler();

    CY_ASSERT(0);
    return 0;
}

/* FreeRTOS stack overflow hook – fires when configCHECK_FOR_STACK_OVERFLOW >= 1 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    taskDISABLE_INTERRUPTS();
    printf("\r\n!!! STACK OVERFLOW in task: %s !!!\r\n", pcTaskName);
    CY_ASSERT(0);
}
