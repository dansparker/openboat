# OpenBoat

Modularer Open-Source-Kartenplotter / Multifunktionsdisplay für Boote –
funktional angelehnt an Geräte wie den Garmin GPSMAP 9000xsv. Schwesterprojekt von
[OpenEFIS](https://github.com/dansparker/open-efis), gleiche Architektur.

**C++20 · Qt 6 / QML · CMake · Raspberry Pi / Linux-SBC · NMEA 0183 · NMEA 2000**

> ⚠️ **Sicherheitshinweis:** Experimentell, **nicht zugelassen**, kein Ersatz für amtliche
> Seekarten, ECDIS oder ordentliche Seemannschaft. Die freien Karten können veraltet, lückenhaft
> oder falsch sein. Immer eine unabhängige Navigationsmöglichkeit an Bord haben.

![OpenBoat – Simulator, Tagmodus](docs/screenshots/day.png)

<details><summary>Nachtmodus</summary>

![OpenBoat – Nachtmodus](docs/screenshots/night.png)
</details>

<details><summary>Kurs oben</summary>

![OpenBoat – Kurs oben](docs/screenshots/courseup.png)
</details>

<details><summary>AIS-Zielliste</summary>

![OpenBoat – AIS-Zielliste](docs/screenshots/ais.png)
</details>

<details><summary>Einstellungen</summary>

![OpenBoat – Einstellungen](docs/screenshots/settings.png)
</details>

Simulator mit Demo-Route (`--demo`), Grundkarte aus OpenStreetMap (`tools/make_basemap.py`), OpenSeaMap-Seezeichen und **synthetischen** Tiefenlinien (nur Demo, keine echten Tiefen des Attersees). Die Screenshots erzeugt die CI bei jedem Lauf (Artefakt „screenshots“).

## Vorbild und Stand

| GPSMAP 9000xsv | OpenBoat | Stand |
|---|---|---|
| Kartenplotter (BlueChart g3 / Navionics) | Rasterkarten aus MBTiles: **selbst gerenderte Grundkarte aus OpenStreetMap** (`tools/make_basemap.py`) + OpenSeaMap-Seezeichen, Eigenschiff, COG-Vektor, Kursstrich | ✅ v0.2 |
| GPS / Kompass / Log / Echolot / Wind | NMEA 0183 (UDP, TCP, seriell, Logdatei) und NMEA 2000 (SocketCAN, candump) | ✅ v0.1 (ungetestet an Hardware) |
| Datenleiste | SOG, COG, Steuerkurs, Fahrt durchs Wasser, Tiefe, scheinbarer/wahrer Wind, Wassertemperatur | ✅ v0.1 |
| AIS | Ziele aus AIVDM (Typ 1/2/3/4/5/18/19/21/24) und NMEA 2000 (129038/129039/129041/129793), CPA/TCPA, Kollisionswarnung | ✅ v0.1 |
| AIS-Seezeichen und Basisstationen | Eigene Symbole (Raute, virtuell gestrichelt; Quadrat), Seezeichenart, Warnung bei vertriebener Tonne in der Nähe | ✅ v0.2 |
| AIS-Sicherheitsmeldungen | Typ 12/14 und PGN 129801/129802 in der AIS-Liste; Warnung, von Notsendern Alarm, Testmeldungen ohne Alarm | ✅ v0.2 |
| AIS-Zielliste | Notsender zuerst, dann Kollisionsgefahr, dann Distanz; Kursverlauf der letzten 10 min auf der Karte; Details (Rufzeichen, Typ, Maße, Status), „Auf Karte“, Ziel auf der Karte antippen | ✅ v0.2 |
| AIS-SART / MOB / EPIRB | Erkennung an der MMSI (970/972/974), eigenes Kartensymbol, Alarm unabhängig vom CPA; Testaussendungen (Status 15) ohne Alarm | ✅ v0.2 |
| Missweisung | Vom Gerät (RMC, HDG, PGN 127250/127258), sonst aus dem World Magnetic Model (WMM2025, mitgeliefert, gültig bis Ende 2029) | ✅ v0.2 |
| Alarme | Ankerwache, Flachwasser, AIS-Kollision, GNSS-Ausfall, Tiefenausfall; Quittierung; **Ton über Lautsprecher und GPIO-Summer** (Alarm: Dauerpiepen, Warnung: Doppelpiep), Summer-Selbsttest beim Start | ✅ v0.2 |
| Nachtmodus | Rote, abgedunkelte Darstellung | ✅ v0.1 |
| Simulator | Boot auf dem Attersee mit magnetischem Kompass, AIS-Zielen und MOB-Sender im Testmodus | ✅ v0.1 |
| Tiefenlinien | `tools/make_depth.py`: Tiefenlinien, Tiefenzahlen, Flachwasser-Schattierung aus EMODnet/GEBCO-Rastern oder S-57-Tiefenlinien (über GDAL); Sicherheitstiefe in der App einstellbar (dicke Sicherheitslinie, wirkt sofort) | ✅ v0.2 |
| Kurs oben / Nord oben | Kartendrehung mit Hysterese, Nordpfeil zum Umschalten; Ortsnamen und Tiefenzahlen bleiben aufrecht (abschaltbar) | ✅ v0.2 |
| Vektorkarten (S-57 / Inland ENC) | `tools/make_enc.py` (GDAL): Tiefenflächen und -linien mit Sicherheitstiefe in der App, Tonnen/Baken/Feuer mit vereinfachten S-52-Symbolen (Form, Farben, Kardinal-Toppzeichen), **Feuersektoren** mit Kennung (z. B. „Oc R 4s 3M“), Fahrwasser, Leit-/Peillinien, Lotungen und Namen; **Brücken, Freileitungen, Rohrbrücken mit Durchfahrtshöhe** | ✅ v0.4 (vereinfachte Symbolik, kein zertifiziertes ECDIS) |
| Wegpunkte, Routen, Go-To, MOB | Wegpunkt per Langdruck auf die Karte, Routen-Editor, automatischer Wegpunktwechsel, XTE/BTW/DTW/TTG, Ankunftsalarm, MOB-Taste; Speicherung als GPX | ✅ v0.2 |
| Routen bearbeiten, GPX über USB, Routenprüfung | Punkte ziehen/einfügen/löschen; GPX-Import/-Export über USB-Stick; Prüfung jeder Strecke gegen die Sicherheitstiefe der Tiefenkarte (flache Stellen rot markiert, Strecken ohne Tiefendaten gemeldet) und gegen die **Durchfahrtshöhe des Boots** (zu niedrige Brücken/Leitungen rot, auch auf der Karte) | ✅ v0.3 |
| Track | Aufzeichnung mit Sprungfilter (vor Anker keine Punktwolke), Tagesdateien nur durch Anhängen (stromausfallsicher), Zeit aus dem GNSS, GPX-Export, Anzeige auf der Karte, frühere Tage einblendbar | ✅ v0.2 |
| Logbuch | Ein-Tipp-Einträge (Abgelegt, Anker, Reff, …) mit Zeit, Position, SOG/COG, Tiefe und Wind; Tageswerte aus dem Track; CSV-Export auf USB | ✅ v0.3 |
| NMEA-Mitschnitt | Rohdaten von NMEA 0183 (mit Zeit-Tag-Block) und NMEA 2000 (candump) pro Tag, Wiedergabe im Originaltakt | ✅ v0.3 |
| Einstellungen | Einheiten (kn/km/h, m/ft, sm/km), Alarmgrenzen, Anker-/Ankunftsradius, Kursvektor, Track, Tiefenoffset (vom Geber/manuell, Bezug wird angezeigt), Overzoom mit Warnhinweis; wirken sofort, atomar gespeichert | ✅ v0.2 |
| Autopilot-Anbindung | NMEA 0183 RMB/APB/XTE über UDP oder seriell (`autopilot_output`) | ✅ v0.2 (ungetestet am Autopiloten) |
| Echolot-Bild (CHIRP, ClearVü/SideVü) | Nur über offene Sonar-Hardware möglich, siehe [ADR 0004](docs/adr/0004-sonar-radar.md) | 🔬 Recherche |
| Radar | Nur Geräte mit offengelegtem/reverse-engineertem Protokoll | 🔬 Recherche |

## Architektur in einem Satz

Alle Module (Datenquellen, Simulator, Navigation/Alarme, UI) kommunizieren **ausschließlich
über einen typsicheren Datenbus** – jede Quelle ist austauschbar, ohne die Anzeige anzufassen.
Details: [docs/architecture.md](docs/architecture.md).

```
src/
├── core/            Qt-freier Kern: DataBus, Datentypen, Navigationsmathematik
├── hal/             UDP, TCP, seriell, SocketCAN, candump-Wiedergabe
├── modules/
│   ├── nmea0183/    Satz-Parser, AIS-Decoder, Datenquelle
│   ├── nmea2000/    PGN-Decoder, Fast-Packet, Datenquelle (nur Empfang)
│   ├── nav/         Wahrer Wind, AIS-Zieltabelle (CPA/TCPA), Alarme, Missweisung (WMM)
│   └── sim/         Simulator
└── app/             Qt/QML-Anwendung: Karte, Datenleiste, Alarme
tests/               GoogleTest-Unit-Tests
tools/               make_basemap.py (Grundkarte aus OSM), make_depth.py (Tiefen), make_enc.py (S-57/Inland ENC), fetch_tiles.py (Overlays)
config/              Beispielkonfiguration
docs/                Architektur, Karten, Hardware, Roadmap, ADRs
```

## Bauen

```bash
# Nur Kern + Tests (kein Qt nötig)
cmake --preset core-only && cmake --build --preset core-only && ctest --preset core-only

# Mit Anwendung (Qt ≥ 6.5 mit Quick und Sql)
cmake --preset release && cmake --build --preset release
./build/release/src/app/openboat --config config/boat.example.json
```

Ohne Karten startet die Anwendung mit dem Simulator auf leerem Hintergrund.
Karten besorgen: [docs/charts.md](docs/charts.md).

## Konfiguration

`config/boat.example.json` kopieren und anpassen: Datenquellen (`sources`), Karten
(`charts`, erste = Basiskarte, weitere = Overlays), Tiefenoffset und Alarmgrenzen.
Relative Pfade gelten relativ zur Konfigurationsdatei.

## Dokumentation

- [Architektur](docs/architecture.md)
- [Freie Karten: Quellen, Lizenzen, Grenzen](docs/charts.md)
- [Hardware-Vorschlag](docs/hardware.md)
- [Roadmap](docs/roadmap.md)
- ADRs: [0001 Tech-Stack](docs/adr/0001-tech-stack.md) ·
  [0002 NMEA 2000 nur Empfang](docs/adr/0002-nmea2000-listen-only.md) ·
  [0003 Karten](docs/adr/0003-charts.md) · [0004 Sonar/Radar](docs/adr/0004-sonar-radar.md)

## Lizenz

MIT – siehe [LICENSE](LICENSE). Kartendaten haben eigene Lizenzen (OpenStreetMap/OpenSeaMap:
ODbL bzw. CC BY-SA) und verlangen Namensnennung; die Anwendung blendet sie ein.
