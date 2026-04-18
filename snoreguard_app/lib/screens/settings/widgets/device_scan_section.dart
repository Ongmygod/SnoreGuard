import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../../app/theme.dart';
import '../../../providers/ble_provider.dart';
import '../../../services/ble/ble_service.dart';

class DeviceScanSection extends StatelessWidget {
  const DeviceScanSection({super.key});

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleProvider>();

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SectionHeader('Device'),
        const SizedBox(height: 12),
        if (ble.isConnected) ...[
          _ConnectedTile(
            deviceName:
                ble.connectedDeviceName ?? 'SnoreGuard',
            onDisconnect: () => ble.disconnect(),
          ),
        ] else if (ble.connectionState == BleConnectionState.connecting) ...[
          const Center(
            child: Padding(
              padding: EdgeInsets.all(16),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  SizedBox(
                    width: 18,
                    height: 18,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  ),
                  SizedBox(width: 12),
                  Text('Connecting...',
                      style: TextStyle(color: AppColors.textSecondary)),
                ],
              ),
            ),
          ),
        ] else ...[
          ElevatedButton.icon(
            onPressed: ble.isScanning || !ble.bluetoothOn
                ? null
                : () => ble.startScan(),
            icon: ble.isScanning
                ? const SizedBox(
                    width: 16,
                    height: 16,
                    child: CircularProgressIndicator(
                        strokeWidth: 2, color: Colors.white),
                  )
                : const Icon(Icons.bluetooth_searching),
            label: Text(ble.isScanning ? 'Scanning...' : 'Scan for SnoreGuard'),
          ),
          if (ble.scanResults.isNotEmpty) ...[
            const SizedBox(height: 12),
            Text('Found devices:',
                style: Theme.of(context).textTheme.labelSmall?.copyWith(
                      color: AppColors.textSecondary,
                    )),
            const SizedBox(height: 6),
            ...ble.scanResults.map(
              (r) => Card(
                margin: const EdgeInsets.only(bottom: 6),
                child: ListTile(
                  leading: const Icon(Icons.bluetooth, color: AppColors.primary),
                  title: Text(
                    r.device.platformName.isNotEmpty
                        ? r.device.platformName
                        : 'SnoreGuard',
                    style: const TextStyle(color: AppColors.textPrimary),
                  ),
                  subtitle: Text(
                    r.device.remoteId.str,
                    style: TextStyle(
                        color: AppColors.textSecondary, fontSize: 11),
                  ),
                  trailing: ElevatedButton(
                    onPressed: () => ble.connectToDevice(r.device),
                    style: ElevatedButton.styleFrom(
                        padding: const EdgeInsets.symmetric(
                            horizontal: 16, vertical: 8)),
                    child: const Text('Connect'),
                  ),
                ),
              ),
            ),
          ],
        ],
        // Error message
        if (ble.errorMessage != null && _isScanError(ble.errorMessage!)) ...[
          const SizedBox(height: 10),
          _ErrorBanner(
            message: ble.errorMessage!,
            onDismiss: ble.clearError,
          ),
        ],
      ],
    );
  }

  bool _isScanError(String msg) =>
      msg.contains('Scan') ||
      msg.contains('scan') ||
      msg.contains('connect') ||
      msg.contains('Connect') ||
      msg.contains('Bluetooth') ||
      msg.contains('permission');
}

class _ConnectedTile extends StatelessWidget {
  final String deviceName;
  final VoidCallback onDisconnect;

  const _ConnectedTile(
      {required this.deviceName, required this.onDisconnect});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: AppColors.success.withAlpha(20),
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: AppColors.success.withAlpha(60)),
      ),
      child: Row(
        children: [
          const Icon(Icons.bluetooth_connected,
              color: AppColors.success, size: 22),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(deviceName,
                    style: const TextStyle(
                        color: AppColors.textPrimary,
                        fontWeight: FontWeight.w600)),
                Text('Connected',
                    style: TextStyle(
                        color: AppColors.success, fontSize: 12)),
              ],
            ),
          ),
          TextButton(
            onPressed: onDisconnect,
            style: TextButton.styleFrom(
                foregroundColor: AppColors.error),
            child: const Text('Disconnect'),
          ),
        ],
      ),
    );
  }
}

class _SectionHeader extends StatelessWidget {
  final String text;
  const _SectionHeader(this.text);

  @override
  Widget build(BuildContext context) {
    return Text(
      text.toUpperCase(),
      style: Theme.of(context).textTheme.labelSmall?.copyWith(
            color: AppColors.textSecondary,
            letterSpacing: 1.0,
          ),
    );
  }
}

class _ErrorBanner extends StatelessWidget {
  final String message;
  final VoidCallback onDismiss;

  const _ErrorBanner({required this.message, required this.onDismiss});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
      decoration: BoxDecoration(
        color: AppColors.error.withAlpha(20),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: AppColors.error.withAlpha(60)),
      ),
      child: Row(
        children: [
          const Icon(Icons.error_outline, color: AppColors.error, size: 16),
          const SizedBox(width: 8),
          Expanded(
            child: Text(message,
                style: const TextStyle(
                    color: AppColors.error, fontSize: 12)),
          ),
          GestureDetector(
            onTap: onDismiss,
            child: const Icon(Icons.close,
                color: AppColors.error, size: 16),
          ),
        ],
      ),
    );
  }
}
