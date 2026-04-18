import 'package:permission_handler/permission_handler.dart';

class PermissionUtils {
  /// Request all BLE-related permissions.
  /// Returns true when all required permissions are granted.
  static Future<bool> requestBlePermissions() async {
    final statuses = await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.location,
    ].request();

    return statuses.values.every((s) => s.isGranted || s.isLimited);
  }

  /// Check if all BLE permissions are currently granted.
  static Future<bool> areBlePermissionsGranted() async {
    final results = await Future.wait([
      Permission.bluetoothScan.status,
      Permission.bluetoothConnect.status,
      Permission.location.status,
    ]);
    return results.every((s) => s.isGranted || s.isLimited);
  }

  /// Check if any BLE permission is permanently denied (requires app settings).
  static Future<bool> anyBlePermanentlyDenied() async {
    final results = await Future.wait([
      Permission.bluetoothScan.status,
      Permission.bluetoothConnect.status,
      Permission.location.status,
    ]);
    return results.any((s) => s.isPermanentlyDenied);
  }

  /// Open app permission settings.
  static Future<void> openSettings() => openAppSettings();
}
