import 'dart:math' as math;

import 'l10n.dart';

/// Live sensor frame from the level ("D," line).
class LiveData {
  final double ax, ay, az; // acceleration in g
  final double gx, gy, gz; // gyro in deg/s
  final double rawPitch, pitch; // deg, raw and calibrated/smoothed
  final int mode; // 0 red/blue, 1 fade, 2 bubble, 3 locked, 9 lightsaber

  const LiveData({
    this.ax = 0,
    this.ay = 0,
    this.az = 1,
    this.gx = 0,
    this.gy = 0,
    this.gz = 0,
    this.rawPitch = 0,
    this.pitch = 0,
    this.mode = 0,
  });

  double get magnitude => math.sqrt(ax * ax + ay * ay + az * az);

  static LiveData? parse(List<String> p) {
    if (p.length < 10) return null;
    final v = [for (var i = 1; i <= 8; i++) double.tryParse(p[i])];
    if (v.contains(null)) return null;
    return LiveData(
      ax: v[0]!,
      ay: v[1]!,
      az: v[2]!,
      gx: v[3]!,
      gy: v[4]!,
      gz: v[5]!,
      rawPitch: v[6]!,
      pitch: v[7]!,
      mode: int.tryParse(p[9]) ?? 0,
    );
  }

  String get modeName => tr('mode_$mode');
}

/// One row of the firmware settings table ("S," line).
class SettingItem {
  final String key;
  double value;
  final double min, max;
  final bool isInt;
  SettingItem(this.key, this.value, this.min, this.max, this.isInt);
}

/// Progress of the 5-step calibration ("C," lines).
class CalState {
  bool running = false;
  int step = 0; // 1..5
  String phase = 'idle'; // idle, wait, meas, done
  double waitSeconds = 0;
  DateTime? waitStart;
  final Map<int, double> measured = {};
  String? error;
  List<double>? result; // offH, offVp, offVn

  void reset() {
    running = false;
    step = 0;
    phase = 'idle';
    waitSeconds = 0;
    waitStart = null;
    measured.clear();
    error = null;
    result = null;
  }
}
