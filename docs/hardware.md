# Hardware-Vorschlag

Ein GPSMAP 9000xsv ist vor allem **robuste Hardware**: sonnenlichttaugliches Display,
IPX7, 10–32 V, Betrieb von −15 bis +55 °C. Genau hier liegen die Schwachstellen eines
Selbstbaus – sie sind im Folgenden mitgedacht.

| Baustein | Vorschlag | Warum / Fallstrick |
|---|---|---|
| Rechner | Raspberry Pi 5 (4 GB) oder CM5 auf Trägerplatine | Pi 4 reicht für Rasterkarten; CM5 lässt sich besser abdichten |
| Display | Sonnenlichttauglich, **≥ 1000 cd/m²**, optisch gebondet, kapazitiver Touch mit Handschuh-/Nässe-Modus | Normale 300-cd/m²-Displays sind in der Sonne unlesbar; ohne Bonding beschlägt das Glas |
| Gehäuse | IP67, Aluminium als Kühlkörper, Druckausgleichselement, seewasserfeste Stecker (M12) | Kondenswasser ist der häufigste Ausfallgrund |
| Stromversorgung | 9–32 V DC-DC-Wandler (isoliert), Verpolungsschutz, Sicherung, Unterspannungsabschaltung, **USV/Supercap für geordnetes Herunterfahren** | Motorstart lässt die Bordspannung einbrechen; hartes Abschalten zerstört SD-Karten |
| Speicher | NVMe/eMMC statt SD-Karte, **schreibgeschütztes Root-Dateisystem (overlayfs)** | SD-Karten sterben an Stromausfällen |
| NMEA 2000 | CAN-HAT mit **galvanischer Trennung** (z. B. MCP2518FD isoliert), Micro-C-Stecker | Masseschleifen zwischen Bordnetz und N2K; Busteilnehmer zählen als LEN-Last |
| NMEA 0183 | RS-422-Empfänger (isoliert) oder USB-Adapter; 4800 Bd (Standard) / 38400 Bd (AIS) | 0183 ist differentiell – nicht einfach an RS-232 hängen |
| GNSS | Eigener Empfänger mit Außenantenne (NMEA 2000 oder 0183), 5–10 Hz | Interne Antennen unter Deck sind unbrauchbar |
| AIS | Empfänger oder Class-B-Transponder mit NMEA-Ausgang | Transponder braucht eigene Antenne/Splitter |
| Alarm | **Summer über GPIO** (Treiberstufe) zusätzlich zur Anzeige | Wer unter Deck schläft, sieht kein blinkendes Banner |

## Inbetriebnahme NMEA 2000 (SocketCAN)

```bash
sudo ip link set can0 up type can bitrate 250000 listen-only on
```

`listen-only on` stellt sicher, dass der Controller nicht einmal Acknowledge-Bits sendet –
passend zu [ADR 0002](adr/0002-nmea2000-listen-only.md).
