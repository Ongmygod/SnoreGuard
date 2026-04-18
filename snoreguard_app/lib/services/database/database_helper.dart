import 'package:sqflite/sqflite.dart';
import 'package:path/path.dart';

class DatabaseHelper {
  static const String _databaseName = 'snoreguard.db';
  static const int _databaseVersion = 1;

  static Database? _database;

  /// Singleton database instance.
  static Future<Database> get database async {
    _database ??= await _initDatabase();
    return _database!;
  }

  static Future<Database> _initDatabase() async {
    final dbPath = await getDatabasesPath();
    final path = join(dbPath, _databaseName);
    return openDatabase(
      path,
      version: _databaseVersion,
      onCreate: _onCreate,
    );
  }

  static Future<void> _onCreate(Database db, int version) async {
    await db.execute('''
      CREATE TABLE snore_events (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        session_date TEXT NOT NULL,
        event_timestamp INTEGER NOT NULL,
        duration_s INTEGER NOT NULL,
        haptic_success INTEGER NOT NULL DEFAULT 0,
        haptic_flag INTEGER NOT NULL DEFAULT 0,
        is_fallback_timestamp INTEGER NOT NULL DEFAULT 0
      )
    ''');

    // Index for session-date queries
    await db.execute(
        'CREATE INDEX idx_session_date ON snore_events(session_date)');

    // Unique index prevents duplicate events when Morning Sync is run twice
    await db.execute(
        'CREATE UNIQUE INDEX idx_event_dedup ON snore_events(event_timestamp, duration_s, haptic_flag)');
  }

  /// Close and reset the database (used in tests).
  static Future<void> close() async {
    await _database?.close();
    _database = null;
  }
}
