# SnoreGuard Firmware – Setup Guide

## Overview

This project combines:
- `Bluetooth_LE_Hello_Sensor` (BLE stack, FreeRTOS, bonding, kv-store)
- `DEEPCRAFT_Deploy_Model_Audio` (PDM/PCM capture, IMAI model inference)

## Directory Structure

```
SnoreGuard_Firmware/
├── Makefile                        ← NEW (combined)
├── source/
│   ├── main.c                      ← NEW (combined entry point)
│   ├── snore/
│   │   ├── snore_detect.h/c        ← NEW (sliding window, haptic, timestamps)
│   │   └── snore_flash_log.h/c     ← NEW (dual-tier SRAM+flash event log)
│   ├── app_bt/
│   │   ├── app_bt_event_handler.h/c← MODIFIED (BLE timeout timers added)
│   │   ├── app_bt_gatt_handler.h/c ← MODIFIED (Sleep Monitor service)
│   │   ├── app_bt_bonding.h/c      ← COPY from Bluetooth_LE_Hello_Sensor
│   │   ├── app_bt_utils.h/c        ← COPY from Bluetooth_LE_Hello_Sensor
│   │   └── app_flash_common.h/c    ← COPY from Bluetooth_LE_Hello_Sensor (in app_hw/)
│   └── app_hw/
│       ├── app_hw_device.h/c       ← MODIFIED (haptic PWM added)
│       └── app_flash_common.h/c    ← COPY from Bluetooth_LE_Hello_Sensor
├── GeneratedSource/
│   ├── cycfg_gatt_db.h/c           ← NEW (Sleep Monitor GATT service)
│   ├── cycfg_bt_settings.h/c       ← COPY from Bluetooth_LE_Hello_Sensor
│   ├── cycfg_gap.h/c               ← COPY from Bluetooth_LE_Hello_Sensor (update name)
│   └── cycfg_gap.c                 ← COPY, change device name to "SnoreGuard"
├── models/
│   ├── model.h                     ← COPY from DEEPCRAFT_Deploy_Model_Audio
│   └── model.c                     ← COPY from DEEPCRAFT_Deploy_Model_Audio
├── configs/                        ← COPY from Bluetooth_LE_Hello_Sensor
└── bsps/                           ← COPY from Bluetooth_LE_Hello_Sensor
```

## Step 1: Copy Files from Templates

Run these commands from the `test_ai/` directory:

```bash
# BLE template files (unchanged)
cp Bluetooth_LE_Hello_Sensor/source/app_bt/app_bt_bonding.h    SnoreGuard_Firmware/source/app_bt/
cp Bluetooth_LE_Hello_Sensor/source/app_bt/app_bt_bonding.c    SnoreGuard_Firmware/source/app_bt/
cp Bluetooth_LE_Hello_Sensor/source/app_bt/app_bt_utils.h      SnoreGuard_Firmware/source/app_bt/
cp Bluetooth_LE_Hello_Sensor/source/app_bt/app_bt_utils.c      SnoreGuard_Firmware/source/app_bt/
cp Bluetooth_LE_Hello_Sensor/source/app_hw/app_flash_common.h  SnoreGuard_Firmware/source/app_hw/
cp Bluetooth_LE_Hello_Sensor/source/app_hw/app_flash_common.c  SnoreGuard_Firmware/source/app_hw/

# Generated BLE config (copy, then edit device name)
cp Bluetooth_LE_Hello_Sensor/GeneratedSource/cycfg_bt_settings.h  SnoreGuard_Firmware/GeneratedSource/
cp Bluetooth_LE_Hello_Sensor/GeneratedSource/cycfg_bt_settings.c  SnoreGuard_Firmware/GeneratedSource/
cp Bluetooth_LE_Hello_Sensor/GeneratedSource/cycfg_gap.h           SnoreGuard_Firmware/GeneratedSource/
cp Bluetooth_LE_Hello_Sensor/GeneratedSource/cycfg_gap.c           SnoreGuard_Firmware/GeneratedSource/

# BSP, configs, libs
cp -r Bluetooth_LE_Hello_Sensor/bsps     SnoreGuard_Firmware/
cp -r Bluetooth_LE_Hello_Sensor/configs  SnoreGuard_Firmware/
cp -r Bluetooth_LE_Hello_Sensor/libs     SnoreGuard_Firmware/

# ML model from DEEPCRAFT
cp DEEPCRAFT_Deploy_Model_Audio/models/model.h  SnoreGuard_Firmware/models/
cp DEEPCRAFT_Deploy_Model_Audio/models/model.c  SnoreGuard_Firmware/models/
```

## Step 2: Library Dependencies

The project needs libraries from BOTH templates. Copy `.mtb` files:

```bash
# From DEEPCRAFT (ML libraries not in BLE template)
cp DEEPCRAFT_Deploy_Model_Audio/deps/ml-middleware.mtb   SnoreGuard_Firmware/libs/
cp DEEPCRAFT_Deploy_Model_Audio/deps/ml-tflite-micro.mtb SnoreGuard_Firmware/libs/
cp DEEPCRAFT_Deploy_Model_Audio/deps/cmsis.mtb           SnoreGuard_Firmware/libs/
```

Then run `make getlibs` in `SnoreGuard_Firmware/` to fetch all dependencies.

## Step 3: Update Device Name in cycfg_gap.c

In `GeneratedSource/cycfg_gap.c`, change the advertisement data to use "SnoreGuard":
```c
/* Before */
uint8_t app_gap_device_name[] = {'H','e','l','l','o','\0'};

/* After */
uint8_t app_gap_device_name[] = {'S','n','o','r','e','G','u','a','r','d','\0'};
```

Also update the advertisement packet local name field in the same file.

## Step 4: Hardware – Haptic Motor Pin

In `source/app_hw/app_hw_device.c`, change `HAPTIC_MOTOR_PIN` to the actual
GPIO connected to your vibration motor driver:

```c
#define HAPTIC_MOTOR_PIN   P9_0   /* example: Arduino header pin D8 */
```

The default `CYBSP_USER_LED1` is only for visual feedback during development.

## Step 5: AI Model

The included `models/model.h/c` is the Imagimob speech demo model (36 classes).
For real snore detection:
1. Train your snore/noise/unlabeled model in Imagimob DeepCraft
2. Export INT8 PTQ C-code
3. Replace `models/model.h` and `models/model.c`
4. In `source/main.c`, set `SNORE_LABEL_IDX` to the snore class index

## BLE Protocol Reference

| Characteristic    | Handle | Direction  | Format                        |
|-------------------|--------|------------|-------------------------------|
| Time Sync         | 0x000C | Write      | uint32 epoch, little-endian   |
| Log Transfer      | 0x000E | Notify     | 7 bytes per event (see below) |
| Haptic Intensity  | 0x0011 | Read/Write | uint8, 0-4 (20%-100%)         |

**Event Packet (7 bytes)**:
```
[timestamp: 4B LE][duration_s: 1B][haptic_success: 1B][haptic_flag: 1B]
```

## Morning Sync Workflow

1. Phone connects and writes Time Sync characteristic with current epoch
2. Phone enables Log Transfer notifications (write CCCD = 0x0001)
3. User short-presses button on device
4. Device sends all logged events one-by-one as 7-byte notifications
5. After last event, device clears flash log

## BLE Timeout Behaviour

| Scenario                          | Behaviour                              |
|-----------------------------------|----------------------------------------|
| No BLE connection after 5 min     | Standalone mode; uptime-based timestamps |
| Connected, no Time Sync after 30s | Fallback uptime timestamps; log warning |
| Connected, Time Sync received     | Full UTC timestamps from phone          |

## Build

```bash
cd SnoreGuard_Firmware
make getlibs
make build
make program
```
