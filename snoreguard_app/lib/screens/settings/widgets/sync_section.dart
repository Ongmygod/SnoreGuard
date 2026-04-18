import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../../app/theme.dart';
import '../../../providers/ble_provider.dart';
import '../../../providers/session_provider.dart';

class SyncSection extends StatelessWidget {
  const SyncSection({super.key});

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleProvider>();

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SectionHeader('Morning Sync'),
        const SizedBox(height: 8),
        Text(
          'Press the button on your SnoreGuard device, then tap Sync below. '
          'The device will transfer last night\'s sleep data.',
          style: Theme.of(context)
              .textTheme
              .bodySmall
              ?.copyWith(color: AppColors.textSecondary, height: 1.5),
        ),
        const SizedBox(height: 16),
        // Sync button
        SizedBox(
          width: double.infinity,
          child: ElevatedButton.icon(
            onPressed: (ble.isConnected && !ble.isSyncing)
                ? () => _doSync(context)
                : null,
            icon: ble.isSyncing
                ? const SizedBox(
                    width: 16,
                    height: 16,
                    child: CircularProgressIndicator(
                        strokeWidth: 2, color: Colors.white),
                  )
                : const Icon(Icons.sync),
            label: Text(ble.isSyncing ? 'Syncing...' : 'Sync Now'),
          ),
        ),
        const SizedBox(height: 12),
        // Progress / result
        if (ble.isSyncing) ...[
          _SyncProgressBar(eventsReceived: ble.syncEventsReceived),
        ] else if (ble.syncComplete) ...[
          _SyncResultBanner(
            newEvents: ble.syncNewEvents,
            total: ble.syncEventsReceived,
          ),
        ],
        // Error
        if (ble.errorMessage != null && _isSyncError(ble.errorMessage!)) ...[
          const SizedBox(height: 8),
          _SyncErrorBanner(
            message: ble.errorMessage!,
            onDismiss: () {
              ble.clearError();
              ble.resetSyncState();
            },
          ),
        ],
        if (!ble.isConnected && !ble.isSyncing) ...[
          const SizedBox(height: 8),
          Text(
            'Connect to your device first.',
            style: Theme.of(context)
                .textTheme
                .labelSmall
                ?.copyWith(color: AppColors.textSecondary),
          ),
        ],
      ],
    );
  }

  Future<void> _doSync(BuildContext context) async {
    final bleProvider = context.read<BleProvider>();
    final sessionProvider = context.read<SessionProvider>();

    bleProvider.resetSyncState();
    await bleProvider.performMorningSync(
      onComplete: () => sessionProvider.loadRecentSessions(),
    );
  }

  bool _isSyncError(String msg) =>
      msg.contains('Sync') ||
      msg.contains('sync') ||
      msg.contains('Connection lost') ||
      msg.contains('event');
}

class _SyncProgressBar extends StatelessWidget {
  final int eventsReceived;

  const _SyncProgressBar({required this.eventsReceived});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: AppColors.primary.withAlpha(20),
        borderRadius: BorderRadius.circular(8),
      ),
      child: Row(
        children: [
          const SizedBox(
            width: 16,
            height: 16,
            child: CircularProgressIndicator(strokeWidth: 2),
          ),
          const SizedBox(width: 12),
          Text(
            eventsReceived == 0
                ? 'Waiting for device...'
                : 'Received $eventsReceived event${eventsReceived == 1 ? '' : 's'}...',
            style: const TextStyle(
                color: AppColors.textPrimary, fontSize: 13),
          ),
        ],
      ),
    );
  }
}

class _SyncResultBanner extends StatelessWidget {
  final int newEvents;
  final int total;

  const _SyncResultBanner(
      {required this.newEvents, required this.total});

  @override
  Widget build(BuildContext context) {
    final message = newEvents == 0
        ? total == 0
            ? 'No events on device.'
            : 'Already synced — no new events.'
        : 'Synced $newEvents new event${newEvents == 1 ? '' : 's'}!';

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
      decoration: BoxDecoration(
        color: AppColors.success.withAlpha(20),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: AppColors.success.withAlpha(60)),
      ),
      child: Row(
        children: [
          const Icon(Icons.check_circle_outline,
              color: AppColors.success, size: 16),
          const SizedBox(width: 8),
          Text(message,
              style:
                  const TextStyle(color: AppColors.success, fontSize: 13)),
        ],
      ),
    );
  }
}

class _SyncErrorBanner extends StatelessWidget {
  final String message;
  final VoidCallback onDismiss;

  const _SyncErrorBanner(
      {required this.message, required this.onDismiss});

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
          const Icon(Icons.warning_amber_outlined,
              color: AppColors.warning, size: 16),
          const SizedBox(width: 8),
          Expanded(
            child: Text(message,
                style:
                    const TextStyle(color: AppColors.warning, fontSize: 12)),
          ),
          GestureDetector(
            onTap: onDismiss,
            child: const Icon(Icons.close,
                color: AppColors.textSecondary, size: 16),
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
