import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../../app/theme.dart';
import '../../../providers/ble_provider.dart';
import '../../../services/ble/ble_constants.dart';

class HapticIntensitySlider extends StatelessWidget {
  const HapticIntensitySlider({super.key});

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleProvider>();
    final level = ble.hapticLevel;
    final isConnected = ble.isConnected;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SectionHeader('Haptic Intensity'),
        const SizedBox(height: 4),
        Text(
          'Adjust vibration strength for posture correction.',
          style: Theme.of(context)
              .textTheme
              .bodySmall
              ?.copyWith(color: AppColors.textSecondary),
        ),
        const SizedBox(height: 16),
        // Level labels
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: BleConstants.hapticLabels
              .asMap()
              .entries
              .map(
                (e) => Text(
                  e.value,
                  style: TextStyle(
                    fontSize: 10,
                    color: e.key == level
                        ? AppColors.primary
                        : AppColors.textSecondary,
                    fontWeight: e.key == level
                        ? FontWeight.w700
                        : FontWeight.normal,
                  ),
                ),
              )
              .toList(),
        ),
        // Slider
        SliderTheme(
          data: SliderTheme.of(context).copyWith(
            trackHeight: 4,
            thumbShape:
                const RoundSliderThumbShape(enabledThumbRadius: 8),
            overlayShape:
                const RoundSliderOverlayShape(overlayRadius: 16),
          ),
          child: Slider(
            value: level.toDouble(),
            min: 0,
            max: 4,
            divisions: 4,
            label: BleConstants.hapticLabels[level],
            onChanged: isConnected
                ? (v) => ble.setHapticLevel(v.round())
                : null,
          ),
        ),
        const SizedBox(height: 8),
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(
              'Current: ${BleConstants.hapticLabels[level]}',
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: AppColors.textPrimary,
                    fontWeight: FontWeight.w600,
                  ),
            ),
            OutlinedButton.icon(
              onPressed: isConnected
                  ? () => _testHaptic(context, ble)
                  : null,
              icon: const Icon(Icons.vibration, size: 16),
              label: const Text('Test'),
              style: OutlinedButton.styleFrom(
                padding: const EdgeInsets.symmetric(
                    horizontal: 16, vertical: 8),
                textStyle: const TextStyle(fontSize: 13),
              ),
            ),
          ],
        ),
        if (!isConnected) ...[
          const SizedBox(height: 8),
          Text(
            'Connect to a device to adjust haptic intensity.',
            style: Theme.of(context)
                .textTheme
                .labelSmall
                ?.copyWith(color: AppColors.textSecondary),
          ),
        ],
      ],
    );
  }

  Future<void> _testHaptic(BuildContext context, BleProvider ble) async {
    // Re-write the current level to trigger a short buzz on the device
    await ble.setHapticLevel(ble.hapticLevel);
    if (context.mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
              'Test vibration sent at ${BleConstants.hapticLabels[ble.hapticLevel]}'),
          duration: const Duration(seconds: 2),
        ),
      );
    }
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
