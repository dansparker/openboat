import QtQuick

// Raster chart in Web Mercator (EPSG:3857, XYZ tiles from MBTiles) with
// own ship, COG/heading vectors, AIS targets and the anchor circle.
// North-up; drag to pan (stops following the boat), "Folgen" re-centres.
Item {
    id: chart
    clip: true

    property var layers: []
    property int zoom: 14
    readonly property int minZoom: 3
    readonly property int maxZoom: 18
    property bool follow: true
    // View centre in world pixels at the current zoom
    property real centerX: 0
    property real centerY: 0
    property real vectorMinutes: 6  // length of the COG prediction vector

    readonly property real worldSize: 256 * Math.pow(2, zoom)

    function worldX(lon) { return (lon + 180) / 360 * worldSize; }
    function worldY(lat) {
        const s = Math.sin(Math.max(-85.0511, Math.min(85.0511, lat)) * Math.PI / 180);
        return (0.5 - Math.log((1 + s) / (1 - s)) / (4 * Math.PI)) * worldSize;
    }
    function screenX(lon) { return worldX(lon) - centerX + width / 2; }
    function screenY(lat) { return worldY(lat) - centerY + height / 2; }
    // Metres per screen pixel at a latitude
    function metresPerPixel(lat) { return 40075016.686 * Math.cos(lat * Math.PI / 180) / worldSize; }

    function recentre() {
        if (!boat.positionValid) return;
        centerX = worldX(boat.longitude);
        centerY = worldY(boat.latitude);
    }
    function setZoom(z) {
        z = Math.max(minZoom, Math.min(maxZoom, z));
        if (z === zoom) return;
        const f = Math.pow(2, z - zoom);
        centerX *= f;
        centerY *= f;
        zoom = z;
    }

    Connections {
        target: boat
        function onChanged() {
            if (chart.follow) chart.recentre();
            overlay.requestPaint();
        }
    }

    // ---- Tiles ----------------------------------------------------------------
    readonly property var tiles: {
        const n = Math.pow(2, zoom);
        const x0 = Math.floor((centerX - width / 2) / 256), x1 = Math.floor((centerX + width / 2) / 256);
        const y0 = Math.max(0, Math.floor((centerY - height / 2) / 256));
        const y1 = Math.min(n - 1, Math.floor((centerY + height / 2) / 256));
        const list = [];
        for (let y = y0; y <= y1; ++y)
            for (let x = x0; x <= x1; ++x)
                list.push({ x: x, y: y, wx: ((x % n) + n) % n });  // wrap at the date line
        return list;
    }

    Rectangle { anchors.fill: parent; color: "#a8c8e0" }  // shown where no chart exists

    Repeater {
        model: chart.layers
        delegate: Item {
            id: layer
            required property var modelData
            anchors.fill: parent
            opacity: Theme.chartDimming
            // Overzoom: beyond maxZoom the layer is simply not drawn (no blurry upscaling in v0.1)
            visible: chart.zoom >= modelData.minZoom && chart.zoom <= modelData.maxZoom
            Repeater {
                model: layer.visible ? chart.tiles : []
                delegate: Image {
                    required property var modelData
                    x: modelData.x * 256 - chart.centerX + chart.width / 2
                    y: modelData.y * 256 - chart.centerY + chart.height / 2
                    width: 256
                    height: 256
                    asynchronous: false  // SQLite connection belongs to the GUI thread
                    cache: true
                    source: "image://" + layer.modelData.id + "/" + chart.zoom + "/" + modelData.wx + "/" + modelData.y
                }
            }
        }
    }

    // ---- Vectors (own ship, AIS, anchor) --------------------------------------
    Canvas {
        id: overlay
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        function shipPolygon(ctx, x, y, angleDeg, size) {
            ctx.save();
            ctx.translate(x, y);
            ctx.rotate(angleDeg * Math.PI / 180);
            ctx.beginPath();
            ctx.moveTo(0, -size);
            ctx.lineTo(size * 0.55, size * 0.7);
            ctx.lineTo(-size * 0.55, size * 0.7);
            ctx.closePath();
            ctx.restore();
        }

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();

            // Anchor circle
            if (boat.anchorActive) {
                const ax = chart.screenX(boat.anchorLon), ay = chart.screenY(boat.anchorLat);
                const r = boat.anchorRadius / chart.metresPerPixel(boat.anchorLat);
                ctx.strokeStyle = String(Theme.anchor);
                ctx.lineWidth = 3;
                ctx.setLineDash([8, 6]);
                ctx.beginPath();
                ctx.arc(ax, ay, r, 0, 2 * Math.PI);
                ctx.stroke();
                ctx.setLineDash([]);
                ctx.fillStyle = String(Theme.anchor);
                ctx.font = "bold 22px sans-serif";
                ctx.fillText("⚓", ax - 9, ay + 8);
            }

            // AIS targets: triangle + COG vector
            for (const t of boat.aisTargets) {
                const x = chart.screenX(t.lon), y = chart.screenY(t.lat);
                const colour = t.dangerous ? String(Theme.danger) : t.lost ? String(Theme.aisLost) : String(Theme.ais);
                ctx.strokeStyle = colour;
                ctx.fillStyle = colour;
                ctx.lineWidth = t.dangerous ? 3 : 2;
                if (t.sogKn > 0.5 && !t.lost) {
                    const len = t.sogKn * 1852 / 60 * chart.vectorMinutes / chart.metresPerPixel(t.lat);
                    ctx.beginPath();
                    ctx.moveTo(x, y);
                    ctx.lineTo(x + len * Math.sin(t.cog * Math.PI / 180), y - len * Math.cos(t.cog * Math.PI / 180));
                    ctx.stroke();
                }
                shipPolygon(ctx, x, y, t.cog, 12);
                if (t.dangerous) ctx.fill(); else ctx.stroke();
                if (t.name) {
                    ctx.font = "14px sans-serif";
                    ctx.fillText(t.name, x + 14, y + 4);
                }
            }

            if (!boat.positionValid) return;
            const ox = chart.screenX(boat.longitude), oy = chart.screenY(boat.latitude);
            const mpp = chart.metresPerPixel(boat.latitude);

            // COG vector: where the boat will be in `vectorMinutes`
            if (boat.cogValid && boat.sogKn > 0.3) {
                const len = boat.sogKn * 1852 / 60 * chart.vectorMinutes / mpp;
                ctx.strokeStyle = String(Theme.accent);
                ctx.lineWidth = 3;
                ctx.beginPath();
                ctx.moveTo(ox, oy);
                ctx.lineTo(ox + len * Math.sin(boat.cog * Math.PI / 180), oy - len * Math.cos(boat.cog * Math.PI / 180));
                ctx.stroke();
            }
            // Heading line (fixed length)
            const hdg = boat.headingValid ? boat.heading : boat.cog;
            if (boat.headingValid) {
                ctx.strokeStyle = String(Theme.ownShip);
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.moveTo(ox, oy);
                ctx.lineTo(ox + 60 * Math.sin(hdg * Math.PI / 180), oy - 60 * Math.cos(hdg * Math.PI / 180));
                ctx.stroke();
            }
            shipPolygon(ctx, ox, oy, hdg, 16);
            ctx.fillStyle = String(Theme.ownShip);
            ctx.fill();
            ctx.strokeStyle = "black";
            ctx.lineWidth = 2;
            ctx.stroke();
        }
    }

    // ---- Interaction ----------------------------------------------------------
    MouseArea {
        anchors.fill: parent
        property real lastX: 0
        property real lastY: 0
        onPressed: mouse => { lastX = mouse.x; lastY = mouse.y; }
        onPositionChanged: mouse => {
            chart.follow = false;
            chart.centerX -= mouse.x - lastX;
            chart.centerY -= mouse.y - lastY;
            lastX = mouse.x;
            lastY = mouse.y;
            overlay.requestPaint();
        }
        onWheel: wheel => { chart.setZoom(chart.zoom + (wheel.angleDelta.y > 0 ? 1 : -1)); overlay.requestPaint(); }
    }
    PinchHandler {
        target: null
        property int startZoom: 14
        onActiveChanged: if (active) startZoom = chart.zoom
        onActiveScaleChanged: { chart.setZoom(startZoom + Math.round(Math.log2(activeScale))); overlay.requestPaint(); }
    }

    // Scale bar
    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 8
        readonly property real nm: 100 * chart.metresPerPixel(boat.positionValid ? boat.latitude : 47) / 1852
        text: "100 px = " + (nm < 0.1 ? (nm * 1852).toFixed(0) + " m" : nm.toFixed(2) + " sm") + "   Z" + chart.zoom
        color: "black"
        style: Text.Outline
        styleColor: "white"
        font.pixelSize: 16
    }

    // Attribution is a licence requirement of OpenStreetMap / OpenSeaMap data
    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 4
        text: chart.layers.length > 0 ? "© OpenStreetMap-Mitwirkende, OpenSeaMap" : "Keine Karte geladen – siehe docs/charts.md"
        color: "black"
        style: Text.Outline
        styleColor: "white"
        font.pixelSize: 12
    }

    Component.onCompleted: recentre()
}
