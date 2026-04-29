# SnoreGuard App Software — Source File Guide

Flutter app project: `snoreguard_app/lib/`
Framework: Flutter 3.27.x (Dart 3.6.x) | Min SDK: Android API 29 (Android 10)

---

## Entry Point

### `lib/main.dart`
The app's starting point. Initializes the SQLite database before the first frame, then mounts the widget tree wrapped in a `MultiProvider` that creates and starts all three top-level providers (`SettingsProvider`, `SessionProvider`, `BleProvider`) so they are available to every screen.

---

## `lib/app/` — App Shell

### `app/app.dart`
Root `MaterialApp` widget. Applies the global theme from `theme.dart`, sets the initial route (onboarding on first launch, home otherwise), and wires up the named-route table from `routes.dart`.

### `app/routes.dart`
Defines the named route map (`/`, `/home`, `/session`, `/settings`, `/onboarding`) and maps each name to its screen widget. Centralizes navigation so screens navigate by name rather than importing each other directly.

### `app/theme.dart`
Global dark theme definition. Background `#121220`, primary accent `#6C63FF` (purple). Configured for sleep-friendly low-brightness display. Applied once in `app.dart`; all widgets inherit it automatically.

---

## `lib/models/` — Data Models

### `models/snore_event.dart`
Data class representing one logged snore episode (maps to one row in the `snore_events` SQLite table). Fields:

| Field | Type | Description |
|-------|------|-------------|
| `sessionDate` | `String` | ISO date of the sleep session (YYYY-MM-DD) |
| `eventTimestamp` | `int` | Unix epoch of episode start |
| `durationS` | `int` | Episode duration in seconds (0–255) |
| `hapticFlag` | `int` | 1 = haptic motor fired during this episode |
| `hapticSuccess` | `int` | 1 = no snoring for 15 min after haptic fire |
| `isFallbackTimestamp` | `bool` | True if the firmware had no real clock sync |

Provides `fromMap()` / `toMap()` for SQLite serialization, and computed helpers `eventDateTime`, `formattedTime`, `wasHapticTriggered`, `wasHapticSuccessful`.

### `models/sleep_session.dart`
Aggregates all `SnoreEvent` records belonging to one night into a single object. Computed properties include total snore count, total duration, haptic trigger count, and success rate — used by the Home screen session cards and the weekly summary chart.

---

## `lib/providers/` — State Management (Provider Pattern)

### `providers/ble_provider.dart`
BLE state and Morning Sync orchestration. Sits between `BleService` (raw BLE) and the UI:

- **Scan**: calls `BleService.scanForDevices()`, validates Bluetooth is on and permissions are granted, exposes `scanResults` list.
- **Connect**: calls `BleService.connectToDevice()`, then automatically sends Time Sync and reads back the current haptic intensity level (to detect any auto-escalation the firmware did overnight).
- **Morning Sync** (`performMorningSync()`): subscribes to the Log Transfer stream, collects `SnoreEvent` objects as they arrive, batch-inserts them into SQLite via `SnoreEventDao`, **writes Sync Ack (`0x01`) to the device after a successful insert** so the firmware clears its log, handles partial sync on connection loss, then reads the haptic level again.
- **Haptic level**: `setHapticLevel(level)` writes the new level to the device and updates local state.
- Exposes `connectionState`, `isSyncing`, `syncEventsReceived`, `syncNewEvents`, `hapticLevel`, `errorMessage`, `bluetoothOn`, `permissionsGranted` for the UI to observe.

### `providers/session_provider.dart`
SQLite session data state for the Home and Session Detail screens. Loads the last 7 days of sessions from `SnoreEventDao` on startup and exposes `sessions` (list of `SleepSession`), `isLoading`, and `errorMessage`. `refreshSessions()` re-queries the database — called by `BleProvider` after a successful Morning Sync so the home screen updates automatically.

### `providers/settings_provider.dart`
Persists user preferences (currently: whether onboarding has been completed). Uses `shared_preferences` for lightweight key-value storage. `loadSettings()` is called at startup; `completeOnboarding()` sets the flag and triggers navigation to the home screen.

---

## `lib/screens/` — UI Screens

### `screens/onboarding/onboarding_screen.dart`
Three-page introduction shown only on the first app launch. Explains the SnoreGuard device, the BLE sync workflow, and the haptic posture correction feature. Has a "Get Started" button on the last page that calls `SettingsProvider.completeOnboarding()` and navigates to Home. Never shown again after completion.

### `screens/home/home_screen.dart`
Main screen showing up to 7 days of sleep history. Displays a weekly summary bar chart at the top and a scrollable list of session cards below. Tapping a card navigates to `SessionDetailScreen`. Shows an empty-state illustration when no sessions have been synced yet.

#### `screens/home/widgets/weekly_summary_card.dart`
Bar chart (`fl_chart`) showing snore episode count per day for the past 7 days. Provides a quick visual overview of snoring trends across the week.

#### `screens/home/widgets/session_card.dart`
One card per sleep session in the home list. Shows the session date, total snore episodes, total snore duration, and a colour-coded haptic success badge.

#### `screens/home/widgets/empty_state_widget.dart`
Illustration and call-to-action text shown when the session list is empty. Prompts the user to connect the device and perform a Morning Sync.

### `screens/session_detail/session_detail_screen.dart`
Detailed view for a single night. Displays the scatter timeline chart, the per-event list, and the stats row. Receives a `SleepSession` object via route arguments.

#### `screens/session_detail/widgets/event_timeline_chart.dart`
Two-row pill timeline chart using `CustomPainter`. The top row (green) shows snore episodes as rounded rectangles; the bottom row (purple) shows haptic pulses. The X-axis spans from the earliest to the latest real-timestamp event. **Domain computation deliberately excludes fallback-timestamp events** (`isFallbackTimestamp = true`): those have epoch ≈ Nov 2023, and mixing them with real Apr 2026 timestamps would produce a 2.5-year axis that compresses all real events into invisible slivers at the far right. Fallback events still render but clamp to `x = 0` (left edge). If the session contains only fallback events, all events are used for the domain.

#### `screens/session_detail/widgets/event_list_tile.dart`
One row per snore episode: start time, duration in seconds, and haptic status icons (motor fired / success / fail). Rows with `isFallbackTimestamp = true` show a clock-warning icon indicating the firmware had no real-time sync.

#### `screens/session_detail/widgets/stats_row.dart`
Summary bar at the top of the session detail: total episodes, total duration, number of haptic interventions, and intervention success rate for the night.

### `screens/settings/settings_screen.dart`
Device management and configuration screen. Assembles five settings widgets in order: `DeviceScanSection` → `SyncSection` → `HapticEnableToggle` → `HapticIntensitySlider` → `AboutSection`.

#### `screens/settings/widgets/device_scan_section.dart`
BLE scan and connect UI. "Scan" button triggers `BleProvider.startScan()`, results appear in a list, tapping a result calls `BleProvider.connectToDevice()`. Shows spinner while scanning or connecting, error messages inline.

#### `screens/settings/widgets/connection_status_bar.dart`
Persistent banner at the top of the Settings screen showing current BLE state (disconnected / scanning / connecting / connected / syncing) and the connected device name.

#### `screens/settings/widgets/sync_section.dart`
Morning Sync trigger. Shows a "Start Sync" button (only active when connected), a live event counter during sync, and a completion summary (X new events saved). Reminds the user to press the physical button on the board to begin streaming.

#### `screens/settings/widgets/haptic_enable_toggle.dart`
Toggle switch (ON/OFF) for enabling or disabling the haptic motor. Reads `BleProvider.hapticEnabled` and writes via `BleProvider.setHapticEnabled(bool)`. Disabled (greyed) when not connected. When OFF, the intensity slider below is also greyed out and the Test button is inactive.

#### `screens/settings/widgets/haptic_intensity_slider.dart`
Discrete slider (5 steps: 20 % → 100 %) for adjusting the motor vibration intensity. Reads the current level from `BleProvider.hapticLevel` on screen load (reflecting any overnight auto-escalation by the firmware) and writes back to the device on change via `BleProvider.setHapticLevel()`. Slider and Test button are disabled when not connected **or** when haptic is disabled (`BleProvider.hapticEnabled == false`).

#### `screens/settings/widgets/about_section.dart`
Static information panel: app version, firmware compatibility note, and a link to the project's GitHub repository.

---

## `lib/services/ble/` — Bluetooth LE Layer

### `services/ble/ble_constants.dart`
Single source of truth for all BLE identifiers and timing constants:

| Constant | Value | Purpose |
|----------|-------|---------|
| `deviceName` | `"SnoreGuard"` | BLE scan name filter |
| `serviceUuid` | `e01f5698-...` | Sleep Monitor GATT service |
| `timeSyncUuid` | `e11f5698-...` | Time Sync characteristic (Write) |
| `logTransferUuid` | `e21f5698-...` | Log Transfer characteristic (Notify) |
| `hapticIntensityUuid` | `e31f5698-...` | Haptic Intensity characteristic (Read/Write) |
| `syncAckUuid` | `e41f5698-...` | Sync Ack characteristic (Write) |
| `hapticEnabledUuid` | `e51f5698-...` | Haptic Enable characteristic (Read/Write) |
| `scanTimeout` | 10 s | BLE scan duration |
| `syncEosTimeout` | 2 s | Silence period to detect end of sync stream |

UUIDs are derived from the firmware's `GeneratedSource/cycfg_gatt_db.h`.

### `services/ble/ble_service.dart`
Raw BLE operations using `flutter_blue_plus`. Manages the full device lifecycle:

- **`scanForDevices()`**: starts a scan filtered to `withNames: ['SnoreGuard']`, collects `ScanResult` objects, returns after timeout.
- **`connectToDevice(device)`**: connects, discovers GATT services, caches all five characteristic references (Time Sync, Log Transfer, Haptic Intensity, Sync Ack, Haptic Enable). Throws a descriptive exception if the Sleep Monitor service or any characteristic is missing.
- **`sendTimeSync()`**: encodes the current Unix epoch as 4-byte little-endian and writes it to the Time Sync characteristic.
- **`startMorningSync()`**: enables notifications on Log Transfer, returns a `Stream<SnoreEvent>`. Uses a **60-second initial timer** (waiting for the user to press the board's physical button), then switches to a **2-second silence timer** after the first packet arrives to detect end-of-stream (the firmware sends no explicit EOS marker).
- **`readHapticIntensity()` / `writeHapticIntensity(level)`**: read and write the 1-byte haptic level characteristic.
- **`readHapticEnabled()` / `writeHapticEnabled(bool)`**: read and write the Haptic Enable characteristic (`0x01` = enabled, `0x00` = disabled). Both called on connect and after morning sync to stay in sync with the device state.
- **`sendSyncAck({required bool success})`**: writes `0x01` (success) or `0x00` (failure) to the Sync Ack characteristic. Non-fatal if the write fails — the firmware log is preserved and will re-transmit on the next sync.

Exposes `connectionStateStream` (broadcast `Stream<BleConnectionState>`) consumed by `BleProvider`.

### `services/ble/ble_packet_parser.dart`
Binary packet encoding and decoding. Stateless utility class:

- **`parseEventPacket(bytes)`**: decodes a 7-byte BLE notification into a `SnoreEvent`. Validates length, rejects `haptic_success > 1`, `haptic_flag > 1`, and zero-duration events with no haptic flag. Calls `TimestampUtils.isFallbackTimestamp()` to detect firmware-generated fallback timestamps and `SessionDateUtils.computeSessionDateFromEpoch()` to assign the correct calendar date using a 6 AM session boundary.
- **`encodeTimeSyncPayload()`**: encodes `DateTime.now()` as a 4-byte little-endian `uint32`.
- **`encodeHapticLevel(level)`**: encodes a single-byte haptic level value.
- **`encodeSyncAck({required bool success})`**: encodes `0x01` (success) or `0x00` (failure) as a single-byte payload for the Sync Ack write.

---

## `lib/services/database/` — SQLite Persistence

### `services/database/database_helper.dart`
Singleton SQLite database manager (`snoreguard.db`, schema version 1). Creates the `snore_events` table on first launch with:

- `PRIMARY KEY AUTOINCREMENT` id
- `INDEX idx_session_date` for fast per-night queries
- `UNIQUE INDEX idx_event_dedup` on `(event_timestamp, duration_s, haptic_flag)` — prevents duplicate rows when Morning Sync is run more than once for the same night

### `services/database/snore_event_dao.dart`
All SQL queries against the `snore_events` table:

| Method | Description |
|--------|-------------|
| `insertEvent(event)` | Insert one event; silently ignores duplicates |
| `insertEvents(events)` | Batch insert in a single transaction; returns new-row count |
| `getEventsBySession(date)` | All events for one night, ordered by timestamp |
| `getRecentSessionDates(days)` | Distinct session dates in the last N days |
| `getWeeklyStats(days)` | Per-session aggregate stats (count, duration, haptic counts) |
| `purgeOldEvents(days)` | Delete records older than N days (called on launch) |
| `getRecentSessions(days)` | Builds `List<SleepSession>` from the last N days |

---

## `lib/utils/` — Utilities

### `utils/timestamp_utils.dart`
Timestamp helpers:

- `nowSeconds()` — current Unix epoch as `int` (used when encoding Time Sync).
- `isFallbackTimestamp(ts)` — returns `true` if `ts >= 1700000000 && ts < 1731536000` (the firmware's fallback epoch range ≈ Nov 2023). Used to flag events where the board had no real-time sync.

### `utils/session_date_utils.dart`
Session date assignment logic:

- `computeSessionDateFromEpoch(epoch)` — converts a Unix epoch to a session date string (YYYY-MM-DD) using a **6 AM boundary**: events before 06:00 local time are assigned to the previous calendar day, matching overnight sleep sessions that run past midnight.
- `today()` — returns today's ISO date string; used for fallback-timestamp events.

### `utils/permission_utils.dart`
Android BLE permission management. `areBlePermissionsGranted()` checks whether `BLUETOOTH_SCAN` and `BLUETOOTH_CONNECT` permissions are granted (required from Android API 31+). `requestBlePermissions()` triggers the system permission dialog and returns the result. Called by `BleProvider` before any scan or connect attempt.

---

## Architecture Summary

```
lib/
├── main.dart                          ← app entry, provider setup, DB init
├── app/
│   ├── app.dart                       ← MaterialApp root, theme, initial route
│   ├── routes.dart                    ← named route table
│   └── theme.dart                     ← dark theme definition
├── models/
│   ├── snore_event.dart               ← per-episode data class, SQLite serialization
│   └── sleep_session.dart             ← nightly aggregate (list of SnoreEvent)
├── providers/
│   ├── ble_provider.dart              ← BLE state + Morning Sync orchestration
│   ├── session_provider.dart          ← SQLite session queries for UI
│   └── settings_provider.dart        ← onboarding flag, user preferences
├── screens/
│   ├── onboarding/                    ← first-launch intro (3 pages)
│   ├── home/                          ← 7-day history + weekly chart
│   ├── session_detail/                ← per-night timeline, stats, event list
│   └── settings/                     ← BLE scan/connect, sync, haptic slider
├── services/
│   ├── ble/
│   │   ├── ble_constants.dart         ← all UUIDs, timeouts, labels
│   │   ├── ble_service.dart           ← raw flutter_blue_plus operations
│   │   └── ble_packet_parser.dart     ← 7-byte packet decode / encode
│   └── database/
│       ├── database_helper.dart       ← SQLite singleton, schema creation
│       └── snore_event_dao.dart       ← all SQL queries
└── utils/
    ├── timestamp_utils.dart           ← epoch helpers, fallback detection
    ├── session_date_utils.dart        ← 6 AM session boundary logic
    └── permission_utils.dart          ← Android BLE permission requests
```
