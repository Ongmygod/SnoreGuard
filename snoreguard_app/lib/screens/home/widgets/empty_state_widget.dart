import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../../app/routes.dart';
import '../../../app/theme.dart';
import '../../../providers/ble_provider.dart';

class EmptyStateWidget extends StatelessWidget {
  const EmptyStateWidget({super.key});

  @override
  Widget build(BuildContext context) {
    final isConnected = context.watch<BleProvider>().isConnected;

    return Center(
      child: Padding(
        padding: const EdgeInsets.all(40),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              Icons.bedtime_outlined,
              size: 80,
              color: AppColors.textSecondary.withAlpha(100),
            ),
            const SizedBox(height: 24),
            Text(
              'No sleep sessions yet',
              style: Theme.of(context).textTheme.titleLarge?.copyWith(
                    color: AppColors.textSecondary,
                    fontWeight: FontWeight.w600,
                  ),
            ),
            const SizedBox(height: 12),
            Text(
              'Sync your SnoreGuard device in the morning\nto see your sleep history here.',
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                    color: AppColors.textSecondary,
                    height: 1.5,
                  ),
            ),
            const SizedBox(height: 32),
            if (!isConnected)
              ElevatedButton.icon(
                onPressed: () =>
                    Navigator.pushNamed(context, Routes.settings),
                icon: const Icon(Icons.bluetooth_outlined),
                label: const Text('Set Up Device'),
              ),
          ],
        ),
      ),
    );
  }
}
