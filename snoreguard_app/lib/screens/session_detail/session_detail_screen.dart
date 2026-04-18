import 'package:flutter/material.dart';
import '../../app/theme.dart';
import '../../models/sleep_session.dart';
import 'widgets/stats_row.dart';
import 'widgets/event_timeline_chart.dart';
import 'widgets/event_list_tile.dart';

class SessionDetailScreen extends StatelessWidget {
  final SleepSession session;

  const SessionDetailScreen({super.key, required this.session});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(session.displayTitle),
        leading: const BackButton(),
      ),
      body: session.events.isEmpty
          ? const Center(
              child: Text('No events recorded for this session.',
                  style: TextStyle(color: AppColors.textSecondary)),
            )
          : SingleChildScrollView(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  // Stats row
                  StatsRow(session: session),
                  const SizedBox(height: 20),

                  // Timeline section
                  _SectionTitle('Event Timeline', icon: Icons.timeline),
                  const SizedBox(height: 8),
                  EventTimelineChart(events: session.events),
                  const EventTimelineLegend(),

                  // Fallback timestamp warning
                  if (session.hasFallbackTimestamps) ...[
                    const SizedBox(height: 12),
                    _FallbackWarning(),
                  ],

                  const SizedBox(height: 20),

                  // Haptic summary (if any interventions)
                  if (session.hapticTriggerCount > 0) ...[
                    _SectionTitle('Haptic Interventions',
                        icon: Icons.vibration),
                    const SizedBox(height: 8),
                    _HapticSummaryCard(session: session),
                    const SizedBox(height: 20),
                  ],

                  // Event list
                  _SectionTitle('All Events',
                      icon: Icons.list_alt_outlined),
                  const SizedBox(height: 8),
                  // Column header
                  Padding(
                    padding: const EdgeInsets.symmetric(
                        horizontal: 16, vertical: 4),
                    child: Row(
                      children: [
                        const SizedBox(width: 28),
                        SizedBox(
                          width: 60,
                          child: Text('Time',
                              style: TextStyle(
                                  color: AppColors.textSecondary,
                                  fontSize: 11)),
                        ),
                        Text('Duration',
                            style: TextStyle(
                                color: AppColors.textSecondary,
                                fontSize: 11)),
                      ],
                    ),
                  ),
                  ...session.events.asMap().entries.map(
                        (e) => EventListTile(
                          event: e.value,
                          index: e.key,
                        ),
                      ),
                  const SizedBox(height: 32),
                ],
              ),
            ),
    );
  }
}

class _SectionTitle extends StatelessWidget {
  final String title;
  final IconData icon;

  const _SectionTitle(this.title, {required this.icon});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16),
      child: Row(
        children: [
          Icon(icon, size: 16, color: AppColors.primary),
          const SizedBox(width: 6),
          Text(
            title,
            style: Theme.of(context).textTheme.titleSmall?.copyWith(
                  color: AppColors.textPrimary,
                  fontWeight: FontWeight.w600,
                ),
          ),
        ],
      ),
    );
  }
}

class _FallbackWarning extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 16),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: AppColors.warning.withAlpha(20),
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: AppColors.warning.withAlpha(60)),
      ),
      child: Row(
        children: [
          Icon(Icons.warning_amber_outlined,
              color: AppColors.warning, size: 18),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              'Timestamps are approximate — the device was not time-synced before this session.',
              style: TextStyle(
                  color: AppColors.warning.withAlpha(220), fontSize: 12),
            ),
          ),
        ],
      ),
    );
  }
}

class _HapticSummaryCard extends StatelessWidget {
  final SleepSession session;

  const _HapticSummaryCard({required this.session});

  @override
  Widget build(BuildContext context) {
    final rate = session.hapticSuccessRate ?? 0.0;
    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 16),
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: AppColors.cardBackground,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  '${session.hapticTriggerCount} intervention${session.hapticTriggerCount == 1 ? '' : 's'}',
                  style: Theme.of(context).textTheme.titleSmall?.copyWith(
                        color: AppColors.textPrimary,
                      ),
                ),
                const SizedBox(height: 4),
                Text(
                  '${session.hapticSuccessCount} successful · ${session.hapticTriggerCount - session.hapticSuccessCount} no effect',
                  style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: AppColors.textSecondary,
                      ),
                ),
              ],
            ),
          ),
          // Success rate circle indicator
          Stack(
            alignment: Alignment.center,
            children: [
              SizedBox(
                width: 52,
                height: 52,
                child: CircularProgressIndicator(
                  value: rate,
                  strokeWidth: 5,
                  backgroundColor: AppColors.error.withAlpha(40),
                  valueColor: AlwaysStoppedAnimation<Color>(
                    rate >= 0.5 ? AppColors.success : AppColors.error,
                  ),
                ),
              ),
              Text(
                '${(rate * 100).toStringAsFixed(0)}%',
                style: TextStyle(
                  color: rate >= 0.5 ? AppColors.success : AppColors.error,
                  fontSize: 12,
                  fontWeight: FontWeight.w700,
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}
