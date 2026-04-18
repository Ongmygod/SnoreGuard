import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../../app/theme.dart';
import '../../../providers/ble_provider.dart';

class AboutSection extends StatelessWidget {
  const AboutSection({super.key});

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleProvider>();

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          'ABOUT',
          style: Theme.of(context).textTheme.labelSmall?.copyWith(
                color: AppColors.textSecondary,
                letterSpacing: 1.0,
              ),
        ),
        const SizedBox(height: 12),
        Container(
          decoration: BoxDecoration(
            color: AppColors.cardBackground,
            borderRadius: BorderRadius.circular(12),
          ),
          child: Column(
            children: [
              _InfoTile(
                icon: Icons.phone_android_outlined,
                label: 'App Version',
                value: '1.0.0',
              ),
              const Divider(height: 1, indent: 48),
              _InfoTile(
                icon: Icons.memory_outlined,
                label: 'Device',
                value: ble.isConnected
                    ? (ble.connectedDeviceName ?? 'SnoreGuard')
                    : 'Not connected',
                valueColor: ble.isConnected
                    ? AppColors.success
                    : AppColors.textSecondary,
              ),
              const Divider(height: 1, indent: 48),
              _InfoTile(
                icon: Icons.storage_outlined,
                label: 'Data Retention',
                value: '7 days',
              ),
              const Divider(height: 1, indent: 48),
              _InfoTile(
                icon: Icons.wifi_off_outlined,
                label: 'Connectivity',
                value: 'Offline only',
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),
        Center(
          child: Text(
            'KOSEN KMUTT · SnoreGuard Sleep Module\n'
            'Edge AI Snore Detection with BLE Logging',
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.labelSmall?.copyWith(
                  color: AppColors.textSecondary,
                  height: 1.6,
                ),
          ),
        ),
      ],
    );
  }
}

class _InfoTile extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;
  final Color? valueColor;

  const _InfoTile({
    required this.icon,
    required this.label,
    required this.value,
    this.valueColor,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
      child: Row(
        children: [
          Icon(icon, size: 18, color: AppColors.textSecondary),
          const SizedBox(width: 14),
          Expanded(
            child: Text(label,
                style: const TextStyle(color: AppColors.textPrimary)),
          ),
          Text(
            value,
            style: TextStyle(
              color: valueColor ?? AppColors.textSecondary,
              fontSize: 13,
            ),
          ),
        ],
      ),
    );
  }
}
