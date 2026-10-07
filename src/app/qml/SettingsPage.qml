import QtQuick
import QtQuick.Layouts

// Settings: units, alarms, navigation, chart, track. Changes apply at once
// and are saved immediately (settings.json).
Rectangle {
    id: page
    signal closeRequested()

    color: Theme.panel
    border.color: Theme.panelBorder
    radius: 6

    // Label + choice buttons
    component ChoiceRow: RowLayout {
        id: choice
        property string label
        property var options: []  // [{ value, text }]
        property var current
        signal chosen(var value)
        width: parent ? parent.width : 0
        Text { text: choice.label; color: Theme.text; font.pixelSize: 18; Layout.fillWidth: true }
        Repeater {
            model: choice.options
            TouchButton {
                required property var modelData
                text: modelData.text
                fontSize: 16
                implicitHeight: 48
                checked: choice.current === modelData.value
                onClicked: choice.chosen(modelData.value)
            }
        }
    }

    // Label + value with − / + (press and hold repeats)
    component StepRow: RowLayout {
        id: step
        property string label
        property real value
        property real stepSize: 1
        property real minimum: 0
        property real maximum: 100
        property int decimals: 0
        property string unit
        property string zeroText  // shown instead of 0 (e.g. "aus")
        signal changedTo(real value)
        width: parent ? parent.width : 0
        function set(v) { step.changedTo(Math.max(step.minimum, Math.min(step.maximum, Math.round(v / step.stepSize) * step.stepSize))); }
        Text { text: step.label; color: Theme.text; font.pixelSize: 18; Layout.fillWidth: true }
        TouchButton { text: "−"; implicitHeight: 48; autoRepeat: true; onClicked: step.set(step.value - step.stepSize) }
        Text {
            Layout.preferredWidth: 120
            horizontalAlignment: Text.AlignHCenter
            text: step.value === 0 && step.zeroText ? step.zeroText : step.value.toFixed(step.decimals) + " " + step.unit
            color: Theme.text
            font.pixelSize: 20
            font.bold: true
            font.family: Theme.mono
        }
        TouchButton { text: "+"; implicitHeight: 48; autoRepeat: true; onClicked: step.set(step.value + step.stepSize) }
    }

    component Heading: Text {
        color: Theme.accent
        font.pixelSize: 16
        font.bold: true
        topPadding: 10
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Einstellungen"; color: Theme.text; font.pixelSize: 22; font.bold: true; Layout.fillWidth: true }
            TouchButton { text: "✕"; implicitHeight: 46; onClicked: page.closeRequested() }
        }
        Text { visible: settings.lastError !== ""; text: settings.lastError; color: Theme.danger; font.pixelSize: 14 }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: content.implicitHeight
            clip: true

            Column {
                id: content
                width: parent.width
                spacing: 6

                Heading { text: "Einheiten" }
                ChoiceRow {
                    label: "Geschwindigkeit"
                    options: [{ value: "kn", text: "kn" }, { value: "kmh", text: "km/h" }]
                    current: settings.speedUnit
                    onChosen: v => settings.speedUnit = v
                }
                ChoiceRow {
                    label: "Tiefe"
                    options: [{ value: "m", text: "m" }, { value: "ft", text: "ft" }]
                    current: settings.depthUnit
                    onChosen: v => settings.depthUnit = v
                }
                ChoiceRow {
                    label: "Distanz"
                    options: [{ value: "nm", text: "sm" }, { value: "km", text: "km" }]
                    current: settings.distanceUnit
                    onChosen: v => settings.distanceUnit = v
                }

                Heading { text: "Alarme" }
                StepRow {
                    label: "Flachwasser unter"
                    // shown in the selected depth unit, stored in metres
                    value: settings.shallowAlarm * settings.depthFactor
                    stepSize: settings.depthUnit === "ft" ? 1 : 0.5
                    maximum: 50 * settings.depthFactor
                    decimals: settings.depthUnit === "ft" ? 0 : 1
                    unit: settings.depthLabel
                    zeroText: "aus"
                    onChangedTo: v => settings.shallowAlarm = v / settings.depthFactor
                }
                StepRow {
                    label: "AIS: Annäherung (CPA) unter"
                    value: settings.cpaNm
                    stepSize: 0.05
                    minimum: 0.05
                    maximum: 5
                    decimals: 2
                    unit: "sm"
                    onChangedTo: v => settings.cpaNm = v
                }
                StepRow {
                    label: "AIS: innerhalb von (TCPA)"
                    value: settings.tcpaMin
                    minimum: 1
                    maximum: 60
                    unit: "min"
                    onChangedTo: v => settings.tcpaMin = v
                }
                StepRow {
                    label: "Ankerwache: Radius"
                    value: settings.anchorRadius
                    stepSize: 5
                    minimum: 5
                    maximum: 500
                    unit: "m"
                    onChangedTo: v => settings.anchorRadius = v
                }

                Heading { text: "Tiefe" }
                ChoiceRow {
                    label: "Tiefenoffset"
                    options: [{ value: "transducer", text: "vom Geber" }, { value: "manual", text: "manuell" }]
                    current: settings.depthOffsetMode
                    onChosen: v => settings.depthOffsetMode = v
                }
                StepRow {
                    visible: settings.depthOffsetMode === "manual"
                    label: "Offset (− unter Kiel, + unter Wasserlinie)"
                    value: settings.depthOffset * settings.depthFactor
                    stepSize: settings.depthUnit === "ft" ? 0.5 : 0.1
                    minimum: -10 * settings.depthFactor
                    maximum: 10 * settings.depthFactor
                    decimals: 1
                    unit: settings.depthLabel
                    onChangedTo: v => settings.depthOffset = v / settings.depthFactor
                }
                Text {
                    width: content.width
                    wrapMode: Text.WordWrap
                    color: Theme.label
                    font.pixelSize: 15
                    // Lets the skipper check the setting against a known depth
                    text: boat.depthValid
                          ? "Geber misst " + (boat.depthBelowTransducer * settings.depthFactor).toFixed(1) + " " + settings.depthLabel
                            + ", Offset " + (boat.depthOffset * settings.depthFactor).toFixed(1) + " " + settings.depthLabel
                            + " → angezeigt " + (boat.depth * settings.depthFactor).toFixed(1) + " " + settings.depthLabel
                            + (settings.depthOffsetMode === "manual" ? " (manuell, ersetzt den Offset des Gebers)" : "")
                          : "Keine Tiefendaten"
                }

                Heading { text: "Navigation & Karte" }
                ChoiceRow {
                    label: "Overzoom (Karte über ihre Auflösung vergrößern)"
                    options: [{ value: true, text: "Ein" }, { value: false, text: "Aus" }]
                    current: settings.overzoom
                    onChosen: v => settings.overzoom = v
                }
                StepRow {
                    label: "Ankunftskreis Wegpunkt"
                    value: settings.arrivalRadius
                    stepSize: 10
                    minimum: 10
                    maximum: 1000
                    unit: "m"
                    onChangedTo: v => settings.arrivalRadius = v
                }
                StepRow {
                    label: "Kursvektor (Vorausschau)"
                    value: settings.vectorMinutes
                    maximum: 60
                    unit: "min"
                    zeroText: "aus"
                    onChangedTo: v => settings.vectorMinutes = v
                }

                Heading { text: "Track" }
                ChoiceRow {
                    label: "Aufzeichnung"
                    options: [{ value: true, text: "Ein" }, { value: false, text: "Aus" }]
                    current: settings.trackRecording
                    onChosen: v => settings.trackRecording = v
                }
                ChoiceRow {
                    label: "Auf der Karte zeigen"
                    options: [{ value: true, text: "Ein" }, { value: false, text: "Aus" }]
                    current: settings.showTrack
                    onChosen: v => settings.showTrack = v
                }
                StepRow {
                    label: "Punktabstand"
                    value: settings.trackSpacing
                    stepSize: 5
                    minimum: 5
                    maximum: 500
                    unit: "m"
                    onChangedTo: v => settings.trackSpacing = v
                }
                Text {
                    width: content.width
                    wrapMode: Text.WordWrap
                    color: boat.timeFromGnss ? Theme.label : Theme.danger
                    font.pixelSize: 15
                    text: "Heute: " + (boat.trackTodayNm * settings.distanceFactor).toFixed(2) + " " + settings.distanceLabel
                          + "   ·   Zeit: " + (boat.timeFromGnss ? "GNSS" : "Systemuhr (kein GNSS-Zeitsignal – Zeitstempel evtl. falsch)")
                }
                RowLayout {
                    width: content.width
                    TouchButton { text: "Track als GPX exportieren"; fontSize: 16; implicitHeight: 48; onClicked: settings.exportTrack() }
                    TouchButton { text: "Anzeige leeren"; fontSize: 16; implicitHeight: 48; onClicked: settings.clearTrackDisplay() }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    visible: boat.trackExport !== ""
                    width: content.width
                    wrapMode: Text.WrapAnywhere
                    text: "Export: " + boat.trackExport
                    color: Theme.label
                    font.pixelSize: 14
                }
            }
        }
    }
}
