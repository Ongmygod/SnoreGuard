import 'package:flutter/material.dart';
import '../models/sleep_session.dart';
import '../services/database/snore_event_dao.dart';

class SessionProvider extends ChangeNotifier {
  final SnoreEventDao _dao = SnoreEventDao();

  List<SleepSession> _recentSessions = [];
  SleepSession? _selectedSession;
  WeeklySummary? _weeklySummary;
  bool _isLoading = false;
  String? _errorMessage;

  List<SleepSession> get recentSessions => _recentSessions;
  SleepSession? get selectedSession => _selectedSession;
  WeeklySummary? get weeklySummary => _weeklySummary;
  bool get isLoading => _isLoading;
  String? get errorMessage => _errorMessage;
  bool get hasSessions => _recentSessions.isNotEmpty;

  /// Purge old records and reload the 7-day session list.
  Future<void> loadRecentSessions() async {
    _isLoading = true;
    _errorMessage = null;
    notifyListeners();

    try {
      // Remove records older than 7 days
      await _dao.purgeOldEvents(7);

      // Load sessions from last 7 days
      _recentSessions = await _dao.getRecentSessions(7);

      // Build weekly summary
      _weeklySummary = WeeklySummary.fromSessions(_recentSessions);

      // Refresh selected session if it is still in the list
      if (_selectedSession != null) {
        final updated = _recentSessions
            .where((s) => s.sessionDate == _selectedSession!.sessionDate)
            .firstOrNull;
        _selectedSession = updated;
      }
    } catch (e) {
      _errorMessage = 'Failed to load sessions: $e';
    } finally {
      _isLoading = false;
      notifyListeners();
    }
  }

  /// Load and select a specific session for the detail view.
  Future<void> selectSession(String sessionDate) async {
    // Try to find from already-loaded sessions first
    final existing = _recentSessions
        .where((s) => s.sessionDate == sessionDate)
        .firstOrNull;

    if (existing != null) {
      _selectedSession = existing;
      notifyListeners();
      return;
    }

    // Load from DB if not already in memory
    try {
      final events = await _dao.getEventsBySession(sessionDate);
      _selectedSession = SleepSession(sessionDate: sessionDate, events: events);
    } catch (e) {
      _errorMessage = 'Failed to load session: $e';
    }
    notifyListeners();
  }

  void clearSelectedSession() {
    _selectedSession = null;
    notifyListeners();
  }

  void clearError() {
    _errorMessage = null;
    notifyListeners();
  }
}
