# Roadmap

Reihenfolge nach Nutzen für die Sicherheit an Bord, nicht nach Aufwand.

## v0.2 – sicher benutzbar
- ~~Akustischer Alarm~~ ✅ (Lautsprecher über Qt Multimedia, GPIO-Summer über `buzzer` in der Konfiguration)
- Einstellungsseite: Alarmgrenzen, Ankerradius, Tiefenoffset, Einheiten (kn/km/h, m/ft)
- Missweisung aus Weltmagnetfeldmodell (WMM, aus OpenEFIS übernehmbar), wenn kein Gerät sie liefert
- Overzoom (Kacheln der höchsten Stufe vergrößern) und Kursoben-Darstellung (course-up)
- Logbuch/Track-Aufzeichnung (GPX), NMEA-Mitschnitt für Replay
- Raspberry-Pi-Image mit schreibgeschütztem Root-Dateisystem (wie OpenEFIS `image/`)

## v0.3 – Navigation
- Wegpunkte, Routen, Go-To mit XTE, BTW/DTW, ETA; MOB-Taste
- AIS-Zielliste und Detailansicht, AIS-SART/MOB-Erkennung (MMSI 970/972/974)
- Ausgabe von RMB/APB (NMEA 0183) für Autopiloten

## v0.4 – Vektorkarten
- S-57 / Inland ENC lesen (GDAL) und in eigenes Kachel- oder Geometrieformat konvertieren
- Darstellung nach S-52-Grundzügen: Tiefenflächen (DEPARE), Sicherheitskontur, Tonnen, Fahrrinnen
- Tiefenschattierung aus EMODnet

## später
- NMEA 2000 senden mit Address Claim (ISO 11783-5) – erst mit Tests gegen echte Geräte ([ADR 0002](adr/0002-nmea2000-listen-only.md))
- Motordaten-Seite (PGN 127488/127489), Tankstände, Batterie
- Signal K als zusätzliche Quelle
- Sonar/Radar über offene Hardware ([ADR 0004](adr/0004-sonar-radar.md))
- Mehrere Displays (Datenbus über Netzwerk)
