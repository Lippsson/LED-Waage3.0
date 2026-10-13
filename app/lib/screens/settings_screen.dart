import 'package:flutter/material.dart';

import '../ble_service.dart';
import '../l10n.dart';
import '../models.dart';

class SettingInfo {
  final String group; // l10n key of the group heading
  final String unit;
  const SettingInfo(this.group, [this.unit = '']);
}

// Group and unit per key of the firmware settings table (see AppSettings.ino).
// Titles and help texts are looked up as s_<key> / h_<key> in l10n.dart.
const settingInfo = <String, SettingInfo>{
  'offH': SettingInfo('g_cal', '°'),
  'offVp': SettingInfo('g_cal', '°'),
  'offVn': SettingInfo('g_cal', '°'),
  'calWait': SettingInfo('g_cal', 'ms'),
  'axes': SettingInfo('g_mount'),
  'invX': SettingInfo('g_mount'),
  'invY': SettingInfo('g_mount'),
  'invZ': SettingInfo('g_mount'),
  'rate': SettingInfo('g_smooth', 'Hz'),
  'round': SettingInfo('g_smooth'),
  'dlpf': SettingInfo('g_smooth'),
  'smooth': SettingInfo('g_smooth'),
  'bsmooth': SettingInfo('g_smooth'),
  'brange': SettingInfo('g_bubble', '°'),
  'bubbleLeds': SettingInfo('g_bubble', 'LED'),
  'postL': SettingInfo('g_bubble'),
  'postR': SettingInfo('g_bubble'),
  'travel': SettingInfo('g_bubble', 'LED'),
  'fadeMs': SettingInfo('g_bubble', 'ms'),
  'holdLevel': SettingInfo('g_bubble', 'ms'),
  'bright': SettingInfo('g_device'),
  'sound': SettingInfo('g_device'),
};

/// Settings that belong to the calibration (offsets, wait time) are shown on the Calibrate tab.
bool isCalibrationSetting(String key) => settingInfo[key]?.group == 'g_cal';

class SettingsScreen extends StatelessWidget {
  final BleService ble;
  const SettingsScreen({super.key, required this.ble});

  @override
  Widget build(BuildContext context) {
    if (!ble.settingsLoaded && ble.settings.isEmpty) {
      return const Center(child: CircularProgressIndicator());
    }

    // group in first-seen order
    final groups = <String, List<SettingItem>>{};
    for (final s in ble.settings) {
      if (isCalibrationSetting(s.key)) continue; // shown on the Calibrate tab
      groups.putIfAbsent(tr(settingInfo[s.key]?.group ?? 'g_other'), () => []).add(s);
    }

    return ListView(
      padding: const EdgeInsets.fromLTRB(12, 8, 12, 24),
      children: [
        for (final g in groups.entries) ...[
          Padding(
            padding: const EdgeInsets.fromLTRB(4, 16, 4, 4),
            child: Text(g.key, style: Theme.of(context).textTheme.titleMedium),
          ),
          for (final s in g.value) SettingTile(ble: ble, item: s),
        ],
        const SizedBox(height: 16),
        Row(
          children: [
            Expanded(
              child: FilledButton.icon(
                onPressed: () {
                  ble.saveSettings();
                  ScaffoldMessenger.of(context)
                    ..hideCurrentSnackBar()
                    ..showSnackBar(SnackBar(content: Text(tr('saved_snack'))));
                },
                icon: const Icon(Icons.save),
                label: Text(tr('save')),
              ),
            ),
            const SizedBox(width: 8),
            IconButton.outlined(
              tooltip: tr('reload'),
              onPressed: ble.reloadSettings,
              icon: const Icon(Icons.refresh),
            ),
          ],
        ),
        const SizedBox(height: 8),
        OutlinedButton.icon(
          onPressed: () async {
            final ok = await showDialog<bool>(
              context: context,
              builder: (c) => AlertDialog(
                title: Text(tr('defaults_q')),
                content: Text(tr('defaults_body')),
                actions: [
                  TextButton(onPressed: () => Navigator.pop(c, false), child: Text(tr('cal_cancel'))),
                  FilledButton(onPressed: () => Navigator.pop(c, true), child: Text(tr('reset'))),
                ],
              ),
            );
            if (ok == true) ble.resetSettings();
          },
          icon: const Icon(Icons.restore),
          label: Text(tr('defaults')),
        ),
        const SizedBox(height: 8),
        Text(tr('settings_note'), style: const TextStyle(fontSize: 12)),
      ],
    );
  }
}

class SettingTile extends StatelessWidget {
  final BleService ble;
  final SettingItem item;
  const SettingTile({required this.ble, required this.item});

  // Dialog for typing a value; accepts a comma or a dot as decimal separator.
  Future<void> _editValue(BuildContext context, int digits, String unit) async {
    final ctrl = TextEditingController(text: item.value.toStringAsFixed(digits));
    ctrl.selection = TextSelection(baseOffset: 0, extentOffset: ctrl.text.length);
    final result = await showDialog<double>(
      context: context,
      builder: (dialogContext) {
        String? error;
        return StatefulBuilder(builder: (context, setLocal) {
          void submit() {
            final v = double.tryParse(ctrl.text.trim().replaceAll(',', '.'));
            if (v == null || v < item.min || v > item.max) {
              setLocal(() => error = tr('invalid_value'));
              return;
            }
            Navigator.pop(dialogContext, v);
          }

          return AlertDialog(
            title: Text(tr('s_${item.key}')),
            content: TextField(
              controller: ctrl,
              autofocus: true,
              keyboardType: TextInputType.numberWithOptions(decimal: !item.isInt, signed: item.min < 0),
              decoration: InputDecoration(
                suffixText: unit,
                helperText: tr('range_hint', {
                  'min': item.min.toStringAsFixed(digits),
                  'max': item.max.toStringAsFixed(digits),
                }),
                errorText: error,
              ),
              onSubmitted: (_) => submit(),
            ),
            actions: [
              TextButton(onPressed: () => Navigator.pop(dialogContext), child: Text(tr('cal_cancel'))),
              FilledButton(onPressed: submit, child: Text(tr('apply'))),
            ],
          );
        });
      },
    );
    if (result != null) ble.setSetting(item, result);
  }

  @override
  Widget build(BuildContext context) {
    if (item.key == 'axes') return _AxesTile(ble: ble, item: item);
    if (item.key == 'round') return _RoundTile(ble: ble, item: item);
    if (item.key == 'dlpf') return _FilterTile(ble: ble, item: item);
    if (item.key.startsWith('inv')) return _InvertTile(ble: ble, item: item);
    final info = settingInfo[item.key];
    final digits = item.isInt ? 0 : (item.max - item.min <= 12 ? 2 : 1);
    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(14, 10, 14, 4),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Expanded(child: Text(info == null ? item.key : tr('s_${item.key}'), style: const TextStyle(fontWeight: FontWeight.w600))),
                // tap the value to type it in
                InkWell(
                  borderRadius: BorderRadius.circular(6),
                  onTap: () => _editValue(context, digits, info?.unit ?? ''),
                  child: Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Text('${item.value.toStringAsFixed(digits)} ${info?.unit ?? ''}',
                            style: const TextStyle(fontWeight: FontWeight.bold)),
                        const SizedBox(width: 4),
                        const Icon(Icons.edit, size: 14),
                      ],
                    ),
                  ),
                ),
              ],
            ),
            if (info != null) Text(tr('h_${item.key}'), style: const TextStyle(fontSize: 12)),
            Slider(
              value: item.value.clamp(item.min, item.max),
              min: item.min,
              max: item.max,
              divisions: item.isInt && (item.max - item.min) <= 300 ? (item.max - item.min).toInt() : null,
              onChanged: (v) => ble.setSetting(item, v, sendNow: false),
              onChangeEnd: (v) => ble.setSetting(item, v),
            ),
          ],
        ),
      ),
    );
  }
}

// Sensor axis permutation: logical X/Y/Z are taken from these sensor axes.
const _perms = ['XYZ', 'XZY', 'YXZ', 'YZX', 'ZXY', 'ZYX'];

String _permLabel(int i) {
  final seq = _perms[i].split('').join(' · ');
  final String note;
  switch (i) {
    case 0:
      note = tr('axes_std');
    case 1:
      note = tr('axes_swap', {'a': 'Y', 'b': 'Z'});
    case 2:
      note = tr('axes_swap', {'a': 'X', 'b': 'Y'});
    case 5:
      note = tr('axes_swap', {'a': 'X', 'b': 'Z'});
    default:
      note = tr('axes_cycle');
  }
  return '$seq  ($note)';
}

class _AxesTile extends StatelessWidget {
  final BleService ble;
  final SettingItem item;
  const _AxesTile({required this.ble, required this.item});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(14, 10, 14, 10),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(tr('s_axes'), style: const TextStyle(fontWeight: FontWeight.w600)),
            Text(tr('h_axes'), style: const TextStyle(fontSize: 12)),
            const SizedBox(height: 8),
            DropdownButton<int>(
              isExpanded: true,
              value: item.value.round().clamp(0, 5),
              items: [
                for (var i = 0; i < _perms.length; i++) DropdownMenuItem(value: i, child: Text(_permLabel(i))),
              ],
              onChanged: (v) {
                if (v != null) ble.setSetting(item, v.toDouble());
              },
            ),
          ],
        ),
      ),
    );
  }
}

class _InvertTile extends StatelessWidget {
  final BleService ble;
  final SettingItem item;
  const _InvertTile({required this.ble, required this.item});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: SwitchListTile(
        title: Text(tr('s_${item.key}'), style: const TextStyle(fontWeight: FontWeight.w600)),
        subtitle: Text(tr('h_inv'), style: const TextStyle(fontSize: 12)),
        value: item.value >= 0.5,
        onChanged: (v) => ble.setSetting(item, v ? 1 : 0),
      ),
    );
  }
}
// Rounding of the calibrated tilt in the app and on the OLED.
class _RoundTile extends StatelessWidget {
  final BleService ble;
  final SettingItem item;
  const _RoundTile({required this.ble, required this.item});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(14, 10, 14, 12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(tr('s_round'), style: const TextStyle(fontWeight: FontWeight.w600)),
            Text(tr('h_round'), style: const TextStyle(fontSize: 12)),
            const SizedBox(height: 8),
            SizedBox(
              width: double.infinity,
              child: SegmentedButton<int>(
                showSelectedIcon: false,
                segments: [
                  ButtonSegment(value: 0, label: Text(tr('round_0'), textAlign: TextAlign.center)),
                  ButtonSegment(value: 1, label: Text(tr('round_1'), textAlign: TextAlign.center)),
                  ButtonSegment(value: 2, label: Text(tr('round_2'), textAlign: TextAlign.center)),
                ],
                selected: {item.value.round().clamp(0, 2)},
                onSelectionChanged: (s) => ble.setSetting(item, s.first.toDouble()),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
// Bandwidth of the sensor's built-in low-pass filter (MPU6050 DLPF_CFG 0..6).
const _filterHz = ['260', '184', '94', '44', '21', '10', '5'];

class _FilterTile extends StatelessWidget {
  final BleService ble;
  final SettingItem item;
  const _FilterTile({required this.ble, required this.item});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(14, 10, 14, 10),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(tr('s_dlpf'), style: const TextStyle(fontWeight: FontWeight.w600)),
            Text(tr('h_dlpf'), style: const TextStyle(fontSize: 12)),
            const SizedBox(height: 8),
            DropdownButton<int>(
              isExpanded: true,
              value: item.value.round().clamp(0, 6),
              items: [
                for (var i = 0; i < _filterHz.length; i++)
                  DropdownMenuItem(
                    value: i,
                    child: Text(i == 0 ? tr('dlpf_off') : '${_filterHz[i]} Hz' + (i == 3 ? '  (${tr('dlpf_default')})' : '')),
                  ),
              ],
              onChanged: (v) {
                if (v != null) ble.setSetting(item, v.toDouble());
              },
            ),
          ],
        ),
      ),
    );
  }
}