import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

class SettingsProvider extends ChangeNotifier {
  static const _keyOnboarding = 'onboarding_complete';

  bool _onboardingComplete = false;
  bool _isInitialized = false;

  bool get onboardingComplete => _onboardingComplete;
  bool get isInitialized => _isInitialized;

  /// Load settings from SharedPreferences.
  Future<void> loadSettings() async {
    final prefs = await SharedPreferences.getInstance();
    _onboardingComplete = prefs.getBool(_keyOnboarding) ?? false;
    _isInitialized = true;
    notifyListeners();
  }

  /// Mark onboarding as complete and persist.
  Future<void> completeOnboarding() async {
    _onboardingComplete = true;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool(_keyOnboarding, true);
    notifyListeners();
  }

  /// Reset onboarding (used in debug/test scenarios).
  Future<void> resetOnboarding() async {
    _onboardingComplete = false;
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_keyOnboarding);
    notifyListeners();
  }
}
