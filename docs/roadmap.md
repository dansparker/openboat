# Roadmap

Reihenfolge nach Nutzen für die Sicherheit an Bord, nicht nach Aufwand.

## v0.2 – sicher benutzbar
- ~~Akustischer Alarm~~ ✅ (Lautsprecher über Qt Multimedia, GPIO-Summer über `buzzer` in der Konfiguration)
- ~~Einstellungsseite~~ ✅ inkl. Tiefenoffset (vom Geber / manuell, wirkt sofort)
- ~~Missweisung aus Weltmagnetfeldmodell (WMM), wenn kein Gerät sie liefert~~ ✅ (WMM2025 bis Ende 2029; danach neue WMM.COF von NOAA einspielen)
- ~~Overzoom~~ ✅ (mit Warnhinweis, abschaltbar), ~~Kursoben-Darstellung~~ ✅
- ~~Track-Aufzeichnung (GPX)~~ ✅, ~~Tracks früherer Tage~~ ✅; offen: Logbuch-Ansicht, NMEA-Mitschnitt für Replay
- Raspberry-Pi-Image mit schreibgeschütztem Root-Dateisystem (wie OpenEFIS `image/`)

## v0.3 – Navigation
- ~~Wegpunkte, Routen, Go-To mit XTE, BTW/DTW, ETA; MOB-Taste~~ ✅
- Routen bearbeiten (Punkte verschieben/einfügen), GPX-Import/-Export über USB-Stick
- ~~AIS-Zielliste und Detailansicht, AIS-SART/MOB-Erkennung (MMSI 970/972/974)~~ ✅; offen: Sicherheitsmeldungen (Typ 14, z. B. „SART ACTIVE“), Basisstationen/AtoN (Typ 4/21)
- ~~Ausgabe von RMB/APB (NMEA 0183) für Autopiloten~~ ✅ (Test an echten Autopiloten offen)
- Routenprüfung gegen Untiefen (braucht Tiefendaten, v0.4)

## v0.4 – Vektorkarten
- S-57 / Inland ENC lesen (GDAL) und in eigenes Kachel- oder Geometrieformat konvertieren
- Darstellung nach S-52-Grundzügen: Tiefenflächen (DEPARE), Sicherheitskontur, Tonnen, Fahrrinnen
- ~~Tiefenlinien/-schattierung aus EMODnet bzw. S-57-Tiefenlinien~~ ✅ (`tools/make_depth.py`), ~~Sicherheitstiefe in der App~~ ✅, ~~aufrechte Beschriftungen~~ ✅

## später
- NMEA 2000 senden mit Address Claim (ISO 11783-5) – erst mit Tests gegen echte Geräte ([ADR 0002](adr/0002-nmea2000-listen-only.md))
- Motordaten-Seite (PGN 127488/127489), Tankstände, Batterie
- Signal K als zusätzliche Quelle
- Sonar/Radar über offene Hardware ([ADR 0004](adr/0004-sonar-radar.md))
- Mehrere Displays (Datenbus über Netzwerk)
