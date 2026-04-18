import 'package:flutter/material.dart';
import '../../../app/theme.dart';
import '../../../models/sleep_session.dart';

class SessionCard extends StatelessWidget {
  final SleepSession session;
  final VoidCallback onTap;

  const SessionCard({
    super.key,
    required this.session,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    final successRate = session.hapticSuccessRate;

    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 6),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              // Date block
              Column(
                mainAxisSize: MainAxisSize.min,
                children: session.shortDate
                    .split('\n')
                    .asMap()
                    .entries
                    .map((e) => Text(
                          e.value,
                          style: e.key == 0
                              ? Theme.of(context)
                                  .textTheme
                                  .labelSmall
                                  ?.copyWith(color: AppColors.textSecondary)
                              : Theme.of(context)
                                  .textTheme
                                  .titleMedium
                                  ?.copyWith(
                                    color: AppColors.primary,
                                    fontWeight: FontWeight.w700,
                                  ),
                        ))
                    .toList(),
              ),
              const SizedBox(width: 16),
              const VerticalDivider(width: 1, thickness: 1),
              const SizedBox(width: 16),
              // Main stats
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Row(
                      children: [
                        const Icon(Icons.mic_none_outlined,
                            size: 14, color: AppColors.neutral),
                        const SizedBox(width: 4),
                        Text(
                          '${session.totalSnoreCount} snore event${session.totalSnoreCount == 1 ? '' : 's'}',
                          style: Theme.of(context)
                              .textTheme
                              .titleSmall
                              ?.copyWith(color: AppColors.textPrimary),
                        ),
                        if (session.hasFallbackTimestamps) ...[
                          const SizedBox(width: 6),
                          Tooltip(
                            message: 'Approximate timestamps',
                            child: Icon(Icons.warning_amber_outlined,
                                size: 14, color: AppColors.warning),
                          ),
                        ],
                      ],
                    ),
                    const SizedBox(height: 4),
                    Text(
                      'Duration: ${session.formattedDuration}',
                      style: Theme.of(context)
                          .textTheme
                          .bodySmall
                          ?.copyWith(color: AppColors.textSecondary),
                    ),
                  ],
                ),
              ),
              // Haptic stats
              if (session.hapticTriggerCount > 0)
                Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.end,
                  children: [
                    Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        const Icon(Icons.vibration,
                            size: 14, color: AppColors.textSecondary),
                        const SizedBox(width: 2),
                        Text(
                          '${session.hapticTriggerCount}',
                          style: Theme.of(context)
                              .textTheme
                              .bodySmall
                              ?.copyWith(color: AppColors.textSecondary),
                        ),
                      ],
                    ),
                    if (successRate != null)
                      Text(
                        '${(successRate * 100).toStringAsFixed(0)}%',
                        style: Theme.of(context).textTheme.labelSmall?.copyWith(
                              color: successRate >= 0.5
                                  ? AppColors.success
                                  : AppColors.error,
                              fontWeight: FontWeight.w600,
                            ),
                      ),
                  ],
                ),
              const SizedBox(width: 8),
              const Icon(Icons.chevron_right,
                  size: 18, color: AppColors.textSecondary),
            ],
          ),
        ),
      ),
    );
  }
}
