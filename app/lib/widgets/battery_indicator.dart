import 'package:flutter/material.dart';

/// Battery symbol with the percentage next to it (number only, no % sign).
/// Colours match the LEDs on the device: <20 red, <40 yellow, <60 orange, else green.
class BatteryIndicator extends StatelessWidget {
  final int percent;
  const BatteryIndicator({super.key, required this.percent});

  static Color colorFor(int p) {
    if (p < 20) return const Color(0xFFE5484D);
    if (p < 40) return const Color(0xFFF5C400);
    if (p < 60) return const Color(0xFFF76B15);
    return const Color(0xFF30A46C);
  }

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        SizedBox(
          width: 30,
          height: 15,
          child: CustomPaint(
            painter: _BatteryPainter(percent, colorFor(percent), Theme.of(context).colorScheme.onSurface),
          ),
        ),
        const SizedBox(width: 6),
        Text('$percent', style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600)),
      ],
    );
  }
}

class _BatteryPainter extends CustomPainter {
  final int percent;
  final Color fill, outline;
  _BatteryPainter(this.percent, this.fill, this.outline);

  @override
  void paint(Canvas canvas, Size size) {
    const cap = 3.0;
    final body = RRect.fromLTRBR(0.75, 0.75, size.width - cap - 0.75, size.height - 0.75, const Radius.circular(3));
    canvas.drawRRect(
      body,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.5
        ..color = outline,
    );
    // terminal on the right
    canvas.drawRRect(
      RRect.fromLTRBR(size.width - cap, size.height * 0.3, size.width, size.height * 0.7, const Radius.circular(1)),
      Paint()..color = outline,
    );
    // charge level
    const pad = 2.5;
    final inner = Rect.fromLTRB(pad, pad, size.width - cap - pad, size.height - pad);
    final w = inner.width * percent.clamp(0, 100) / 100.0;
    if (w > 0) {
      canvas.drawRRect(
        RRect.fromRectAndRadius(Rect.fromLTWH(inner.left, inner.top, w, inner.height), const Radius.circular(1.5)),
        Paint()..color = fill,
      );
    }
  }

  @override
  bool shouldRepaint(covariant _BatteryPainter old) =>
      old.percent != percent || old.fill != fill || old.outline != outline;
}
