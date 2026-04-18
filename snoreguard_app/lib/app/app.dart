import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../app/theme.dart';
import '../app/routes.dart';
import '../models/sleep_session.dart';
import '../providers/settings_provider.dart';
import '../screens/onboarding/onboarding_screen.dart';
import '../screens/home/home_screen.dart';
import '../screens/session_detail/session_detail_screen.dart';
import '../screens/settings/settings_screen.dart';

class SnoreGuardApp extends StatelessWidget {
  const SnoreGuardApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'SnoreGuard',
      debugShowCheckedModeBanner: false,
      theme: SnoreGuardTheme.darkTheme,
      // Home is determined by onboarding state via Consumer
      home: Consumer<SettingsProvider>(
        builder: (context, settings, _) {
          if (!settings.isInitialized) {
            // Brief loading splash while SharedPreferences loads
            return const Scaffold(
              body: Center(child: CircularProgressIndicator()),
            );
          }
          if (!settings.onboardingComplete) {
            return const OnboardingScreen();
          }
          return const HomeScreen();
        },
      ),
      // Named routes for push navigation
      routes: {
        Routes.onboarding: (_) => const OnboardingScreen(),
        Routes.home: (_) => const HomeScreen(),
        Routes.settings: (_) => const SettingsScreen(),
      },
      onGenerateRoute: (routeSettings) {
        if (routeSettings.name == Routes.sessionDetail) {
          final session = routeSettings.arguments as SleepSession;
          return MaterialPageRoute(
            builder: (_) => SessionDetailScreen(session: session),
          );
        }
        return null;
      },
    );
  }
}
