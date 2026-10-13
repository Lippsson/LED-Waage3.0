import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// One saved measurement: the corrected tilt at the moment of saving plus a free text.
class Note {
  final String id;
  DateTime time;
  double value; // calibrated tilt in degrees, as shown in the app
  String text;

  Note({required this.id, required this.time, required this.value, this.text = ''});

  Map<String, dynamic> toJson() =>
      {'id': id, 'time': time.toIso8601String(), 'value': value, 'text': text};

  static Note? fromJson(Object? j) {
    if (j is! Map) return null;
    final time = DateTime.tryParse('${j['time']}');
    final value = (j['value'] as num?)?.toDouble();
    if (time == null || value == null) return null;
    return Note(id: '${j['id']}', time: time, value: value, text: '${j['text'] ?? ''}');
  }
}

/// Notes live on the phone (shared_preferences), newest first.
class NotesStore extends ChangeNotifier {
  static const _key = 'notes';
  final List<Note> notes = [];
  bool loaded = false;

  Future<void> load() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final raw = prefs.getString(_key);
      if (raw != null) {
        final list = jsonDecode(raw);
        if (list is List) {
          notes
            ..clear()
            ..addAll(list.map(Note.fromJson).whereType<Note>());
        }
      }
    } catch (_) {}
    loaded = true;
    notifyListeners();
  }

  Future<void> _save() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString(_key, jsonEncode([for (final n in notes) n.toJson()]));
    } catch (_) {}
  }

  /// Stores [value] as a new entry on top of the list and returns it.
  Note add(double value) {
    final n = Note(id: DateTime.now().microsecondsSinceEpoch.toString(), time: DateTime.now(), value: value);
    notes.insert(0, n);
    notifyListeners();
    _save();
    return n;
  }

  void setText(Note n, String text) {
    n.text = text;
    _save(); // no rebuild needed, the text field already shows it
  }

  void recapture(Note n, double value) {
    n.value = value;
    n.time = DateTime.now();
    notifyListeners();
    _save();
  }

  void remove(Note n) {
    notes.remove(n);
    notifyListeners();
    _save();
  }

  void clear() {
    notes.clear();
    notifyListeners();
    _save();
  }

  static String _two(int v) => v.toString().padLeft(2, '0');

  static String formatTime(DateTime t) =>
      '${t.year}-${_two(t.month)}-${_two(t.day)} ${_two(t.hour)}:${_two(t.minute)}:${_two(t.second)}';

  /// Plain text for the export file, oldest entry first.
  String exportText(String title, String exportedLabel) {
    final b = StringBuffer()
      ..writeln(title)
      ..writeln('$exportedLabel: ${formatTime(DateTime.now())}')
      ..writeln();
    final ordered = notes.reversed.toList();
    for (var i = 0; i < ordered.length; i++) {
      final n = ordered[i];
      b.writeln('${i + 1}) ${formatTime(n.time)}   ${n.value.toStringAsFixed(2)}°');
      if (n.text.trim().isNotEmpty) {
        for (final line in n.text.trim().split('\n')) {
          b.writeln('   $line');
        }
      }
      b.writeln();
    }
    return b.toString();
  }

  String get fileName {
    final t = DateTime.now();
    return 'led-waage-notes-${t.year}${_two(t.month)}${_two(t.day)}-${_two(t.hour)}${_two(t.minute)}.txt';
  }
}
