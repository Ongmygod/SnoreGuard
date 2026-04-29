import 'package:flutter/material.dart';
import 'package:fl_chart/fl_chart.dart';
import 'package:intl/intl.dart';
import 'package:provider/provider.dart';
import '../../../app/theme.dart';
import '../../../models/sleep_session.dart';
import '../../../providers/session_provider.dart';

class WeeklySummaryCard extends StatelessWidget {
  const WeeklySummaryCard({super.key});

  @override
  Widget build(BuildContext context) {
    final summary = context.watch<SessionProvider>().weeklySummary;

    if (summary == null) return const SizedBox.shrink();

    final sevenDayStats = _buildSevenDayStats(summary.dailyStats);
    final maxDuration = sevenDayStats
            .map((d) => d.totalDurationS.toDouble())
            .fold(0.0, (a, b) => a > b ? a : b);

    return Card(
      margin: const EdgeInsets.fromLTRB(16, 8, 16, 6),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Text(
                  'This Week',
                  style: Theme.of(context).textTheme.titleMedium?.copyWith(
                        color: AppColors.textPrimary,
                        fontWeight: FontWeight.w600,
                      ),
                ),
                const SizedBox(width: 8),
                _TrendBadge(trend: summary.trend),
              ],
            ),
            const SizedBox(height: 12),
            // Summary stats row
            Row(
              children: [
                _StatChip(
                  label: 'Avg Snores',
                  value: summary.avgSnoreCount.toStringAsFixed(1),
                ),
                const SizedBox(width: 12),
                _StatChip(
                  label: 'Avg Duration',
                  value: _formatAvgDuration(summary.avgDurationS),
                ),
                const SizedBox(width: 12),
                if (summary.overallHapticSuccessRate != null)
                  _StatChip(
                    label: 'Haptic Success',
                    value:
                        '${(summary.overallHapticSuccessRate! * 100).toStringAsFixed(0)}%',
                  ),
              ],
            ),
            const SizedBox(height: 16),
            // Bar chart
            SizedBox(
              height: 100,
              child: maxDuration == 0
                  ? Center(
                      child: Text(
                        'No data',
                        style: TextStyle(color: AppColors.textSecondary),
                      ),
                    )
                  : BarChart(
                      BarChartData(
                        alignment: BarChartAlignment.spaceAround,
                        maxY: maxDuration + (maxDuration * 0.2) + 1,
                        barTouchData: BarTouchData(
                          enabled: true,
                          touchTooltipData: BarTouchTooltipData(
                            getTooltipColor: (_) => AppColors.surface,
                            getTooltipItem: (group, groupIndex, rod, rodIndex) {
                              final stat = sevenDayStats[group.x];
                              return BarTooltipItem(
                                _formatAvgDuration(stat.totalDurationS.toDouble()),
                                const TextStyle(
                                    color: AppColors.textPrimary, fontSize: 12),
                              );
                            },
                          ),
                        ),
                        titlesData: FlTitlesData(
                          show: true,
                          bottomTitles: AxisTitles(
                            sideTitles: SideTitles(
                              showTitles: true,
                              reservedSize: 22,
                              getTitlesWidget: (value, meta) {
                                final stat = sevenDayStats[value.toInt()];
                                final dt = DateTime.parse(stat.date);
                                return SideTitleWidget(
                                  axisSide: meta.axisSide,
                                  child: Text(
                                    DateFormat('E').format(dt),
                                    style: TextStyle(
                                      color: AppColors.textSecondary,
                                      fontSize: 10,
                                    ),
                                  ),
                                );
                              },
                            ),
                          ),
                          leftTitles: AxisTitles(
                              sideTitles: SideTitles(showTitles: false)),
                          topTitles: AxisTitles(
                              sideTitles: SideTitles(showTitles: false)),
                          rightTitles: AxisTitles(
                              sideTitles: SideTitles(showTitles: false)),
                        ),
                        gridData: FlGridData(show: false),
                        borderData: FlBorderData(show: false),
                        barGroups: List.generate(
                          sevenDayStats.length,
                          (i) => BarChartGroupData(
                            x: i,
                            barRods: [
                              BarChartRodData(
                                toY: sevenDayStats[i].totalDurationS.toDouble(),
                                color: sevenDayStats[i].totalDurationS > 0
                                    ? AppColors.primary
                                    : AppColors.divider,
                                width: 16,
                                borderRadius: const BorderRadius.only(
                                  topLeft: Radius.circular(4),
                                  topRight: Radius.circular(4),
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                    ),
            ),
          ],
        ),
      ),
    );
  }

  /// Build 7 DailyStat entries (oldest → newest), filling zeros for missing days.
  List<DailyStat> _buildSevenDayStats(List<DailyStat> existing) {
    final fmt = DateFormat('yyyy-MM-dd');
    return List.generate(7, (i) {
      final date =
          fmt.format(DateTime.now().subtract(Duration(days: 6 - i)));
      return existing.firstWhere(
        (d) => d.date == date,
        orElse: () =>
            DailyStat(date: date, snoreCount: 0, totalDurationS: 0),
      );
    });
  }

  String _formatAvgDuration(double seconds) {
    if (seconds < 60) return '${seconds.toStringAsFixed(0)}s';
    return '${(seconds / 60).toStringAsFixed(1)}m';
  }
}

class _StatChip extends StatelessWidget {
  final String label;
  final String value;

  const _StatChip({required this.label, required this.value});

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          value,
          style: Theme.of(context).textTheme.titleSmall?.copyWith(
                color: AppColors.textPrimary,
                fontWeight: FontWeight.w700,
              ),
        ),
        Text(
          label,
          style: Theme.of(context)
              .textTheme
              .labelSmall
              ?.copyWith(color: AppColors.textSecondary),
        ),
      ],
    );
  }
}

class _TrendBadge extends StatelessWidget {
  final String trend;

  const _TrendBadge({required this.trend});

  @override
  Widget build(BuildContext context) {
    final (icon, color, label) = switch (trend) {
      'improving' => (Icons.trending_down, AppColors.success, 'Improving'),
      'worsening' => (Icons.trending_up, AppColors.error, 'Worsening'),
      _ => (Icons.trending_flat, AppColors.textSecondary, 'Stable'),
    };

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
      decoration: BoxDecoration(
        color: color.withAlpha(30),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 12, color: color),
          const SizedBox(width: 4),
          Text(label,
              style: TextStyle(
                  color: color,
                  fontSize: 11,
                  fontWeight: FontWeight.w600)),
        ],
      ),
    );
  }
}
