import QtQuick
import QtQuick.Layouts

// Saved waypoints and routes: Go To, start (forwards/backwards), delete.
Rectangle {
    id: page
    signal closeRequested()
    signal newRoute()

    color: Theme.panel
    border.color: Theme.panelBorder
    radius: 6

    function nm(v) { return v < 1 ? (v * 1852).toFixed(0) + " m" : v.toFixed(1) + " sm"; }
    function distanceNm(w) {
        if (!boat.positionValid) return "";
        const r = Math.PI / 180;
        const dLat = (w.lat - boat.latitude) * r, dLon = (w.lon - boat.longitude) * r;
        const a = Math.sin(dLat / 2) ** 2 + Math.cos(boat.latitude * r) * Math.cos(w.lat * r) * Math.sin(dLon / 2) ** 2;
        return nm(2 * 3440.065 * Math.asin(Math.sqrt(a)));
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Wegpunkte & Routen"; color: Theme.text; font.pixelSize: 22; font.bold: true; Layout.fillWidth: true }
            TouchButton { text: "Neue Route"; fontSize: 15; implicitHeight: 46; onClicked: page.newRoute() }
            TouchButton { text: "✕"; implicitHeight: 46; onClicked: page.closeRequested() }
        }
        Text {
            visible: routes.lastError !== ""
            text: "Speichern fehlgeschlagen: " + routes.lastError
            color: Theme.danger
            font.pixelSize: 14
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: content.implicitHeight
            clip: true

            Column {
                id: content
                width: parent.width
                spacing: 6

                Text { text: "Routen"; color: Theme.label; font.pixelSize: 16; visible: routes.routes.length > 0 }
                Repeater {
                    model: routes.routes
                    RowLayout {
                        required property var modelData
                        required property int index
                        width: content.width
                        Text {
                            Layout.fillWidth: true
                            text: modelData.name + "  (" + modelData.points.length + " Punkte, " + page.nm(modelData.lengthNm) + ")"
                            color: Theme.text; font.pixelSize: 17; elide: Text.ElideRight
                        }
                        TouchButton { text: "Start"; fontSize: 14; implicitHeight: 44; onClicked: { routes.startRoute(index, false); page.closeRequested(); } }
                        TouchButton { text: "Rückwärts"; fontSize: 14; implicitHeight: 44; onClicked: { routes.startRoute(index, true); page.closeRequested(); } }
                        TouchButton { text: "Löschen"; fontSize: 14; implicitHeight: 44; onHeld: routes.removeRoute(index) }
                    }
                }

                Text { text: "Wegpunkte"; color: Theme.label; font.pixelSize: 16; visible: routes.waypoints.length > 0 }
                Repeater {
                    model: routes.waypoints
                    RowLayout {
                        required property var modelData
                        required property int index
                        width: content.width
                        Text {
                            Layout.fillWidth: true
                            text: modelData.name + "   " + page.distanceNm(modelData)
                            color: Theme.text; font.pixelSize: 17; elide: Text.ElideRight
                        }
                        TouchButton { text: "Go To"; fontSize: 14; implicitHeight: 44; onClicked: { routes.goTo(modelData.lat, modelData.lon, modelData.name); page.closeRequested(); } }
                        TouchButton { text: "Löschen"; fontSize: 14; implicitHeight: 44; onHeld: routes.removeWaypoint(index) }
                    }
                }

                Text {
                    visible: routes.routes.length === 0 && routes.waypoints.length === 0
                    width: content.width
                    wrapMode: Text.WordWrap
                    text: "Noch nichts gespeichert. Lange auf die Karte drücken, um einen Wegpunkt zu setzen, oder „Neue Route“ und Punkte antippen."
                    color: Theme.label
                    font.pixelSize: 16
                }
                Text {
                    width: content.width
                    text: "Löschen: Taste gedrückt halten."
                    color: Theme.label
                    font.pixelSize: 13
                }
            }
        }
    }
}
