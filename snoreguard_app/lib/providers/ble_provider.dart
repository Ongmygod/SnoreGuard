import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/snore_event.dart';
import '../services/ble/ble_service.dart';
import '../services/ble/ble_constants.dart';
import '../services/database/snore_event_dao.dart';
import '../utils/permission_utils.dart';

class BleProvider extends ChangeNotifier {
  final BleService _bleService = BleService();
  final SnoreEventDao _dao = SnoreEventDao();

  BleConnectionState _connectionState = BleConnectionState.disconnected;
  List<ScanResult> _scanResults = [];
  bool _isSyncing = false;
  bool _syncComplete = false;
  int _syncEventsReceived = 0;
  int _syncNewEvents = 0;
  int _hapticLevel = BleConstants.defaultHapticLevel;
  bool _hapticEnabled = true;
  String? _errorMessage;
  String? _connectedDeviceName;
  bool _permissionsGranted = false;
  bool _bluetoothOn = false;

  StreamSubscription<BleConnectionState>? _stateSub;
  StreamSubscription<BluetoothAdapterState>? _adapterSub;

  // ---------- Getters ----------
  BleConnectionState get connectionState => _connectionState;
  List<ScanResult> get scanResults => _scanResults;
  bool get isSyncing => _isSyncing;
  bool get syncComplete => _syncComplete;
  int get syncEventsReceived => _syncEventsReceived;
  int get syncNewEvents => _syncNewEvents;
  int get hapticLevel => _hapticLevel;
  bool get hapticEnabled => _hapticEnabled;
  String? get errorMessage => _errorMessage;
  String? get connectedDeviceName => _connectedDeviceName;
  bool get permissionsGranted => _permissionsGranted;
  bool get bluetoothOn => _bluetoothOn;
  bool get isConnected => _connectionState == BleConnectionState.connected;
  bool get isScanning => _connectionState == BleConnectionState.scanning;

  // ---------- Init ----------

  /// Initialize BLE: check permissions and adapter state.
  Future<void> initialize() async {
    // Watch connection state changes from BleService
    _stateSub = _bleService.connectionStateStream.listen((state) {
      _connectionState = state;
      _connectedDeviceName = _bleService.connectedDeviceName;
      notifyListeners();
    });

    // Watch Bluetooth adapter state
    _adapterSub = FlutterBluePlus.adapterState.listen((state) {
      _bluetoothOn = state == BluetoothAdapterState.on;
      notifyListeners();
    });

    _bluetoothOn =
        await FlutterBluePlus.adapterState.first ==
            BluetoothAdapterState.on;

    _permissionsGranted = await PermissionUtils.areBlePermissionsGranted();
    notifyListeners();
  }

  // ---------- Scan ----------

  Future<void> startScan() async {
    _clearError();
    _scanResults = [];

    // Check prerequisites
    if (!_bluetoothOn) {
      _setError('Bluetooth is off. Please enable it.');
      return;
    }

    if (!_permissionsGranted) {
      _permissionsGranted = await PermissionUtils.requestBlePermissions();
      if (!_permissionsGranted) {
        _setError('Bluetooth permissions are required to scan for devices.');
        return;
      }
    }

    try {
      final results = await _bleService.scanForDevices();
      _scanResults = results;
      if (results.isEmpty) {
        _setError(
            'No SnoreGuard device found. Make sure it is powered on and nearby.');
      }
    } catch (e) {
      _setError('Scan failed: ${_friendly(e)}');
    }
    notifyListeners();
  }

  Future<void> stopScan() async {
    await _bleService.stopScan();
  }

  // ---------- Connect ----------

  Future<void> connectToDevice(BluetoothDevice device) async {
    _clearError();
    try {
      await _bleService.connectToDevice(device);

      // Auto Time Sync on connect
      await _bleService.sendTimeSync();

      // Read current haptic level (firmware may have auto-escalated during sleep)
      final level = await _bleService.readHapticIntensity();
      if (level != null) _hapticLevel = level;

      // Read haptic enable state from device
      final enabled = await _bleService.readHapticEnabled();
      if (enabled != null) _hapticEnabled = enabled;
    } on TimeoutException {
      _setError('Connection timed out. Move closer to the device and try again.');
    } on StateError catch (e) {
      _setError(e.message);
    } catch (e) {
      _setError('Could not connect: ${_friendly(e)}');
    }
    notifyListeners();
  }

  Future<void> disconnect() async {
    await _bleService.disconnect();
    notifyListeners();
  }

  // ---------- Morning Sync ----------

  /// Collect events from the device, insert into DB, refresh haptic level.
  /// [onComplete] is called after DB insert so the UI can trigger a session reload.
  Future<void> performMorningSync({VoidCallback? onComplete}) async {
    if (!isConnected) {
      _setError('Not connected to a device.');
      return;
    }

    _clearError();
    _isSyncing = true;
    _syncComplete = false;
    _syncEventsReceived = 0;
    _syncNewEvents = 0;
    notifyListeners();

    final events = <SnoreEvent>[];

    try {
      final stream = _bleService.startMorningSync();
      await for (final event in stream) {
        events.add(event);
        _syncEventsReceived = events.length;
        notifyListeners();
      }

      // Batch insert (dedup via UNIQUE index)
      _syncNewEvents = await _dao.insertEvents(events);
      _syncComplete = true;

      // Acknowledge to device that all events were saved — triggers log clear on firmware
      if (events.isNotEmpty) {
        await _bleService.sendSyncAck(success: true);
      }

      // Read back haptic level and enable state — firmware may have auto-escalated
      final level = await _bleService.readHapticIntensity();
      if (level != null) _hapticLevel = level;
      final enabled = await _bleService.readHapticEnabled();
      if (enabled != null) _hapticEnabled = enabled;

      onComplete?.call();
    } on StateError catch (e) {
      _setError(e.message);
    } catch (e) {
      final msg = _friendly(e);
      if (events.isNotEmpty) {
        // Partial sync: events already buffered are saved
        _syncNewEvents = await _dao.insertEvents(events).catchError((_) => 0);
        _setError(
            'Connection lost. $_syncNewEvents event(s) saved. Re-sync to get remaining events.');
        _syncComplete = true;
        onComplete?.call();
      } else {
        _setError('Sync failed: $msg');
      }
    } finally {
      _isSyncing = false;
      notifyListeners();
    }
  }

  void resetSyncState() {
    _syncComplete = false;
    _syncEventsReceived = 0;
    _syncNewEvents = 0;
    notifyListeners();
  }

  // ---------- Haptic Enable ----------

  Future<void> setHapticEnabled(bool enabled) async {
    if (!isConnected) {
      _setError('Not connected to a device.');
      return;
    }
    _clearError();
    try {
      await _bleService.writeHapticEnabled(enabled);
      _hapticEnabled = enabled;
    } catch (e) {
      _setError('Could not update haptic enabled state: ${_friendly(e)}');
    }
    notifyListeners();
  }

  // ---------- Haptic Intensity ----------

  Future<void> setHapticLevel(int level) async {
    if (!isConnected) {
      _setError('Not connected to a device.');
      return;
    }
    _clearError();
    try {
      await _bleService.writeHapticIntensity(level);
      _hapticLevel = level;
    } catch (e) {
      _setError('Could not update haptic level: ${_friendly(e)}');
    }
    notifyListeners();
  }

  // ---------- Helpers ----------

  void _setError(String message) {
    _errorMessage = message;
    notifyListeners();
  }

  void _clearError() {
    _errorMessage = null;
  }

  void clearError() {
    _clearError();
    notifyListeners();
  }

  String _friendly(Object e) {
    final s = e.toString();
    if (s.contains('timeout') || s.contains('Timeout')) return 'Operation timed out';
    if (s.contains('permission') || s.contains('Permission')) {
      return 'Permission denied';
    }
    if (s.contains('disconnected') || s.contains('Disconnected')) {
      return 'Device disconnected';
    }
    return s.length > 80 ? '${s.substring(0, 80)}…' : s;
  }

  @override
  void dispose() {
    _stateSub?.cancel();
    _adapterSub?.cancel();
    _bleService.dispose();
    super.dispose();
  }
}
