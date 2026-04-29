import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../../app/theme.dart';
import '../../../providers/ble_provider.dart';

class HapticEnableToggle extends StatelessWidget {
  const HapticEnableToggle({super.key});

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleProvider>();
    final isConnected = ble.isConnected;
    final hapticEnabled = ble.hapticEnabled;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SectionHeader('Haptic Motor'),
        const SizedBox(height: 4),
        Row(
          children: [
            Expanded(
              child: Text(
                'Enable haptic posture correction during sleep.',
                style: Theme.of(context)
                    .textTheme
                    .bodySmall
                    ?.copyWith(color: AppColors.textSecondary),
              ),
            ),
            Switch(
              value: hapticEnabled,
              onChanged: isConnected
                  ? (value) => ble.setHapticEnabled(value)
                  : null,
              activeColor: AppColors.primary,
            ),
          ],
        ),
        if (!isConnected) ...[
          const SizedBox(height: 4),
          Text(
            'Connect to a device to control haptic motor.',
            style: Theme.of(context)
                .textTheme
                .labelSmall
                ?.copyWith(color: AppColors.textSecondary),
          ),
        ],
      ],
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
