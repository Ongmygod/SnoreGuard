import 'package:flutter/material.dart';
import 'widgets/connection_status_bar.dart';
import 'widgets/device_scan_section.dart';
import 'widgets/haptic_enable_toggle.dart';
import 'widgets/haptic_intensity_slider.dart';
import 'widgets/sync_section.dart';
import 'widgets/about_section.dart';

class SettingsScreen extends StatelessWidget {
  const SettingsScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Device Settings'),
        leading: const BackButton(),
      ),
      body: Column(
        children: [
          // Persistent connection banner at top
          const ConnectionStatusBar(),
          // Scrollable content
          Expanded(
            child: SingleChildScrollView(
              padding: const EdgeInsets.fromLTRB(16, 20, 16, 40),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  // Device pairing section
                  const DeviceScanSection(),
                  const SizedBox(height: 28),
                  const Divider(),
                  const SizedBox(height: 28),

                  // Morning sync section
                  const SyncSection(),
                  const SizedBox(height: 28),
                  const Divider(),
                  const SizedBox(height: 28),

                  // Haptic motor enable/disable
                  const HapticEnableToggle(),
                  const SizedBox(height: 28),
                  const Divider(),
                  const SizedBox(height: 28),

                  // Haptic intensity section
                  const HapticIntensitySlider(),
                  const SizedBox(height: 28),
                  const Divider(),
                  const SizedBox(height: 28),

                  // About section
                  const AboutSection(),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
