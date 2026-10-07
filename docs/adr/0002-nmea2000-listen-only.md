# ADR 0002: NMEA 2000 vorerst nur empfangen

**Status:** angenommen

## Kontext
Ein Gerät, das auf einem NMEA-2000-Bus sendet, muss eine Adresse beanspruchen (ISO 11783-5
Address Claim), auf Konflikte reagieren und Pflicht-PGNs (Product Information, ISO Request)
beantworten. Fehler hier können zertifizierte Geräte – insbesondere den Autopiloten – stören.

## Entscheidung
v0.x empfängt nur. Der CAN-Controller wird zusätzlich im Listen-only-Modus betrieben
(`docs/hardware.md`). Senden kommt erst mit vollständigem Address Claim, Tests gegen echte
Geräte und einer Abschaltmöglichkeit in der Konfiguration.

## Folgen
Kein Autopilot-Steuern und keine Datenausgabe über N2K in v0.x. Für Autopiloten ist der
Weg über NMEA 0183 (RMB/APB) geplant – dort kann ein Empfänger nicht gestört werden.
