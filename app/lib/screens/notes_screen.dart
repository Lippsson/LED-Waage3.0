import 'dart:convert';
import 'dart:typed_data';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';

import '../ble_service.dart';
import '../l10n.dart';
import '../notes_store.dart';

class NotesScreen extends StatelessWidget {
  final BleService ble;
  final NotesStore store;
  const NotesScreen({super.key, required this.ble, required this.store});

  Future<void> _download(BuildContext context) async {
    final messenger = ScaffoldMessenger.of(context);
    try {
      final text = store.exportText(tr('notes_title'), tr('notes_exported'));
      final uri = await FilePicker.saveFile(
        fileName: store.fileName,
        bytes: Uint8List.fromList(utf8.encode(text)),
        mimeType: 'text/plain',
        dialogTitle: tr('notes_download'),
      );
      if (uri != null) {
        messenger
          ..hideCurrentSnackBar()
          ..showSnackBar(SnackBar(content: Text(tr('notes_file_saved'))));
      }
    } catch (e) {
      messenger
        ..hideCurrentSnackBar()
        ..showSnackBar(SnackBar(content: Text(tr('notes_export_failed', {'e': '$e'}))));
    }
  }

  Future<void> _resetAll(BuildContext context) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: Text(tr('notes_reset_q')),
        content: Text(tr('notes_reset_body')),
        actions: [
          TextButton(onPressed: () => Navigator.pop(c, false), child: Text(tr('cal_cancel'))),
          FilledButton(onPressed: () => Navigator.pop(c, true), child: Text(tr('reset'))),
        ],
      ),
    );
    if (ok == true) store.clear();
  }

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: store,
      builder: (context, _) {
        final notes = store.notes;
        return ListView(
          padding: const EdgeInsets.fromLTRB(12, 12, 12, 24),
          children: [
            // the current corrected value, saved with the button below
            Card(
              child: Padding(
                padding: const EdgeInsets.all(14),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(tr('notes_current'), style: const TextStyle(fontSize: 12)),
                    Text('${ble.live.pitch.toStringAsFixed(2)} °',
                        style: const TextStyle(fontSize: 34, fontWeight: FontWeight.w700)),
                    const SizedBox(height: 8),
                    SizedBox(
                      width: double.infinity,
                      child: FilledButton.icon(
                        onPressed: () => store.add(ble.live.pitch),
                        icon: const Icon(Icons.add),
                        label: Text(tr('notes_new')),
                      ),
                    ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: notes.isEmpty ? null : () => _download(context),
                    icon: const Icon(Icons.download),
                    label: Text(tr('notes_download_short')),
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: notes.isEmpty ? null : () => _resetAll(context),
                    icon: const Icon(Icons.delete_sweep),
                    label: Text(tr('notes_reset_all')),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 8),
            if (notes.isEmpty)
              Padding(
                padding: const EdgeInsets.all(24),
                child: Center(child: Text(tr('notes_empty'), textAlign: TextAlign.center)),
              ),
            for (final n in notes) _NoteCard(key: ValueKey(n.id), note: n, ble: ble, store: store),
          ],
        );
      },
    );
  }
}

class _NoteCard extends StatefulWidget {
  final Note note;
  final BleService ble;
  final NotesStore store;
  const _NoteCard({super.key, required this.note, required this.ble, required this.store});

  @override
  State<_NoteCard> createState() => _NoteCardState();
}

class _NoteCardState extends State<_NoteCard> {
  late final TextEditingController _ctrl = TextEditingController(text: widget.note.text);

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final n = widget.note;
    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(14, 10, 6, 10),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text('${n.value.toStringAsFixed(2)} °',
                          style: const TextStyle(fontSize: 24, fontWeight: FontWeight.w700)),
                      Text(NotesStore.formatTime(n.time), style: const TextStyle(fontSize: 12)),
                    ],
                  ),
                ),
                IconButton(
                  tooltip: tr('notes_recapture'),
                  icon: const Icon(Icons.refresh),
                  onPressed: () => widget.store.recapture(n, widget.ble.live.pitch),
                ),
                IconButton(
                  tooltip: tr('notes_delete'),
                  icon: const Icon(Icons.delete_outline),
                  onPressed: () => widget.store.remove(n),
                ),
              ],
            ),
            Padding(
              padding: const EdgeInsets.only(right: 8, top: 4),
              child: TextField(
                controller: _ctrl,
                minLines: 1,
                maxLines: 5,
                textCapitalization: TextCapitalization.sentences,
                decoration: InputDecoration(
                  hintText: tr('notes_hint'),
                  border: const OutlineInputBorder(),
                  isDense: true,
                ),
                onChanged: (v) => widget.store.setText(n, v),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
