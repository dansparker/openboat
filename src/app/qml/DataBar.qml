import QtQuick
import QtQuick.Layouts

// Data fields at the side of the chart (like the GPSMAP data bar); they share the
// available height so the panel fits any screen.
ColumnLayout {
    spacing: 4

    DataField { Layout.fillWidth: true; Layout.fillHeight: true; label: "SOG"; unit: settings.speedLabel; valid: boat.cogValid; value: (boat.sogKn * settings.speedFactor).toFixed(1) }
    DataField { Layout.fillWidth: true; Layout.fillHeight: true; label: "COG"; unit: "°"; valid: boat.cogValid && boat.sogKn > 0.5; value: Math.round(boat.cog).toString().padStart(3, "0") }
    DataField {
        Layout.fillWidth: true
        Layout.fillHeight: true
        label: boat.headingTrue ? "HDG" : "HDG (mag)"
        unit: "°"
        valid: boat.headingValid
        value: Math.round(boat.heading).toString().padStart(3, "0")
    }
    DataField { Layout.fillWidth: true; Layout.fillHeight: true; label: "Fahrt (STW)"; unit: settings.speedLabel; valid: boat.stwValid; value: (boat.stwKn * settings.speedFactor).toFixed(1) }
    DataField {
        Layout.fillWidth: true
        Layout.fillHeight: true
        label: "Tiefe"
        unit: settings.depthLabel
        valid: boat.depthValid
        value: (boat.depth * settings.depthFactor).toFixed(1)
        color: boat.alarms.some(a => a.id === 1 /* ShallowWater */) ? "#5a0000" : Theme.panel
    }
    DataField { Layout.fillWidth: true; Layout.fillHeight: true; label: "Wind (scheinbar)"; unit: "kn"; valid: boat.windValid; value: Math.round(boat.awa) + "° " + boat.awsKn.toFixed(0) }
    DataField { Layout.fillWidth: true; Layout.fillHeight: true; label: "Wind (wahr)"; unit: "kn"; valid: boat.windValid; value: Math.round(boat.twd) + "° " + boat.twsKn.toFixed(0) }
    DataField { Layout.fillWidth: true; Layout.fillHeight: true; label: "Wassertemp."; unit: "°C"; valid: boat.waterTempValid; value: boat.waterTemp.toFixed(1) }
}
