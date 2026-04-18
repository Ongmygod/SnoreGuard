import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import '../../../app/theme.dart';
import '../../../models/snore_event.dart';

class EventTimelineChart extends StatelessWidget {
  final List<SnoreEvent> events;

  const EventTimelineChart({super.key, required this.events});

  @override
  Widget build(BuildContext context) {
    if (events.isEmpty) {
      return const SizedBox(
        height: 180,
        child: Center(
          child: Text(
            'No events to display',
            style: TextStyle(color: AppColors.textSecondary),
          ),
        ),
      );
    }

    final start = events
        .map((e) => e.eventTimestamp)
        .reduce((a, b) => a < b ? a : b);
    final end = events
        .map((e) => e.eventTimestamp + e.durationS)
        .reduce((a, b) => a > b ? a : b);
    final span = end - start;
    final pad = (span * 0.02).round().clamp(1, 1 << 30);
    final domainStart = start - pad;
    final domainEnd = end + pad;

    return Container(
      height: 200,
      margin: const EdgeInsets.symmetric(horizontal: 8),
      padding: const EdgeInsets.fromLTRB(16, 16, 16, 12),
      decoration: BoxDecoration(
        color: AppColors.surface,
        borderRadius: BorderRadius.circular(16),
      ),
      child: CustomPaint(
        painter: _TimelinePainter(
          events: events,
          domainStart: domainStart,
          domainEnd: domainEnd,
        ),
        size: Size.infinite,
      ),
    );
  }
}

class _TimelinePainter extends CustomPainter {
  final List<SnoreEvent> events;
  final int domainStart;
  final int domainEnd;

  _TimelinePainter({
    required this.events,
    required this.domainStart,
    required this.domainEnd,
  });

  static const double _snoreRowHeight = 56;
  static const double _pulseRowHeight = 40;
  static const double _rowGap = 12;
  static const double _axisGap = 8;
  static const double _minBarWidth = 4;
  static const double _pulseBarWidth = 3;

  @override
  void paint(Canvas canvas, Size size) {
    final width = size.width;
    final domain = (domainEnd - domainStart).toDouble();
    if (domain <= 0) return;

    double xFor(int t) =>
        ((t - domainStart) / domain).clamp(0.0, 1.0) * width;

    const snoreTop = 0.0;
    const snoreBottom = snoreTop + _snoreRowHeight;
    const pulseTop = snoreBottom + _rowGap;
    const pulseBottom = pulseTop + _pulseRowHeight;
    const axisY = pulseBottom + _axisGap;

    _drawRowBackground(
      canvas,
      const Rect.fromLTRB(0, snoreTop, 1, snoreBottom),
      width,
    );
    _drawRowBackground(
      canvas,
      const Rect.fromLTRB(0, pulseTop, 1, pulseBottom),
      width,
    );

    final snorePaint = Paint()..color = AppColors.timelineSnore;
    const snoreBarHeight = _snoreRowHeight * 0.72;
    const snoreBarY = snoreTop + (_snoreRowHeight - snoreBarHeight) / 2;
    const snoreRadius = Radius.circular(snoreBarHeight / 2);

    for (final e in events) {
      final x1 = xFor(e.eventTimestamp);
      final x2 = xFor(e.eventTimestamp + e.durationS);
      final w = (x2 - x1).clamp(_minBarWidth, width);
      final rect = RRect.fromRectAndRadius(
        Rect.fromLTWH(x1, snoreBarY, w, snoreBarHeight),
        snoreRadius,
      );
      canvas.drawRRect(rect, snorePaint);
    }

    final pulseGlowPaint = Paint()
      ..color = AppColors.timelinePulse.withValues(alpha: 0.25);
    final pulsePaint = Paint()..color = AppColors.timelinePulse;
    const pulseBarHeight = _pulseRowHeight * 0.82;
    const pulseBarY = pulseTop + (_pulseRowHeight - pulseBarHeight) / 2;
    const pulseRadius = Radius.circular(_pulseBarWidth / 2);
    const glowRadius = Radius.circular(_pulseBarWidth);

    for (final e in events) {
      if (!e.wasHapticTriggered) continue;
      final cx = xFor(e.eventTimestamp);
      final glowRect = RRect.fromRectAndRadius(
        Rect.fromLTWH(
          cx - _pulseBarWidth,
          pulseBarY - 2,
          _pulseBarWidth * 3,
          pulseBarHeight + 4,
        ),
        glowRadius,
      );
      canvas.drawRRect(glowRect, pulseGlowPaint);
      final rect = RRect.fromRectAndRadius(
        Rect.fromLTWH(
          cx - _pulseBarWidth / 2,
          pulseBarY,
          _pulseBarWidth,
          pulseBarHeight,
        ),
        pulseRadius,
      );
      canvas.drawRRect(rect, pulsePaint);
    }

    _drawAxisLabels(canvas, width, axisY);
  }

  void _drawRowBackground(Canvas canvas, Rect proto, double width) {
    final rect = Rect.fromLTRB(0, proto.top, width, proto.bottom);
    final paint = Paint()..color = AppColors.background.withValues(alpha: 0.4);
    canvas.drawRRect(
      RRect.fromRectAndRadius(rect, const Radius.circular(12)),
      paint,
    );
  }

  void _drawAxisLabels(Canvas canvas, double width, double y) {
    final fmt = DateFormat('h:mm a');
    final midEpoch = domainStart + (domainEnd - domainStart) ~/ 2;
    final labels = <(double, String)>[
      (0, fmt.format(_toLocal(domainStart))),
      (width / 2, fmt.format(_toLocal(midEpoch))),
      (width, fmt.format(_toLocal(domainEnd))),
    ];

    for (final (x, text) in labels) {
      final tp = TextPainter(
        text: TextSpan(
          text: text,
          style: const TextStyle(
            color: AppColors.textSecondary,
            fontSize: 11,
          ),
        ),
        textDirection: ui.TextDirection.ltr,
      )..layout();

      double dx;
      if (x == 0) {
        dx = 0;
      } else if (x == width) {
        dx = width - tp.width;
      } else {
        dx = x - tp.width / 2;
      }
      tp.paint(canvas, Offset(dx, y));
    }
  }

  DateTime _toLocal(int epochSeconds) =>
      DateTime.fromMillisecondsSinceEpoch(epochSeconds * 1000).toLocal();

  @override
  bool shouldRepaint(covariant _TimelinePainter old) =>
      old.events != events ||
      old.domainStart != domainStart ||
      old.domainEnd != domainEnd;
}

class EventTimelineLegend extends StatelessWidget {
  const EventTimelineLegend({super.key});

  @override
  Widget build(BuildContext context) {
    return const Padding(
      padding: EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          _LegendDot(color: AppColors.timelineSnore, label: 'SNORE EVENT'),
          SizedBox(width: 24),
          _LegendDot(color: AppColors.timelinePulse, label: 'VIBRATION PULSE'),
        ],
      ),
    );
  }
}

class _LegendDot extends StatelessWidget {
  final Color color;
  final String label;

  const _LegendDot({required this.color, required this.label});

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          width: 10,
          height: 10,
          decoration: BoxDecoration(color: color, shape: BoxShape.circle),
        ),
        const SizedBox(width: 8),
        Text(
          label,
          style: const TextStyle(
            color: AppColors.textSecondary,
            fontSize: 11,
            fontWeight: FontWeight.w600,
            letterSpacing: 0.8,
          ),
        ),
      ],
    );
  }
}
