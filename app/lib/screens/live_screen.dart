import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../ble_service.dart';
import '../l10n.dart';
import '../models.dart';
import '../widgets/view3d.dart';
import '../widgets/vial.dart';

class LiveScreen extends StatefulWidget {
  final BleService ble;
  const LiveScreen({super.key, required this.ble});

  @override
  State<LiveScreen> createState() => _LiveScreenState();
}

class _LiveScreenState extends State<LiveScreen> {
  View3DMode mode = View3DMode.orientation;
  bool expert = false; // false = simple: only the calibrated tilt

  @override
  void initState() {
    super.initState();
    _loadExpert();
  }

  Future<void> _loadExpert() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final v = prefs.getBool('expert') ?? false;
      if (mounted && v != expert) setState(() => expert = v);
    } catch (_) {}
  }

  Future<void> _toggleExpert() async {
    setState(() => expert = !expert);
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setBool('expert', expert);
    } catch (_) {}
  }

  @override
  Widget build(BuildContext context) {
    final d = widget.ble.live;
    if (MediaQuery.orientationOf(context) == Orientation.landscape) return _landscape(d);
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 0),
          child: SegmentedButton<View3DMode>(
            segments: [
              ButtonSegment(
                  value: View3DMode.orientation, label: Text(tr('view_pose')), icon: const Icon(Icons.view_in_ar)),
              ButtonSegment(
                  value: View3DMode.vector, label: Text(tr('view_vector')), icon: const Icon(Icons.call_made)),
            ],
            selected: {mode},
            onSelectionChanged: (s) => setState(() => mode = s.first),
          ),
        ),
        Expanded(child: View3D(data: d, mode: mode, simple: !expert)),
        Padding(
          padding: const EdgeInsets.only(bottom: 4),
          child: Text('${tr('drag_hint')}\n${tr('model_dims')}',
              textAlign: TextAlign.center, style: const TextStyle(fontSize: 12)),
        ),
        Padding(
          padding: EdgeInsets.fromLTRB(12, 4, 12, expert ? 12 : 8),
          child: expert ? _expertValues(d) : _simpleValues(d),
        ),
        if (!expert) _vial(d),
      ],
    );
  }

  // Landscape: the 3D graphic is hidden; values and vial scroll inside the area above the tabs.
  Widget _landscape(LiveData d) {
    return SingleChildScrollView(
      padding: const EdgeInsets.only(top: 8, bottom: 8),
      child: Column(
        children: [
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: expert ? _expertValues(d) : _simpleValues(d),
          ),
          if (!expert) Padding(padding: const EdgeInsets.only(top: 8), child: _vial(d, maxHeight: 84)),
        ],
      ),
    );
  }

  // vial across the full screen width; the bubble follows the deviation from level/plumb
  Widget _vial(LiveData d, {double? maxHeight}) {
    final vertical = d.pitch.abs() >= 45;
    final dev = vertical ? d.pitch - (d.pitch > 0 ? 90.0 : -90.0) : d.pitch;
    const range = 5.0; // deviation (deg) at which the bubble touches the end of the tube
    return Padding(
      padding: const EdgeInsets.only(bottom: 12),
      child: TweenAnimationBuilder<double>(
        tween: Tween<double>(end: dev),
        duration: const Duration(milliseconds: 120),
        builder: (context, v, _) => Vial(deviation: v, range: range, vertical: vertical, maxHeight: maxHeight),
      ),
    );
  }

  Widget _toggleButton() {
    return Expanded(
      child: Card(
        margin: const EdgeInsets.all(3),
        child: Padding(
          padding: const EdgeInsets.all(8),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(tr('ui_mode'), style: const TextStyle(fontSize: 12)),
              const SizedBox(height: 4),
              SizedBox(
                width: double.infinity,
                child: FilledButton.tonalIcon(
                  onPressed: _toggleExpert,
                  icon: const Icon(Icons.swap_horiz, size: 18),
                  label: FittedBox(child: Text(expert ? tr('ui_expert') : tr('ui_simple'))),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _simpleValues(LiveData d) {
    return Row(
      children: [
        Expanded(
          flex: 2,
          child: Card(
            margin: const EdgeInsets.all(3),
            child: Padding(
              padding: const EdgeInsets.symmetric(vertical: 10, horizontal: 14),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(tr('pitch_cal'), style: const TextStyle(fontSize: 12)),
                  FittedBox(
                    fit: BoxFit.scaleDown,
                    child: Text('${d.pitch.toStringAsFixed(2)} °',
                        style: const TextStyle(fontSize: 34, fontWeight: FontWeight.w700)),
                  ),
                ],
              ),
            ),
          ),
        ),
        _toggleButton(),
      ],
    );
  }

  Widget _expertValues(LiveData d) {
    return Column(
      children: [
        Row(children: [
          _value('X', d.ax, 'g', const Color(0xFFE5484D)),
          _value('Y', d.ay, 'g', const Color(0xFF30A46C)),
          _value('Z', d.az, 'g', const Color(0xFF3E63DD)),
        ]),
        Row(children: [
          _value(tr('gyro', {'axis': 'X'}), d.gx, '°/s', const Color(0xFFE5484D), digits: 1),
          _value(tr('gyro', {'axis': 'Y'}), d.gy, '°/s', const Color(0xFF30A46C), digits: 1),
          _value(tr('gyro', {'axis': 'Z'}), d.gz, '°/s', const Color(0xFF3E63DD), digits: 1),
        ]),
        Row(children: [
          _value(tr('pitch_raw'), d.rawPitch, '°', null, digits: 2),
          _value(tr('pitch_cal'), d.pitch, '°', null, digits: 2),
          _toggleButton(),
        ]),
      ],
    );
  }

  Widget _value(String label, double v, String unit, Color? color, {int digits = 3}) {
    return Expanded(
      child: Card(
        margin: const EdgeInsets.all(3),
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 8, horizontal: 10),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(label, style: TextStyle(fontSize: 12, color: color)),
              FittedBox(
                fit: BoxFit.scaleDown,
                child: Text('${v.toStringAsFixed(digits)} $unit',
                    style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w600)),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
