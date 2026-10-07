# ADR 0004: Echolot-Bild und Radar

**Status:** Recherche

## Kontext
Das „xsv“ im Vorbild steht für eingebautes CHIRP-Sonar mit ClearVü/SideVü. Garmin-Geber und
-Radare sprechen proprietäre Protokolle. Offene Alternativen sind wenige, aber vorhanden:
Open-Source-Echolot-Projekte mit eigener Analogelektronik und Geber, sowie Radare, deren
Netzwerkprotokoll reverse-engineert wurde (Referenz: radar_pi-Plugin von OpenCPN).

## Entscheidung
Kein Sonar/Radar in v0.x. Tiefe kommt als Zahl über NMEA. Für später wird je eine Modul-
Schnittstelle definiert (`SonarPing`: Amplituden je Tiefenbin; `RadarSpoke`: Amplituden je
Peilung), sodass Hardware-Treiber unabhängig von der Anzeige entstehen können.

## Folgen
OpenBoat ist zunächst Kartenplotter + Instrumentendisplay + AIS, kein Fishfinder.
