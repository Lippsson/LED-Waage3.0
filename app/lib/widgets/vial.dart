import 'dart:math' as math;

import 'package:flutter/material.dart';

/// Classic spirit-level vial: green (blue when vertical) tube, four marks, gliding bubble.
/// [deviation] is the signed angle to the nearest level/plumb position in degrees,
/// [range] the deviation (deg) at which the bubble reaches the end of the tube.
class Vial extends StatelessWidget {
  final double deviation;
  final double range;
  final bool vertical;
  final double? maxHeight; // cap for landscape
  const Vial({super.key, required this.deviation, required this.range, required this.vertical, this.maxHeight});

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(builder: (context, c) {
      var h = math.max(56.0, c.maxWidth / 7.5);
      if (maxHeight != null) h = math.min(h, maxHeight!);
      return SizedBox(
        width: c.maxWidth,
        height: h,
        child: CustomPaint(painter: _VialPainter(deviation, range, vertical)),
      );
    });
  }
}

class _VialPainter extends CustomPainter {
  final double deviation, range;
  final bool vertical;
  _VialPainter(this.deviation, this.range, this.vertical);

  @override
  void paint(Canvas canvas, Size size) {
    final w = size.width, h = size.height;

    // tube
    final marginX = h * 0.1;
    final tubeH = h * 0.62;
    final tube = RRect.fromLTRBR(marginX, (h - tubeH) / 2, w - marginX, (h + tubeH) / 2, Radius.circular(tubeH / 2));
    final tubeW = tube.width;
    final cy = h / 2;

    final colors = vertical
        ? const [Color(0xFF7DB6F5), Color(0xFF3F7FDB), Color(0xFF2559B0)]
        : const [Color(0xFFB4DE4A), Color(0xFF8CC31F), Color(0xFF6C9C14)];
    canvas.drawRRect(
      tube.shift(const Offset(0, 3)),
      Paint()..color = Colors.black.withValues(alpha: 0.35)..maskFilter = const MaskFilter.blur(BlurStyle.normal, 4),
    );
    canvas.drawRRect(
      tube,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: colors,
          stops: const [0.0, 0.5, 1.0],
        ).createShader(tube.outerRect),
    );

    canvas.save();
    canvas.clipRRect(tube);

    // marks
    final mark = Paint()
      ..color = const Color(0xFF222222)
      ..strokeWidth = math.max(2.5, h * 0.045);
    for (final f in const [0.40, 0.447, 0.553, 0.603]) {
      final x = tube.left + tubeW * f;
      canvas.drawLine(Offset(x, tube.top), Offset(x, tube.bottom), mark);
    }

    // bubble
    final r = tubeH * 0.4;
    final maxOff = tubeW / 2 - r - tubeH * 0.15;
    final f = (deviation / range).clamp(-1.0, 1.0);
    final bc = Offset(tube.center.dx + f * maxOff, cy);
    canvas.drawCircle(bc + Offset(r * 0.08, r * 0.12), r, Paint()..color = Colors.black.withValues(alpha: 0.18));
    canvas.drawCircle(
      bc,
      r,
      Paint()
        ..shader = RadialGradient(
          center: const Alignment(-0.35, -0.4),
          radius: 1.0,
          colors: vertical
              ? const [Color(0xFFD2E6FB), Color(0xFF5E9BE8), Color(0xFF2A62BF)]
              : const [Color(0xFFD9F08A), Color(0xFF8CC31F), Color(0xFF5F8E10)],
          stops: const [0.0, 0.55, 1.0],
        ).createShader(Rect.fromCircle(center: bc, radius: r)),
    );
    canvas.drawCircle(
      bc,
      r,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.2
        ..color = Colors.black.withValues(alpha: 0.25),
    );
    canvas.drawOval(
      Rect.fromCenter(center: bc + Offset(r * 0.38, -r * 0.4), width: r * 0.5, height: r * 0.42),
      Paint()..color = Colors.white.withValues(alpha: 0.9),
    );

    // gloss lines
    final gloss = Paint()
      ..strokeCap = StrokeCap.round
      ..strokeWidth = math.max(1.5, h * 0.025)
      ..color = Colors.white.withValues(alpha: 0.75);
    canvas.drawLine(
        Offset(tube.left + tubeW * 0.05, tube.top + tubeH * 0.14), Offset(tube.left + tubeW * 0.27, tube.top + tubeH * 0.14), gloss);
    gloss.color = Colors.white.withValues(alpha: 0.5);
    canvas.drawLine(
        Offset(tube.left + tubeW * 0.72, tube.bottom - tubeH * 0.14), Offset(tube.left + tubeW * 0.93, tube.bottom - tubeH * 0.14), gloss);
    canvas.restore();

    canvas.drawRRect(
      tube,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.2
        ..color = Colors.black.withValues(alpha: 0.3),
    );
  }

  @override
  bool shouldRepaint(covariant _VialPainter old) =>
      old.deviation != deviation || old.range != range || old.vertical != vertical;
}
