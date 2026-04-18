import 'package:intl/intl.dart';

class SessionDateUtils {
  static const int _sessionBoundaryHour = 6;

  static final _dateFormat = DateFormat('yyyy-MM-dd');

  /// Compute session date for a given event DateTime.
  /// Events between midnight (00:00) and 06:00 local time belong
  /// to the previous calendar day's session.
  static String computeSessionDate(DateTime eventTime) {
    final local = eventTime.toLocal();
    final effective = local.hour < _sessionBoundaryHour
        ? local.subtract(const Duration(days: 1))
        : local;
    return _dateFormat.format(effective);
  }

  /// Compute session date from a Unix epoch (seconds).
  static String computeSessionDateFromEpoch(int epochSeconds) {
    final dt = DateTime.fromMillisecondsSinceEpoch(epochSeconds * 1000);
    return computeSessionDate(dt);
  }

  /// Return today's session date string (yyyy-MM-dd).
  static String today() => _dateFormat.format(DateTime.now());

  /// Return a date string N days ago.
  static String daysAgo(int n) =>
      _dateFormat.format(DateTime.now().subtract(Duration(days: n)));

  /// Format a session date string for display, e.g. "Night of Apr 14".
  static String formatDisplay(String sessionDate) {
    final dt = DateTime.parse(sessionDate);
    return 'Night of ${DateFormat('MMM d').format(dt)}';
  }

  /// Format a date for the session card header, e.g. "Mon\nApr 14".
  static String formatShort(String sessionDate) {
    final dt = DateTime.parse(sessionDate);
    return '${DateFormat('EEE').format(dt)}\n${DateFormat('MMM d').format(dt)}';
  }
}
