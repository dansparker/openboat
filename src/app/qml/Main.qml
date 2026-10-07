import QtQuick
import QtQuick.Layouts

Window {
    id: window
    width: 1280
    height: 800
    visible: true
    visibility: startFullScreen ? Window.FullScreen : Window.Windowed
    title: "OpenBoat"
    color: Theme.background
    Component.onCompleted: {
        Theme.night = startNight;
        chartView.setZoom(startZoom);
        navPage.visible = startPage === "routes";
        settingsPage.visible = startPage === "settings";
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ChartView {
                id: chartView
                anchors.fill: parent
                layers: chartLayers
                waypoints: routes.waypoints
                vectorMinutes: settings.vectorMinutes
                onLongPressed: (lat, lon, x, y) => {
                    if (editing) return;
                    menu.lat = lat;
                    menu.lon = lon;
                    menu.x = Math.min(x, width - menu.width - 8);
                    menu.y = Math.min(y, height - menu.height - 8);
                    menu.visible = true;
                }
                onTapped: (lat, lon) => {
                    menu.visible = false;
                    if (editing) editPoints = editPoints.concat([{ lat: lat, lon: lon }]);
                }
            }

            NavStrip {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 8
                anchors.bottomMargin: 30
            }

            // Route editor bar
            Rectangle {
                visible: chartView.editing
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 30
                width: editRow.implicitWidth + 16
                height: 64
                radius: 6
                color: Theme.panel
                border.color: Theme.anchor
                border.width: 2
                RowLayout {
                    id: editRow
                    anchors.centerIn: parent
                    Text { text: "Neue Route: " + chartView.editPoints.length + " Punkte – Karte antippen"; color: Theme.text; font.pixelSize: 17 }
                    TouchButton { text: "↶"; implicitHeight: 46; enabled: chartView.editPoints.length > 0; onClicked: chartView.editPoints = chartView.editPoints.slice(0, -1) }
                    TouchButton {
                        text: "Speichern"; fontSize: 15; implicitHeight: 46
                        onClicked: { routes.addRoute(chartView.editPoints); chartView.editing = false; chartView.editPoints = []; }
                    }
                    TouchButton {
                        text: "Speichern & Start"; fontSize: 15; implicitHeight: 46
                        onClicked: {
                            routes.addRoute(chartView.editPoints);
                            if (chartView.editPoints.length >= 2) routes.startRoute(routes.routes.length - 1, false);
                            chartView.editing = false;
                            chartView.editPoints = [];
                        }
                    }
                    TouchButton { text: "Abbrechen"; fontSize: 15; implicitHeight: 46; onClicked: { chartView.editing = false; chartView.editPoints = []; } }
                }
            }

            // Long-press menu on the chart
            Rectangle {
                id: menu
                property real lat: 0
                property real lon: 0
                visible: false
                width: 230
                height: menuColumn.implicitHeight + 16
                radius: 6
                color: Theme.panel
                border.color: Theme.panelBorder
                border.width: 2
                Column {
                    id: menuColumn
                    anchors.centerIn: parent
                    width: parent.width - 16
                    spacing: 6
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.label
                        font.pixelSize: 13
                        font.family: Theme.mono
                        function dm(v, pos, neg, w) {
                            const a = Math.abs(v), d = Math.floor(a);
                            return d.toString().padStart(w, "0") + "°" + ((a - d) * 60).toFixed(3).padStart(6, "0") + "'" + (v < 0 ? neg : pos);
                        }
                        text: dm(menu.lat, "N", "S", 2) + "  " + dm(menu.lon, "E", "W", 3)
                    }
                    TouchButton { width: parent.width; text: "Go To"; fontSize: 16; onClicked: { routes.goTo(menu.lat, menu.lon, ""); menu.visible = false; } }
                    TouchButton { width: parent.width; text: "Wegpunkt speichern"; fontSize: 16; onClicked: { routes.addWaypoint(menu.lat, menu.lon); menu.visible = false; } }
                    TouchButton {
                        width: parent.width; text: "Route ab hier"; fontSize: 16
                        onClicked: { chartView.editPoints = [{ lat: menu.lat, lon: menu.lon }]; chartView.editing = true; menu.visible = false; }
                    }
                    TouchButton { width: parent.width; text: "Abbrechen"; fontSize: 16; onClicked: menu.visible = false }
                }
            }

            NavPage {
                id: navPage
                visible: false
                anchors.fill: parent
                anchors.margins: 30
                // keep the close button clear of the alarm banner
                anchors.topMargin: alarmBanner.visible ? alarmBanner.height + 28 : 30
                onCloseRequested: visible = false
                onNewRoute: { visible = false; chartView.editPoints = []; chartView.editing = true; }
            }

            SettingsPage {
                id: settingsPage
                visible: false
                anchors.fill: parent
                anchors.margins: 30
                // keep the close button clear of the alarm banner
                anchors.topMargin: alarmBanner.visible ? alarmBanner.height + 28 : 30
                onCloseRequested: visible = false
            }
        }

        ColumnLayout {
            // Fixed width: the chart gets all remaining space
            Layout.preferredWidth: 260
            Layout.maximumWidth: 260
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 6

            // Man overboard: one touch, no confirmation - every second counts
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 56
                radius: 6
                color: mobMouse.pressed ? "#ff6060" : Theme.danger
                border.color: "white"
                border.width: 2
                Text { anchors.centerIn: parent; text: "MOB"; color: "white"; font.pixelSize: 26; font.bold: true }
                MouseArea { id: mobMouse; anchors.fill: parent; onClicked: routes.manOverboard() }
            }

            DataBar {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: 6
                rowSpacing: 6
                TouchButton { Layout.fillWidth: true; text: "+"; onClicked: chartView.setZoom(chartView.zoom + 1) }
                TouchButton { Layout.fillWidth: true; text: "−"; onClicked: chartView.setZoom(chartView.zoom - 1) }
                TouchButton {
                    Layout.fillWidth: true
                    text: "Folgen"
                    fontSize: 16
                    checked: chartView.follow
                    onClicked: { chartView.follow = true; chartView.recentre(); }
                }
                TouchButton {
                    Layout.fillWidth: true
                    text: "Nacht"
                    fontSize: 16
                    checked: Theme.night
                    onClicked: Theme.night = !Theme.night
                }
                TouchButton {
                    Layout.fillWidth: true
                    fontSize: 16
                    text: "Routen"
                    checked: navPage.visible
                    onClicked: { settingsPage.visible = false; navPage.visible = !navPage.visible; }
                }
                TouchButton {
                    Layout.fillWidth: true
                    fontSize: 16
                    text: "Setup"
                    checked: settingsPage.visible
                    onClicked: { navPage.visible = false; settingsPage.visible = !settingsPage.visible; }
                }
                TouchButton {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    fontSize: 16
                    checked: boat.anchorActive
                    text: boat.anchorActive ? "Anker auf (halten) " + boat.anchorDistance.toFixed(0) + "/" + boat.anchorRadius.toFixed(0) + " m"
                                            : "Ankerwache " + settings.anchorRadius.toFixed(0) + " m"
                    onClicked: if (!boat.anchorActive) boat.dropAnchor(settings.anchorRadius)
                    // Hold to raise: a single accidental touch must not switch the watch off
                    onHeld: if (boat.anchorActive) boat.raiseAnchor()
                }
            }
        }
    }

    AlarmBanner {
        id: alarmBanner
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 16
        width: Math.min(parent.width - 40, 760)
        alarms: boat.alarms
        onAcknowledge: boat.acknowledgeAlarms()
    }
}
