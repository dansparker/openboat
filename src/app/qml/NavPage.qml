import QtQuick
import QtQuick.Layouts

// Saved waypoints and routes: Go To, start (forwards/backwards), delete.
Rectangle {
    id: page
    signal closeRequested()
    signal newRoute()
    signal editRoute(int index)

    color: Theme.panel
    border.color: Theme.panelBorder
    radius: 6

    function nm(v) { return v < 0.5 ? (v * 1852).toFixed(0) + " m" : (v * settings.distanceFactor).toFixed(1) + " " + settings.distanceLabel; }
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
            TouchButton { text: "USB"; fontSize: 15; implicitHeight: 46; checked: usbBox.visible; onClicked: { usbBox.visible = !usbBox.visible; if (usbBox.visible) routes.refreshUsb(); } }
            TouchButton { text: "✕"; implicitHeight: 46; onClicked: page.closeRequested() }
        }
        // USB stick: export everything, import GPX files (OpenCPN, Garmin, Navionics, ...)
        Rectangle {
            id: usbBox
            visible: false
            Layout.fillWidth: true
            implicitHeight: usbCol.implicitHeight + 16
            color: Theme.background
            radius: 4
            border.color: Theme.panelBorder
            Column {
                id: usbCol
                x: 8; y: 8
                width: parent.width - 16
                spacing: 6
                RowLayout {
                    width: parent.width
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        font.pixelSize: 15
                        text: routes.usbDrives.length === 0 ? "Kein USB-Stick gefunden – einstecken und „Aktualisieren“"
                                                             : "USB: " + routes.usbDrives.join(", ")
                    }
                    TouchButton { text: "Aktualisieren"; fontSize: 14; implicitHeight: 44; onClicked: routes.refreshUsb() }
                    TouchButton { text: "Alles exportieren"; fontSize: 14; implicitHeight: 44; enabled: routes.usbDrives.length > 0; onClicked: routes.exportToUsb() }
                }
                Text { visible: routes.usbMessage !== ""; width: parent.width; wrapMode: Text.WordWrap; color: Theme.accent; font.pixelSize: 14; text: routes.usbMessage }
                Repeater {
                    model: routes.usbFiles
                    RowLayout {
                        required property var modelData
                        width: usbCol.width
                        Text { Layout.fillWidth: true; elide: Text.ElideMiddle; color: Theme.text; font.pixelSize: 15; text: modelData.drive + "/" + modelData.name }
                        TouchButton { text: "Importieren"; fontSize: 14; implicitHeight: 44; onClicked: routes.importGpx(modelData.path) }
                    }
                }
            }
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
                        TouchButton { text: "Bearbeiten"; fontSize: 14; implicitHeight: 44; onClicked: page.editRoute(index) }
                        TouchButton { text: "Prüfen"; fontSize: 14; implicitHeight: 44; onClicked: { routes.checkRoute(index, settings.safetyDepth); page.closeRequested(); } }
                        TouchButton { text: "Löschen"; fontSize: 14; implicitHeight: 44; onHeld: routes.removeRoute(index) }
                    }
                }

                Text { text: "Tracks"; color: Theme.label; font.pixelSize: 16; visible: boat.trackDays.length > 0 }
                Repeater {
                    model: boat.trackDays
                    RowLayout {
                        required property var modelData
                        required property int index
                        width: content.width
                        Text {
                            Layout.fillWidth: true
                            text: modelData.date + (modelData.today ? " (heute)" : "")
                                  + "   " + page.nm(modelData.lengthNm)
                            color: Theme.text; font.pixelSize: 17; elide: Text.ElideRight
                        }
                        TouchButton {
                            visible: !modelData.today  // today is always on the chart
                            text: "Zeigen"; fontSize: 14; implicitHeight: 44
                            checked: modelData.shown
                            onClicked: boat.setTrackDayShown(modelData.date, !modelData.shown)
                        }
                        TouchButton { text: "GPX"; fontSize: 14; implicitHeight: 44; onClicked: boat.exportTrackDay(modelData.date) }
                    }
                }
                Text {
                    visible: boat.trackExport !== ""
                    width: content.width
                    wrapMode: Text.WrapAnywhere
                    text: "Export: " + boat.trackExport
                    color: Theme.label
                    font.pixelSize: 13
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
                    visible: routes.routes.length === 0 && routes.waypoints.length === 0 && boat.trackDays.length === 0
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
