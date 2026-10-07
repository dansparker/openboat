import QtQuick
import QtQuick.Layouts

// One labelled value. Shows dashes when the value is invalid (stale source).
Rectangle {
    id: field
    property string label
    property string value
    property string unit
    property bool valid: true

    implicitWidth: 150
    implicitHeight: 72
    Layout.minimumHeight: 44
    Layout.preferredHeight: 72
    color: Theme.panel
    border.color: Theme.panelBorder
    radius: 4

    Text {
        x: 8; y: 4
        text: field.label
        color: Theme.label
        font.pixelSize: 15
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        text: (field.valid ? field.value : "---") + (field.unit ? " " + field.unit : "")
        color: Theme.text
        font.pixelSize: Math.max(18, Math.min(30, field.height * 0.45))
        font.bold: true
        font.family: Theme.mono
    }
}
