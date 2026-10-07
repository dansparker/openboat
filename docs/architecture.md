# Architektur

```
 NMEA 0183 ─┐                                   ┌─> BoatModel (Qt, 5 Hz) ─> QML: Karte, Daten, Alarme
 NMEA 2000 ─┼─> Parser/Decoder ─> DataBus ──────┤
 Simulator ─┘                       ▲  │        └─> NavModule: wahrer Wind, AIS-Tabelle, Alarme ─┐
                                    │  └──────────────────────────────────────────────────────────┤
                                    └──────────── AlarmList, AisTargetList, AnchorState ───────────┘
```

## Grundsätze

1. **DataBus** (`src/core`): typsicheres Publish/Subscribe, ein Topic pro Datentyp, jeder
   Wert mit Zeitstempel. Module kennen einander nicht, nur den Bus. Übernommen aus OpenEFIS.
2. **Einheiten im Kern immer SI** (m, m/s, °C, Grad rechtweisend). Umrechnung in kn, sm, ft
   nur in der UI. Kurse gelten als rechtweisend; ein Steuerkurs ohne bekannte Missweisung wird
   als magnetisch markiert (`Heading::is_true = false`) und auch so angezeigt.
3. **Veraltete Daten sind ungültig.** Die UI zeigt `---` statt eines eingefrorenen Werts;
   GNSS- und Tiefenausfall sind eigene Alarme. Fällt die Alarmüberwachung selbst aus, meldet
   die UI das.
4. **Qt nur in `src/app`.** Kern, Protokolle und Navigationslogik sind ohne Qt baubar und
   vollständig unit-getestet (`cmake --preset core-only`).
5. **Logik rein, Zeit von außen.** `AisTable` und `AlarmEvaluator` bekommen die Zeit als
   Parameter – testbar ohne Warten; `NavModule` verdrahtet sie mit dem Bus (1 Hz).
6. **Quellen fallen nicht um.** Jede Datenquelle verbindet sich nach Fehlern selbst neu
   (Multiplexer bootet später, USB-Empfänger wird abgesteckt, N2K-Bus ohne Strom).

## Datentypen (Auszug, `marine_data.hpp`)

`Position`, `CourseOverGround`, `Heading`, `SpeedThroughWater`, `Depth` (mit Offset:
positiv = unter Wasserlinie, negativ = unter Kiel), `ApparentWind`, `TrueWind`,
`WaterTemperature`, `EngineData`, `AisReport`. Aus `nav`: `AisTargetList`, `AlarmList`,
`AnchorState`, `AnchorCommand` (UI → Ankerwache).

## Wahrer Wind

Wird **nur** aus scheinbarem Wind berechnet (MWV „R“, PGN 130306 Referenz „apparent“), mit
Steuerkurs (sonst COG) und Fahrt durchs Wasser (sonst SOG). Vom Windgeber gelieferte „wahre“
Werte werden ignoriert, damit überall dieselbe Definition gilt.

## Neue Datenquelle hinzufügen

`core::Module` implementieren (`start` startet einen Worker-Thread, `stop` beendet ihn
idempotent), Werte mit `bus.publish(...)` veröffentlichen, in `src/app/src/main.cpp`
(`make_source`) einen Konfigurationstyp registrieren.
