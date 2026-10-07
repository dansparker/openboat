import QtQuick

// Active alarms. A new alarm blinks until acknowledged; acknowledged
// alarms stay visible (dimmed) as long as the condition persists.
Rectangle {
    id: banner
    property var alarms: []        // [{ id, text }]
    property var acknowledged: []  // alarm ids
    readonly property var unacknowledged: alarms.filter(a => acknowledged.indexOf(a.id) < 0)

    visible: alarms.length > 0
    height: column.implicitHeight + 16
    color: unacknowledged.length > 0 && blink.on ? Theme.danger : "#5a0000"
    radius: 6
    border.color: "white"
    border.width: 2

    // Forget acknowledgements of alarms that went away, so they alarm again
    onAlarmsChanged: acknowledged = acknowledged.filter(id => alarms.some(a => a.id === id))

    Timer {
        id: blink
        property bool on: true
        interval: 500
        repeat: true
        running: banner.unacknowledged.length > 0
        onTriggered: on = !on
        onRunningChanged: on = true
    }

    Column {
        id: column
        anchors.centerIn: parent
        Repeater {
            model: banner.alarms
            Text {
                required property var modelData
                text: modelData.text
                color: "white"
                font.pixelSize: 24
                font.bold: true
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: banner.acknowledged = banner.alarms.map(a => a.id)
    }
}
