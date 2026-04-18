import 'package:sqflite/sqflite.dart';
import 'package:intl/intl.dart';
import '../../models/snore_event.dart';
import '../../models/sleep_session.dart';
import 'database_helper.dart';

class SnoreEventDao {
  static final _dateFormat = DateFormat('yyyy-MM-dd');

  /// Insert a single event, ignoring duplicates.
  Future<int> insertEvent(SnoreEvent event) async {
    final db = await DatabaseHelper.database;
    return db.insert(
      'snore_events',
      event.toMap(),
      conflictAlgorithm: ConflictAlgorithm.ignore,
    );
  }

  /// Insert a batch of events in a single transaction.
  /// Returns the number of newly inserted (non-duplicate) events.
  Future<int> insertEvents(List<SnoreEvent> events) async {
    if (events.isEmpty) return 0;
    final db = await DatabaseHelper.database;
    int inserted = 0;
    await db.transaction((txn) async {
      for (final event in events) {
        final rowId = await txn.insert(
          'snore_events',
          event.toMap(),
          conflictAlgorithm: ConflictAlgorithm.ignore,
        );
        if (rowId > 0) inserted++;
      }
    });
    return inserted;
  }

  /// Return all events for [sessionDate] ordered by timestamp ascending.
  Future<List<SnoreEvent>> getEventsBySession(String sessionDate) async {
    final db = await DatabaseHelper.database;
    final rows = await db.query(
      'snore_events',
      where: 'session_date = ?',
      whereArgs: [sessionDate],
      orderBy: 'event_timestamp ASC',
    );
    return rows.map(SnoreEvent.fromMap).toList();
  }

  /// Return distinct session dates from the past [days] days, descending.
  Future<List<String>> getRecentSessionDates(int days) async {
    final db = await DatabaseHelper.database;
    final cutoff =
        _dateFormat.format(DateTime.now().subtract(Duration(days: days)));
    final result = await db.rawQuery(
      'SELECT DISTINCT session_date FROM snore_events '
      'WHERE session_date >= ? '
      'ORDER BY session_date DESC',
      [cutoff],
    );
    return result.map((r) => r['session_date'] as String).toList();
  }

  /// Return aggregate stats per session for the past [days] days.
  Future<List<Map<String, dynamic>>> getWeeklyStats(int days) async {
    final db = await DatabaseHelper.database;
    final cutoff =
        _dateFormat.format(DateTime.now().subtract(Duration(days: days)));
    return db.rawQuery('''
      SELECT
        session_date,
        COUNT(*) AS snore_count,
        SUM(duration_s) AS total_duration_s,
        SUM(CASE WHEN haptic_flag = 1 THEN 1 ELSE 0 END) AS haptic_trigger_count,
        SUM(CASE WHEN haptic_flag = 1 AND haptic_success = 1 THEN 1 ELSE 0 END) AS haptic_success_count
      FROM snore_events
      WHERE session_date >= ?
      GROUP BY session_date
      ORDER BY session_date DESC
    ''', [cutoff]);
  }

  /// Delete all events older than [days] days.
  /// Returns the number of deleted rows.
  Future<int> purgeOldEvents(int days) async {
    final db = await DatabaseHelper.database;
    final cutoff =
        _dateFormat.format(DateTime.now().subtract(Duration(days: days)));
    return db.delete(
      'snore_events',
      where: 'session_date < ?',
      whereArgs: [cutoff],
    );
  }

  /// Build a list of [SleepSession] from recent session dates.
  Future<List<SleepSession>> getRecentSessions(int days) async {
    final dates = await getRecentSessionDates(days);
    final sessions = <SleepSession>[];
    for (final date in dates) {
      final events = await getEventsBySession(date);
      sessions.add(SleepSession(sessionDate: date, events: events));
    }
    return sessions;
  }
}
