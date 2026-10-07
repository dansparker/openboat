# Freie Karten

OpenBoat zeigt in v0.1 **Rasterkarten im MBTiles-Format** (Web Mercator, XYZ-Kacheln).
Die erste Datei in `charts` ist die Basiskarte, alle weiteren werden darübergelegt
(z. B. Seezeichen). Vektorkarten (S-57) folgen später, siehe [ADR 0003](adr/0003-charts.md).

## Quellen

| Quelle | Inhalt | Gebiet | Lizenz / Bedingungen | Nutzung in OpenBoat |
|---|---|---|---|---|
| **OpenSeaMap** | Seezeichen, Tonnen, Leuchtfeuer, Häfen (Overlay) | weltweit, Qualität je nach Region | CC BY-SA 2.0; Kachelserver von Freiwilligen betrieben | Overlay; für Offline-Gebrauch bevorzugt die fertigen Downloads auf openseamap.org, sonst `fetch_tiles.py` mit kleinem Gebiet |
| **OpenStreetMap** | Küstenlinie, Land, Seen, Flüsse | weltweit | ODbL; **tile.openstreetmap.org verbietet Massen-/Offline-Download** | Basiskarte nur über einen Anbieter, der Offline-Nutzung erlaubt, oder selbst gerendert (z. B. mit TileMill/Maperitive/Tilemaker + Renderer) |
| **Inland ENC** (Donau AT: viadonau; DE: ELWIS/WSV) | Amtliche Binnen-Vektorkarten (S-57-Profil Inland ENC) | Wasserstraßen (Donau, Rhein, …) | kostenlos, Bedingungen der Herausgeber beachten | 🔜 S-57-Darstellung (Roadmap); bis dahin z. B. in OpenCPN |
| **NOAA ENC / NCDS** | Amtliche Seekarten | USA | gemeinfrei | 🔜 S-57; Raster-MBTiles von NOAA direkt nutzbar |
| **EMODnet Bathymetry** | Tiefenmodell | europäische Meere inkl. Mittelmeer | frei (CC BY 4.0) | 🔜 Tiefenschattierung / Tiefenlinien |
| **GEBCO** | grobes Tiefenmodell | weltweit | frei | 🔜 Übersicht |
| Seen-Bathymetrie der Länder (data.gv.at) | Tiefenlinien österreichischer Seen | einzelne Seen | meist CC BY 4.0 | 🔜 |

**Nicht nutzbar:** Garmin BlueChart, Navionics, C-MAP – proprietär und verschlüsselt.
Für Küstengewässer außerhalb der USA (z. B. Mittelmeer) gibt es **keine freien amtlichen
Seekarten**; OpenSeaMap + OSM + EMODnet sind dort die freie Kombination – mit entsprechend
geringerer Verlässlichkeit, vor allem bei Tiefenangaben.

## Grundkarte selbst rendern

OpenSeaMap liefert **nur die Seezeichen**; die Karte darunter ist OpenStreetMap, und deren
Kachelserver verbieten Massen-Downloads. Die OSM-**Daten** sind aber frei (ODbL). Deshalb
rendert `tools/make_basemap.py` eine Grundkarte im Seekartenstil selbst: Land, Wasser,
Uferlinie, Flüsse, Häfen/Marinas, Stege/Molen, Brücken und Ortsnamen (ohne Überlappungen).

```bash
pip install pillow
python tools/make_basemap.py --bbox 13.47,47.78,13.62,47.96 --zooms 10-16 --out charts/base.mbtiles
```

- Kleine Gebiete (bis 0,25 Grad²) holt das Werkzeug über die Overpass-API. Für größere Gebiete
  einen eigenen Auszug verwenden (`--osm-json`), z. B. von Geofabrik + osmium/Overpass lokal.
- **Küsten/Meer:** OSM hat keine fertigen Meeresflächen. Dafür die freien „land polygons“ von
  osmdata.openstreetmap.de (`land-polygons-split-4326`) laden und `--land-polygons …/land_polygons.shp`
  angeben (braucht `pip install pyshp`). Ohne diese Datei warnt das Werkzeug, weil das Meer sonst
  als Land gezeichnet würde.
- Die Grundkarte enthält **keine Tiefen**. Tiefenlinien kommen später aus Inland ENC/EMODnet.

## Tiefenlinien

`tools/make_depth.py` erzeugt ein Tiefen-Overlay. Die Kacheln enthalten die **Tiefe je Pixel**
(nicht fertige Farben); die App färbt sie für die **Sicherheitstiefe aus Setup** ein (flacher:
blau, Sicherheitslinie dick, Tiefenlinien bei 2/3/5/10/15/20/30/50/100 … m, tiefes Wasser
heller) – eine geänderte Sicherheitstiefe wirkt sofort, ohne die Karte neu zu erzeugen.
Tiefenzahlen (und bei der Grundkarte die Ortsnamen) stehen in `*.labels.json` neben der
Kartendatei; die App zeichnet sie immer aufrecht, auch bei „Kurs oben“.

```bash
pip install numpy pillow
# Meer / Mittelmeer: EMODnet-Raster (ESRI-ASCII, „Download per tile“ auf emodnet.ec.europa.eu)
python tools/make_depth.py --grid E5_2022.asc --bbox 13.5,44.8,14.0,45.2 --zooms 9-14 --attribution "EMODnet Bathymetry, CC BY 4.0" --out charts/depth.mbtiles
# Binnen: Tiefenlinien/-flächen einer Inland ENC (S-57) über GDAL
ogr2ogr -f GeoJSON depcnt.json ZELLE.000 DEPCNT
python tools/make_depth.py --contours depcnt.json --zooms 12-16 --attribution "Inland ENC viadonau" --out charts/depth.mbtiles
```

In `boat.json` zwischen Grund- und Seezeichenkarte eintragen. Die Rasterauflösung landet in
der Quellenangabe auf der Karte – **EMODnet (~115 m) und GEBCO (~450 m) sind für Übersicht und
Sicherheitsschattierung, nicht für enge Hafeneinfahrten.** Für österreichische Seen sind
frei zugängliche Tiefendaten selten; wo ein Land Tiefenlinien als Shapefile/GeoJSON
veröffentlicht, funktioniert `--contours` damit genauso.

> Die Tiefenlinien in den Screenshots sind **synthetisch** (CI, aus der Uferlinie errechnet)
> und auf der Karte so beschriftet.

## Seezeichen-Overlay herunterladen (Beispiel Attersee)

```bash
python tools/fetch_tiles.py --url "https://tiles.openseamap.org/seamark/{z}/{x}/{y}.png" --bbox 13.47,47.78,13.62,47.96 --zooms 10-16 --out charts/seamarks.mbtiles --name "OpenSeaMap seamarks" --attribution "OpenSeaMap, CC BY-SA" --overlay
```

`fetch_tiles.py` begrenzt Kachelzahl (Standard 5000) und Abfragerate, setzt einen eindeutigen
User-Agent, setzt abgebrochene Downloads fort und verweigert Server, deren Bedingungen
Massen-Downloads verbieten. **Vor jedem Download die Bedingungen des Servers prüfen.**

## Aktualität

Seezeichen werden verlegt, Fahrrinnen ändern sich. Karten vor jeder Saison neu laden und bei
amtlichen Daten (Inland ENC) die Ausgabedaten der Herausgeber beachten.
