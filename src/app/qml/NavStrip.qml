import QtQuick
import QtQuick.Layouts

// Guidance to the active waypoint (shown over the chart while navigating).
Rectangle {
    id: strip
    readonly property var g: boat.guidance
    visible: g.active === true
    height: 64
    width: row.implicitWidth + 16
    radius: 6
    color: Theme.panel
    border.color: g.mode === "mob" ? Theme.danger : Theme.route
    border.width: 2

    function nm(v) {
        if (v === undefined || v < 0) return "---";
        return v < 0.5 ? (v * 1852).toFixed(0) + " m" : (v * settings.distanceFactor).toFixed(2) + " " + settings.distanceLabel;
    }
    function ttg(min) {
        if (min === undefined || min < 0) return "---";
        if (min < 60) return min.toFixed(0) + " min";
        return Math.floor(min / 60) + " h " + Math.round(min % 60).toString().padStart(2, "0");
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 14

        Column {
            Text { text: strip.g.mode === "mob" ? "MOB" : strip.g.mode === "route" ? "Route " + (strip.g.leg + 1) + "/" + strip.g.legs : "Go To"; color: Theme.label; font.pixelSize: 13 }
            Text { text: strip.g.target || ""; color: Theme.text; font.pixelSize: 20; font.bold: true }
        }
        Column {
            Text { text: "DTW"; color: Theme.label; font.pixelSize: 13 }
            Text { text: strip.nm(strip.g.dtwNm); color: Theme.text; font.pixelSize: 20; font.bold: true; font.family: Theme.mono }
        }
        Column {
            Text { text: "BTW"; color: Theme.label; font.pixelSize: 13 }
            Text { text: strip.g.btw === undefined || strip.g.btw < 0 ? "---" : Math.round(strip.g.btw).toString().padStart(3, "0") + "°"; color: Theme.text; font.pixelSize: 20; font.bold: true; font.family: Theme.mono }
        }
        Column {
            visible: strip.g.hasXte === true
            Text { text: "XTE"; color: Theme.label; font.pixelSize: 13 }
            // XTE > 0: right of track -> steer left
            Text {
                text: (Math.abs(strip.g.xteNm) * 1852 < 1000 ? (Math.abs(strip.g.xteNm) * 1852).toFixed(0) + " m" : Math.abs(strip.g.xteNm).toFixed(2) + " sm")
                      + (strip.g.xteNm > 0 ? " ◀" : strip.g.xteNm < 0 ? " ▶" : "")
                color: Theme.text; font.pixelSize: 20; font.bold: true; font.family: Theme.mono
            }
        }
        Column {
            Text { text: "TTG"; color: Theme.label; font.pixelSize: 13 }
            Text { text: strip.ttg(strip.g.ttgMin); color: Theme.text; font.pixelSize: 20; font.bold: true; font.family: Theme.mono }
        }
        TouchButton {
            visible: strip.g.mode === "route" && strip.g.leg + 1 < strip.g.legs
            text: "Nächster"
            fontSize: 15
            implicitHeight: 46
            onClicked: routes.nextWaypoint()
        }
        TouchButton {
            text: "Stopp"
            fontSize: 15
            implicitHeight: 46
            // MOB is stopped by holding: it must not end by an accidental touch
            onClicked: if (strip.g.mode !== "mob") routes.stopNavigation()
            onHeld: routes.stopNavigation()
        }
    }
}
