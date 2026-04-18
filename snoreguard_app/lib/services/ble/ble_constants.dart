import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class BleConstants {
  /// The BLE device name advertised by the firmware.
  static const String deviceName = 'SnoreGuard';

  // ---------- 128-bit UUIDs (big-endian / standard string form) ----------
  // Derived from firmware cycfg_gatt_db.h little-endian byte arrays.
  // Base: E0-1F-56-98-4B-21-47-10-A0-F6-00-11-22-33-44-55

  /// Sleep Monitor service UUID.
  static final Guid serviceUuid =
      Guid('e01f5698-4b21-4710-a0f6-001122334455');


  /// Time Sync characteristic (Write only, 4-byte uint32 LE epoch).
  static final Guid timeSyncUuid =
      Guid('e11f5698-4b21-4710-a0f6-001122334455');

  /// Log Transfer characteristic (Notify, 7 bytes per event).
  static final Guid logTransferUuid =
      Guid('e21f5698-4b21-4710-a0f6-001122334455');

  /// Haptic Intensity characteristic (Read/Write, 1 byte, levels 0–4).
  static final Guid hapticIntensityUuid =
      Guid('e31f5698-4b21-4710-a0f6-001122334455');

  // ---------- Packet sizes ----------
  static const int eventPacketSize = 7;
  static const int timeSyncSize = 4;
  static const int hapticIntensitySize = 1;

  // ---------- Haptic levels ----------
  static const List<String> hapticLabels = ['20%', '40%', '60%', '80%', '100%'];
  static const int defaultHapticLevel = 2; // 60%

  // ---------- Scan / connect timeouts ----------
  static const Duration scanTimeout = Duration(seconds: 10);
  static const Duration connectTimeout = Duration(seconds: 10);

  /// Silence timeout after last notification to detect end of Morning Sync stream.
  static const Duration syncEosTimeout = Duration(seconds: 2);
}
