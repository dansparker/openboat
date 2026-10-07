# ADR 0001: Tech-Stack wie OpenEFIS

**Status:** angenommen

## Kontext
OpenEFIS hat sich mit C++20, Qt 6/QML und einem Qt-freien, unit-getesteten Kern bewährt.
Ein Kartenplotter braucht dieselben Eigenschaften: flüssige Touch-UI auf einem Raspberry Pi,
deterministische Protokollverarbeitung, Betrieb ohne Netzwerk.

## Entscheidung
C++20, CMake, Qt 6 (Quick, Sql) nur in `src/app`; Kern, HAL und Module Qt-frei mit GoogleTest.
DataBus, Modul-Schnittstelle und HAL werden aus OpenEFIS übernommen.

## Alternativen
- **Web-Stack (MapLibre + Node)**: schnellere Kartenentwicklung, aber Browser auf dem Pi
  braucht mehr RAM/CPU, startet langsamer, Echtzeit-CAN umständlich.
- **OpenCPN erweitern**: ausgereift (inkl. S-57), aber Desktop-UI (wxWidgets), nicht für
  Touch/Sonnenlicht gebaut. OpenCPN bleibt Referenz für S-57-Darstellung.

## Folgen
Code und Wissen zwischen OpenEFIS und OpenBoat sind austauschbar (z. B. WMM, Pi-Image).
