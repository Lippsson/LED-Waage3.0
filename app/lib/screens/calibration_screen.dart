import 'dart:async';

import 'package:flutter/material.dart';

import '../ble_service.dart';
import '../l10n.dart';
import 'settings_screen.dart' show SettingTile, isCalibrationSetting;

class CalibrationScreen extends StatefulWidget {
  final BleService ble;
  const CalibrationScreen({super.key, required this.ble});

  @override
  State<CalibrationScreen> createState() => _CalibrationScreenState();
}

class _CalibrationScreenState extends State<CalibrationScreen> {
  Timer? _tick;

  @override
  void initState() {
    super.initState();
    // refresh the countdown while waiting
    _tick = Timer.periodic(const Duration(milliseconds: 200), (_) {
      if (mounted && widget.ble.cal.running) setState(() {});
    });
  }

  @override
  void dispose() {
    _tick?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final cal = widget.ble.cal;
    final scheme = Theme.of(context).colorScheme;
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Text(tr('cal_intro')),
        const SizedBox(height: 16),
        if (!cal.running)
          FilledButton.icon(
            onPressed: widget.ble.connected ? widget.ble.startCalibration : null,
            icon: const Icon(Icons.tune),
            label: Text(tr('cal_start')),
          )
        else
          OutlinedButton.icon(
            onPressed: widget.ble.cancelCalibration,
            icon: const Icon(Icons.close),
            label: Text(tr('cal_cancel')),
          ),
        if (cal.error != null)
          Card(
            color: scheme.errorContainer,
            margin: const EdgeInsets.only(top: 16),
            child: ListTile(
              leading: Icon(Icons.error_outline, color: scheme.onErrorContainer),
              title: Text(tr('cal_aborted', {'r': cal.error!}), style: TextStyle(color: scheme.onErrorContainer)),
            ),
          ),
        if (cal.result != null && cal.result!.length == 3)
          Card(
            color: scheme.primaryContainer,
            margin: const EdgeInsets.only(top: 16),
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(tr('cal_saved'),
                      style: TextStyle(fontWeight: FontWeight.bold, color: scheme.onPrimaryContainer)),
                  const SizedBox(height: 8),
                  Text('${tr('cal_off_h', {'v': cal.result![0].toStringAsFixed(3)})}\n'
                      '${tr('cal_off_vp', {'v': cal.result![1].toStringAsFixed(3)})}\n'
                      '${tr('cal_off_vn', {'v': cal.result![2].toStringAsFixed(3)})}'),
                ],
              ),
            ),
          ),
        const SizedBox(height: 16),
        for (var i = 1; i <= 5; i++) _stepTile(context, i),
        ..._calibrationSettings(context),
      ],
    );
  }

  // offsets and wait time (moved here from the Settings tab)
  List<Widget> _calibrationSettings(BuildContext context) {
    final items = widget.ble.settings.where((s) => isCalibrationSetting(s.key)).toList();
    if (items.isEmpty) return const [];
    return [
      Padding(
        padding: const EdgeInsets.fromLTRB(4, 20, 4, 4),
        child: Text(tr('cal_settings'), style: Theme.of(context).textTheme.titleMedium),
      ),
      for (final s in items) SettingTile(ble: widget.ble, item: s),
      const SizedBox(height: 8),
      FilledButton.icon(
        onPressed: () {
          widget.ble.saveSettings();
          ScaffoldMessenger.of(context)
            ..hideCurrentSnackBar()
            ..showSnackBar(SnackBar(content: Text(tr('saved_snack'))));
        },
        icon: const Icon(Icons.save),
        label: Text(tr('save')),
      ),
      const SizedBox(height: 16),
    ];
  }

  Widget _stepTile(BuildContext context, int n) {
    final cal = widget.ble.cal;
    final scheme = Theme.of(context).colorScheme;
    final active = cal.running && cal.step == n;
    final done = cal.measured.containsKey(n) && !(active && cal.phase == 'meas');

    Widget? extra;
    if (active && cal.phase == 'next') {
      // step finished successfully: the next one starts only after the user confirms
      extra = Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const SizedBox(height: 8),
          if (cal.measured[n] != null) Text(tr('cal_measured', {'v': cal.measured[n]!.toStringAsFixed(2)})),
          const SizedBox(height: 4),
          Text(tr('cal_step_ok', {'n': '$n'})),
          const SizedBox(height: 8),
          FilledButton.icon(
            onPressed: widget.ble.nextCalibration,
            icon: const Icon(Icons.arrow_forward),
            label: Text(tr('cal_next')),
          ),
        ],
      );
    } else if (active && cal.phase == 'wait' && cal.waitStart != null) {
      final elapsed = DateTime.now().difference(cal.waitStart!).inMilliseconds / 1000.0;
      final left = (cal.waitSeconds - elapsed).clamp(0.0, cal.waitSeconds);
      extra = Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const SizedBox(height: 8),
          LinearProgressIndicator(value: cal.waitSeconds == 0 ? 0 : 1 - left / cal.waitSeconds),
          const SizedBox(height: 4),
          Text(tr('cal_wait', {'s': left.toStringAsFixed(1)})),
        ],
      );
    } else if (active && cal.phase == 'meas') {
      extra = Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const SizedBox(height: 8),
          const LinearProgressIndicator(),
          const SizedBox(height: 4),
          Text(tr('cal_measuring')),
        ],
      );
    } else if (done) {
      extra = Padding(
        padding: const EdgeInsets.only(top: 4),
        child: Text(tr('cal_measured', {'v': cal.measured[n]!.toStringAsFixed(2)})),
      );
    }

    return Card(
      color: active ? scheme.secondaryContainer : null,
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            CircleAvatar(
              radius: 14,
              backgroundColor: done ? scheme.primary : scheme.surfaceContainerHighest,
              child: done
                  ? Icon(Icons.check, size: 16, color: scheme.onPrimary)
                  : Text('$n', style: const TextStyle(fontSize: 13)),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [Text(tr('cal_step$n')), if (extra != null) extra],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
