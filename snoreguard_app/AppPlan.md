# SnoreGuard Flutter App — Application Plan

**Project:** KOSEN KMUTT · Edge AI Snore Detection with Posture Adjuster and BLE-Enabled Sleep-Logging  
**App Platform:** Android (min API 29 / Android 10, target API 35 / Android 15)  
**Flutter SDK:** 3.27.x · Dart 3.6.x  

---

## 1. System Overview

The SnoreGuard app is the companion Android application for the PSoC™ 6 (CY8CKIT-062S2-AI) sleep monitoring device. The board runs a Conv1D Edge AI model for real-time snore detection and fires a haptic vibration motor when repeated snoring is detected. The app's role:

1. **Time Sync** — write the current Unix epoch to the device at session start so it logs real calendar timestamps.
2. **Morning Sync** — retrieve the night's binary event log from the device via BLE notifications.
3. **Dashboard** — display a 7-day snore history with per-session timelines, haptic trigger markers, and intervention success/fail indicators.
4. **Settings** — configure haptic intensity, trigger Morning Sync, and manage BLE pairing.

The app operates **fully offline**. No internet connectivity is required at any point.

---

## 2. BLE Communication Protocol

### 2.1 Device Identity
- **Advertised name:** `SnoreGuard`
- **Connection:** BLE 5.0 via CYW43439 (Murata Type 1YN)

### 2.2 GATT Service — Sleep Monitor
**Service UUID:** `e01f5698-4b21-4710-a0f6-001122334455`  
*(Derived from firmware `cycfg_gatt_db.h` little-endian bytes: `0x55,0x44,...,0xE0`)*

| Characteristic | UUID | Direction | Size | Purpose |
|---|---|---|---|---|
| **Time Sync** | `e11f5698-...` | App → Device (Write) | 4 bytes | Unix epoch uint32 little-endian |
| **Log Transfer** | `e21f5698-...` | Device → App (Notify) | 7 bytes/event | Binary event packet stream |
| **Haptic Intensity** | `e31f5698-...` | Both (Read/Write) | 1 byte | Level 0–4 = 20/40/60/80/100% |
| **Sync Ack** | `e41f5698-...` | App → Device (Write) | 1 byte | `0x01` after SQLite save → firmware clears log |
| **Haptic Enable** | `e51f5698-...` | Both (Read/Write) | 1 byte | `0x01` = enabled (default), `0x00` = motor suppressed |

### 2.3 Binary Event Packet (7 bytes, little-endian)

```
Offset  Size  Field            Description
[0..3]   4    timestamp        Unix epoch (uint32 LE) of snore event start
[4]      1    duration_s       Snore duration in seconds (0–255)
[5]      1    haptic_success   1 = snoring ceased within 15-min post-haptic window
[6]      1    haptic_flag      1 = haptic motor fired for this event
```

### 2.4 Morning Sync Flow
1. User connects app to device (CCCD auto-restored from BLE bonding)
2. App taps **Sync Now** → subscribes to Log Transfer notifications
3. User presses the **short button** (50–250 ms) on the physical device
4. Device streams all buffered events as individual BLE notifications
5. App detects end-of-stream via 2-second silence timeout (no explicit EOS marker)
6. App batch-inserts events into SQLite (deduplication via UNIQUE index)
7. App writes **Sync Ack** (`0x01`) to firmware → firmware clears its event log
8. App reads back current haptic level and haptic-enable state (firmware may have auto-escalated during sleep)

### 2.5 Fallback Timestamps
If the device is not time-synced before sleep (no BLE connection within 30 s), it uses a fallback epoch base of `1700000000` (≈ Nov 2023) + device uptime. The app detects timestamps in the range `[1700000000, 1700000000 + 31536000)` and marks them as `isFallbackTimestamp = true`, using today's date as the session date and displaying a warning in the Session Detail view.

---

## 3. Local Database Schema (SQLite / sqflite)

### Table: `snore_events`

| Column | Type | Constraint | Description |
|---|---|---|---|
| `id` | INTEGER | PRIMARY KEY AUTOINCREMENT | Row identifier |
| `session_date` | TEXT | NOT NULL | ISO date `yyyy-MM-dd` (6 AM boundary rule) |
| `event_timestamp` | INTEGER | NOT NULL | Unix epoch of event start |
| `duration_s` | INTEGER | NOT NULL | Snore duration 0–255 s |
| `haptic_success` | INTEGER | NOT NULL DEFAULT 0 | 1 = successful intervention |
| `haptic_flag` | INTEGER | NOT NULL DEFAULT 0 | 1 = haptic fired |
| `is_fallback_timestamp` | INTEGER | NOT NULL DEFAULT 0 | 1 = timestamp is approximate |

**Indexes:**
- `idx_session_date` on `(session_date)` — fast session queries
- `idx_event_dedup` UNIQUE on `(event_timestamp, duration_s, haptic_flag)` — prevents duplicate Morning Sync

**Auto-purge:** Records with `session_date < (today − 7 days)` are deleted on every app launch.

### Session Date Boundary Rule
Events between midnight and **06:00 local time** belong to the **previous calendar day's session**.  
Example: snore at 2025-06-11 02:30 → `session_date = "2025-06-10"`.

---

## 4. Application Screens

### 4.1 Home Screen
- **Weekly Summary Card** — 7-day bar chart (fl_chart BarChart) showing nightly snore counts; trend badge (Improving / Stable / Worsening); avg snore count, avg duration, haptic success rate
- **Session List** — SessionCards sorted newest-first; each shows date, snore count, total duration, haptic trigger count, success rate; warning icon if fallback timestamps
- **Empty State** — icon + guidance + "Set Up Device" button when no sessions exist
- **AppBar** — BLE connection status icon (green when connected) + settings gear

### 4.2 Session Detail Screen
- **Stats Row** — 3 stat cards: Snore Events, Total Duration, Haptic Success %
- **Event Timeline Chart** — fl_chart ScatterChart; X = time (minutes from first event), Y = duration (s); dot colors: blue (no haptic), green (haptic success), red (haptic fail); touch tooltips
- **Fallback Warning** — orange banner when timestamps are approximate
- **Haptic Summary Card** — intervention count, success count, circular progress indicator
- **Event List** — all events with time, duration, and haptic success/fail badge

### 4.3 Device Settings Screen
- **Connection Status Bar** — persistent colored banner at top
- **Device Scan Section** — Scan button, results list with Connect buttons; connected device with Disconnect
- **Morning Sync Section** — instructions, Sync Now button, live event count progress, result banner
- **Haptic Motor Toggle** — ON/OFF switch to enable or disable the haptic motor entirely; greyed when disconnected
- **Haptic Intensity Slider** — 5 discrete levels (20–100%), Test button to send a test vibration; disabled when haptic is OFF or device disconnected
- **About Section** — app version, device name, data retention policy

### 4.4 Onboarding Screen (first launch only)
3-page PageView:
1. **Welcome** — app description
2. **Permissions** — Bluetooth permission grant (with fallback to app settings)
3. **Pair Device** — scan and connect; "Skip for Now" option

---

## 5. State Management (Provider)

| Provider | Responsibility |
|---|---|
| `SettingsProvider` | First-launch / onboarding flag (SharedPreferences) |
| `SessionProvider` | Session list, selected session, weekly summary, DB purge |
| `BleProvider` | BLE scan/connect, Time Sync, Morning Sync, haptic level, error messages |

**Cross-provider wiring:** After `BleProvider.performMorningSync()` completes, it calls an `onComplete` callback (passed from the `SyncSection` widget) which triggers `SessionProvider.loadRecentSessions()`.

---

## 6. Key Flutter Packages

| Package | Version | Purpose |
|---|---|---|
| `flutter_blue_plus` | ^1.31.0 | BLE scan, GATT characteristic read/write/notify |
| `sqflite` | ^2.3.0 | SQLite local database |
| `path_provider` | ^2.1.0 | Platform filesystem paths |
| `fl_chart` | ^0.69.0 | Bar chart (weekly summary) + Scatter chart (event timeline) |
| `provider` | ^6.1.0 | State management |
| `intl` | ^0.19.0 | Date/time formatting |
| `shared_preferences` | ^2.2.0 | Onboarding flag persistence |
| `permission_handler` | ^11.0.0 | Runtime BLE permission requests |

---

## 7. Error Handling

| Scenario | Detection | Response |
|---|---|---|
| Bluetooth OFF | `FlutterBluePlus.adapterState` → `off` | Status bar shows "Bluetooth is off" |
| BLE permissions denied | `permission_handler` status | Dialog + "Open App Settings" button |
| Device not found | Scan timeout, 0 results | Error banner with Retry option |
| Connection timeout | `connect(timeout: 10s)` throws | SnackBar "Could not connect" |
| Connection lost mid-sync | `connectionState` → disconnected | Partial events saved; warning banner |
| Corrupt BLE packet | `parseEventPacket()` → null | Silently skipped, logged |
| Duplicate events (double sync) | `INSERT OR IGNORE` on UNIQUE index | "0 new events" result banner |
| No events on device | 2-s EOS timeout with 0 events | "No events on device" banner |
| Fallback timestamps | Epoch in `[1700000000, 1731536000)` | Warning card on Session Detail |
| Database error | try/catch all DB operations | SnackBar "Database error, restart app" |
| Haptic write failure | `characteristic.write()` throws | SnackBar "Could not update level" |

---

## 8. Project File Structure

```
lib/
├── main.dart                           App entry point, MultiProvider setup
├── app/
│   ├── app.dart                        MaterialApp, routing
│   ├── theme.dart                      Dark theme (AppColors + SnoreGuardTheme)
│   └── routes.dart                     Route name constants
├── models/
│   ├── snore_event.dart                SnoreEvent data class
│   └── sleep_session.dart              SleepSession aggregate + WeeklySummary
├── services/
│   ├── ble/
│   │   ├── ble_constants.dart          UUIDs, packet sizes, device name
│   │   ├── ble_packet_parser.dart      7-byte packet decode, epoch encode
│   │   └── ble_service.dart            BLE scan, connect, Time Sync, sync stream
│   └── database/
│       ├── database_helper.dart        SQLite init, schema, indexes
│       └── snore_event_dao.dart        CRUD, batch insert, weekly stats, purge
├── providers/
│   ├── ble_provider.dart               BLE state, sync orchestration, error handling
│   ├── session_provider.dart           Session loading, selection, weekly summary
│   └── settings_provider.dart          First-launch / onboarding persistence
├── screens/
│   ├── onboarding/
│   │   └── onboarding_screen.dart      3-page first-time setup flow
│   ├── home/
│   │   ├── home_screen.dart
│   │   └── widgets/
│   │       ├── session_card.dart
│   │       ├── weekly_summary_card.dart
│   │       └── empty_state_widget.dart
│   ├── session_detail/
│   │   ├── session_detail_screen.dart
│   │   └── widgets/
│   │       ├── stats_row.dart
│   │       ├── event_timeline_chart.dart
│   │       └── event_list_tile.dart
│   └── settings/
│       ├── settings_screen.dart
│       └── widgets/
│           ├── connection_status_bar.dart
│           ├── device_scan_section.dart
│           ├── haptic_enable_toggle.dart
│           ├── haptic_intensity_slider.dart
│           ├── sync_section.dart
│           └── about_section.dart
└── utils/
    ├── session_date_utils.dart         6 AM boundary, date formatting
    ├── timestamp_utils.dart            Fallback epoch detection
    └── permission_utils.dart           BLE runtime permission helpers

android/
├── app/
│   ├── build.gradle.kts               minSdk=29, targetSdk=35
│   └── src/main/AndroidManifest.xml   BLUETOOTH_SCAN, BLUETOOTH_CONNECT, LOCATION
```

---

## 9. Improvements Beyond Proposal

1. **Dark mode by default** — sleep-friendly UI with purple/dark palette
2. **Onboarding flow** — guided 3-step first-time BLE permission + device pairing
3. **Event deduplication** — UNIQUE DB index prevents double-sync duplicates
4. **Fallback timestamp detection** — warns user when device had no time sync
5. **Smart session boundary** — 6 AM cutoff keeps late-night events in the correct session
6. **Weekly trend analysis** — "Improving / Stable / Worsening" badge based on snore count trend
7. **Haptic auto-escalation awareness** — reads back haptic level after sync (firmware may escalate during sleep)
8. **Real-time sync progress** — live event count shown during Morning Sync
9. **Empty state UI** — clear guidance when no data or no device paired
10. **Robust connection management** — disconnect detection, error banners, retry flows
11. **Haptic Test button** — verify vibration at selected intensity from the settings screen
12. **Battery-friendly scanning** — 10-second targeted scan filtered by device name "SnoreGuard"
13. **Circular haptic progress indicator** — visual success/fail rate in session detail
14. **Touch tooltips on timeline** — tap any dot for exact time, duration, and haptic status
