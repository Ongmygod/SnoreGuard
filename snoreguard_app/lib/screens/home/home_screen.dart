import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../app/routes.dart';
import '../../app/theme.dart';
import '../../providers/session_provider.dart';
import '../../providers/ble_provider.dart';
import 'widgets/empty_state_widget.dart';
import 'widgets/session_card.dart';
import 'widgets/weekly_summary_card.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      context.read<SessionProvider>().loadRecentSessions();
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.bedtime, color: AppColors.primary, size: 22),
            const SizedBox(width: 8),
            const Text('SnoreGuard'),
          ],
        ),
        centerTitle: false,
        actions: [
          // BLE connection indicator
          Consumer<BleProvider>(
            builder: (context, ble, child) => IconButton(
              icon: Icon(
                ble.isConnected
                    ? Icons.bluetooth_connected
                    : Icons.bluetooth_outlined,
                color: ble.isConnected
                    ? AppColors.success
                    : AppColors.textSecondary,
              ),
              tooltip: ble.isConnected
                  ? 'Connected: ${ble.connectedDeviceName ?? 'SnoreGuard'}'
                  : 'Not connected',
              onPressed: () =>
                  Navigator.pushNamed(context, Routes.settings),
            ),
          ),
          IconButton(
            icon: const Icon(Icons.settings_outlined),
            tooltip: 'Settings',
            onPressed: () => Navigator.pushNamed(context, Routes.settings),
          ),
        ],
      ),
      body: Consumer<SessionProvider>(
        builder: (context, sessionProvider, _) {
          if (sessionProvider.isLoading) {
            return const Center(child: CircularProgressIndicator());
          }

          return RefreshIndicator(
            onRefresh: () => sessionProvider.loadRecentSessions(),
            child: sessionProvider.hasSessions
                ? CustomScrollView(
                    slivers: [
                      const SliverToBoxAdapter(child: WeeklySummaryCard()),
                      const SliverToBoxAdapter(
                        child: Padding(
                          padding: EdgeInsets.fromLTRB(16, 16, 16, 4),
                          child: _SectionHeader('Recent Sessions'),
                        ),
                      ),
                      SliverList(
                        delegate: SliverChildBuilderDelegate(
                          (ctx, i) {
                            final session =
                                sessionProvider.recentSessions[i];
                            return SessionCard(
                              session: session,
                              onTap: () async {
                                await sessionProvider
                                    .selectSession(session.sessionDate);
                                if (context.mounted) {
                                  Navigator.pushNamed(
                                    context,
                                    Routes.sessionDetail,
                                    arguments: session,
                                  );
                                }
                              },
                            );
                          },
                          childCount:
                              sessionProvider.recentSessions.length,
                        ),
                      ),
                      const SliverToBoxAdapter(
                          child: SizedBox(height: 24)),
                    ],
                  )
                : ListView(
                    children: const [
                      SizedBox(height: 80),
                      EmptyStateWidget(),
                    ],
                  ),
          );
        },
      ),
    );
  }
}

class _SectionHeader extends StatelessWidget {
  final String title;
  const _SectionHeader(this.title);

  @override
  Widget build(BuildContext context) {
    return Text(
      title,
      style: Theme.of(context).textTheme.labelMedium?.copyWith(
            color: AppColors.textSecondary,
            letterSpacing: 0.8,
          ),
    );
  }
}
