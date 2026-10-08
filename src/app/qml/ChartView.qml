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
    property bool editing: false    // route edit mode: tap appends, (+) inserts, drag moves, hold deletes
    property int editRouteIndex: -1  // route being changed, -1 = new route

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
    // Shows a position (AIS target, waypoint) without following the boat
    function centreOn(lat, lon) {
        follow = false;
        centerX = worldX(lon);
        centerY = worldY(lat);
        overlay.requestPaint();
    }
    // ---- Route editing (unrotated chart pixels) ----
    function legMid(i) {
        const a = editPoints[i], b = editPoints[i + 1];
        return { x: (screenX(a.lon) + screenX(b.lon)) / 2, y: (screenY(a.lat) + screenY(b.lat)) / 2 };
    }
    // Index of the edit point / leg handle under an unrotated chart pixel, else -1
    function editPointAt(x, y) {
        for (let i = editPoints.length - 1; i >= 0; --i) {
            if (Math.hypot(screenX(editPoints[i].lon) - x, screenY(editPoints[i].lat) - y) < 24) return i;
        }
        return -1;
    }
    function editLegAt(x, y) {
        for (let i = 0; i + 1 < editPoints.length; ++i) {
            const m = legMid(i);
            if (Math.hypot(m.x - x, m.y - y) < 22) return i;
        }
        return -1;
    }
    function setEditPoint(i, lat, lon) {
        const pts = editPoints.slice();
        pts[i] = { lat: lat, lon: lon, name: pts[i].name };
        editPoints = pts;
    }

    // AIS target within a finger's width of a position (tap), else null
    function aisAt(lat, lon) {
        let best = null, bestD = 30;
        for (const t of boat.aisTargets) {
            const d = Math.hypot(worldX(t.lon) - worldX(lon), worldY(t.lat) - worldY(lat));
            if (d < bestD) { best = t; bestD = d; }
        }
        return best;
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
                            + (chartLayer.info.depth ? "?s=" + settings.safetyDepth.toFixed(1) : "")
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
            if (label) texts.push({ x: x, y: y, dx: 10, dy: -10, text: label, font: "bold 15px sans-serif", colour: colour, align: "left" });
        }

        // Text drawn by the upright label canvas: [{x, y (unrotated), dx, dy (upright offset), text, font, colour, align}]
        property var texts: []

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            texts = [];
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

            // Shallow spots found by the route check
            for (const m of (routes.routeCheck.marks || [])) {
                const x = chart.screenX(m.lon), y = chart.screenY(m.lat);
                ctx.strokeStyle = String(Theme.danger);
                ctx.lineWidth = 4;
                ctx.beginPath();
                ctx.moveTo(x - 10, y - 10); ctx.lineTo(x + 10, y + 10);
                ctx.moveTo(x + 10, y - 10); ctx.lineTo(x - 10, y + 10);
                ctx.stroke();
                texts.push({ x: x, y: y, dx: 14, dy: 0, text: settings.depthText(m.depth), font: "bold 15px sans-serif", colour: String(Theme.danger), align: "left" });
            }

            // Route being edited
            const orange = String(Theme.anchor);
            line(ctx, chart.editPoints, orange, 3);
            for (let i = 0; i < chart.editPoints.length; ++i) mark(ctx, chart.editPoints[i], orange, String(i + 1));
            // Insert handles in the middle of each leg
            for (let i = 0; i + 1 < chart.editPoints.length; ++i) {
                const m = chart.legMid(i);
                ctx.fillStyle = orange;
                ctx.beginPath();
                ctx.arc(m.x, m.y, 9, 0, 2 * Math.PI);
                ctx.fill();
                texts.push({ x: m.x, y: m.y, dx: 0, dy: 0, text: "+", font: "bold 16px sans-serif", colour: "white", align: "center" });
            }

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
                texts.push({ x: ax, y: ay, dx: 0, dy: 0, text: "⚓", font: "bold 22px sans-serif", colour: String(Theme.anchor), align: "center" });
            }

            // AIS targets: triangle + COG vector
            for (const t of boat.aisTargets) {
                const x = chart.screenX(t.lon), y = chart.screenY(t.lat);
                if (t.station !== "vessel") {
                    // Aid to navigation: diamond (virtual: dashed, only a radio signal, no
                    // real mark!); base station: square. Drifted buoy: red with a cross.
                    const c = t.offPosition ? String(Theme.danger) : t.lost ? String(Theme.aisLost) : String(Theme.route);
                    ctx.strokeStyle = c;
                    ctx.lineWidth = 2;
                    ctx.setLineDash(t.virtualAton ? [4, 3] : []);
                    ctx.beginPath();
                    if (t.station === "aton") {
                        ctx.moveTo(x, y - 10); ctx.lineTo(x + 8, y); ctx.lineTo(x, y + 10); ctx.lineTo(x - 8, y); ctx.closePath();
                    } else {
                        ctx.rect(x - 7, y - 7, 14, 14);
                    }
                    if (t.offPosition) { ctx.moveTo(x - 6, y - 6); ctx.lineTo(x + 6, y + 6); ctx.moveTo(x + 6, y - 6); ctx.lineTo(x - 6, y + 6); }
                    ctx.stroke();
                    ctx.setLineDash([]);
                    const label = t.station === "base" ? "AIS-Basis" : (t.virtualAton ? "V-AIS " : "") + (t.name || "AIS-Seezeichen");
                    texts.push({ x: x, y: y, dx: 12, dy: 0, text: label, font: "12px sans-serif", colour: c, align: "left" });
                    continue;
                }
                // Where the target has been (last 10 min): dotted
                if (t.trail && t.trail.length > 0 && !t.lost) {
                    ctx.strokeStyle = t.dangerous ? String(Theme.danger) : String(Theme.ais);
                    ctx.lineWidth = 2;
                    ctx.setLineDash([2, 5]);
                    ctx.beginPath();
                    ctx.moveTo(chart.screenX(t.trail[0].lon), chart.screenY(t.trail[0].lat));
                    for (let k = 1; k < t.trail.length; ++k) ctx.lineTo(chart.screenX(t.trail[k].lon), chart.screenY(t.trail[k].lat));
                    ctx.lineTo(x, y);
                    ctx.stroke();
                    ctx.setLineDash([]);
                }
                if (t.kind !== "vessel") {
                    // Emergency beacon (S-52 style): red circle with a cross
                    const c = t.beaconTest ? String(Theme.aisLost) : String(Theme.danger);
                    ctx.strokeStyle = c;
                    ctx.lineWidth = 3;
                    ctx.beginPath();
                    ctx.arc(x, y, 11, 0, 2 * Math.PI);
                    ctx.moveTo(x - 8, y - 8); ctx.lineTo(x + 8, y + 8);
                    ctx.moveTo(x + 8, y - 8); ctx.lineTo(x - 8, y + 8);
                    ctx.stroke();
                    texts.push({ x: x, y: y, dx: 16, dy: 0, text: t.kind === "sart" ? "AIS-SART" : t.kind === "mob" ? "MOB" : "EPIRB",
                                 font: "bold 14px sans-serif", colour: c, align: "left" });
                    continue;
                }
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
                if (t.name) texts.push({ x: x, y: y, dx: 16, dy: 0, text: t.name, font: "14px sans-serif", colour: colour, align: "left" });
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
                texts.push({ x: mx, y: my, dx: 0, dy: 0, text: "MOB", font: "bold 13px sans-serif", colour: "white", align: "center" });
            }
            labelCanvas.requestPaint();

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

    // ---- Labels (always upright) ----------------------------------------------
    // Chart labels come from the *.labels.json sidecars: already placed without
    // overlaps per zoom level by the chart tools, so only the visible ones are drawn.
    // Converted to world pixels only when the zoom or the charts change.
    readonly property var chartLabels: {
        const out = [];
        for (const l of layers) {
            if (!l.labels || zoom < l.minZoom) continue;
            const ez = Math.min(zoom, l.maxZoom);  // overzoom: the label set of the highest level
            if (zoom - ez > 6 || (zoom > l.maxZoom && !settings.overzoom)) continue;
            for (const lab of l.labels) {
                if (lab.z.indexOf(ez) < 0) continue;
                out.push({ wx: worldX(lab.lon), wy: worldY(lab.lat), text: lab.text, kind: lab.kind });
            }
        }
        return out;
    }
    onChartLabelsChanged: labelCanvas.requestPaint()

    Canvas {
        id: labelCanvas
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        // unrotated chart position -> screen (rotated around the view centre)
        function toScreen(x, y) {
            const v = chart.rot(x - chart.width / 2, y - chart.height / 2, -chart.upDeg);
            return { x: chart.width / 2 + v.x, y: chart.height / 2 + v.y };
        }
        function draw(ctx, x, y, text, font, colour, halo, align) {
            ctx.font = font;
            ctx.textAlign = align;
            ctx.textBaseline = "middle";
            ctx.lineWidth = 3;
            ctx.strokeStyle = halo;
            ctx.strokeText(text, x, y);
            ctx.fillStyle = colour;
            ctx.fillText(text, x, y);
        }

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const halo = Theme.night ? "black" : "rgba(244,236,210,0.9)";
            const place = Theme.night ? "#a03030" : "#282828";
            const depth = Theme.night ? "#802020" : "#1e325a";
            const depthHalo = Theme.night ? "black" : "rgba(235,242,250,0.95)";
            if (settings.showLabels) {
                for (const l of chart.chartLabels) {
                    const p = toScreen(l.wx - chart.centerX + chart.width / 2, l.wy - chart.centerY + chart.height / 2);
                    if (p.x < -100 || p.y < -20 || p.x > width + 100 || p.y > height + 20) continue;
                    if (l.kind === "depth") draw(ctx, p.x, p.y, l.text, "11px sans-serif", depth, depthHalo, "center");
                    else draw(ctx, p.x, p.y, l.text, (l.kind === "city" || l.kind === "town") ? "bold 16px sans-serif" : "13px sans-serif",
                              place, halo, "center");
                }
            }
            for (const t of overlay.texts) {
                const p = toScreen(t.x, t.y);
                draw(ctx, p.x + t.dx, p.y + t.dy, t.text, t.font, t.colour, Theme.night ? "black" : "rgba(255,255,255,0.8)", t.align);
            }
        }
    }

    // ---- Interaction ----------------------------------------------------------
    MouseArea {
        anchors.fill: parent
        property real lastX: 0
        property real lastY: 0
        property bool moved: false
        property int dragPoint: -1  // route editing: point being moved
        onPressed: mouse => {
            lastX = mouse.x; lastY = mouse.y; moved = false;
            const p = chart.unrotate(mouse.x, mouse.y);
            dragPoint = chart.editing ? chart.editPointAt(p.x, p.y) : -1;
        }
        onPositionChanged: mouse => {
            // A tap with a slightly wet finger jitters: only a real drag stops following the boat
            if (!moved && Math.abs(mouse.x - lastX) + Math.abs(mouse.y - lastY) < 12) return;
            moved = true;
            if (dragPoint >= 0) {
                const p = chart.unrotate(mouse.x, mouse.y);
                chart.setEditPoint(dragPoint, chart.latAt(p.y), chart.lonAt(p.x));
                overlay.requestPaint();
                return;
            }
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
            if (chart.editing) {
                const pts = chart.editPoints.slice();
                const leg = chart.editLegAt(p.x, p.y);
                if (chart.editPointAt(p.x, p.y) >= 0) return;  // tap on a point: nothing (drag moves, hold deletes)
                if (leg >= 0) pts.splice(leg + 1, 0, { lat: chart.latAt(p.y), lon: chart.lonAt(p.x) });
                else pts.push({ lat: chart.latAt(p.y), lon: chart.lonAt(p.x) });
                chart.editPoints = pts;
                overlay.requestPaint();
                return;
            }
            chart.tapped(chart.latAt(p.y), chart.lonAt(p.x));
        }
        onPressAndHold: mouse => {
            if (moved) return;
            const p = chart.unrotate(mouse.x, mouse.y);
            if (chart.editing) {
                const i = chart.editPointAt(p.x, p.y);
                if (i >= 0) {
                    const pts = chart.editPoints.slice();
                    pts.splice(i, 1);
                    chart.editPoints = pts;
                    overlay.requestPaint();
                }
                return;
            }
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
