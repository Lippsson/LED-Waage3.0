# LED-Waage 3.0

Digitale LED-Wasserwaage mit ESP32, MPU6050 und zwei Streifen WS2812-LEDs, jetzt mit **Bluetooth-App für Android**, optionalem **OLED-Display** und vielen einstellbaren Werten.

> **Herkunft:** Dieses Projekt ist eine Weiterentwicklung der **LED-Wasserwaage 2.0** aus dem Make Magazin
> ([MakeMagazinDE/LED-Waage2.0](https://github.com/MakeMagazinDE/LED-Waage2.0), Make 2/26, ab Seite 116).
> Hardware, Grundidee und die Umschlagmessung bei der Kalibrierung stammen von dort. Dieses Repository ist ein Fork davon.
> Der ursprüngliche Sketch liegt unverändert als `LED_Waage_20.ino` im Hauptordner, die neue Firmware in `LED_Waage_30/`.

## Inhalt des Repositorys

| Ordner / Datei | Inhalt |
|---|---|
| `LED_Waage_30/` | Neue Firmware (Arduino-Sketch, mehrere Tabs) |
| `app/` | Android-App (Flutter) |
| `docs/PROTOKOLL.md` | Beschreibung der Bluetooth-Schnittstelle zwischen App und Waage |
| `LED_Waage_20.ino`, `*.png`, `doc/` | Unveränderte Dateien des Originals (Make Magazin) |

## Was ist neu gegenüber 2.0?

**Firmware (`LED_Waage_30/`)**
- Anpassung an **2 × 44 LEDs** (88 insgesamt), Libelle mit Torpfosten in der Mitte.
- Libelle **weiß** bei waagerecht, **blau** bei senkrecht. Die zwei Pfosten leuchten **grün** (exakt im Zentrum), **gelb** (eine LED daneben) oder **rot**.
- **Akkuanzeige** nur auf den zwei äußersten LEDs: 0–10 % rot blinkend, 10–20 % rot, 20–40 % gelb, 40–60 % orange, 60–100 % grün.
- Der Schlafmodus ist abgeschaltet, die 45°-Anzeige wurde entfernt: angezeigt werden nur waagerecht und senkrecht.
- **Bluetooth Low Energy:** Live-Daten (Beschleunigung, Gyro, Neigung), Fernsteuerung der Kalibrierung, Einstellungen lesen und schreiben.
- Alle wichtigen Werte sind **zur Laufzeit einstellbar und im Flash gespeichert**: Glättung, Messrate, Libellen-Größe, Helligkeit, Signalton, Wartezeit der Kalibrierung, Rundung, Filter des Sensors.
- **Sensor-Montage einstellbar:** Achsen vertauschen und umkehren, falls der Sensor anders eingebaut ist.
- **Sensor-Filter** (digitaler Tiefpass des MPU6050) wählbar, Mittelung der Messwerte, Glättung unabhängig von der Messrate.
- Optionales **SH1106-OLED (128×64, I2C)** zeigt die kalibrierte Neigung groß an. Es wird ohne zusätzliche Bibliothek angesteuert.
- Kalibrierung in 5 Schritten wahlweise **mit Weiter-Button in der App**.

**App (`app/`)**
- Live-3D-Ansicht (Lage und X/Y/Z-Vektor), Einfach- und Expert-Modus, Libelle im Stil einer Röhrenlibelle.
- Geführte Kalibrierung, alle Einstellungen per Regler oder Zahleneingabe.
- Reiter **Notes**: aktuellen korrigierten Wert mit Notiz speichern, als Textdatei exportieren, alles zurücksetzen.
- Sprachen: Deutsch, Englisch, Schwedisch.
- Keine Internet-Berechtigung, keine Datensammlung.

## Hardware

Getestet mit einem **LOLIN32 Lite**. Pinbelegung wie im Sketch:

| Funktion | Pin |
|---|---|
| LED-Streifen links (Daten) | GPIO 5 |
| LED-Streifen rechts (Daten) | GPIO 18 |
| LED-Masse schalten (Transistor BC337 über 1 kΩ) | GPIO 27 |
| Summer | GPIO 25 |
| Akku-Messung (ADC) | GPIO 36 |
| MPU6050-Interrupt | GPIO 13 |
| I2C SDA / SCL (MPU6050 und OLED) | GPIO 15 / GPIO 4 |

MPU6050 hat die I2C-Adresse `0x68`, das OLED `0x3C` oder `0x3D`. Beide hängen am selben Bus (3,3 V).
Den Schaltplan und die Hardware-Änderungen für den Betrieb findest du im Artikel der Make 2/26 und im Original-Repository.

## Firmware flashen

1. Arduino IDE 2.x installieren, in den Boards-Manager das Paket **esp32** (Espressif, getestet mit 3.3.8) installieren.
2. Bibliotheken installieren: **Adafruit NeoPixel** und **MPU6050_tockn**. BLE und Preferences gehören zum ESP32-Paket.
3. Den Ordner `LED_Waage_30/` öffnen (Datei `LED_Waage_30.ino`), Board **WEMOS LOLIN32 Lite** wählen.
4. Der Sketch belegt etwa 90 % des Programmspeichers. Falls du mehr Platz brauchst: *Tools → Partition Scheme → Huge APP*.
5. Hochladen.

Beim ersten Start sind die Standardwerte aktiv. Danach in der App die **Kalibrierung** durchführen. Nach einer Änderung der Achsen-Zuordnung muss neu kalibriert werden.

## App bauen

Voraussetzungen: Flutter SDK (getestet mit 3.47) und das Android SDK (Plattform 37).

```bash
cd app
flutter pub get
flutter build apk --release
```

Die APK liegt danach unter `app/build/app/outputs/flutter-apk/app-release.apk`. Die App ist mit dem Debug-Schlüssel signiert, für den Play Store braucht es einen eigenen Schlüssel.

## Bluetooth-Schnittstelle

Die Waage meldet sich per BLE als **„LED-Waage“**. Das Protokoll ist in [docs/PROTOKOLL.md](docs/PROTOKOLL.md) beschrieben, sodass man eigene Apps oder Skripte schreiben kann.

## Lizenz und Rechte

- **App (`app/`)**: [MIT-Lizenz](app/LICENSE).
- **Firmware (`LED_Waage_30/`)**: baut auf dem Sketch aus dem Make Magazin auf. Das Original-Repository enthält **keine Lizenzdatei**, die Rechte daran liegen beim Urheber (Maker Media GmbH / Make Magazin). Die Firmware wird hier deshalb ohne eigene Lizenzzusage veröffentlicht, die neuen Teile sind private Hobby-Erweiterungen. Sollte der Urheber eine Lizenz festlegen oder die Veröffentlichung beanstanden, wird dieses Repository angepasst.
- Verwendete Bibliotheken behalten ihre eigenen Lizenzen (Adafruit NeoPixel: LGPL, ESP32-Kern: LGPL, Flutter und die App-Pakete: BSD/MIT). In der App stehen sie unter *Info → Open-Source-Lizenzen*.

Dies ist ein privates Hobbyprojekt, die Nutzung erfolgt auf eigene Gefahr und ohne Gewähr.
Rückfragen bitte über die [Issues](https://github.com/Lippsson/LED-Waage3.0/issues) dieses Repositorys.
