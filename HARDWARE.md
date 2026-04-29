# SnoreGuard Hardware Components & Wiring Guide

## Components List

| # | Component | Purpose | Notes |
|---|-----------|---------|-------|
| 1 | **CY8CKIT-062S2-AI** | Main controller, microphone, BLE | Already has dual MEMS mics + CYW43439 BLE built-in |
| 2 | **ERM Vibration Motor** (~3V, ~100mA) | Haptic posture adjuster (pillow-mounted) | Coin vibrator or pager motor |
| 3 | **NPN Transistor** (2N2222 or BC547) | Motor driver — GPIO can't sink 100mA directly | 1 unit |
| 4 | **Flyback Diode** (1N4148 or 1N4001) | Back-EMF protection for MCU | 1 unit |
| 5 | **1kΩ Resistor** | Transistor base current limiter | 1 unit |
| 6 | **500mAh Li-Po Battery** | Power supply (8-hr target) | 3.7V single cell |
| 7 | **TP4056 USB Type-C Li-Po charging module** | Charges Li-Po safely | Get version with protection IC (DW01) |
| 8 | **5V Boost Converter module** (MT3608 or PAM2401) | Boost Li-Po 3.7V → 5V for board VIN | Optional if board accepts 3.7V directly on VIN |
| 9 | **Jumper wires + breadboard** | Prototyping connections | — |

---

## On-Board Connections (No External Wiring Needed)

| Feature | Status |
|---------|--------|
| Dual MEMS Microphones (IM72D128V01XTMA1) | **Built-in** — PDM/PCM interface, no wiring required |
| BLE 5.0 (CYW43439 / Murata Type 1YN) | **Built-in** — antenna on board |
| User Button (`CYBSP_USER_BTN`) | **Built-in** — short press = Morning Sync, 5s hold = pairing mode |
| LED2 (`CYBSP_USER_LED2`) | **Built-in** — BLE status indicator |
| USB Type-C (KitProg3) | **Built-in** — firmware flashing and debug UART (115200 baud) |

---

## Firmware Pin Change Required Before Hardware Deployment

The default `HAPTIC_MOTOR_PIN` in the firmware points to `CYBSP_USER_LED1` (`P5_3`) for
dev-kit visual feedback only. Change it to a PWM-capable Arduino header pin before
connecting the real motor.

Edit `snoregurad_board/SnoreGuard_Firmware/source/app_hw/app_hw_device.c` line 70:

```c
// Current (dev-kit mode — do NOT connect motor here):
#define HAPTIC_MOTOR_PIN   CYBSP_USER_LED1   // P5_3

// Change to (Arduino D10 header — PWM-capable):
#define HAPTIC_MOTOR_PIN   P10_0
```

---

## Wiring: Motor Driver Circuit

The firmware drives `HAPTIC_MOTOR_PIN` at **200 Hz PWM**, 20–100% duty cycle (5 levels).

### Schematic

```
PSoC 6 P10_0 (Arduino D10 header)
        |
      [1kΩ]
        |
        └──── Base ──┐
                   [2N2222 NPN]
                     |
        Collector ───┴──── Motor (−) ───┬──── [1N4148 anode]
                                        |              |
                          Motor (+) ────┴──── [1N4148 cathode]
                                |
                             3.3V (board header)

                     |
                  Emitter
                     |
                    GND
```

### Step-by-Step Connections

| From | To | Notes |
|------|----|-------|
| `P10_0` (Arduino D10) | 1kΩ resistor leg A | PWM signal out |
| 1kΩ resistor leg B | Transistor **Base** | Limits base current |
| Transistor **Collector** | Motor **negative** (−) wire | Switched ground path |
| Motor **positive** (+) wire | Board **3.3V** pin | Motor power supply |
| Transistor **Emitter** | Board **GND** | Common ground |
| **1N4148 Anode** | Transistor Collector (motor −) | Flyback protection |
| **1N4148 Cathode** | Motor positive / 3.3V side | Flyback protection |

> The flyback diode is reverse-biased during normal operation and only conducts when
> the motor's inductive back-EMF spikes above 3.3V, protecting the transistor and MCU.

---

## Wiring: Power Supply (Li-Po + TP4056 + Boost)

### Schematic

```
USB-C (wall charger or PC)
        |
  ┌─────┴──────┐
  │  TP4056    │
  │  Module    │
  └──┬─────┬──┘
   BAT+   BAT−
     |       |
  [Li-Po +] [Li-Po −]
     |       |
  ┌──┴───────┴──┐
  │  Boost      │  ← MT3608 or PAM2401, set output to 5V
  │  Converter  │
  └──┬───────┬──┘
   5V OUT   GND
     |         |
  Board VIN   Board GND   (Arduino power header)
```

### Step-by-Step Connections

| From | To | Notes |
|------|----|-------|
| USB-C port | TP4056 module input | Charging input |
| TP4056 **BAT+** | Li-Po **positive** (+) | Battery connection |
| TP4056 **BAT−** | Li-Po **negative** (−) | Battery connection |
| Li-Po **+** | Boost converter **VIN+** | Input to boost |
| Li-Po **−** | Boost converter **GND** | Input to boost |
| Boost converter **VOUT** (set to 5V) | Board **VIN** (Arduino header) | Powers the PSoC 6 board |
| Boost converter **GND** | Board **GND** | Common ground |

> If your boost module has an adjustment potentiometer (MT3608), turn it until the
> output reads 5.0V with a multimeter before connecting to the board.
>
> If the CY8CKIT-062S2-AI VIN header accepts 3.7–4.2V directly (check the board's
> power specs), the boost converter can be omitted and the Li-Po output connected
> to VIN directly.

---

## Full System Diagram

```
                       ┌──────────────────────────────────────┐
                       │        CY8CKIT-062S2-AI               │
                       │                                        │
  [USB-C Charger] ─→  │  VIN ←── [Boost 5V] ←── [TP4056]      │
                       │                              ↕         │
                       │                          [Li-Po 500mAh]│
                       │                                        │
                       │  [MEMS Mic × 2]  (built-in)            │
                       │  [BLE CYW43439]  (built-in)            │
                       │  [User Button]   (built-in)            │
                       │                                        │
                       │  P10_0 ──[1kΩ]──[2N2222]──────────────┼──→ [ERM Motor]
                       │  3.3V  ──────────────────────────────┐ │    (under pillow)
                       │  GND   ──────────[Emitter]           │ │
                       └──────────────────────────────────────┼─┘
                                          [1N4148] across motor│
                                                               ↓
                                                           3.3V rail
```

### Physical Placement
- **PSoC 6 board** — mounted on the headboard; handles audio capture and BLE communication.
- **ERM vibration motor** — connected via cable, placed discreetly under the user's pillow for haptic posture correction.
- **Li-Po battery + TP4056 + boost module** — co-located with the PSoC 6 board.

---

## Quick Reference: Key Firmware Constants

Defined in `source/app_hw/app_hw_device.c`:

| Constant | Value | Description |
|----------|-------|-------------|
| `HAPTIC_MOTOR_PIN` | `P10_0` (after change) | GPIO driving the motor transistor |
| `HAPTIC_PWM_FREQUENCY_HZ` | 200 Hz | ERM motor drive frequency |
| `HAPTIC_SHAKE_ROUNDS` | 6 | On/off pulses per haptic intervention |
| `HAPTIC_SHAKE_ON_MS` | 500 ms | Motor-on duration per pulse |
| `HAPTIC_SHAKE_OFF_MS` | 500 ms | Gap between pulses |
| `HAPTIC_DUTY_PERCENT[5]` | 20, 40, 60, 80, 100 | PWM duty per intensity level 0–4 |
