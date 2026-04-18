import 'package:intl/intl.dart';

class SnoreEvent {
  final int? id;
  final String sessionDate;
  final int eventTimestamp;
  final int durationS;
  final int hapticSuccess;
  final int hapticFlag;
  final bool isFallbackTimestamp;

  const SnoreEvent({
    this.id,
    required this.sessionDate,
    required this.eventTimestamp,
    required this.durationS,
    required this.hapticSuccess,
    required this.hapticFlag,
    required this.isFallbackTimestamp,
  });

  factory SnoreEvent.fromMap(Map<String, dynamic> map) {
    return SnoreEvent(
      id: map['id'] as int?,
      sessionDate: map['session_date'] as String,
      eventTimestamp: map['event_timestamp'] as int,
      durationS: map['duration_s'] as int,
      hapticSuccess: map['haptic_success'] as int,
      hapticFlag: map['haptic_flag'] as int,
      isFallbackTimestamp: (map['is_fallback_timestamp'] as int) == 1,
    );
  }

  Map<String, dynamic> toMap() {
    return {
      if (id != null) 'id': id,
      'session_date': sessionDate,
      'event_timestamp': eventTimestamp,
      'duration_s': durationS,
      'haptic_success': hapticSuccess,
      'haptic_flag': hapticFlag,
      'is_fallback_timestamp': isFallbackTimestamp ? 1 : 0,
    };
  }

  /// Local DateTime of this event.
  DateTime get eventDateTime =>
      DateTime.fromMillisecondsSinceEpoch(eventTimestamp * 1000).toLocal();

  /// Formatted local time string (HH:mm).
  String get formattedTime => DateFormat('HH:mm').format(eventDateTime);

  /// Formatted local time + seconds (HH:mm:ss).
  String get formattedTimeFull => DateFormat('HH:mm:ss').format(eventDateTime);

  /// Whether haptic intervention was triggered for this event.
  bool get wasHapticTriggered => hapticFlag == 1;

  /// Whether the haptic intervention succeeded (only meaningful if hapticFlag == 1).
  bool get wasHapticSuccessful => hapticFlag == 1 && hapticSuccess == 1;
}
