import QtQuick
import QtQuick.Layouts

// AIS target list (like the GPSMAP "AIS list"): emergency beacons first, then
// collision danger, then by range. Tap a row for details; "Auf Karte" centres it.
Rectangle {
    id: page
    signal closeRequested()
    signal showOnChart(real lat, real lon)

    // MMSI of the expanded row (also set by tapping a target on the chart)
    property var selected: 0

    color: Theme.panel
    border.color: Theme.panelBorder
    radius: 6

    function nm(v) { return v < 0 ? "—" : v < 0.5 ? (v * 1852).toFixed(0) + " m" : (v * settings.distanceFactor).toFixed(1) + " " + settings.distanceLabel; }
    function deg(v) { return v < 0 ? "—" : Math.round(v).toString().padStart(3, "0") + "°"; }
    function tcpa(t) { return t.tcpaMin < 0 || t.cpaNm < 0 ? "—" : t.tcpaMin < 1 ? "<1 min" : Math.round(t.tcpaMin) + " min"; }
    function kindText(t) {
        if (t.kind === "sart") return t.beaconTest ? "AIS-SART (Test)" : "AIS-SART – Rettungsinsel!";
        if (t.kind === "mob") return t.beaconTest ? "MOB-Sender (Test)" : "MOB-Sender – Person im Wasser!";
        if (t.kind === "epirb") return t.beaconTest ? "EPIRB (Test)" : "EPIRB – Seenotfall!";
        return "";
    }
    // ITU-R M.1371 ship type (first digit = category)
    function typeText(n) {
        if (n === 30) return "Fischerei";
        if (n === 31 || n === 32) return "Schleppverband";
        if (n === 33) return "Baggerei / Unterwasserarbeiten";
        if (n === 35) return "Militär";
        if (n === 36) return "Segelboot";
        if (n === 37) return "Sportboot";
        if (n === 50) return "Lotsenboot";
        if (n === 51) return "Such- und Rettungsfahrzeug";
        if (n === 52) return "Schlepper";
        if (n === 55) return "Behörde";
        if (n === 58) return "Krankentransport";
        const c = Math.floor(n / 10);
        return c === 4 ? "Hochgeschwindigkeitsfahrzeug" : c === 6 ? "Fahrgastschiff" : c === 7 ? "Frachtschiff"
             : c === 8 ? "Tanker" : n > 0 ? "Typ " + n : "unbekannt";
    }
    // ITU-R M.1371 table 74 (aid to navigation type)
    function atonText(n) {
        const names = ["", "Referenzpunkt", "RACON", "Bauwerk im Wasser", "", "Feuer", "Sektorenfeuer",
                       "Richtfeuer (Unterfeuer)", "Richtfeuer (Oberfeuer)", "Kardinalbake Nord", "Kardinalbake Ost",
                       "Kardinalbake Süd", "Kardinalbake West", "Backbord-Bake", "Steuerbord-Bake",
                       "Abzweigungsbake Backbord", "Abzweigungsbake Steuerbord", "Einzelgefahrbake", "Ansteuerungsbake",
                       "Sonderbake", "Kardinaltonne Nord", "Kardinaltonne Ost", "Kardinaltonne Süd", "Kardinaltonne West",
                       "Backbordtonne", "Steuerbordtonne", "Abzweigungstonne Backbord", "Abzweigungstonne Steuerbord",
                       "Einzelgefahrtonne", "Ansteuerungstonne", "Sondertonne", "Feuerschiff / Großtonne"];
        return names[n] || "Seezeichen";
    }
    function stationText(t) {
        if (t.station === "base") return "AIS-Basisstation";
        if (t.station === "aton") return (t.virtualAton ? "Virtuelles Seezeichen (nur Funksignal!) – " : "") + atonText(t.atonType)
                                         + (t.offPosition ? " – NICHT AUF POSITION" : "");
        return "";
    }
    function statusText(s) {
        return ["in Fahrt (Maschine)", "vor Anker", "manövrierunfähig", "manövrierbehindert", "tiefgangbehindert",
                "festgemacht", "auf Grund", "beim Fischen", "in Fahrt (Segel)"][s] || "";
    }
    function age(s) { return s < 60 ? Math.round(s) + " s" : Math.round(s / 60) + " min"; }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text { text: "AIS-Ziele (" + boat.aisTargets.length + ")"; color: Theme.text; font.pixelSize: 22; font.bold: true; Layout.fillWidth: true }
            TouchButton { text: "✕"; implicitHeight: 46; onClicked: page.closeRequested() }
        }

        // Column headings
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 8
            Repeater {
                model: [["Name / MMSI", 0], ["Distanz", 90], ["Peilung", 70], ["CPA", 90], ["TCPA", 70], ["Fahrt", 80]]
                Text {
                    required property var modelData
                    text: modelData[0]
                    color: Theme.label
                    font.pixelSize: 14
                    Layout.fillWidth: modelData[1] === 0
                    Layout.preferredWidth: modelData[1]
                    horizontalAlignment: modelData[1] === 0 ? Text.AlignLeft : Text.AlignRight
                }
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: list.implicitHeight
            clip: true

            Column {
                id: list
                width: parent.width
                spacing: 4

                // Safety related messages (AIS 12/14), newest first
                Repeater {
                    model: boat.aisMessages
                    Rectangle {
                        required property var modelData
                        readonly property bool urgent: modelData.kind !== "vessel" && modelData.text.indexOf("TEST") < 0
                        width: list.width
                        height: msgText.implicitHeight + 12
                        radius: 4
                        color: urgent ? (Theme.night ? "#3a0000" : "#8a1010") : Theme.button
                        border.color: Theme.buttonBorder
                        Text {
                            id: msgText
                            anchors.fill: parent
                            anchors.margins: 6
                            anchors.leftMargin: 8
                            wrapMode: Text.WordWrap
                            color: parent.urgent ? "white" : Theme.text
                            font.pixelSize: 16
                            text: "✉ " + modelData.sender + (modelData.addressed ? " an " + modelData.destination : "") + ": „" + modelData.text + "“  ·  vor "
                                  + (modelData.ageMin < 1 ? "<1" : Math.round(modelData.ageMin)) + " min"
                        }
                    }
                }

                Text {
                    visible: boat.aisTargets.length === 0
                    width: list.width
                    wrapMode: Text.WordWrap
                    color: Theme.label
                    font.pixelSize: 16
                    text: "Keine AIS-Ziele empfangen. AIS kommt über NMEA 0183 (!AIVDM) oder NMEA 2000 (PGN 129038/129039)."
                }

                Repeater {
                    model: boat.aisTargets
                    Rectangle {
                        id: row
                        required property var modelData
                        readonly property var t: modelData
                        readonly property bool beacon: t.kind !== "vessel" && !t.beaconTest
                        readonly property bool open: page.selected === t.mmsi
                        readonly property color fg: beacon || t.dangerous ? "white" : t.lost ? String(Theme.aisLost) : String(Theme.text)
                        width: list.width
                        height: body.implicitHeight + 12
                        radius: 4
                        color: beacon || t.dangerous ? (Theme.night ? "#3a0000" : "#8a1010") : Theme.button
                        border.color: open ? Theme.accent : Theme.buttonBorder

                        MouseArea { anchors.fill: parent; onClicked: page.selected = row.open ? 0 : row.t.mmsi }

                        ColumnLayout {
                            id: body
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 6
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    color: row.fg
                                    font.pixelSize: 17
                                    font.bold: row.beacon || row.t.dangerous
                                    text: (row.t.kind !== "vessel" ? page.kindText(row.t) + "  " : "")
                                          + (row.t.station === "base" ? "AIS-Basisstation " : row.t.station === "aton" ? (row.t.virtualAton ? "V-AIS " : "◇ ") : "")
                                          + (row.t.name || row.t.mmsi) + (row.t.offPosition ? " – nicht auf Position!" : "")
                                          + (row.t.lost ? " (verloren)" : "")
                                }
                                Text { Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight; color: row.fg; font.pixelSize: 17; font.family: Theme.mono; text: page.nm(row.t.rangeNm) }
                                Text { Layout.preferredWidth: 70; horizontalAlignment: Text.AlignRight; color: row.fg; font.pixelSize: 17; font.family: Theme.mono; text: page.deg(row.t.bearing) }
                                Text { Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight; color: row.fg; font.pixelSize: 17; font.family: Theme.mono; text: row.t.tcpaMin < 0 ? "—" : page.nm(row.t.cpaNm) }
                                Text { Layout.preferredWidth: 70; horizontalAlignment: Text.AlignRight; color: row.fg; font.pixelSize: 17; font.family: Theme.mono; text: page.tcpa(row.t) }
                                Text { Layout.preferredWidth: 80; horizontalAlignment: Text.AlignRight; color: row.fg; font.pixelSize: 17; font.family: Theme.mono; text: (row.t.sogKn * settings.speedFactor).toFixed(1) + " " + settings.speedLabel }
                            }

                            // Details
                            RowLayout {
                                visible: row.open
                                Layout.fillWidth: true
                                spacing: 12
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    color: row.fg
                                    font.pixelSize: 15
                                    text: {
                                        const t = row.t;
                                        const parts = ["MMSI " + t.mmsi + (t.station !== "vessel" ? "" : t.classB ? " · Klasse B" : " · Klasse A")];
                                        if (t.callsign) parts.push("Rufzeichen " + t.callsign);
                                        if (t.station !== "vessel") parts.push(page.stationText(t));
                                        else if (t.kind === "vessel") parts.push(page.typeText(t.shipType));
                                        if (t.lengthM > 0) parts.push(t.lengthM.toFixed(0) + " × " + t.beamM.toFixed(0) + " m");
                                        if (page.statusText(t.navStatus)) parts.push(page.statusText(t.navStatus));
                                        if (t.station === "vessel") parts.push("COG " + (t.hasCog ? page.deg(t.cog) : "—") + " · HDG " + page.deg(t.heading));
                                        parts.push("letzte Position vor " + page.age(t.ageS));
                                        return parts.join("  ·  ");
                                    }
                                }
                                TouchButton {
                                    text: "Auf Karte"
                                    fontSize: 15
                                    implicitHeight: 46
                                    onClicked: page.showOnChart(row.t.lat, row.t.lon)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
