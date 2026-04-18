# APPDETAIL.md — SnoreGuard Flutter App Implementation Reference

This file captures implementation-level details not obvious from `AppPlan.md`. Its purpose: allow full reconstruction of the app from scratch without guessing.

For high-level design, features, and screen descriptions see `AppPlan.md`.  
For BLE firmware protocol and GATT details see `D:\ACADEMIC\snoregurad_board\SnoreGuard_Firmware\CLAUDE.md`.

---

## Build Commands

```bash
# Install dependencies
flutter pub get

# Static analysis — must pass with zero issues before building
flutter analyze

# Build release APK (output: build/app/outputs/flutter-apk/app-release.apk)
flutter build apk --release

# Run on connected device
flutter run
```

---

## Known Build Gotchas

### Windows: Cross-Drive Kotlin Incremental Compilation Crash

**Symptom:** `flutter build apk` fails with:
```
e: Daemon compilation failed: null
java.lang.IllegalArgumentException: this and base files have different roots
```
**Cause:** Kotlin's incremental compiler cannot relativize paths when the Flutter Pub cache (`C:\Users\...\AppData\Local\Pub\Cache`) and the project (`D:\...`) are on different drives.

**Fix:** Add to `android/gradle.properties`:
```properties
kotlin.incremental=false
```

### Flutter SDK: `CardThemeData` not `CardTheme`

In `lib/app/theme.dart`, the `ThemeData` constructor takes `cardTheme: CardThemeData(...)`, **not** `CardTheme(...)`. Using `CardTheme` causes a type error at build time.

---

## Android Configuration

### `android/app/build.gradle.kts`
```kotlin
minSdk = 29      // Required: BLUETOOTH_SCAN permission only exists on API 29+
targetSdk = 35
```

### `android/app/src/main/AndroidManifest.xml`
```xml
<uses-permission android:name="android.permission.BLUETOOTH_SCAN"
    android:usesPermissionFlags="neverForLocation" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
```
`neverForLocation` is required on API 31+ so `BLUETOOTH_SCAN` does not implicitly grant location access.

### `android/gradle.properties`
```properties
org.gradle.jvmargs=-Xmx8G -XX:MaxMetaspaceSize=4G -XX:ReservedCodeCacheSize=512m -XX:+HeapDumpOnOutOfMemoryError
android.useAndroidX=true
kotlin.incremental=false
```

---

## Theme & Colors (`lib/app/theme.dart`)

All colors are defined as static constants on `AppColors` and referenced throughout the UI.

```dart
class AppColors {
  static const background   = Color(0xFF121220);
  static const surface      = Color(0xFF1E1E2E);
  static const surfaceAlt   = Color(0xFF252535);
  static const primary      = Color(0xFF6C63FF);  // soft purple
  static const primaryLight = Color(0xFF9C94FF);
  static const success      = Color(0xFF4CAF50);  // haptic worked (green)
  static const error        = Color(0xFFEF5350);  // haptic failed (red)
  static const info         = Color(0xFF42A5F5);  // no haptic (blue)
  static const warning      = Color(0xFFFFA726);  // fallback timestamp (orange)
  static const textPrimary  = Color(0xFFE8E8F0);
  static const textSecondary= Color(0xFF9090A8);
}
```

`SnoreGuardTheme.darkTheme` returns a `ThemeData` with:
- `brightness: Brightness.dark`
- `scaffoldBackgroundColor: AppColors.background`
- `cardTheme: CardThemeData(color: AppColors.surface, ...)`  ← note `CardThemeData`

---

## Routing (`lib/app/app.dart` + `lib/app/routes.dart`)

Route constants:
```dart
class Routes {
  static const onboarding   = '/onboarding';
  static const home         = '/home';
  static const sessionDetail= '/detail';
  static const settings     = '/settings';
}
```

`app.dart` uses a `Consumer<SettingsProvider>` to pick the initial route. During provider initialization (`isInitialized == false`), it shows a black splash screen. Once initialized:
- `onboardingComplete == false` → navigate to `/onboarding`
- `onboardingComplete == true`  → navigate to `/home`

This avoids a flash of the wrong screen on cold start.

---

## Entry Point (`lib/main.dart`)

```dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await DatabaseHelper.database;   // warm up SQLite on the main thread
  runApp(MultiProvider(
    providers: [
      ChangeNotifierProvider(create: (_) => BleProvider()..initialize()),
      ChangeNotifierProvider(create: (_) => SessionProvider()..loadRecentSessions()),
      ChangeNotifierProvider(create: (_) => SettingsProvider()..loadSettings()),
    ],
    child: const SnoreGuardApp(),
  ));
}
```

`DatabaseHelper.database` must be awaited before `runApp` so the singleton is ready before any widget accesses it.

---

## Session Date Boundary (`lib/utils/session_date_utils.dart`)

```dart
// Events before 06:00 local time belong to the previous calendar day.
static String computeSessionDate(int unixEpoch) {
  final dt = DateTime.fromMillisecondsSinceEpoch(unixEpoch * 1000).toLocal();
  final boundary = DateTime(dt.year, dt.month, dt.day, 6, 0, 0);
  final sessionDay = dt.isBefore(boundary) ? dt.subtract(const Duration(days: 1)) : dt;
  return DateFormat('yyyy-MM-dd').format(sessionDay);
}
```

---

## Fallback Timestamp Detection (`lib/utils/timestamp_utils.dart`)

```dart
// Firmware fallback = 1700000000 + uptime_s (approximately Nov 2023).
// Upper bound = 1700000000 + 365 days = 1731536000.
static bool isFallbackTimestamp(int epoch) =>
    epoch >= 1700000000 && epoch < 1731536000;
```

Events with fallback timestamps use today's local date as `session_date`.

---

## SQLite Schema & DAO

### Schema (`lib/services/database/database_helper.dart`)
```sql
CREATE TABLE snore_events (
  id                    INTEGER PRIMARY KEY AUTOINCREMENT,
  session_date          TEXT    NOT NULL,
  event_timestamp       INTEGER NOT NULL,
  duration_s            INTEGER NOT NULL,
  haptic_success        INTEGER NOT NULL DEFAULT 0,
  haptic_flag           INTEGER NOT NULL DEFAULT 0,
  is_fallback_timestamp INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_session_date ON snore_events(session_date);
CREATE UNIQUE INDEX idx_event_dedup ON snore_events(event_timestamp, duration_s, haptic_flag);
```

The UNIQUE index is the **only** deduplication mechanism. If Morning Sync is run twice, the second pass silently inserts 0 rows.

### Batch Insert (`lib/services/database/snore_event_dao.dart`)
```dart
// Returns count of newly inserted rows (ignores duplicates).
Future<int> insertEvents(List<SnoreEvent> events) async {
  final db = await DatabaseHelper.database;
  int inserted = 0;
  await db.transaction((txn) async {
    for (final e in events) {
      final rows = await txn.insert(
        'snore_events', e.toMap(),
        conflictAlgorithm: ConflictAlgorithm.ignore,
      );
      inserted += rows;
    }
  });
  return inserted;
}
```

### Weekly Stats Query
`getWeeklyStats()` uses a raw SQL `GROUP BY session_date` aggregate to return `DailyStat` objects. Called by `SessionProvider` to build `WeeklySummary`.

---

## BLE Layer

### `ble_constants.dart` — UUIDs
UUIDs are stored as `Guid` objects (flutter_blue_plus type):
```dart
static final serviceUuid         = Guid('e01f5698-4b21-4710-a0f6-001122334455');
static final timeSyncUuid        = Guid('e11f5698-4b21-4710-a0f6-001122334455');
static final logTransferUuid     = Guid('e21f5698-4b21-4710-a0f6-001122334455');
static final hapticIntensityUuid = Guid('e31f5698-4b21-4710-a0f6-001122334455');

static const deviceName    = 'SnoreGuard';
static const scanTimeout   = Duration(seconds: 10);
static const connectTimeout= Duration(seconds: 10);
static const syncEosTimeout= Duration(seconds: 2);  // silence = end of stream
```

### `ble_packet_parser.dart` — Packet Decoding
```dart
// Returns null for: wrong length, haptic_success > 1, haptic_flag > 1,
// or duration==0 with haptic_flag==0 (zero-content events).
static SnoreEvent? parseEventPacket(List<int> bytes) { ... }

// Encodes current epoch as uint32 little-endian for Time Sync write.
static Uint8List encodeTimeSyncPayload() { ... }

// Encodes haptic level 0-4 as single byte.
static Uint8List encodeHapticLevel(int level) { ... }
```

### `ble_service.dart` — Morning Sync Stream

The stream uses a `StreamController` + `Timer` for EOS detection (no explicit end-of-stream marker from firmware):

```dart
Stream<SnoreEvent> startMorningSync() {
  final controller = StreamController<SnoreEvent>();
  Timer? eosTimer;

  void resetEosTimer() {
    eosTimer?.cancel();
    eosTimer = Timer(BleConstants.syncEosTimeout, close);  // 2-second silence
  }

  _logTransferChar!.setNotifyValue(true).then((_) {
    resetEosTimer();  // start timer even if device has 0 events
    _notificationSub = _logTransferChar!.onValueReceived.listen((value) {
      final event = BlePacketParser.parseEventPacket(value);
      if (event != null) controller.add(event);
      resetEosTimer();
    });
  });

  controller.onCancel = () { close(); };
  return controller.stream;
}
```

Key points:
- The initial EOS timer fires immediately if device has no events to send.
- `onValueReceived` (not deprecated `value`) is used from flutter_blue_plus.
- `controller.onCancel` resets BLE state if the caller cancels mid-stream.

---

## Provider Pattern

### `BleProvider` — Morning Sync Flow
```dart
Future<void> performMorningSync({VoidCallback? onComplete}) async {
  // 1. Subscribe to sync stream
  // 2. Collect events into List<SnoreEvent>
  // 3. Batch insert → get newEventCount
  // 4. Read back haptic level (firmware may have auto-escalated)
  // 5. Call onComplete?.call()
}
```

`_friendly(Object e)` normalizes raw exception messages to user-readable strings (timeout, permission denied, device disconnected).

### Cross-Provider Wiring (`lib/screens/settings/widgets/sync_section.dart`)
```dart
// SyncSection widget reads both providers and passes a callback:
final bleProvider = context.read<BleProvider>();
final sessionProvider = context.read<SessionProvider>();

bleProvider.performMorningSync(
  onComplete: () => sessionProvider.loadRecentSessions(),
);
```

This is the **only** place cross-provider triggering happens. `BleProvider` has no direct reference to `SessionProvider`.

### `SettingsProvider`
Persists a single `bool` under key `'onboarding_complete'` in SharedPreferences.  
Exposes `isInitialized` (false until `loadSettings()` completes) used by `app.dart` to show the splash screen.

---

## UI Implementation Details

### Weekly Summary Bar Chart (`weekly_summary_card.dart`)
`_buildSevenDayStats()` fills exactly 7 days ending today. Days with no session data get a `BarChartRodData` with `toY: 0`. This ensures the chart always shows 7 bars even with sparse data.

Trend computation in `WeeklySummary.fromSessions()`:
- Split sessions into first half / second half of the 7-day window
- `worsening` if second-half avg > first-half avg + 1
- `improving` if first-half avg > second-half avg + 1
- otherwise `stable`

### Event Timeline Chart (`event_timeline_chart.dart`)
- **X axis:** minutes elapsed from the first event in the session (not wall-clock time). This is intentional — it works correctly even for fallback-timestamp sessions where absolute times are meaningless.
- **Y axis:** `duration_s`
- **Dot color logic:** `haptic_flag == 0` → blue (`AppColors.info`); `haptic_flag == 1 && haptic_success == 1` → green; `haptic_flag == 1 && haptic_success == 0` → red
- **Tooltips:** `spotEventMap` is a `Map<int, SnoreEvent>` keyed by scatter spot index, populated during chart build, read in `touchCallback`.

### `BleConnectionState` in `connection_status_bar.dart`
Uses a `switch` on the full `BleConnectionState` enum (disconnected / scanning / connecting / connected / syncing). Each state gets a distinct color and label string — never use `isConnected` bool in the status bar as it collapses too many states.

---

## WeeklySummary Model (`lib/models/sleep_session.dart`)

```dart
class WeeklySummary {
  final List<DailyStat> dailyStats;   // 7 entries, one per day, zeros for missing
  final double avgSnoreCount;
  final double avgDurationS;
  final double hapticSuccessRate;
  final String trend;                 // 'improving' | 'stable' | 'worsening'
}

class DailyStat {
  final String date;       // yyyy-MM-dd
  final int snoreCount;
  final int totalDurationS;
}
```

---

## flutter_blue_plus API Notes

These APIs changed between versions — pinned to `^1.31.0`:

| Usage | Correct API |
|---|---|
| Check if scan running | `FlutterBluePlus.isScanningNow` (sync bool) |
| Start scan filtered by name | `FlutterBluePlus.startScan(withNames: ['SnoreGuard'])` |
| Scan results stream | `FlutterBluePlus.scanResults` |
| Is scanning stream | `FlutterBluePlus.isScanning` (Stream\<bool\>) |
| Receive notifications | `characteristic.onValueReceived` (not `.value`) |
| Connection state | `device.connectionState` (Stream\<BluetoothConnectionState\>) |

---

## File Dependency Order (for reconstruction)

Build these in order — each layer depends on the previous:

1. `pubspec.yaml`, `android/` config files
2. `lib/app/theme.dart`, `lib/app/routes.dart`
3. `lib/models/snore_event.dart`, `lib/models/sleep_session.dart`
4. `lib/utils/session_date_utils.dart`, `lib/utils/timestamp_utils.dart`, `lib/utils/permission_utils.dart`
5. `lib/services/database/database_helper.dart`, `lib/services/database/snore_event_dao.dart`
6. `lib/services/ble/ble_constants.dart`, `lib/services/ble/ble_packet_parser.dart`, `lib/services/ble/ble_service.dart`
7. `lib/providers/settings_provider.dart`, `lib/providers/session_provider.dart`, `lib/providers/ble_provider.dart`
8. All `lib/screens/` widgets and screens
9. `lib/app/app.dart`, `lib/main.dart`
