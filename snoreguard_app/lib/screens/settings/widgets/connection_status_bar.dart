import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../../app/theme.dart';
import '../../../providers/ble_provider.dart';
import '../../../services/ble/ble_service.dart';

class ConnectionStatusBar extends StatelessWidget {
  const ConnectionStatusBar({super.key});

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleProvider>();
    final (color, icon, label) = _statusInfo(ble);

    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
      color: color.withAlpha(25),
      child: Row(
        children: [
          Container(
            width: 8,
            height: 8,
            decoration: BoxDecoration(color: color, shape: BoxShape.circle),
          ),
          const SizedBox(width: 10),
          Icon(icon, size: 16, color: color),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              label,
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: color,
                    fontWeight: FontWeight.w600,
                  ),
            ),
          ),
          if (!ble.bluetoothOn)
            TextButton(
              onPressed: () {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(
                    content: Text('Please enable Bluetooth in system settings.'),
                  ),
                );
              },
              style: TextButton.styleFrom(
                foregroundColor: color,
                padding: EdgeInsets.zero,
              ),
              child: const Text('Enable'),
            ),
        ],
      ),
    );
  }

  (Color, IconData, String) _statusInfo(BleProvider ble) {
    if (!ble.bluetoothOn) {
      return (AppColors.error, Icons.bluetooth_disabled,
          'Bluetooth is off');
    }
    return switch (ble.connectionState) {
      BleConnectionState.connected => (
          AppColors.success,
          Icons.bluetooth_connected,
          'Connected: ${ble.connectedDeviceName ?? 'SnoreGuard'}'
        ),
      BleConnectionState.connecting => (
          AppColors.warning,
          Icons.bluetooth_searching,
          'Connecting...'
        ),
      BleConnectionState.scanning => (
          AppColors.warning,
          Icons.bluetooth_searching,
          'Scanning...'
        ),
      BleConnectionState.syncing => (
          AppColors.primary,
          Icons.sync,
          'Syncing...'
        ),
      BleConnectionState.disconnected => (
          AppColors.textSecondary,
          Icons.bluetooth_outlined,
          'Not connected'
        ),
    };
  }
}
