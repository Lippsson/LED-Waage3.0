import 'package:flutter/material.dart';

import 'ble_service.dart';
import 'l10n.dart';
import 'notes_store.dart';
import 'widgets/about_button.dart';
import 'widgets/battery_indicator.dart';
import 'widgets/language_button.dart';
import 'screens/calibration_screen.dart';
import 'screens/connect_screen.dart';
import 'screens/live_screen.dart';
import 'screens/notes_screen.dart';
import 'screens/settings_screen.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await initLanguage();
  runApp(const LedWaageApp());
}

class LedWaageApp extends StatelessWidget {
  const LedWaageApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      onGenerateTitle: (_) => tr('app_name'),
      debugShowCheckedModeBanner: false,
      theme: ThemeData(colorSchemeSeed: const Color(0xFF3E63DD), useMaterial3: true),
      darkTheme: ThemeData(
        colorSchemeSeed: const Color(0xFF3E63DD),
        brightness: Brightness.dark,
        useMaterial3: true,
      ),
      home: const Root(),
    );
  }
}

class Root extends StatefulWidget {
  const Root({super.key});

  @override
  State<Root> createState() => _RootState();
}

class _RootState extends State<Root> {
  final ble = BleService();
  final notes = NotesStore();
  int tab = 0;

  @override
  void initState() {
    super.initState();
    notes.load();
  }

  @override
  void dispose() {
    ble.dispose();
    notes.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: Listenable.merge([ble, appLang]),
      builder: (context, _) {
        if (!ble.connected) return ConnectScreen(ble: ble);

        final pages = [
          LiveScreen(ble: ble),
          CalibrationScreen(ble: ble),
          SettingsScreen(ble: ble),
          NotesScreen(ble: ble, store: notes),
        ];
        return Scaffold(
          appBar: AppBar(
            title: Text(tr('app_name')),
            actions: [
              if (ble.batteryPercent != null)
                Padding(
                  padding: const EdgeInsets.only(right: 4),
                  child: Center(child: BatteryIndicator(percent: ble.batteryPercent!)),
                ),
              const LanguageButton(),
              const AboutButton(),
              IconButton(
                tooltip: tr('disconnect'),
                icon: const Icon(Icons.bluetooth_disabled),
                onPressed: ble.disconnect,
              ),
            ],
          ),
          body: SafeArea(child: pages[tab]),
          bottomNavigationBar: NavigationBar(
            selectedIndex: tab,
            onDestinationSelected: (i) => setState(() => tab = i),
            destinations: [
              NavigationDestination(icon: const Icon(Icons.view_in_ar), label: tr('nav_live')),
              NavigationDestination(icon: const Icon(Icons.tune), label: tr('nav_cal')),
              NavigationDestination(icon: const Icon(Icons.settings), label: tr('nav_settings')),
              NavigationDestination(icon: const Icon(Icons.edit_note), label: tr('nav_notes')),
            ],
          ),
        );
      },
    );
  }
}
