import 'package:flutter/material.dart';

import '../ble_service.dart';
import '../l10n.dart';
import '../widgets/about_button.dart';
import '../widgets/language_button.dart';

class ConnectScreen extends StatelessWidget {
  final BleService ble;
  const ConnectScreen({super.key, required this.ble});

  @override
  Widget build(BuildContext context) {
    final scanning = ble.link == LinkState.scanning;
    final connecting = ble.link == LinkState.connecting;
    return Scaffold(
      appBar: AppBar(
        title: Text(tr('connect_title')),
        actions: const [LanguageButton(), AboutButton()],
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Text(tr('connect_hint')),
          const SizedBox(height: 16),
          if (scanning)
            FilledButton.tonalIcon(
              onPressed: ble.stopScan,
              icon: const Icon(Icons.stop),
              label: Text(tr('stop_search')),
            )
          else
            FilledButton.icon(
              onPressed: connecting ? null : ble.startScan,
              icon: connecting
                  ? const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2))
                  : const Icon(Icons.bluetooth_searching),
              label: Text(connecting ? tr('connecting') : tr('search')),
            ),
          if (scanning)
            const Padding(padding: EdgeInsets.only(top: 12), child: LinearProgressIndicator()),
          if (ble.error != null)
            Padding(
              padding: const EdgeInsets.only(top: 16),
              child: Text(ble.error!, style: TextStyle(color: Theme.of(context).colorScheme.error)),
            ),
          const SizedBox(height: 16),
          for (final r in ble.found)
            Card(
              child: ListTile(
                leading: const Icon(Icons.straighten),
                title: Text(r.device.platformName.isEmpty ? 'LED-Waage' : r.device.platformName),
                subtitle: Text('${r.device.remoteId}   ·   ${r.rssi} dBm'),
                trailing: const Icon(Icons.chevron_right),
                onTap: connecting ? null : () => ble.connect(r.device),
              ),
            ),
          if (!scanning && !connecting && ble.found.isEmpty && ble.error == null)
            Padding(
              padding: const EdgeInsets.only(top: 24),
              child: Center(child: Text(tr('no_devices'))),
            ),
        ],
      ),
    );
  }
}
