# Bluetooth-Protokoll (BLE)

Die Waage meldet sich als **`LED-Waage`**. Es gibt einen Dienst mit zwei Merkmalen (Characteristics). Alle Nachrichten sind UTF-8-Textzeilen. Die App fordert eine MTU von 247 an, die Zeilen sind kürzer als 100 Bytes.

| Rolle | UUID |
|---|---|
| Dienst | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX (App schreibt hierhin) | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX (Waage sendet per Notify) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

## Waage → App (TX, Notify)

| Zeile | Bedeutung |
|---|---|
| `D,ax,ay,az,gx,gy,gz,rawPitch,pitch,mode` | Live-Daten mit 25 Hz. Beschleunigung in g, Gyro in °/s, Neigung roh und kalibriert (gerundet je nach Einstellung). `mode`: 0 Rot/Blau, 1 Überblendung, 2 Libelle, 3 eingerastet, 9 Lichtschwert |
| `B,adc,percent` | Akkustand alle 2 s (ADC-Rohwert und Prozent) |
| `S,key,value,min,max,type` | Eine Zeile der Einstellungstabelle (Antwort auf `GET`). `type`: `f` Kommazahl, `i` Ganzzahl |
| `S,END` | Ende der Einstellungsliste |
| `C,step,PHASE[,werte]` | Fortschritt der Kalibrierung, siehe unten |

### Kalibrierung (`C`-Zeilen)

| Zeile | Bedeutung |
|---|---|
| `C,0,START` | Kalibrierung gestartet |
| `C,n,WAIT,sekunden` | Schritt `n` (1–5): Zeit zum Umlagern |
| `C,n,MEAS` | Messung läuft, nicht bewegen |
| `C,n,DONE,wert` | Messwert von Schritt `n` in Grad |
| `C,n,NEXT` | Schritt `n` erfolgreich, die Waage wartet auf `CAL NEXT` |
| `C,5,OK,offH,offVp,offVn` | Fertig, neue Offsets wurden gespeichert |
| `C,0,ERR,grund` | Abgebrochen, `grund` ist ein englischer Text |

## App → Waage (RX, Write)

| Befehl | Wirkung |
|---|---|
| `GET` | Einstellungstabelle senden |
| `SET key wert` | Wert ändern (sofort wirksam, noch nicht im Flash) |
| `SAVE` | Alle Einstellungen im Flash speichern |
| `RESET` | Standardwerte laden und speichern (die Kalibrier-Offsets bleiben) |
| `CAL START` | Kalibrierung starten (nur im Wasserwaagen-Modus) |
| `CAL NEXT` | Nächsten Kalibrierschritt starten |
| `CAL CANCEL` | Kalibrierung abbrechen |

## Einstellungen

Die Liste wird von der Waage selbst geliefert, die App baut ihre Oberfläche daraus. Stand dieser Version:

| Key | Typ | Bereich | Standard | Bedeutung |
|---|---|---|---|---|
| `offH` | f | −10 … 10 | −0,6 | Offset waagerecht (°) |
| `offVp` | f | −10 … 10 | 0,4 | Offset senkrecht, Ende A oben (°) |
| `offVn` | f | −10 … 10 | −0,4 | Offset senkrecht, Ende B oben (°) |
| `smooth` | f | 0,01 … 1 | 0,10 | Winkel-Glättung (gilt für 25 Messungen/s, wird mit `rate` umgerechnet) |
| `dlpf` | i | 0 … 6 | 3 | Sensor-Filter: 0 aus (260 Hz), 1 184, 2 94, 3 44, 4 21, 5 10, 6 5 Hz |
| `round` | i | 0 … 2 | 0 | Rundung der angezeigten Neigung: 0 keine, 1 auf 0,05°, 2 auf 0,1° |
| `bsmooth` | f | 0,01 … 1 | 0,15 | Glättung der Libelle |
| `brange` | f | 0,2 … 5 | 1,0 | Abweichung in °, ab der die Libelle erscheint |
| `bubbleLeds` | i | 2 … 40 | 12 | Breite der Libelle in LEDs |
| `travel` | i | 1 … 60 | 38 | Verfahrweg der Libelle in LEDs |
| `fadeMs` | i | 0 … 2000 | 300 | Dauer der Überblendung (ms) |
| `holdLevel` | i | 0 … 5000 | 1000 | Haltezeit „eingerastet“ (ms) |
| `bright` | i | 5 … 255 | 255 | LED-Helligkeit |
| `sound` | i | 0 … 1 | 1 | Signalton |
| `calWait` | i | 2000 … 30000 | 5000 | Wartezeit pro Kalibrierschritt (ms) |
| `axes` | i | 0 … 5 | 0 | Achsen-Zuordnung: 0 XYZ, 1 XZY, 2 YXZ, 3 YZX, 4 ZXY, 5 ZYX |
| `invX`, `invY`, `invZ` | i | 0 … 1 | 0 | Achse umkehren |
| `rate` | i | 5 … 100 | 25 | Messungen pro Sekunde für die Waagen-Logik (gemittelt) |
