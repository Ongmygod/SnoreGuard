/*******************************************************************************
 * File Name: posture_validation.c
 *
 * Description: Fast posture-change validation for SnoreGuard.
 *
 * Ported from: code/Detection_Logic.c::POSTURE_CONFIRMATION
 *******************************************************************************/

#include "posture_validation.h"
#include "snore_detect.h"       /* snore_haptic_level_increment() */
#include "cy_retarget_io.h"

/*******************************************************************************
 * Private State
 *******************************************************************************/

static bool     s_pv_active        = false;
static uint32_t s_silence_start_s  = 0;
static bool     s_silence_started  = false;

/*******************************************************************************
 * Public API
 *******************************************************************************/

void posture_validation_start(void)
{
    s_pv_active       = true;
    s_silence_start_s = 0u;
    s_silence_started = false;

#ifdef SNOREGUARD_DEBUG_LOG
    printf("[PostureVal] Validation window started (%us timeout).\r\n",
           FAST_POSTURE_VALIDATION_S);
#endif
}

void posture_validation_on_frame(bool snore_now, uint32_t now_s)
{
    if (!s_pv_active) return;

    if (snore_now)
    {
        /* Snoring resumed → FAIL: escalate haptic level */
        s_pv_active       = false;
        s_silence_started = false;
        snore_haptic_level_increment();
        printf("[PostureVal] FAIL – snoring resumed. Haptic level bumped.\r\n");
    }
    else
    {
        /* Silence frame – start or continue timing */
        if (!s_silence_started)
        {
            s_silence_start_s = now_s;
            s_silence_started = true;
        }
        else if ((now_s - s_silence_start_s) >= FAST_POSTURE_VALIDATION_S)
        {
            /* Held silence for 10 s → SUCCESS */
            s_pv_active       = false;
            s_silence_started = false;
            printf("[PostureVal] SUCCESS – no snore for %us.\r\n",
                   FAST_POSTURE_VALIDATION_S);
        }
    }
}
