import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../app/theme.dart';
import '../../app/routes.dart';
import '../../providers/ble_provider.dart';
import '../../providers/settings_provider.dart';
import '../../services/ble/ble_service.dart';
import '../../utils/permission_utils.dart';

class OnboardingScreen extends StatefulWidget {
  const OnboardingScreen({super.key});

  @override
  State<OnboardingScreen> createState() => _OnboardingScreenState();
}

class _OnboardingScreenState extends State<OnboardingScreen> {
  final _pageController = PageController();
  int _currentPage = 0;

  @override
  void dispose() {
    _pageController.dispose();
    super.dispose();
  }

  void _nextPage() {
    if (_currentPage < 2) {
      _pageController.animateToPage(
        _currentPage + 1,
        duration: const Duration(milliseconds: 300),
        curve: Curves.easeInOut,
      );
    }
  }

  Future<void> _finish(BuildContext context) async {
    await context.read<SettingsProvider>().completeOnboarding();
    if (context.mounted) {
      Navigator.pushReplacementNamed(context, Routes.home);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: Column(
          children: [
            // Skip button
            Align(
              alignment: Alignment.topRight,
              child: TextButton(
                onPressed: () => _finish(context),
                child: const Text('Skip'),
              ),
            ),
            // Pages
            Expanded(
              child: PageView(
                controller: _pageController,
                onPageChanged: (i) => setState(() => _currentPage = i),
                children: [
                  _WelcomePage(onNext: _nextPage),
                  _PermissionsPage(onNext: _nextPage),
                  _PairPage(onFinish: () => _finish(context)),
                ],
              ),
            ),
            // Page indicator
            Padding(
              padding: const EdgeInsets.only(bottom: 32),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: List.generate(
                  3,
                  (i) => AnimatedContainer(
                    duration: const Duration(milliseconds: 200),
                    margin: const EdgeInsets.symmetric(horizontal: 4),
                    width: _currentPage == i ? 20 : 8,
                    height: 8,
                    decoration: BoxDecoration(
                      color: _currentPage == i
                          ? AppColors.primary
                          : AppColors.divider,
                      borderRadius: BorderRadius.circular(4),
                    ),
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// ---------- Page 1: Welcome ----------

class _WelcomePage extends StatelessWidget {
  final VoidCallback onNext;

  const _WelcomePage({required this.onNext});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(40),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Icon(Icons.bedtime, size: 96, color: AppColors.primary),
          const SizedBox(height: 32),
          Text(
            'Welcome to SnoreGuard',
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                  color: AppColors.textPrimary,
                  fontWeight: FontWeight.w700,
                ),
          ),
          const SizedBox(height: 16),
          Text(
            'Your smart sleep companion.\n\n'
            'SnoreGuard monitors your snoring with edge AI, applies gentle haptic nudges for posture correction, '
            'and gives you a 7-day sleep history — all without internet.',
            textAlign: TextAlign.center,
            style: Theme.of(context)
                .textTheme
                .bodyMedium
                ?.copyWith(color: AppColors.textSecondary, height: 1.6),
          ),
          const SizedBox(height: 48),
          ElevatedButton(
            onPressed: onNext,
            style: ElevatedButton.styleFrom(
              minimumSize: const Size(double.infinity, 48),
            ),
            child: const Text('Get Started'),
          ),
        ],
      ),
    );
  }
}

// ---------- Page 2: Permissions ----------

class _PermissionsPage extends StatefulWidget {
  final VoidCallback onNext;

  const _PermissionsPage({required this.onNext});

  @override
  State<_PermissionsPage> createState() => _PermissionsPageState();
}

class _PermissionsPageState extends State<_PermissionsPage> {
  bool _granted = false;
  bool _requesting = false;

  Future<void> _requestPermissions() async {
    setState(() => _requesting = true);
    final granted = await PermissionUtils.requestBlePermissions();
    if (mounted) {
      setState(() {
        _granted = granted;
        _requesting = false;
      });
    }

    if (granted) {
      await Future.delayed(const Duration(milliseconds: 600));
      widget.onNext();
    }
  }

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(40),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(
            _granted ? Icons.check_circle : Icons.bluetooth_outlined,
            size: 96,
            color: _granted ? AppColors.success : AppColors.primary,
          ),
          const SizedBox(height: 32),
          Text(
            'Bluetooth Access',
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                  color: AppColors.textPrimary,
                  fontWeight: FontWeight.w700,
                ),
          ),
          const SizedBox(height: 16),
          Text(
            'SnoreGuard needs Bluetooth access to communicate with your device and sync your sleep data.',
            textAlign: TextAlign.center,
            style: Theme.of(context)
                .textTheme
                .bodyMedium
                ?.copyWith(color: AppColors.textSecondary, height: 1.6),
          ),
          const SizedBox(height: 48),
          ElevatedButton(
            onPressed: _requesting ? null : _requestPermissions,
            style: ElevatedButton.styleFrom(
              minimumSize: const Size(double.infinity, 48),
              backgroundColor:
                  _granted ? AppColors.success : AppColors.primary,
            ),
            child: _requesting
                ? const SizedBox(
                    width: 20,
                    height: 20,
                    child: CircularProgressIndicator(
                        strokeWidth: 2, color: Colors.white),
                  )
                : Text(_granted ? 'Permissions Granted' : 'Grant Access'),
          ),
          if (!_granted && !_requesting) ...[
            const SizedBox(height: 12),
            TextButton(
              onPressed: () => PermissionUtils.openSettings(),
              child: const Text('Open App Settings'),
            ),
          ],
        ],
      ),
    );
  }
}

// ---------- Page 3: Pair Device ----------

class _PairPage extends StatefulWidget {
  final VoidCallback onFinish;

  const _PairPage({required this.onFinish});

  @override
  State<_PairPage> createState() => _PairPageState();
}

class _PairPageState extends State<_PairPage> {
  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleProvider>();

    return Padding(
      padding: const EdgeInsets.all(32),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(
            ble.isConnected
                ? Icons.bluetooth_connected
                : Icons.bluetooth_outlined,
            size: 96,
            color:
                ble.isConnected ? AppColors.success : AppColors.primary,
          ),
          const SizedBox(height: 32),
          Text(
            ble.isConnected ? 'Device Connected!' : 'Pair Your Device',
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                  color: AppColors.textPrimary,
                  fontWeight: FontWeight.w700,
                ),
          ),
          const SizedBox(height: 16),
          Text(
            ble.isConnected
                ? 'SnoreGuard is connected and ready.\nYou can start using the app.'
                : 'Power on your SnoreGuard device and tap Scan to find it.',
            textAlign: TextAlign.center,
            style: Theme.of(context)
                .textTheme
                .bodyMedium
                ?.copyWith(color: AppColors.textSecondary, height: 1.6),
          ),
          const SizedBox(height: 32),
          // Scan results
          if (ble.scanResults.isNotEmpty && !ble.isConnected)
            ...ble.scanResults.map(
              (r) => Card(
                margin: const EdgeInsets.only(bottom: 8),
                child: ListTile(
                  leading: const Icon(Icons.bluetooth,
                      color: AppColors.primary),
                  title: Text(
                    r.device.platformName.isNotEmpty
                        ? r.device.platformName
                        : 'SnoreGuard',
                    style:
                        const TextStyle(color: AppColors.textPrimary),
                  ),
                  trailing: ElevatedButton(
                    onPressed: () => ble.connectToDevice(r.device),
                    style: ElevatedButton.styleFrom(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 14, vertical: 8),
                    ),
                    child: const Text('Connect'),
                  ),
                ),
              ),
            ),
          // Error
          if (ble.errorMessage != null) ...[
            const SizedBox(height: 8),
            Text(
              ble.errorMessage!,
              textAlign: TextAlign.center,
              style: const TextStyle(color: AppColors.error, fontSize: 12),
            ),
          ],
          const SizedBox(height: 24),
          if (!ble.isConnected)
            ElevatedButton.icon(
              onPressed: ble.isScanning ||
                      ble.connectionState ==
                          BleConnectionState.connecting
                  ? null
                  : () => ble.startScan(),
              icon: ble.isScanning
                  ? const SizedBox(
                      width: 16,
                      height: 16,
                      child: CircularProgressIndicator(
                          strokeWidth: 2, color: Colors.white),
                    )
                  : const Icon(Icons.bluetooth_searching),
              label: Text(ble.isScanning ? 'Scanning...' : 'Scan'),
              style: ElevatedButton.styleFrom(
                minimumSize: const Size(double.infinity, 48),
              ),
            ),
          const SizedBox(height: 12),
          SizedBox(
            width: double.infinity,
            child: ElevatedButton(
              onPressed: widget.onFinish,
              style: ElevatedButton.styleFrom(
                minimumSize: const Size(double.infinity, 48),
                backgroundColor: ble.isConnected
                    ? AppColors.success
                    : AppColors.primary,
              ),
              child: Text(ble.isConnected ? 'Start Using SnoreGuard' : 'Skip for Now'),
            ),
          ),
        ],
      ),
    );
  }
}
