import QtQuick
import QtQuick.Shapes

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
    property var waypoints: []      // saved waypoints [{name, lat, lon}]
    property var editPoints: []     // route being edited [{lat, lon}]
    property bool editing: false    // route edit mode: taps add points

    signal longPressed(real lat, real lon, real x, real y)
    signal tapped(real lat, real lon)

    onEditPointsChanged: overlay.requestPaint()
    onWaypointsChanged: overlay.requestPaint()

    readonly property real worldSize: 256 * Math.pow(2, zoom)

    // ---- Orientation ----------------------------------------------------------
    // Course-up: the chart content is rotated by -upDeg around the view centre,
    // and the boat sits lower in the view to show more of what lies ahead.
    readonly property bool courseUp: settings.orientation === "course"
    property real upDeg: 0
    readonly property real lookAhead: courseUp ? height * 0.25 : 0
    onCourseUpChanged: { updateUp(); recentre(); overlay.requestPaint(); }

    // Only rotate after a clear course change: a chart that wobbles with every
    // wave is unreadable. COG when moving, else the compass heading.
    function updateUp() {
        if (!courseUp) { upDeg = 0; return; }
        const target = boat.cogValid && boat.sogKn > 1.0 ? boat.cog : boat.headingValid ? boat.heading : upDeg;
        let diff = ((target - upDeg) % 360 + 540) % 360 - 180;
        if (Math.abs(diff) > 5) upDeg = (target % 360 + 360) % 360;
    }
    // Rotate a vector by a (degrees, clockwise on screen)
    function rot(x, y, a) {
        const r = a * Math.PI / 180, c = Math.cos(r), s = Math.sin(r);
        return { x: x * c - y * s, y: x * s + y * c };
    }
    // Screen position -> unrotated chart position
    function unrotate(x, y) {
        const v = rot(x - width / 2, y - height / 2, upDeg);
        return { x: width / 2 + v.x, y: height / 2 + v.y };
    }

    function worldX(lon) { return (lon + 180) / 360 * worldSize; }
    function worldY(lat) {
        const s = Math.sin(Math.max(-85.0511, Math.min(85.0511, lat)) * Math.PI / 180);
        return (0.5 - Math.log((1 + s) / (1 - s)) / (4 * Math.PI)) * worldSize;
    }
    function lonAt(x) { return (x - width / 2 + centerX) / worldSize * 360 - 180; }
    function latAt(y) {
        const n = Math.PI * (1 - 2 * (y - height / 2 + centerY) / worldSize);
        return Math.atan(Math.sinh(n)) * 180 / Math.PI;
    }
    function screenX(lon) { return worldX(lon) - centerX + width / 2; }
    function screenY(lat) { return worldY(lat) - centerY + height / 2; }
    // Metres per screen pixel at a latitude
    function metresPerPixel(lat) { return 40075016.686 * Math.cos(lat * Math.PI / 180) / worldSize; }

    function recentre() {
        if (!boat.positionValid) return;
        const r = upDeg * Math.PI / 180;
        centerX = worldX(boat.longitude) + lookAhead * Math.sin(r);
        centerY = worldY(boat.latitude) - lookAhead * Math.cos(r);
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
            chart.updateUp();
            if (chart.follow) chart.recentre();
            overlay.requestPaint();
        }
    }

    // ---- Tiles ----------------------------------------------------------------
    // Beyond the highest zoom level of a layer its tiles are enlarged (overzoom, if
    // enabled in the settings) - the chart then looks more detailed than its data is,
    // so the view says so.
    readonly property bool overzoomed: settings.overzoom && layers.some(l => zoom > l.maxZoom && zoom - l.maxZoom <= 6)
    readonly property bool beyondChart: layers.length > 0 && layers.every(l => zoom > l.maxZoom) && !overzoomed

    Rectangle { anchors.fill: parent; color: Theme.water }  // shown where no chart exists

    Repeater {
        model: chart.layers
        delegate: Item {
            id: chartLayer  // not "layer": every Item has a built-in "layer" property that would shadow it
            required property int index
            readonly property var info: chart.layers[index] || ({})
            readonly property string layerId: info.provider || ""
            readonly property int tileZoom: Math.min(chart.zoom, info.maxZoom || 0)
            readonly property real tileScale: Math.pow(2, chart.zoom - tileZoom)  // > 1 when overzoomed
            readonly property real tileSize: 256 * tileScale
            anchors.fill: parent
            opacity: Theme.chartDimming
            transform: Rotation { origin.x: chart.width / 2; origin.y: chart.height / 2; angle: -chart.upDeg }
            // Max. 6 levels of overzoom: beyond that a tile is a few blurred pixels
            visible: layerId !== "" && chart.zoom >= info.minZoom
                     && (chart.zoom <= info.maxZoom || (settings.overzoom && chart.zoom - info.maxZoom <= 6))

            // The visible tile range as a string: the tile list (and the Images) are only
            // rebuilt when the range changes, not on every pixel of panning - matters on a Pi
            readonly property string tileKey: {
                if (!visible) return "";
                const n = Math.pow(2, tileZoom);
                // rotated view: cover the circle around the centre, not just the rectangle
                const half = chart.upDeg !== 0 ? Math.hypot(chart.width, chart.height) / 2 : -1;
                const hw = half > 0 ? half : chart.width / 2, hh = half > 0 ? half : chart.height / 2;
                const x0 = Math.floor((chart.centerX - hw) / tileSize);
                const x1 = Math.floor((chart.centerX + hw) / tileSize);
                const y0 = Math.max(0, Math.floor((chart.centerY - hh) / tileSize));
                const y1 = Math.min(n - 1, Math.floor((chart.centerY + hh) / tileSize));
                return [tileZoom, x0, x1, y0, y1].join("/");
            }
            readonly property var tiles: {
                if (tileKey === "") return [];
                const [z, x0, x1, y0, y1] = tileKey.split("/").map(Number);
                const n = Math.pow(2, z);
                const list = [];
                for (let y = y0; y <= y1; ++y)
                    for (let x = x0; x <= x1; ++x)
                        list.push({ x: x, y: y, wx: ((x % n) + n) % n });  // wrap at the date line
                return list;
            }

            Repeater {
                model: chartLayer.tiles
                delegate: Image {
                    required property var modelData
                    x: modelData.x * chartLayer.tileSize - chart.centerX + chart.width / 2
                    y: modelData.y * chartLayer.tileSize - chart.centerY + chart.height / 2
                    width: chartLayer.tileSize
                    height: chartLayer.tileSize
                    smooth: true
                    asynchronous: false  // SQLite connection belongs to the GUI thread
                    cache: true
                    source: "image://" + chartLayer.layerId + "/" + chartLayer.tileZoom + "/" + modelData.wx + "/" + modelData.y
                }
            }
        }
    }

    // Overzoom warning / no chart at this zoom level
    Rectangle {
        visible: chart.overzoomed || chart.beyondChart
        // top left, below the alarm banner; the bottom belongs to the guidance strip
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 8
        anchors.topMargin: 90
        width: zoomNote.implicitWidth + 16
        height: zoomNote.implicitHeight + 8
        radius: 4
        color: Theme.night ? "#300000" : "#fff3b0"
        border.color: Theme.night ? "#802020" : "#c09000"
        Text {
            id: zoomNote
            anchors.centerIn: parent
            text: chart.overzoomed ? "OVERZOOM – Karte vergrößert, Details ungenau"
                                   : "Keine Karte für diese Zoomstufe (Overzoom in Setup)"
            color: Theme.night ? "#c03030" : "#5a4000"
            font.pixelSize: 14
            font.bold: true
        }
    }

    // ---- Tracks -----------------------------------------------------------------
    // GPU-drawn polylines. Points are converted to world pixels only when the
    // points or the zoom change; panning just moves the item. Coordinates are
    // relative to the first point: absolute world pixels at zoom 18 exceed float
    // precision in the scene graph.
    component TrackLine: Item {
        id: line
        property var points: []
        property color colour: Theme.track
        property real lineWidth: 3
        property var dash: []
        property real originX: 0
        property real originY: 0
        x: originX - chart.centerX + chart.width / 2
        y: originY - chart.centerY + chart.height / 2
        visible: settings.showTrack && path.path.length > 1
        // rotation origin in own coordinates = view centre
        transform: Rotation { origin.x: chart.width / 2 - line.x; origin.y: chart.height / 2 - line.y; angle: -chart.upDeg }

        function rebuild() {
            const pts = points;
            if (!pts || pts.length === 0) { path.path = []; return; }
            originX = chart.worldX(pts[0].lon);
            originY = chart.worldY(pts[0].lat);
            const out = new Array(pts.length);
            for (let i = 0; i < pts.length; ++i)
                out[i] = Qt.point(chart.worldX(pts[i].lon) - originX, chart.worldY(pts[i].lat) - originY);
            path.path = out;
        }
        onPointsChanged: rebuild()
        Connections {
            target: chart
            function onZoomChanged() { line.rebuild(); }
        }

        Shape {
            ShapePath {
                strokeColor: line.colour
                strokeWidth: line.lineWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                strokeStyle: line.dash.length > 0 ? ShapePath.DashLine : ShapePath.SolidLine
                dashPattern: line.dash.length > 0 ? line.dash : [4, 2]
                PathPolyline { id: path }
            }
        }
    }

    // Earlier days (dashed, lighter), below today
    Repeater {
        model: boat.trackHistory
        TrackLine {
            required property var modelData
            points: modelData.points
            colour: Theme.trackOld
            lineWidth: 2.5
            dash: [3, 2]
        }
    }
    TrackLine { points: boat.track }

    // ---- Vectors (own ship, AIS, anchor) --------------------------------------
    Canvas {
        id: overlay
        anchors.fill: parent
        transform: Rotation { origin.x: chart.width / 2; origin.y: chart.height / 2; angle: -chart.upDeg }
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

        function line(ctx, pts, colour, width, dash) {
            if (pts.length < 2) return;
            ctx.strokeStyle = colour;
            ctx.lineWidth = width;
            ctx.setLineDash(dash || []);
            ctx.beginPath();
            ctx.moveTo(chart.screenX(pts[0].lon), chart.screenY(pts[0].lat));
            for (let i = 1; i < pts.length; ++i) ctx.lineTo(chart.screenX(pts[i].lon), chart.screenY(pts[i].lat));
            ctx.stroke();
            ctx.setLineDash([]);
        }

        function mark(ctx, p, colour, label) {
            const x = chart.screenX(p.lon), y = chart.screenY(p.lat);
            ctx.strokeStyle = colour;
            ctx.lineWidth = 3;
            ctx.beginPath();
            ctx.arc(x, y, 7, 0, 2 * Math.PI);
            ctx.stroke();
            if (label) {
                ctx.fillStyle = colour;
                ctx.font = "bold 15px sans-serif";
                ctx.fillText(label, x + 10, y - 8);
            }
        }

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const nav = boat.guidance;
            const magenta = String(Theme.route);

            // Saved waypoints
            for (const w of chart.waypoints) mark(ctx, w, String(Theme.waypoint), w.name);

            // Active navigation: active leg solid, rest of the route dashed
            if (nav.active && nav.mode !== "mob") {
                const pts = nav.points;
                if (nav.from) line(ctx, [nav.from, pts[0]], magenta, 4);
                else if (boat.positionValid) line(ctx, [{ lat: boat.latitude, lon: boat.longitude }, pts[0]], magenta, 4);
                line(ctx, pts, magenta, 3, [10, 8]);
                for (let i = 0; i < pts.length; ++i) mark(ctx, pts[i], magenta, pts[i].name);
            }

            // Route being edited
            const orange = String(Theme.anchor);
            line(ctx, chart.editPoints, orange, 3);
            for (let i = 0; i < chart.editPoints.length; ++i) mark(ctx, chart.editPoints[i], orange, String(i + 1));

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

            // Man overboard: big marker and a line back to it
            if (nav.active && nav.mode === "mob") {
                const m = nav.points[0];
                const mx = chart.screenX(m.lon), my = chart.screenY(m.lat);
                if (boat.positionValid) line(ctx, [{ lat: boat.latitude, lon: boat.longitude }, m], String(Theme.danger), 4);
                ctx.fillStyle = String(Theme.danger);
                ctx.beginPath();
                ctx.arc(mx, my, 14, 0, 2 * Math.PI);
                ctx.fill();
                ctx.fillStyle = "white";
                ctx.font = "bold 13px sans-serif";
                ctx.fillText("MOB", mx - 15, my + 5);
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
        property bool moved: false
        onPressed: mouse => { lastX = mouse.x; lastY = mouse.y; moved = false; }
        onPositionChanged: mouse => {
            // A tap with a slightly wet finger jitters: only a real drag stops following the boat
            if (!moved && Math.abs(mouse.x - lastX) + Math.abs(mouse.y - lastY) < 12) return;
            moved = true;
            chart.follow = false;
            const d = chart.rot(mouse.x - lastX, mouse.y - lastY, chart.upDeg);  // screen -> chart direction
            chart.centerX -= d.x;
            chart.centerY -= d.y;
            lastX = mouse.x;
            lastY = mouse.y;
            overlay.requestPaint();
        }
        onClicked: mouse => {
            if (moved) return;
            const p = chart.unrotate(mouse.x, mouse.y);
            chart.tapped(chart.latAt(p.y), chart.lonAt(p.x));
        }
        onPressAndHold: mouse => {
            if (moved) return;
            const p = chart.unrotate(mouse.x, mouse.y);
            chart.longPressed(chart.latAt(p.y), chart.lonAt(p.x), mouse.x, mouse.y);
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
        text: "100 px = " + (nm < 0.1 ? (nm * 1852).toFixed(0) + " m" : (nm * settings.distanceFactor).toFixed(2) + " " + settings.distanceLabel) + "   Z" + chart.zoom
        color: Theme.overlayText
        style: Text.Outline
        styleColor: Theme.overlayOutline
        font.pixelSize: 16
    }

    // Attribution is a licence requirement of OpenStreetMap / OpenSeaMap data
    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 4
        // Source and licence of every loaded chart (licence requirement; also shows
        // e.g. the resolution of depth data, or that data is synthetic)
        text: chart.layers.length === 0 ? "Keine Karte geladen – siehe docs/charts.md"
              : chart.layers.map(l => l.attribution || l.name).filter((v, i, a) => v && a.indexOf(v) === i).join("  ·  ")
        color: Theme.overlayText
        style: Text.Outline
        styleColor: Theme.overlayOutline
        font.pixelSize: 12
    }

    Component.onCompleted: recentre()

    // North arrow; tap toggles north-up / course-up
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        anchors.topMargin: 90
        width: 54
        height: 54
        radius: 27
        color: Theme.panel
        border.color: chart.courseUp ? Theme.accent : Theme.panelBorder
        border.width: 2
        Item {
            anchors.fill: parent
            rotation: -chart.upDeg
            Text { anchors.horizontalCenter: parent.horizontalCenter; y: 3; text: "N"; color: Theme.text; font.pixelSize: 15; font.bold: true }
            Text { anchors.centerIn: parent; anchors.verticalCenterOffset: 6; text: "▲"; color: Theme.danger; font.pixelSize: 16 }
        }
        Text {
            anchors.top: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 2
            text: chart.courseUp ? "Kurs oben" : "Nord oben"
            color: Theme.overlayText
            style: Text.Outline
            styleColor: Theme.overlayOutline
            font.pixelSize: 12
        }
        MouseArea {
            anchors.fill: parent
            onClicked: settings.orientation = chart.courseUp ? "north" : "course"
        }
    }
}
