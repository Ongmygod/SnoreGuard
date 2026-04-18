import 'package:flutter/material.dart';
import '../../../app/theme.dart';
import '../../../models/sleep_session.dart';

class StatsRow extends StatelessWidget {
  final SleepSession session;

  const StatsRow({super.key, required this.session});

  @override
  Widget build(BuildContext context) {
    final successRate = session.hapticSuccessRate;

    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 16, 16, 0),
      child: Row(
        children: [
          Expanded(
            child: _StatCard(
              icon: Icons.mic_none_outlined,
              iconColor: AppColors.neutral,
              value: '${session.totalSnoreCount}',
              label: 'Snore Events',
            ),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: _StatCard(
              icon: Icons.timer_outlined,
              iconColor: AppColors.warning,
              value: session.formattedDuration,
              label: 'Total Duration',
            ),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: _StatCard(
              icon: Icons.vibration,
              iconColor: successRate == null
                  ? AppColors.textSecondary
                  : successRate >= 0.5
                      ? AppColors.success
                      : AppColors.error,
              value: successRate != null
                  ? '${(successRate * 100).toStringAsFixed(0)}%'
                  : 'N/A',
              label: 'Haptic Success',
            ),
          ),
        ],
      ),
    );
  }
}

class _StatCard extends StatelessWidget {
  final IconData icon;
  final Color iconColor;
  final String value;
  final String label;

  const _StatCard({
    required this.icon,
    required this.iconColor,
    required this.value,
    required this.label,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(vertical: 14, horizontal: 12),
      decoration: BoxDecoration(
        color: AppColors.cardBackground,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, size: 18, color: iconColor),
          const SizedBox(height: 8),
          Text(
            value,
            style: Theme.of(context).textTheme.titleLarge?.copyWith(
                  color: AppColors.textPrimary,
                  fontWeight: FontWeight.w700,
                ),
          ),
          const SizedBox(height: 2),
          Text(
            label,
            style: Theme.of(context).textTheme.labelSmall?.copyWith(
                  color: AppColors.textSecondary,
                ),
          ),
        ],
      ),
    );
  }
}
