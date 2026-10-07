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
    Component.onCompleted: Theme.night = startNight

    RowLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        ChartView {
            id: chartView
            Layout.fillWidth: true
            Layout.fillHeight: true
            layers: chartLayers
        }

        ColumnLayout {
            // Fixed width: the chart gets all remaining space
            Layout.preferredWidth: 260
            Layout.maximumWidth: 260
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 6

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
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    fontSize: 16
                    checked: boat.anchorActive
                    text: boat.anchorActive ? "Anker auf (" + boat.anchorDistance.toFixed(0) + "/" + boat.anchorRadius.toFixed(0) + " m)"
                                            : "Ankerwache " + radius + " m"
                    property int radius: 40
                    // Hold to raise: a single accidental touch must not switch the watch off
                    onClicked: if (!boat.anchorActive) boat.dropAnchor(radius)
                    MouseArea {
                        anchors.fill: parent
                        enabled: boat.anchorActive
                        onPressAndHold: boat.raiseAnchor()
                    }
                }
            }
        }
    }

    AlarmBanner {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 16
        width: Math.min(parent.width - 40, 760)
        alarms: boat.alarms
        onAcknowledge: boat.acknowledgeAlarms()
    }
}
