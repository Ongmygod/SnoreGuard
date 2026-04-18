import 'snore_event.dart';
import '../utils/session_date_utils.dart';

class SleepSession {
  final String sessionDate;
  final List<SnoreEvent> events;

  // Computed properties
  final int totalSnoreCount;
  final int totalDurationS;
  final int hapticTriggerCount;
  final int hapticSuccessCount;
  final bool hasFallbackTimestamps;

  SleepSession({
    required this.sessionDate,
    required this.events,
  })  : totalSnoreCount = events.length,
        totalDurationS = events.fold(0, (sum, e) => sum + e.durationS),
        hapticTriggerCount = events.where((e) => e.hapticFlag == 1).length,
        hapticSuccessCount =
            events.where((e) => e.hapticFlag == 1 && e.hapticSuccess == 1).length,
        hasFallbackTimestamps = events.any((e) => e.isFallbackTimestamp);

  /// Haptic intervention success rate (0.0–1.0), or null if no interventions.
  double? get hapticSuccessRate => hapticTriggerCount > 0
      ? hapticSuccessCount / hapticTriggerCount
      : null;

  /// Human-readable date header, e.g. "Night of Apr 14".
  String get displayTitle => SessionDateUtils.formatDisplay(sessionDate);

  /// Short date for the session card, e.g. "Mon\nApr 14".
  String get shortDate => SessionDateUtils.formatShort(sessionDate);

  /// Total duration formatted as "Xm Ys".
  String get formattedDuration {
    if (totalDurationS < 60) return '${totalDurationS}s';
    final m = totalDurationS ~/ 60;
    final s = totalDurationS % 60;
    return s == 0 ? '${m}m' : '${m}m ${s}s';
  }
}

class WeeklySummary {
  final List<DailyStat> dailyStats;
  final double avgSnoreCount;
  final double avgDurationS;
  final double? overallHapticSuccessRate;
  final String trend;

  WeeklySummary({
    required this.dailyStats,
    required this.avgSnoreCount,
    required this.avgDurationS,
    required this.overallHapticSuccessRate,
    required this.trend,
  });

  factory WeeklySummary.fromSessions(List<SleepSession> sessions) {
    if (sessions.isEmpty) {
      return WeeklySummary(
        dailyStats: [],
        avgSnoreCount: 0,
        avgDurationS: 0,
        overallHapticSuccessRate: null,
        trend: 'stable',
      );
    }

    final dailyStats = sessions
        .map((s) => DailyStat(
              date: s.sessionDate,
              snoreCount: s.totalSnoreCount,
              totalDurationS: s.totalDurationS,
            ))
        .toList();

    final avgSnore =
        dailyStats.map((d) => d.snoreCount).reduce((a, b) => a + b) /
            dailyStats.length;

    final avgDur =
        dailyStats.map((d) => d.totalDurationS).reduce((a, b) => a + b) /
            dailyStats.length;

    // Calculate overall haptic success rate
    final totalTriggers =
        sessions.fold(0, (sum, s) => sum + s.hapticTriggerCount);
    final totalSuccesses =
        sessions.fold(0, (sum, s) => sum + s.hapticSuccessCount);
    final overallRate =
        totalTriggers > 0 ? totalSuccesses / totalTriggers : null;

    // Trend: compare first half vs second half of sessions (ordered desc = newest first)
    String trend = 'stable';
    if (dailyStats.length >= 4) {
      final half = dailyStats.length ~/ 2;
      // Newest sessions are at index 0 (descending order)
      final recentAvg = dailyStats
              .take(half)
              .map((d) => d.snoreCount)
              .reduce((a, b) => a + b) /
          half;
      final olderAvg = dailyStats
              .skip(half)
              .map((d) => d.snoreCount)
              .reduce((a, b) => a + b) /
          (dailyStats.length - half);

      if (recentAvg < olderAvg * 0.8) {
        trend = 'improving';
      } else if (recentAvg > olderAvg * 1.2) {
        trend = 'worsening';
      }
    }

    return WeeklySummary(
      dailyStats: dailyStats,
      avgSnoreCount: avgSnore,
      avgDurationS: avgDur,
      overallHapticSuccessRate: overallRate,
      trend: trend,
    );
  }
}

class DailyStat {
  final String date;
  final int snoreCount;
  final int totalDurationS;

  const DailyStat({
    required this.date,
    required this.snoreCount,
    required this.totalDurationS,
  });
}
