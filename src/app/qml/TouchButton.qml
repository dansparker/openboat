import QtQuick

// Large, glove-friendly button. With autoRepeat, press-and-hold repeats clicked().
Rectangle {
    id: button

    property alias text: label.text
    property bool checked: false
    property bool autoRepeat: false
    property int fontSize: 20

    signal clicked()

    implicitWidth: Math.max(64, label.implicitWidth + 28)
    implicitHeight: 56
    radius: 6
    color: mouse.pressed ? Theme.buttonPressed : checked ? Theme.buttonChecked : Theme.button
    border.color: checked ? Theme.buttonBorderChecked : Theme.buttonBorder
    border.width: 2

    Text {
        id: label
        anchors.centerIn: parent
        color: Theme.text
        font.pixelSize: button.fontSize
        font.bold: true
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        onClicked: button.clicked()
        onPressAndHold: if (button.autoRepeat) repeatTimer.start()
        onReleased: repeatTimer.stop()
        onCanceled: repeatTimer.stop()
    }

    Timer {
        id: repeatTimer
        interval: 90
        repeat: true
        onTriggered: button.clicked()
    }
}
