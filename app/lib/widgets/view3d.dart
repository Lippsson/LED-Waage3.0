import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../models.dart';

enum View3DMode { orientation, vector }

const _xColor = Color(0xFFE5484D);
const _yColor = Color(0xFF30A46C);
const _zColor = Color(0xFF3E63DD);
const _accColor = Color(0xFFF76B15);

/// Small 3D vector helpers (no external package needed).
class V3 {
  final double x, y, z;
  const V3(this.x, this.y, this.z);
  V3 operator +(V3 o) => V3(x + o.x, y + o.y, z + o.z);
  V3 operator -(V3 o) => V3(x - o.x, y - o.y, z - o.z);
  V3 operator *(double s) => V3(x * s, y * s, z * s);
  double get len => math.sqrt(x * x + y * y + z * z);
  double dot(V3 o) => x * o.x + y * o.y + z * o.z;
  V3 cross(V3 o) => V3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x);
  V3 get unit {
    final l = len;
    return l < 1e-9 ? const V3(0, 0, 1) : this * (1 / l);
  }
}

/// Rotation matrix as 3 rows.
class Mat3 {
  final List<double> m; // row-major
  const Mat3(this.m);
  static const identity = Mat3([1, 0, 0, 0, 1, 0, 0, 0, 1]);

  V3 apply(V3 v) => V3(
        m[0] * v.x + m[1] * v.y + m[2] * v.z,
        m[3] * v.x + m[4] * v.y + m[5] * v.z,
        m[6] * v.x + m[7] * v.y + m[8] * v.z,
      );

  /// Rotation by [angle] around unit [axis] (Rodrigues).
  static Mat3 axisAngle(V3 a, double angle) {
    final c = math.cos(angle), s = math.sin(angle), t = 1 - c;
    return Mat3([
      t * a.x * a.x + c, t * a.x * a.y - s * a.z, t * a.x * a.z + s * a.y,
      t * a.x * a.y + s * a.z, t * a.y * a.y + c, t * a.y * a.z - s * a.x,
      t * a.x * a.z - s * a.y, t * a.y * a.z + s * a.x, t * a.z * a.z + c,
    ]);
  }

  /// Rotation taking unit vector [from] onto unit vector [to].
  static Mat3 between(V3 from, V3 to) {
    final d = from.dot(to).clamp(-1.0, 1.0);
    if (d > 0.999999) return identity;
    if (d < -0.999999) return axisAngle(const V3(1, 0, 0), math.pi);
    final axis = from.cross(to).unit;
    return axisAngle(axis, math.acos(d));
  }
}

/// Interactive 3D view: drag to rotate the camera.
///  * orientation: the level tilts like the real one (measured gravity points up)
///  * vector:      fixed device axes, the acceleration vector with its X/Y/Z components
class View3D extends StatefulWidget {
  final LiveData data;
  final View3DMode mode;
  final bool simple; // simple mode: X component forced to 0 (pure pitch)
  const View3D({super.key, required this.data, required this.mode, this.simple = false});

  @override
  State<View3D> createState() => _View3DState();
}

class _View3DState extends State<View3D> {
  double yaw = -0.6;
  double elev = 0.55;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return GestureDetector(
      onPanUpdate: (d) => setState(() {
        yaw += d.delta.dx * 0.01;
        elev = (elev + d.delta.dy * 0.01).clamp(-1.4, 1.4);
      }),
      onDoubleTap: () => setState(() {
        yaw = -0.6;
        elev = 0.55;
      }),
      child: CustomPaint(
        painter: _Painter(widget.data, widget.mode, widget.simple, yaw, elev, scheme),
        child: const SizedBox.expand(),
      ),
    );
  }
}

class _Face {
  final List<V3> pts;
  final Color color;
  final double depth;
  _Face(this.pts, this.color, this.depth);
}

class _Painter extends CustomPainter {
  final LiveData d;
  final View3DMode mode;
  final bool simple;
  final double yaw, elev;
  final ColorScheme scheme;
  _Painter(this.d, this.mode, this.simple, this.yaw, this.elev, this.scheme);

  // measured acceleration; in simple mode the X axis is held at 0
  V3 get _acc => V3(simple ? 0 : d.ax, d.ay, d.az);

  late Size _size;
  double _scale = 100;

  // camera: rotate around z (yaw), then tilt (elev); depth axis is y'.
  V3 _view(V3 p) {
    final cy = math.cos(yaw), sy = math.sin(yaw);
    final x1 = p.x * cy - p.y * sy;
    final y1 = p.x * sy + p.y * cy;
    final ce = math.cos(elev), se = math.sin(elev);
    return V3(x1, y1 * ce - p.z * se, p.z * ce + y1 * se);
  }

  Offset _proj(V3 p) {
    final v = _view(p);
    const camDist = 7.0;
    final f = camDist / (camDist + v.y);
    return Offset(_size.width / 2 + v.x * f * _scale, _size.height / 2 - v.z * f * _scale);
  }

  double _depth(V3 p) => _view(p).y;

  @override
  void paint(Canvas canvas, Size size) {
    _size = size;
    _scale = math.min(size.width, size.height) / 4.2;

    _drawGround(canvas);
    if (mode == View3DMode.orientation) {
      _drawOrientation(canvas);
    } else {
      _drawVector(canvas);
    }
  }

  void _drawGround(Canvas canvas) {
    final paint = Paint()
      ..color = scheme.outlineVariant
      ..strokeWidth = 1;
    const n = 4;
    const half = 2.0;
    const z = -1.35;
    for (var i = -n; i <= n; i++) {
      final t = i * half / n;
      canvas.drawLine(_proj(V3(t, -half, z)), _proj(V3(t, half, z)), paint);
      canvas.drawLine(_proj(V3(-half, t, z)), _proj(V3(half, t, z)), paint);
    }
  }

  void _drawOrientation(Canvas canvas) {
    final up = _acc.unit;
    final rot = Mat3.between(up, const V3(0, 0, 1));

    // the level: 1800 mm long (Y), 25 mm thick (X), 100 mm deep (Z); 1 unit = 600 mm
    const mmPerUnit = 600.0;
    const hx = 25 / 2 / mmPerUnit, hy = 1800 / 2 / mmPerUnit, hz = 100 / 2 / mmPerUnit;
    final corners = <V3>[
      for (final sx in [-1.0, 1.0])
        for (final sy in [-1.0, 1.0])
          for (final sz in [-1.0, 1.0]) rot.apply(V3(sx * hx, sy * hy, sz * hz)),
    ];
    V3 c(int sx, int sy, int sz) => corners[(sx * 4) + (sy * 2) + sz];

    final faces = <_Face>[];
    void add(List<V3> pts, Color col) {
      final centre = pts.fold(const V3(0, 0, 0), (a, b) => a + b) * (1 / pts.length);
      faces.add(_Face(pts, col, _depth(centre)));
    }

    add([c(0, 0, 1), c(1, 0, 1), c(1, 1, 1), c(0, 1, 1)], _zColor.withValues(alpha: 0.85)); // +Z
    add([c(0, 0, 0), c(1, 0, 0), c(1, 1, 0), c(0, 1, 0)], _zColor.withValues(alpha: 0.45)); // -Z
    add([c(1, 0, 0), c(1, 1, 0), c(1, 1, 1), c(1, 0, 1)], _xColor.withValues(alpha: 0.8)); // +X
    add([c(0, 0, 0), c(0, 1, 0), c(0, 1, 1), c(0, 0, 1)], _xColor.withValues(alpha: 0.5)); // -X
    add([c(0, 1, 0), c(1, 1, 0), c(1, 1, 1), c(0, 1, 1)], _yColor.withValues(alpha: 0.85)); // +Y
    add([c(0, 0, 0), c(1, 0, 0), c(1, 0, 1), c(0, 0, 1)], _yColor.withValues(alpha: 0.5)); // -Y

    faces.sort((a, b) => b.depth.compareTo(a.depth)); // far first
    final edge = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.5
      ..color = scheme.onSurface.withValues(alpha: 0.7);
    for (final f in faces) {
      final path = Path()..addPolygon([for (final p in f.pts) _proj(p)], true);
      canvas.drawPath(path, Paint()..color = f.color);
      canvas.drawPath(path, edge);
    }

    // device axes through the body, plus the up-vector (measured gravity reaction)
    _arrow(canvas, const V3(0, 0, 0), rot.apply(const V3(1.2, 0, 0)), _xColor, 'X');
    _arrow(canvas, const V3(0, 0, 0), rot.apply(const V3(0, 2.0, 0)), _yColor, 'Y');
    _arrow(canvas, const V3(0, 0, 0), rot.apply(const V3(0, 0, 1.0)), _zColor, 'Z');
    _arrow(canvas, const V3(0, 0, 0), const V3(0, 0, 1.4), scheme.onSurface, 'oben', dashed: true);
  }

  void _drawVector(Canvas canvas) {
    // device axes (fixed), length 1.6 = 1.6 g visible range
    const axisLen = 1.7;
    _arrow(canvas, const V3(0, 0, 0), const V3(axisLen, 0, 0), _xColor.withValues(alpha: 0.5), 'X');
    _arrow(canvas, const V3(0, 0, 0), const V3(0, axisLen, 0), _yColor.withValues(alpha: 0.5), 'Y');
    _arrow(canvas, const V3(0, 0, 0), const V3(0, 0, axisLen), _zColor.withValues(alpha: 0.5), 'Z');

    final a = _acc;
    final px = V3(a.x, 0, 0), pxy = V3(a.x, a.y, 0);

    // component path: X, then Y, then Z up to the tip
    final comp = Paint()
      ..strokeWidth = 3
      ..strokeCap = StrokeCap.round;
    canvas.drawLine(_proj(const V3(0, 0, 0)), _proj(px), comp..color = _xColor);
    canvas.drawLine(_proj(px), _proj(pxy), comp..color = _yColor);
    canvas.drawLine(_proj(pxy), _proj(a), comp..color = _zColor);

    // projection helpers
    final guide = Paint()
      ..strokeWidth = 1
      ..color = scheme.onSurface.withValues(alpha: 0.25);
    canvas.drawLine(_proj(const V3(0, 0, 0)), _proj(pxy), guide);

    _arrow(canvas, const V3(0, 0, 0), a, _accColor, '|a| ${d.magnitude.toStringAsFixed(2)} g', width: 4);
    canvas.drawCircle(_proj(a), 6, Paint()..color = _accColor);
  }

  void _arrow(Canvas canvas, V3 from, V3 to, Color color, String label,
      {double width = 2.5, bool dashed = false}) {
    final a = _proj(from), b = _proj(to);
    final paint = Paint()
      ..color = color
      ..strokeWidth = width
      ..strokeCap = StrokeCap.round;
    if (dashed) {
      final delta = b - a;
      const seg = 8.0;
      final n = (delta.distance / seg).floor();
      for (var i = 0; i < n; i += 2) {
        canvas.drawLine(a + delta * (i / n), a + delta * ((i + 1) / n), paint);
      }
    } else {
      canvas.drawLine(a, b, paint);
    }
    // arrow head
    final dir = (b - a);
    if (dir.distance > 1) {
      final u = dir / dir.distance;
      final n = Offset(-u.dy, u.dx);
      final head = Path()
        ..moveTo(b.dx, b.dy)
        ..lineTo(b.dx - u.dx * 10 + n.dx * 4.5, b.dy - u.dy * 10 + n.dy * 4.5)
        ..lineTo(b.dx - u.dx * 10 - n.dx * 4.5, b.dy - u.dy * 10 - n.dy * 4.5)
        ..close();
      canvas.drawPath(head, Paint()..color = color);
    }
    final tp = TextPainter(
      text: TextSpan(text: label, style: TextStyle(color: color, fontSize: 12, fontWeight: FontWeight.w600)),
      textDirection: TextDirection.ltr,
    )..layout();
    final off = dir.distance > 1 ? dir / dir.distance * 8 : Offset.zero;
    tp.paint(canvas, b + off - Offset(tp.width / 2, tp.height / 2));
  }

  @override
  bool shouldRepaint(covariant _Painter old) =>
      old.d != d || old.mode != mode || old.simple != simple || old.yaw != yaw || old.elev != elev || old.scheme != scheme;
}
