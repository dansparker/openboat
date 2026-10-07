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

Die Screenshots erzeugt die CI bei jedem Lauf (Artefakt „screenshots“).

## Vorbild und Stand

| GPSMAP 9000xsv | OpenBoat | Stand |
|---|---|---|
| Kartenplotter (BlueChart g3 / Navionics) | Rasterkarten aus MBTiles: OpenSeaMap-Seezeichen über einer Basiskarte, Eigenschiff, COG-Vektor, Kursstrich | ✅ v0.1 |
| GPS / Kompass / Log / Echolot / Wind | NMEA 0183 (UDP, TCP, seriell, Logdatei) und NMEA 2000 (SocketCAN, candump) | ✅ v0.1 (ungetestet an Hardware) |
| Datenleiste | SOG, COG, Steuerkurs, Fahrt durchs Wasser, Tiefe, scheinbarer/wahrer Wind, Wassertemperatur | ✅ v0.1 |
| AIS | Ziele aus AIVDM (Typ 1/2/3/5/18/19/24) und NMEA 2000 (129038/129039), CPA/TCPA, Kollisionswarnung | ✅ v0.1 |
| Alarme | Ankerwache, Flachwasser, AIS-Kollision, GNSS-Ausfall, Tiefenausfall; Quittierung | ✅ v0.1 (optisch; akustisch → Roadmap) |
| Nachtmodus | Rote, abgedunkelte Darstellung | ✅ v0.1 |
| Simulator | Boot auf dem Attersee mit AIS-Ziel | ✅ v0.1 |
| Vektorkarten (S-57 / Inland ENC) | Darstellung nach S-52-Grundzügen | 🔜 [Roadmap](docs/roadmap.md) |
| Wegpunkte, Routen, Track, Go-To | | 🔜 |
| Autopilot-Anbindung | | 🔜 |
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
│   ├── nav/         Wahrer Wind, AIS-Zieltabelle (CPA/TCPA), Alarme
│   └── sim/         Simulator
└── app/             Qt/QML-Anwendung: Karte, Datenleiste, Alarme
tests/               GoogleTest-Unit-Tests
tools/               fetch_tiles.py (Offline-Karten als MBTiles)
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
