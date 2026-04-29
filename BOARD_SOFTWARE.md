# SnoreGuard Board Software — Source File Guide

Firmware project: `snoregurad_board/SnoreGuard_Firmware/`
Platform: Infineon PSoC™ 6 CY8CKIT-062S2-AI | IDE: ModusToolbox™ 3.7 | RTOS: FreeRTOS

---

## Entry Point

### `source/main.c`
The firmware's starting point. Creates the `audio_inference_task` (priority 4, stack 4096 words) which runs forever in a loop: reads a PDM/PCM audio sample from the built-in MEMS microphone via the Imagimob `IMAI_enqueue()` API, waits for `IMAI_dequeue()` to return label scores, applies **audio hysteresis** (entry threshold 0.35, retain threshold 0.20) to prevent rapid label flickering, then calls `snore_detect_on_decision(is_snore)` with the final binary decision. Also defines `vApplicationStackOverflowHook` for FreeRTOS crash diagnostics.

---

## `source/snore/` — Core Detection Logic

### `snore_detect.h` / `snore_detect.c`
The brain of the system. Receives every inference frame and runs the full snore detection pipeline:

- **Episode merging**: groups rapid successive snore breaths (separated by < 15 s) into a single logged episode, preventing the ring buffer from filling with individual breaths. Discards episodes shorter than 3 s as noise.
- **Sliding window haptic trigger**: fires the haptic motor when 5 snore events occur within any 60-second window.
- **10-minute cooldown**: suppresses re-triggering after a haptic fire for 600 seconds.
- **Post-haptic success/fail**: opens a 15-minute monitoring window after the motor fires; sets `haptic_success = 1` if no snoring is detected for the full 900 seconds.
- **Timestamp management**: provides `snore_get_timestamp()` with a three-tier priority — hardware RTC (when seeded) → BLE epoch + uptime offset → fallback epoch `1700000000 + uptime`.
- **BLE time sync state machine**: tracks states `TIME_SYNC_NONE / CONNECTED / OK / FALLBACK / STANDALONE` and handles the 30-second Time Sync timeout and 5-minute BLE connect timeout.

Key tuneable constants: `SNORE_SLIDING_WINDOW_S`, `SNORE_SLIDING_WINDOW_COUNT`, `SNORE_HAPTIC_COOLDOWN_S`, `SNORE_EPISODE_GAP_S`.

### `snore_flash_log.h` / `snore_flash_log.c`
Dual-tier event persistence:

- **Tier 1 (SRAM)**: circular ring buffer of up to 200 `snore_event_t` records (7 bytes each). Fastest read access; used by BLE Morning Sync streaming.
- **Tier 2 (Flash)**: after every new event, `snore_log_flush_to_flash()` serializes the buffer to the kv-store under keys `sg_count` and `sg_data`. On boot, `snore_log_init()` restores the last saved state so data survives power cycles.
- **Morning Sync interface**: `snore_log_get_event(index, &event)` is called by the GATT handler to stream records one by one via BLE notifications.
- **Clear on sync**: `snore_log_clear()` wipes both SRAM and flash after a successful Morning Sync transfer.

### `posture_validation.h` / `posture_validation.c`
Fast haptic escalation layer (ported from `code/Detection_Logic.c`). Runs concurrently with the 15-minute window in `snore_detect.c`, but operates on a 10-second scale:

- **Start**: called immediately when the haptic motor fires (`posture_validation_start()`).
- **Each frame**: `posture_validation_on_frame(snore_now, now_s)` counts consecutive silent seconds.
- **Success** (10 s of silence): no action — haptic level stays the same.
- **Fail** (snore resumes within 10 s): calls `snore_haptic_level_increment()` to bump the PWM intensity by one level (clamped at level 4 = 100%).

### `rtc_time.h` / `rtc_time.c`
Hardware RTC wrapper (ported from `code/RTC.c`). Wraps the PSoC 6 `cyhal_rtc_t` peripheral:

- `rtc_time_init()` — called once in `main()` before the BT stack starts.
- `rtc_time_set_unix(epoch)` — called when the phone writes the Time Sync characteristic; converts a Unix epoch into the RTC's calendar format and seeds the hardware clock.
- `rtc_time_get_unix()` — reads current time from the RTC and converts back to Unix epoch.
- `rtc_time_is_valid()` — returns `true` once seeded; `snore_detect.c` uses this as the top-priority timestamp source.

Once seeded, the RTC keeps accurate time even through BLE disconnects.

---

## `source/app_hw/` — Hardware Peripherals

### `app_hw_device.h` / `app_hw_device.c`
All direct hardware I/O:

- **Haptic motor PWM**: initializes `cyhal_pwm_t` on `HAPTIC_MOTOR_PIN` (default `CYBSP_USER_LED1` for dev-kit; change to `P10_0` for hardware). Drives at 200 Hz with 5 discrete duty cycles (20–100%). `haptic_motor_fire(level)` runs a **6-round shake sequence** (500 ms on / 500 ms off per round) using a one-shot FreeRTOS timer re-armed in its own callback; calls `snore_on_haptic_complete()` when all rounds finish.
- **Button task** (priority 5): monitors `CYBSP_USER_BTN` via GPIO interrupt. Short press (50–250 ms) → triggers Morning Sync via `app_bt_morning_sync_start()`. Long press (5–10 s) → enters BLE pairing mode.
- **LED2**: status indicator — blinking patterns signal BLE advertising/connection state.
- **Optional `level_btn_task`**: enabled by the `SNOREGUARD_ENABLE_LEVEL_BUTTON` compile flag; listens on `P9_2` and calls `snore_haptic_level_increment()` on each press for manual testing.

### `app_flash_common.h` / `app_internal_aux_flash.c`
Configures the kv-store block device that maps to a 32 KB partition (`AUXILIARY_FLASH_LENGTH = 64` pages × 512 bytes) at the end of PSoC 6 internal flash Block 1. Both BLE bonding data and snore event logs share this partition through the same `kvstore_obj` handle defined in `app_bt_bonding.c`.

---

## `source/app_bt/` — Bluetooth LE Stack

### `app_bt_event_handler.h` / `app_bt_event_handler.c`
Top-level BLE stack management callback (`app_bt_management_callback`). Handles:

- Stack enable/disable events — calls `app_bt_application_init()` on stack ready.
- Pairing and bonding events — stores/retrieves link keys via `app_bt_bonding.c`.
- Advertisement state changes.
- Creates two FreeRTOS software timers on init: the 30-second Time Sync timeout and the 5-minute BLE connect timeout; both feed into `snore_detect.c`'s time sync state machine on expiry.

Owns the global `hello_sensor_state` struct that tracks the active connection ID, remote address, peer MTU, and millisecond timer tick counter used for button press timing.

### `app_bt_gatt_handler.h` / `app_bt_gatt_handler.c`
All GATT read/write/notify logic:

- `app_bt_gatt_callback` — dispatches GATT connection and attribute request events.
- **Time Sync write** (handle `0x000C`): extracts the 4-byte little-endian epoch from the phone's write, calls `snore_set_epoch_base()` and `rtc_time_set_unix()`, stops the 30-second timeout timer.
- **Haptic Intensity write** (handle `0x0011`): calls `snore_set_haptic_level(0-4)`.
- **Morning Sync streaming**: `app_bt_morning_sync_start()` (called by `button_task`) flushes any open episode, **snapshots the event count into `s_morning_sync_total`**, then begins sending 7-byte `snore_event_t` packets via `GATT_APP_BUFFER_TRANSMITTED_EVT`-driven calls to `app_bt_morning_sync_send_next()`. The snapshot is critical: the audio inference task keeps running during sync and continuously calls `snore_log_add_event()`, which increments the live `s_count`. Without the snapshot, `snore_log_get_event()` would keep returning `true` as new events are appended, causing the sync to overshoot. `app_bt_morning_sync_send_next()` terminates when `s_morning_sync_idx >= s_morning_sync_total`. After the last event is sent, sets `s_pending_ack = true` — the log is **not** cleared yet.
- **Sync Ack write** (handle `0x0013`): app writes `0x01` after successfully inserting all events into SQLite. Firmware calls `snore_log_clear()` only on receiving this ack. Writing `0x00` or disconnecting without writing preserves the log so the next Morning Sync can re-transmit.
- `app_bt_set_value()` — generic attribute write dispatcher used for all characteristics.

### `app_bt_bonding.h` / `app_bt_bonding.c`
BLE link-key and CCCD persistence. Initializes the shared kv-store (`kvstore_obj`) that is also used by `snore_flash_log.c`. Stores and retrieves peer identity keys and CCCD subscription state across power cycles so the phone does not need to re-pair after a reboot.

### `app_bt_utils.h` / `app_bt_utils.c`
Small utility helpers: functions to print BLE device addresses and event names in human-readable form to the debug UART. Used only in debug builds.

---

## `GeneratedSource/` — ModusToolbox™ Generated Files

### `cycfg_gatt_db.h` / `cycfg_gatt_db.c`
Auto-generated GATT attribute database. Defines the Sleep Monitor Service (128-bit base UUID `E0-1F-56-98...`) and its four characteristics with their attribute handles:

| Handle | Characteristic | Properties |
|--------|---------------|------------|
| `0x000C` | Time Sync | Write |
| `0x000E` | Log Transfer | Notify |
| `0x000F` | Log Transfer CCCD | Read/Write |
| `0x0011` | Haptic Intensity | Read/Write |
| `0x0013` | Sync Ack | Write |
| `0x0015` | Haptic Enable | Read/Write |

Do not edit manually — regenerate via ModusToolbox™ Device Configurator if the service definition changes.

### `cycfg_gap.h` / `cycfg_gap.c`
BLE GAP advertising configuration. Sets the advertised device name to `"SnoreGuard"` (10 bytes) and configures the advertising payload. The 128-bit service UUID was deliberately removed from the advertising packet to stay within the 31-byte BLE advertising payload limit; the service is discoverable via GATT service discovery after connection.

### `cycfg_bt_settings.h` / `cycfg_bt_settings.c`
BLE stack configuration parameters (scan/advert intervals, security modes, MTU size). Generated by ModusToolbox™ Bluetooth Configurator.

---

## `models/` — AI Model (Imagimob Generated)

### `model.h` / `model.c`
The quantized Conv1D snore detection model exported from Imagimob DeepCraft as optimized C code for the ARM Cortex-M4. Provides:

- `IMAI_enqueue(sample)` — feed one 16 kHz PCM audio sample into the model's input buffer.
- `IMAI_dequeue(scores)` — retrieve the latest per-class confidence scores (float array of length `IMAI_DATA_OUT_COUNT`).

The current build contains the Imagimob speech demo model (3 classes). Replace `model.h` and `model.c` with the exported files from the winning pipeline after DeepCraft benchmarking, then set `SNORE_LABEL_IDX` in `main.c` to the correct snore class index.

---

## FreeRTOS Task Summary

| Task | Priority | Stack | File |
|------|----------|-------|------|
| `audio_inference_task` | 4 | 4096 words | `source/main.c` |
| `button_task` | 5 | 2048 words | `source/app_hw/app_hw_device.c` |
| `level_btn_task` *(optional)* | 4 | 512 words | `source/app_hw/app_hw_device.c` |
| FreeRTOS timer service | 3 | 256 words | FreeRTOS internals |
| BT stack internals | varies | managed by WICED | `wiced_bt_stack_init()` |

`button_task` must be higher priority than `audio_inference_task` so GPIO interrupts are serviced without delay. The audio task uses `vTaskDelay(1 ms)` rather than `taskYIELD()` to give lower-priority tasks a chance to run.

---

## Data Flow Summary

```
PDM/PCM mic (16 kHz)
  → IMAI_enqueue(sample)                  [models/model.c]
  → IMAI_dequeue(scores) + hysteresis     [source/main.c]
  → snore_detect_on_decision(is_snore)    [source/snore/snore_detect.c]
      → episode merge + sliding window
      → haptic_motor_fire(level)          [source/app_hw/app_hw_device.c]
          → 6-round shake timer sequence
          → snore_on_haptic_complete()    [source/snore/snore_detect.c]
      → posture_validation_on_frame()     [source/snore/posture_validation.c]
      → snore_log_add_event()             [source/snore/snore_flash_log.c]
          → SRAM ring buffer
          → mtb_kvstore_write()           [kv-store / internal flash]
```
