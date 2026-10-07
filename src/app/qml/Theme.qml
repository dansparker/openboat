pragma Singleton
import QtQuick

// Shared colours and sizes. Night mode dims everything to red/dark so the
// skipper keeps the night vision (a must on a boat, unlike on most screens).
QtObject {
    property bool night: false

    readonly property color background: night ? "#000000" : "#0b1620"
    readonly property color panel: night ? "#0a0000" : "#14222e"
    readonly property color panelBorder: night ? "#401010" : "#35506a"
    readonly property color text: night ? "#c03030" : "white"
    readonly property color label: night ? "#802020" : "#9fb4c8"
    readonly property color accent: night ? "#c03030" : "#00c8ff"
    readonly property color ownShip: night ? "#c03030" : "#ffd200"
    readonly property color ais: night ? "#802020" : "#20e060"
    readonly property color aisLost: "#707070"
    readonly property color danger: "#ff2020"
    readonly property color anchor: night ? "#c03030" : "#ff9c00"
    readonly property color route: night ? "#a02060" : "#e000e0"
    readonly property color waypoint: night ? "#802020" : "#202020"
    readonly property string mono: "monospace"
    readonly property real chartDimming: night ? 0.35 : 1.0
    readonly property color water: night ? "#0a0404" : "#a8c8e0"  // where no chart tile exists
    readonly property color button: night ? "#140000" : "#262626"
    readonly property color buttonPressed: night ? "#300000" : "#505050"
    readonly property color buttonChecked: night ? "#3a0000" : "#00506a"
    readonly property color buttonBorder: night ? "#501010" : "#606060"
    readonly property color buttonBorderChecked: night ? "#c03030" : "#00c8ff"
    readonly property color overlayText: night ? "#c03030" : "black"
    readonly property color overlayOutline: night ? "black" : "white"
}
