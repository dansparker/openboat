import QtQuick
import QtQuick.Layouts

// Logbook: one-touch entries (time, position, speed, depth and wind are filled
// in automatically) and the daily figures from the track recording.
Rectangle {
    id: page
    signal closeRequested()

    color: Theme.panel
    border.color: Theme.panelBorder
    radius: 6

    readonly property var quick: ["Abgelegt", "Angelegt", "Anker gefallen", "Anker auf", "Segel gesetzt", "Segel geborgen",
                                  "Reff", "Motor an", "Motor aus", "Wachwechsel", "Wetter ändert sich", "Besonderes Ereignis"]

    function time(ms) { return ms > 0 ? new Date(ms).toISOString().substr(11, 5) + " UTC" : "—"; }
    function date(ms) { return new Date(ms).toISOString().substr(0, 10); }
    function hm(h) { return Math.floor(h) + " h " + String(Math.round((h % 1) * 60)).padStart(2, "0") + " min"; }
    function position(e) {
        if (!e.hasPosition) return "keine Position";
        const f = (v, pos, neg) => {
            const a = Math.abs(v), d = Math.floor(a);
            return d + "°" + ((a - d) * 60).toFixed(3) + "′" + (v >= 0 ? pos : neg);
        };
        return f(e.lat, "N", "S") + " " + f(e.lon, "E", "W");
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Logbuch"; color: Theme.text; font.pixelSize: 22; font.bold: true; Layout.fillWidth: true }
            TouchButton {
                text: "CSV auf USB"; fontSize: 15; implicitHeight: 46
                onClicked: {
                    routes.refreshUsb();
                    exportResult.text = routes.usbDrives.length === 0 ? "Kein USB-Stick gefunden"
                        : (logbook.exportCsv(routes.usbDrives[0]) || "Export fehlgeschlagen");
                }
            }
            TouchButton { text: "✕"; implicitHeight: 46; onClicked: page.closeRequested() }
        }
        Text { id: exportResult; visible: text !== ""; color: Theme.accent; font.pixelSize: 14; Layout.fillWidth: true; elide: Text.ElideMiddle }
        Text { visible: logbook.lastError !== ""; text: logbook.lastError; color: Theme.danger; font.pixelSize: 14 }

        // One-touch entries
        GridLayout {
            Layout.fillWidth: true
            columns: 6
            columnSpacing: 6
            rowSpacing: 6
            Repeater {
                model: page.quick
                TouchButton {
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 100
                    implicitHeight: 46
                    fontSize: 14
                    text: modelData
                    onClicked: logbook.add(modelData)
                }
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: col.implicitHeight
            clip: true

            Column {
                id: col
                width: parent.width
                spacing: 4

                Text { text: "Einträge"; color: Theme.label; font.pixelSize: 16; visible: logbook.entries.length > 0 }
                Repeater {
                    model: logbook.entries
                    RowLayout {
                        required property var modelData
                        required property int index
                        width: col.width
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            font.pixelSize: 15
                            text: {
                                const e = modelData;
                                let t = page.date(e.timeMs) + "  " + page.time(e.timeMs) + "   " + e.text + "   ·   " + page.position(e);
                                if (e.sogKn !== undefined) t += "   SOG " + (e.sogKn * settings.speedFactor).toFixed(1) + " " + settings.speedLabel + ", COG " + Math.round(e.cog) + "°";
                                if (e.depth !== undefined) t += "   Tiefe " + settings.depthText(e.depth);
                                if (e.twd !== undefined) t += "   Wind " + Math.round(e.twd) + "° " + Math.round(e.twsKn) + " kn";
                                return t;
                            }
                        }
                        TouchButton { text: "Löschen"; fontSize: 13; implicitHeight: 40; onHeld: logbook.remove(index) }
                    }
                }

                Text { text: "Tage (aus der Track-Aufzeichnung)"; color: Theme.label; font.pixelSize: 16; topPadding: 8 }
                Text {
                    visible: boat.trackDays.length === 0
                    color: Theme.label
                    font.pixelSize: 15
                    text: "Noch keine Fahrten aufgezeichnet."
                }
                Repeater {
                    model: boat.trackDays
                    Text {
                        required property var modelData
                        width: col.width
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        font.pixelSize: 15
                        text: modelData.date + (modelData.today ? " (heute)" : "")
                              + "   " + page.time(modelData.startMs) + " – " + page.time(modelData.endMs)
                              + "   " + (modelData.lengthNm * settings.distanceFactor).toFixed(1) + " " + settings.distanceLabel
                              + "   in Fahrt " + page.hm(modelData.underwayH)
                              + "   Ø " + (modelData.underwayH > 0.01 ? (modelData.lengthNm / modelData.underwayH * settings.speedFactor).toFixed(1) : "—")
                              + "   max " + (modelData.maxKn * settings.speedFactor).toFixed(1) + " " + settings.speedLabel
                    }
                }
            }
        }
    }
}
