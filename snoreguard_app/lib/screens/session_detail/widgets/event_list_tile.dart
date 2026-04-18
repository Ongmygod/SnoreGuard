import 'package:flutter/material.dart';
import '../../../app/theme.dart';
import '../../../models/snore_event.dart';

class EventListTile extends StatelessWidget {
  final SnoreEvent event;
  final int index;

  const EventListTile({
    super.key,
    required this.event,
    required this.index,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 3),
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
      decoration: BoxDecoration(
        color: AppColors.cardBackground,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Row(
        children: [
          // Index
          SizedBox(
            width: 28,
            child: Text(
              '${index + 1}',
              style: Theme.of(context).textTheme.labelSmall?.copyWith(
                    color: AppColors.textSecondary,
                  ),
            ),
          ),
          // Time
          SizedBox(
            width: 60,
            child: Text(
              event.isFallbackTimestamp ? '~${event.formattedTime}' : event.formattedTime,
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: AppColors.textPrimary,
                    fontFamily: 'monospace',
                  ),
            ),
          ),
          // Duration
          SizedBox(
            width: 50,
            child: Text(
              '${event.durationS}s',
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: AppColors.neutral,
                    fontWeight: FontWeight.w600,
                  ),
            ),
          ),
          const Spacer(),
          // Haptic badge
          if (event.wasHapticTriggered)
            Container(
              padding:
                  const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
              decoration: BoxDecoration(
                color: event.wasHapticSuccessful
                    ? AppColors.success.withAlpha(30)
                    : AppColors.error.withAlpha(30),
                borderRadius: BorderRadius.circular(6),
              ),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(
                    event.wasHapticSuccessful
                        ? Icons.check_circle_outline
                        : Icons.cancel_outlined,
                    size: 12,
                    color: event.wasHapticSuccessful
                        ? AppColors.success
                        : AppColors.error,
                  ),
                  const SizedBox(width: 4),
                  Text(
                    event.wasHapticSuccessful ? 'Success' : 'No effect',
                    style: TextStyle(
                      fontSize: 11,
                      color: event.wasHapticSuccessful
                          ? AppColors.success
                          : AppColors.error,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ],
              ),
            )
          else
            const SizedBox(width: 70),
        ],
      ),
    );
  }
}
