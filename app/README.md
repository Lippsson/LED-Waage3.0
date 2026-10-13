# LED-Waage App

Android-App (Flutter) für die LED-Waage 3.0. Sie verbindet sich per Bluetooth Low Energy mit der Waage und zeigt die Neigung live in 3D, führt durch die Kalibrierung, ändert alle Einstellungen und speichert Messwerte mit Notizen.

Bauen und weitere Hinweise: siehe das [Haupt-README](../README.md). Das Bluetooth-Protokoll steht in [../docs/PROTOKOLL.md](../docs/PROTOKOLL.md).

```bash
flutter pub get
flutter build apk --release
```

Lizenz: [MIT](LICENSE).

## Aufbau

| Datei | Aufgabe |
|---|---|
| `lib/ble_service.dart` | Suche, Verbindung und Protokoll |
| `lib/models.dart` | Datenklassen (Live-Daten, Einstellungen, Kalibrierstand) |
| `lib/notes_store.dart` | Notizen (lokal gespeichert) |
| `lib/l10n.dart` | Texte in Deutsch, Englisch, Schwedisch |
| `lib/screens/` | Verbinden, Live, Kalibrieren, Einstellungen, Notes, Info |
| `lib/widgets/` | 3D-Ansicht, Libelle, Akkuanzeige |
| `lib/legal.dart` | Links zum Projekt und optionaler Impressumstext |
