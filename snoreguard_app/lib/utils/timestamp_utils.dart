class TimestampUtils {
  /// Firmware fallback epoch base: 1700000000 (~ Nov 2023).
  static const int _fallbackBase = 1700000000;

  /// Window size: 1 year of seconds.
  static const int _fallbackWindow = 365 * 24 * 3600;

  /// Returns true when [epochSeconds] is in the firmware fallback range
  /// (~Nov 2023 to ~Nov 2024). Such timestamps are based on device uptime,
  /// not a real wall-clock time.
  static bool isFallbackTimestamp(int epochSeconds) {
    return epochSeconds >= _fallbackBase &&
        epochSeconds < _fallbackBase + _fallbackWindow;
  }

  /// Convert epoch seconds to DateTime.
  static DateTime toDateTime(int epochSeconds) =>
      DateTime.fromMillisecondsSinceEpoch(epochSeconds * 1000);

  /// Current Unix epoch in seconds.
  static int nowSeconds() => DateTime.now().millisecondsSinceEpoch ~/ 1000;
}
