import QtQuick

// Active alarms. Unacknowledged alarms blink (and sound, see AlarmSound and
// the buzzer module); tapping acknowledges all of them. Acknowledged alarms
// stay visible, dimmed, as long as the condition persists.
Rectangle {
    id: banner
    property var alarms: []  // [{ id, text, acknowledged }]
    signal acknowledge()
    readonly property bool pending: alarms.some(a => !a.acknowledged)

    visible: alarms.length > 0
    height: column.implicitHeight + 16
    color: pending && blink.on ? Theme.danger : "#5a0000"
    radius: 6
    border.color: "white"
    border.width: 2

    Timer {
        id: blink
        property bool on: true
        interval: 500
        repeat: true
        running: banner.pending
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
                color: modelData.acknowledged ? "#d0a0a0" : "white"
                font.pixelSize: 24
                font.bold: true
            }
        }
        Text {
            visible: banner.pending
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Antippen zum Quittieren"
            color: "white"
            font.pixelSize: 14
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: banner.acknowledge()
    }
}
