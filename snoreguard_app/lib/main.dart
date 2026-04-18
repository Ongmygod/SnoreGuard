import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'app/app.dart';
import 'providers/ble_provider.dart';
import 'providers/session_provider.dart';
import 'providers/settings_provider.dart';
import 'services/database/database_helper.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // Open the SQLite database early to avoid first-query latency
  await DatabaseHelper.database;

  runApp(
    MultiProvider(
      providers: [
        ChangeNotifierProvider(
          create: (_) => SettingsProvider()..loadSettings(),
        ),
        ChangeNotifierProvider(
          create: (_) => SessionProvider()..loadRecentSessions(),
        ),
        ChangeNotifierProvider(
          create: (_) => BleProvider()..initialize(),
        ),
      ],
      child: const SnoreGuardApp(),
    ),
  );
}
