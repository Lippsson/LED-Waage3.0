import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

import 'l10n.dart';
import 'models.dart';

const _serviceUuid = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const _rxUuid = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // app writes
const _txUuid = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // device notifies

enum LinkState { disconnected, scanning, connecting, connected }

/// BLE link to the LED level; the protocol is documented in Bluetooth.ino.
class BleService extends ChangeNotifier {
  LinkState link = LinkState.disconnected;
  String? error;
  List<ScanResult> found = [];
  String deviceName = '';

  LiveData live = const LiveData();
  int? batteryPercent; // 0..100, null until the first report

  final List<SettingItem> settings = [];
  bool settingsLoaded = false;
  final CalState cal = CalState();

  BluetoothDevice? _device;
  BluetoothCharacteristic? _rx;
  StreamSubscription? _scanSub, _connSub, _dataSub;
  Timer? _notifyThrottle;
  bool _dirty = false;

  bool get connected => link == LinkState.connected;

  Future<bool> _ensurePermissions() async {
    final r = await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.locationWhenInUse, // only needed (and only asked) on Android < 12
    ].request();
    return (r[Permission.bluetoothScan]?.isGranted ?? false) &&
        (r[Permission.bluetoothConnect]?.isGranted ?? false);
  }

  Future<void> startScan() async {
    error = null;
    if (!await _ensurePermissions()) {
      error = tr('bt_perm');
      notifyListeners();
      return;
    }
    // The adapter state is "unknown" until the first platform event arrives.
    var state = BluetoothAdapterState.unknown;
    try {
      state = await FlutterBluePlus.adapterState
          .firstWhere((s) => s != BluetoothAdapterState.unknown)
          .timeout(const Duration(seconds: 3));
    } catch (_) {}
    if (state == BluetoothAdapterState.off) {
      try {
        await FlutterBluePlus.turnOn();
        state = BluetoothAdapterState.on;
      } catch (_) {}
    }
    if (state == BluetoothAdapterState.off) {
      error = tr('bt_off');
      notifyListeners();
      return;
    }
    found = [];
    link = LinkState.scanning;
    notifyListeners();
    await _scanSub?.cancel();
    _scanSub = FlutterBluePlus.scanResults.listen((list) {
      found = list;
      notifyListeners();
    });
    try {
      await FlutterBluePlus.startScan(
        withServices: [Guid(_serviceUuid)],
        timeout: const Duration(seconds: 10),
      );
      await FlutterBluePlus.isScanning.where((s) => !s).first;
    } catch (e) {
      error = tr('search_failed', {'e': '$e'});
    }
    if (link == LinkState.scanning) link = LinkState.disconnected;
    notifyListeners();
  }

  Future<void> stopScan() async {
    try {
      await FlutterBluePlus.stopScan();
    } catch (_) {}
    if (link == LinkState.scanning) {
      link = LinkState.disconnected;
      notifyListeners();
    }
  }

  Future<void> connect(BluetoothDevice device) async {
    error = null;
    await FlutterBluePlus.stopScan();
    link = LinkState.connecting;
    notifyListeners();
    try {
      await device.connect(timeout: const Duration(seconds: 15));
      final services = await device.discoverServices();
      final svc = services.firstWhere((s) => s.uuid == Guid(_serviceUuid));
      final tx = svc.characteristics.firstWhere((c) => c.uuid == Guid(_txUuid));
      _rx = svc.characteristics.firstWhere((c) => c.uuid == Guid(_rxUuid));
      _device = device;
      deviceName = device.platformName.isNotEmpty ? device.platformName : 'LED-Waage';

      await _dataSub?.cancel();
      _dataSub = tx.onValueReceived.listen(_onData);
      await tx.setNotifyValue(true);

      await _connSub?.cancel();
      _connSub = device.connectionState.listen((s) {
        if (s == BluetoothConnectionState.disconnected) _onDisconnected();
      });

      link = LinkState.connected;
      settings.clear();
      settingsLoaded = false;
      notifyListeners();
      await send('GET');
    } catch (e) {
      error = tr('conn_failed', {'e': '$e'});
      link = LinkState.disconnected;
      try {
        await device.disconnect();
      } catch (_) {}
      notifyListeners();
    }
  }

  Future<void> disconnect() async {
    await _device?.disconnect();
    _onDisconnected();
  }

  void _onDisconnected() {
    _dataSub?.cancel();
    _rx = null;
    _device = null;
    cal.reset();
    if (link != LinkState.disconnected) {
      link = LinkState.disconnected;
      notifyListeners();
    }
  }

  Future<void> send(String cmd) async {
    final rx = _rx;
    if (rx == null) return;
    try {
      await rx.write(utf8.encode(cmd));
    } catch (e) {
      error = tr('send_failed', {'e': '$e'});
      notifyListeners();
    }
  }

  // ---- incoming ----------------------------------------------------------

  void _onData(List<int> raw) {
    final line = utf8.decode(raw, allowMalformed: true).trim();
    if (line.isEmpty) return;
    final p = line.split(',');
    switch (p[0]) {
      case 'D':
        final d = LiveData.parse(p);
        if (d != null) {
          live = d;
          _scheduleNotify();
        }
        break;
      case 'B':
        if (p.length >= 3) {
          batteryPercent = int.tryParse(p[2])?.clamp(0, 100);
          _scheduleNotify();
        }
        break;
      case 'S':
        _onSetting(p);
        break;
      case 'C':
        _onCalibration(p);
        break;
    }
  }

  // Live data arrives at ~20 Hz; redraw at most every 33 ms.
  void _scheduleNotify() {
    _dirty = true;
    _notifyThrottle ??= Timer(const Duration(milliseconds: 33), () {
      _notifyThrottle = null;
      if (_dirty) {
        _dirty = false;
        notifyListeners();
      }
    });
  }

  void _onSetting(List<String> p) {
    if (p.length >= 2 && p[1] == 'END') {
      settingsLoaded = true;
      notifyListeners();
      return;
    }
    if (p.length < 6) return;
    final v = double.tryParse(p[2]), lo = double.tryParse(p[3]), hi = double.tryParse(p[4]);
    if (v == null || lo == null || hi == null) return;
    final item = SettingItem(p[1], v, lo, hi, p[5] == 'i');
    final i = settings.indexWhere((s) => s.key == item.key);
    if (i >= 0) {
      settings[i] = item;
    } else {
      settings.add(item);
    }
  }

  void _onCalibration(List<String> p) {
    if (p.length < 3) return;
    final step = int.tryParse(p[1]) ?? 0;
    switch (p[2]) {
      case 'START':
        cal.reset();
        cal.running = true;
        break;
      case 'WAIT':
        cal.running = true;
        cal.step = step;
        cal.phase = 'wait';
        cal.waitSeconds = p.length > 3 ? (double.tryParse(p[3]) ?? 5) : 5;
        cal.waitStart = DateTime.now();
        break;
      case 'MEAS':
        cal.step = step;
        cal.phase = 'meas';
        break;
      case 'DONE':
        cal.step = step;
        cal.phase = 'done';
        if (p.length > 3) cal.measured[step] = double.tryParse(p[3]) ?? 0;
        break;
      case 'NEXT':
        cal.step = step;
        cal.phase = 'next'; // step succeeded, waiting for the user's button
        break;
      case 'OK':
        cal.running = false;
        cal.phase = 'idle';
        cal.result = [for (var i = 3; i < 6 && i < p.length; i++) double.tryParse(p[i]) ?? 0];
        // offsets changed on the device, refresh the table
        send('GET');
        break;
      case 'ERR':
        cal.running = false;
        cal.phase = 'idle';
        cal.error = p.length > 3 ? trCalError(p.sublist(3).join(',')) : tr('unknown_error');
        break;
    }
    notifyListeners();
  }

  // ---- commands ------------------------------------------------------------

  void startCalibration() {
    cal.reset();
    cal.running = true;
    notifyListeners();
    send('CAL START');
  }

  void cancelCalibration() => send('CAL CANCEL');

  void nextCalibration() {
    cal.phase = 'wait'; // countdown message follows from the device
    cal.waitStart = null;
    notifyListeners();
    send('CAL NEXT');
  }

  /// Change a value locally and on the device (live, not yet stored in flash).
  void setSetting(SettingItem s, double v, {bool sendNow = true}) {
    s.value = s.isInt ? v.roundToDouble() : v;
    notifyListeners();
    if (sendNow) {
      send('SET ${s.key} ${s.isInt ? s.value.toInt() : s.value.toStringAsFixed(3)}');
    }
  }

  Future<void> saveSettings() => send('SAVE');

  Future<void> resetSettings() => send('RESET');

  Future<void> reloadSettings() {
    settingsLoaded = false;
    notifyListeners();
    return send('GET');
  }

  @override
  void dispose() {
    _scanSub?.cancel();
    _connSub?.cancel();
    _dataSub?.cancel();
    _notifyThrottle?.cancel();
    super.dispose();
  }
}
