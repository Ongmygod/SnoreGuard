import 'dart:async';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../../models/snore_event.dart';
import 'ble_constants.dart';
import 'ble_packet_parser.dart';

enum BleConnectionState {
  disconnected,
  scanning,
  connecting,
  connected,
  syncing,
}

class SyncProgress {
  final int eventsReceived;
  final bool isComplete;
  const SyncProgress({required this.eventsReceived, required this.isComplete});
}

class BleService {
  BluetoothDevice? _device;
  BluetoothCharacteristic? _timeSyncChar;
  BluetoothCharacteristic? _logTransferChar;
  BluetoothCharacteristic? _hapticIntensityChar;

  StreamSubscription<BluetoothConnectionState>? _connectionSub;
  StreamSubscription<List<int>>? _notificationSub;

  final _stateController =
      StreamController<BleConnectionState>.broadcast();
  final _syncProgressController =
      StreamController<SyncProgress>.broadcast();

  Stream<BleConnectionState> get connectionStateStream =>
      _stateController.stream;
  Stream<SyncProgress> get syncProgressStream =>
      _syncProgressController.stream;

  BleConnectionState _state = BleConnectionState.disconnected;
  BleConnectionState get currentState => _state;

  String? _connectedDeviceName;
  String? get connectedDeviceName => _connectedDeviceName;

  void _setState(BleConnectionState s) {
    _state = s;
    _stateController.add(s);
  }

  // ---------- Scan ----------

  /// Start scanning for SnoreGuard devices.
  /// Returns the list of found scan results after [BleConstants.scanTimeout].
  Future<List<ScanResult>> scanForDevices() async {
    _setState(BleConnectionState.scanning);

    final results = <ScanResult>[];
    StreamSubscription<List<ScanResult>>? sub;

    try {
      // Stop any ongoing scan first
      if (FlutterBluePlus.isScanningNow) await FlutterBluePlus.stopScan();

      await FlutterBluePlus.startScan(
        timeout: BleConstants.scanTimeout,
        withNames: [BleConstants.deviceName],
      );

      sub = FlutterBluePlus.scanResults.listen((list) {
        for (final r in list) {
          if (!results.any((x) => x.device.remoteId == r.device.remoteId)) {
            results.add(r);
          }
        }
      });

      // Wait for the scan to complete
      await FlutterBluePlus.isScanning
          .where((scanning) => !scanning)
          .first
          .timeout(BleConstants.scanTimeout + const Duration(seconds: 2));
    } finally {
      sub?.cancel();
      if (FlutterBluePlus.isScanningNow) await FlutterBluePlus.stopScan();
    }

    _setState(BleConnectionState.disconnected);
    return results;
  }

  /// Stop an in-progress scan.
  Future<void> stopScan() async {
    if (FlutterBluePlus.isScanningNow) await FlutterBluePlus.stopScan();
    if (_state == BleConnectionState.scanning) {
      _setState(BleConnectionState.disconnected);
    }
  }

  // ---------- Connect ----------

  /// Connect to [device], discover services, and cache characteristic refs.
  /// Returns true on success.
  Future<bool> connectToDevice(BluetoothDevice device) async {
    _setState(BleConnectionState.connecting);

    try {
      await device.connect(
        timeout: BleConstants.connectTimeout,
        autoConnect: false,
      );

      // Listen for disconnection
      _connectionSub?.cancel();
      _connectionSub = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          _onDeviceDisconnected();
        }
      });

      // Discover services
      final services = await device.discoverServices();
      final sleepService = services.firstWhere(
        (s) => s.serviceUuid == BleConstants.serviceUuid,
        orElse: () => throw Exception(
            'Sleep Monitor service not found on device. Check firmware.'),
      );

      // Cache characteristics
      for (final c in sleepService.characteristics) {
        if (c.characteristicUuid == BleConstants.timeSyncUuid) {
          _timeSyncChar = c;
        } else if (c.characteristicUuid == BleConstants.logTransferUuid) {
          _logTransferChar = c;
        } else if (c.characteristicUuid == BleConstants.hapticIntensityUuid) {
          _hapticIntensityChar = c;
        }
      }

      if (_timeSyncChar == null ||
          _logTransferChar == null ||
          _hapticIntensityChar == null) {
        throw Exception('One or more required characteristics not found.');
      }

      _device = device;
      _connectedDeviceName = device.platformName.isNotEmpty
          ? device.platformName
          : BleConstants.deviceName;
      _setState(BleConnectionState.connected);
      return true;
    } catch (e) {
      await device.disconnect().catchError((_) {});
      _setState(BleConnectionState.disconnected);
      rethrow;
    }
  }

  void _onDeviceDisconnected() {
    _device = null;
    _timeSyncChar = null;
    _logTransferChar = null;
    _hapticIntensityChar = null;
    _connectedDeviceName = null;
    _connectionSub?.cancel();
    _connectionSub = null;
    _notificationSub?.cancel();
    _notificationSub = null;
    if (_state != BleConnectionState.disconnected) {
      _setState(BleConnectionState.disconnected);
    }
  }

  /// Disconnect from the current device.
  Future<void> disconnect() async {
    await _device?.disconnect().catchError((_) {});
    _onDeviceDisconnected();
  }

  // ---------- Time Sync ----------

  /// Write current Unix epoch to Time Sync characteristic.
  Future<void> sendTimeSync() async {
    _assertConnected();
    final payload = BlePacketParser.encodeTimeSyncPayload();
    await _timeSyncChar!.write(payload, withoutResponse: false);
  }

  // ---------- Morning Sync ----------

  /// Subscribe to Log Transfer notifications and yield decoded [SnoreEvent]s.
  ///
  /// End-of-stream is detected by a [BleConstants.syncEosTimeout] silence
  /// period after the last notification (the firmware has no explicit EOS marker).
  ///
  /// Throws if called when not connected or if connection is lost mid-sync.
  Stream<SnoreEvent> startMorningSync() {
    _assertConnected();

    final controller = StreamController<SnoreEvent>();
    Timer? eosTimer;
    StreamSubscription<BluetoothConnectionState>? disconnectSub;

    void close() {
      eosTimer?.cancel();
      disconnectSub?.cancel();
      _notificationSub?.cancel();
      _notificationSub = null;
      _logTransferChar?.setNotifyValue(false).ignore();
      if (!controller.isClosed) controller.close();
    }

    void resetEosTimer() {
      eosTimer?.cancel();
      eosTimer = Timer(BleConstants.syncEosTimeout, close);
    }

    _setState(BleConnectionState.syncing);

    _logTransferChar!.setNotifyValue(true).then((_) {
      // Don't start EOS timer yet — wait for the first notification.
      // The firmware only starts streaming after the user presses the
      // physical button, so we use a longer initial wait (60 s).
      eosTimer = Timer(const Duration(seconds: 60), close);

      _notificationSub =
          _logTransferChar!.onValueReceived.listen(
        (value) {
          final event = BlePacketParser.parseEventPacket(value);
          if (event != null && !controller.isClosed) {
            controller.add(event);
          }
          // After first data arrives, switch to short silence detection
          resetEosTimer();
        },
        onError: (e) {
          eosTimer?.cancel();
          if (!controller.isClosed) controller.addError(e);
          close();
        },
      );

      // Watch for disconnection during sync
      disconnectSub = _device!.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected &&
            !controller.isClosed) {
          controller.addError(
              Exception('Connection lost during sync. Partial data saved.'));
          close();
        }
      });
    }).catchError((e) {
      if (!controller.isClosed) controller.addError(e);
      close();
    });

    controller.onCancel = () {
      close();
      if (_state == BleConnectionState.syncing) {
        _setState(BleConnectionState.connected);
      }
    };

    return controller.stream;
  }

  // ---------- Haptic Intensity ----------

  /// Read current haptic level (0–4) from the device.
  Future<int?> readHapticIntensity() async {
    if (_hapticIntensityChar == null) return null;
    try {
      final value = await _hapticIntensityChar!.read();
      if (value.isNotEmpty) return value[0].clamp(0, 4);
    } catch (_) {}
    return null;
  }

  /// Write haptic intensity level (0–4) to the device.
  Future<void> writeHapticIntensity(int level) async {
    _assertConnected();
    final payload = BlePacketParser.encodeHapticLevel(level.clamp(0, 4));
    await _hapticIntensityChar!.write(payload, withoutResponse: false);
  }

  // ---------- Helpers ----------

  void _assertConnected() {
    if (_state != BleConnectionState.connected &&
        _state != BleConnectionState.syncing) {
      throw StateError('BLE: not connected to a device.');
    }
  }

  bool get isConnected =>
      _state == BleConnectionState.connected ||
      _state == BleConnectionState.syncing;

  void dispose() {
    _connectionSub?.cancel();
    _notificationSub?.cancel();
    _stateController.close();
    _syncProgressController.close();
  }
}
