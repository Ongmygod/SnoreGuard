# SnoreGuard Firmware — Feature Reference

**Platform:** Infineon PSoC6 CY8CKIT-062S2-AI  
**Toolchain:** GCC_ARM / ModusToolbox  
**RTOS:** FreeRTOS  

---

## Overview

SnoreGuard is an edge-AI sleep monitor that detects snoring from the onboard PDM/PCM microphone, delivers haptic feedback to prompt a posture change, and logs events to non-volatile flash for review the next morning via a BLE-connected phone app.

---

## Feature Summary

### 1. Snore Detection (AI Inference)

**What it does:**  
Continuously reads audio from the PDM/PCM microphone at 16 kHz, normalises each sample, and feeds it into the Imagimob IMAI model running on-device. Each time the model produces an output, the firmware decides whether snoring is occurring.

**Audio label hysteresis:**  
To prevent rapid flickering between "snore" and "not snoring" labels on borderline audio frames, the firmware applies a two-threshold stickiness rule:
- A new non-silent label is only accepted if its model score is **≥ 0.35** (entry threshold).
- Once accepted, that label stays active as long as its score stays **≥ 0.20** (retain threshold).
- Below both thresholds the result reverts to silent.

**Relevant files:** `source/main.c` (audio task), `models/model.h/c` (IMAI model)

---

### 2. Sliding Window Haptic Trigger

**What it does:**  
Maintains a rolling 60-second window of snore-event timestamps. When **5 or more** snore events occur within that window, the haptic motor fires to alert the user.

**Cooldown:**  
After the haptic fires, a **10-minute cooldown** prevents the motor from re-triggering during the same snoring episode. Once the cooldown expires, the window resets and detection resumes normally.

**Relevant files:** `source/snore/snore_detect.c`, `source/snore/snore_detect.h`

---

### 3. Haptic Motor — 6-Round Shake Sequence

**What it does:**  
Instead of a single continuous buzz, the motor fires in **6 alternating on/off pulses** (500 ms on, 500 ms off) for a more physically noticeable wake stimulus. The sequence runs asynchronously via a FreeRTOS software timer so it does not block the audio or BLE tasks.

**Intensity levels:**  
Five levels (0–4) map to PWM duty cycles of 20%, 40%, 60%, 80%, 100%. The level is set by the phone via BLE or auto-escalated by the posture validation system.

| Level | Duty Cycle | Description |
|-------|-----------|-------------|
| 0 | 20% | Lightest — gentle reminder |
| 1 | 40% | Moderate |
| 2 | 60% | Default starting level |
| 3 | 80% | Strong |
| 4 | 100% | Maximum intensity |

**Relevant files:** `source/app_hw/app_hw_device.c`

---

### 4. Posture Validation & Auto-Escalation

**What it does:**  
After the haptic motor fires, a **10-second fast validation window** opens. The firmware monitors whether the user stops snoring:

- **SUCCESS:** No snore detected for 10 consecutive seconds → posture change confirmed, haptic level unchanged.
- **FAIL:** Snoring resumes within 10 seconds → haptic level automatically increments by 1 (up to the maximum level 4) so the next intervention is stronger.

This provides fast feedback and progressively escalating intensity without requiring phone interaction.

**Long-term outcome tracking (15-minute window):**  
Separately, a 15-minute window determines the `haptic_success` field stored in the flash event log — used by the phone app to show sleep quality trends. Both windows run concurrently.

**Relevant files:** `source/snore/posture_validation.c/.h`, `source/snore/snore_detect.c`

---

### 5. Timestamps — Hardware RTC with BLE Sync

**What it does:**  
Snore events are stored with Unix timestamps. The firmware maintains an accurate real-time clock using the PSoC6 hardware RTC (`cyhal_rtc`), seeded by the phone at the start of each session.

**Priority order for timestamps:**
1. **Hardware RTC** (after phone sync) — accurate calendar time, survives BLE disconnects.
2. **BLE epoch + uptime** — if the phone connected but the RTC is not yet seeded.
3. **Uptime fallback** — if no phone has connected within 5 minutes; timestamps are relative to a fixed 2023 base so the phone app can identify them as estimates.

**Syncing:** The phone writes the current Unix epoch to the Time Sync BLE characteristic at the start of a session. This seeds both the software epoch state and the hardware RTC in one step.

**Relevant files:** `source/snore/rtc_time.c/.h`, `source/snore/snore_detect.c`

---

### 6. Non-Volatile Event Log (Flash Storage)

**What it does:**  
Every snore event is stored in a 200-entry SRAM circular ring buffer and immediately flushed to non-volatile internal flash via the PSoC6 kv-store middleware. Events survive power cycles and board resets.

**Each event record (7 bytes, packed):**

| Bytes | Field | Description |
|-------|-------|-------------|
| 0–3 | `timestamp` | Unix epoch of snore start |
| 4 | `duration_s` | Snore duration in seconds (max 255) |
| 5 | `haptic_success` | 1 = snoring stopped within 15 min after motor fired |
| 6 | `haptic_flag` | 1 = haptic motor fired during this event |

**Capacity:** 200 events. Oldest events are overwritten when full (circular buffer).

**Relevant files:** `source/snore/snore_flash_log.c/.h`

---

### 7. BLE Connectivity (Sleep Monitor Service)

**What it does:**  
The device advertises as `SnoreGuard` on boot and exposes a custom BLE GATT service for phone-side configuration and data retrieval.

**BLE Characteristics:**

| Characteristic | Direction | Function |
|---------------|-----------|----------|
| **Time Sync** | Phone → Device | Phone sends current Unix epoch; seeds hardware RTC and software timestamp |
| **Log Transfer** | Device → Phone | Device streams stored events as 7-byte notifications (Morning Sync) |
| **Log Transfer CCCD** | Phone → Device | Enables/disables BLE notifications; saved to flash for bonded devices |
| **Haptic Intensity** | Phone → Device | Sets haptic level 0–4 (overrides auto-escalation) |
| **Sync Ack** | Phone → Device | App writes `0x01` after saving events to SQLite; triggers log clear on device |
| **Haptic Enable** | Both (Read/Write) | `0x01` = motor fires normally (default); `0x00` = motor suppressed |

**Bond persistence:** Bonding information (link keys, CCCD) is stored in the same kv-store partition as the event log and survives power cycles.

**Relevant files:** `source/app_bt/app_bt_gatt_handler.c`, `source/app_bt/app_bt_event_handler.c`, `source/app_bt/app_bt_bonding.c`

---

### 8. Haptic Enable / Disable

**What it does:**
The phone app can fully suppress the haptic motor via the **Haptic Enable** BLE characteristic. When disabled (`0x00`), `trigger_haptic()` in `snore_detect.c` becomes a no-op — the sliding-window logic still runs and snore events are still logged, but the motor never fires. Re-enabling (`0x01`) restores normal operation. The enabled state is readable by the phone so the UI always reflects the current device setting.

**Implementation:** `snore_set_haptic_enabled()` / `snore_get_haptic_enabled()` in `snore_detect.c`; GATT handle `0x0015` (UUID `e51f5698-...`) in `app_bt_gatt_handler.c`.

**Relevant files:** `source/snore/snore_detect.c/.h`, `source/app_bt/app_bt_gatt_handler.c`

---

### 9. Morning Sync (Log Transfer over BLE)

**What it does:**  
After waking up, the user short-presses the button to stream all stored events to the phone as a sequence of BLE notifications, one 7-byte packet per event. After all events are sent successfully, the log is cleared from both SRAM and flash.

**Flow:**  
Short press → `app_bt_morning_sync_start()` → sends first event notification → each `GATT_APP_BUFFER_TRANSMITTED_EVT` automatically triggers the next event → all events sent → `snore_log_clear()`.

If the phone is not connected when the button is pressed, the firmware starts BLE advertising instead.

**Relevant files:** `source/app_bt/app_bt_gatt_handler.c`, `source/app_hw/app_hw_device.c`

---

### 10. Button Controls

**Main button (CYBSP_USER_BTN):**

| Press Duration | Action |
|---------------|--------|
| 50–250 ms (short) | Morning Sync — stream flash log to phone (or start advertising if disconnected) |
| 5–10 s (long) | Enter pairing mode — clears resolution list and starts high-duty advertising |

**Optional level-adjust button (P9_2):**  
Enable by adding `SNOREGUARD_ENABLE_LEVEL_BUTTON` to `Makefile DEFINES`. Each press increments the haptic level by 1 (wraps from 4 back to 0 only via BLE write — auto-escalation only goes up). Debounced at 200 ms.

**Relevant files:** `source/app_hw/app_hw_device.c/.h`

---

### 11. Debug UART Report

**What it does:**  
When `SNOREGUARD_DEBUG_LOG` is defined (on by default in the Makefile), the firmware prints detailed logs to the UART debug port (115200 baud):

| Tag | Source | Content |
|-----|--------|---------|
| `[AudioTask]` | `main.c` | Model class count on boot, PDM errors |
| `[SnoreDetect]` | `snore_detect.c` | Snore start/end, haptic trigger, cooldown, level changes |
| `[PostureVal]` | `posture_validation.c` | 10-s validation SUCCESS / FAIL, level bump |
| `[Haptic]` | `app_hw_device.c` | Shake sequence progress, motor stop |
| `[RTC]` | `rtc_time.c` | Init status, epoch writes |
| `[FlashLog]` | `snore_flash_log.c` | Event add, flush, restore on boot |
| `[GATT]` | `app_bt_gatt_handler.c` | Characteristic reads/writes, Morning Sync status |
| `[Button]` | `app_hw_device.c` | Short/long press actions |
| `[MorningSync]` | `app_bt_gatt_handler.c` | Event streaming progress |

Call `snore_log_print_report()` at any point to dump the entire current event log as a formatted table to UART.

---

## Startup Sequence

```
Power on / Reset
  ├─ cybsp_init()                  — BSP and clocks
  ├─ retarget-io init              — UART debug port (115200 baud)
  ├─ snore_detect_init()           — clear detection state
  ├─ rtc_time_init()               — init hardware RTC (not yet seeded)
  ├─ cybt_platform_config_init()   — BLE platform config
  ├─ app_kvstore_bd_config()       — flash block device
  ├─ xTaskCreate(audio_inference_task)  — starts PDM capture + IMAI model
  ├─ wiced_bt_stack_init()         — starts BLE stack
  │     └─ BTM_ENABLED_EVT
  │           ├─ app_kv_store_init()   — kvstore ready
  │           ├─ snore_log_init()      — restore events from flash
  │           ├─ app_bt_hw_init()      — haptic PWM, timers, button_task
  │           └─ wiced_bt_start_advertisements() — start advertising
  └─ vTaskStartScheduler()         — hand control to FreeRTOS
```

---

## Key Tunable Parameters

| Parameter | File | Default | Description |
|-----------|------|---------|-------------|
| `SNORE_SLIDING_WINDOW_S` | `snore_detect.h` | 60 s | Rolling window for snore counting |
| `SNORE_SLIDING_WINDOW_COUNT` | `snore_detect.h` | 5 | Events in window to trigger haptic |
| `SNORE_HAPTIC_COOLDOWN_S` | `snore_detect.h` | 600 s | 10-min cooldown after haptic fires |
| `SNORE_POST_HAPTIC_SUCCESS_S` | `snore_detect.h` | 900 s | 15-min window for success/fail log field |
| `FAST_POSTURE_VALIDATION_S` | `posture_validation.h` | 10 s | Silence needed to confirm posture change |
| `AUDIO_LABEL_ENTRY_THRESHOLD` | `main.c` | 0.35 | Min score to accept a new active label |
| `AUDIO_LABEL_RETAIN_THRESHOLD` | `main.c` | 0.20 | Min score to keep sticky label active |
| `HAPTIC_SHAKE_ROUNDS` | `app_hw_device.c` | 6 | Pulses per haptic intervention |
| `HAPTIC_SHAKE_ON_MS` | `app_hw_device.c` | 500 ms | Motor-on duration per pulse |
| `HAPTIC_SHAKE_OFF_MS` | `app_hw_device.c` | 500 ms | Gap between pulses |
| `SNORE_LOG_MAX_EVENTS` | `snore_flash_log.h` | 200 | Flash log capacity (events) |
| `AUXILIARY_FLASH_LENGTH` | `app_flash_common.h` | 64 pages | kv-store partition (32 KB) |
