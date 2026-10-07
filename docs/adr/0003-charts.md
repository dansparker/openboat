# ADR 0003: Karten – erst Raster (MBTiles), dann S-57

**Status:** angenommen

## Kontext
Die Kartendaten des Vorbilds (BlueChart, Navionics) sind proprietär. Frei verfügbar sind
OpenSeaMap/OpenStreetMap (weltweit, Gemeinschaftsdaten) sowie amtliche Vektorkarten nur in
bestimmten Gebieten (Inland ENC für Binnenwasserstraßen in der EU, NOAA ENC in den USA).
S-57 korrekt darzustellen (S-52: Sicherheitskontur, Symbolik, Anzeigekategorien) ist ein
großes Teilprojekt.

## Entscheidung
v0.1 zeigt Rasterkacheln aus MBTiles (Basiskarte + beliebig viele Overlays). Das ist
einfach, offline-fähig, auf dem Pi schnell und deckt OpenSeaMap ab. S-57 folgt in v0.4 über
GDAL-Konvertierung und eine reduzierte S-52-Darstellung.

## Folgen
- In v0.1 keine Tiefenflächen/Sicherheitskontur aus amtlichen Daten → Flachwasseralarm hängt
  am Echolot, nicht an der Karte.
- Lizenzpflicht: Namensnennung wird in der Karte eingeblendet.
- Massen-Download von Kacheln nur, wo die Betreiber es erlauben (`tools/fetch_tiles.py` prüft das).
