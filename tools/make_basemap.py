#!/usr/bin/env python3
"""Render an offline base chart (MBTiles) from OpenStreetMap data.

OpenSeaMap only provides the seamark overlay; the map underneath is
OpenStreetMap, whose tile servers forbid bulk downloads. The OSM *data*
however is free (ODbL), so this tool renders its own chart-style base map:
land, water, shoreline, rivers, marinas, piers, bridges and place names.

Data source (one of):
  default        Overpass API, for small areas (a lake, a stretch of river)
  --osm-json F   a saved Overpass JSON result (for repeat runs / offline)

Coasts (seas): OSM has no closed sea polygons. Pass the free "land polygons"
shapefile from https://osmdata.openstreetmap.de/data/land-polygons.html
(land-polygons-split-4326) with --land-polygons; needs `pip install pyshp`.

Example (Attersee):
    python tools/make_basemap.py --bbox 13.47,47.78,13.62,47.96 --zooms 10-16 --out charts/base.mbtiles

Needs Pillow (`pip install pillow`). Output tiles are 256 px PNG, XYZ scheme.
"""

import argparse
import io
import json
import math
import sys
import urllib.parse
import urllib.request

from fetch_tiles import USER_AGENT, open_mbtiles, tile_range

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow is required: pip install pillow")

OVERPASS = "https://overpass-api.de/api/interpreter"
MAX_AREA_DEG2 = 0.25  # Overpass fair use; larger areas: --osm-json from your own extract

# Paper-chart inspired colours
LAND = (244, 236, 210)
WATER = (200, 225, 240)
SHORE = (90, 90, 90)
MARINA = (170, 205, 230)
STRUCTURE = (70, 70, 70)
TEXT = (40, 40, 40)

SS = 2  # supersampling factor (anti-aliasing)


# ---- data --------------------------------------------------------------------

def overpass_query(bbox):
    lon_min, lat_min, lon_max, lat_max = bbox
    b = f"{lat_min},{lon_min},{lat_max},{lon_max}"
    return f"""[out:json][timeout:180];
(
  way["natural"="water"]({b}); relation["natural"="water"]({b});
  way["waterway"="riverbank"]({b}); relation["waterway"="riverbank"]({b});
  way["waterway"~"^(river|canal)$"]({b});
  way["natural"="coastline"]({b});
  way["leisure"="marina"]({b}); relation["leisure"="marina"]({b});
  way["man_made"~"^(pier|breakwater|groyne)$"]({b});
  way["bridge"="yes"]["highway"]({b});
  node["place"~"^(city|town|village|hamlet)$"]({b});
);
out geom;"""


def fetch_overpass(bbox):
    data = urllib.parse.urlencode({"data": overpass_query(bbox)}).encode()
    req = urllib.request.Request(OVERPASS, data=data, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=300) as r:
        return json.load(r)


def join_rings(ways):
    """Joins way geometries (lists of (lon, lat)) into closed rings."""
    rings, open_ways = [], [list(w) for w in ways if len(w) >= 2]
    while open_ways:
        ring = open_ways.pop()
        changed = True
        while ring[0] != ring[-1] and changed:
            changed = False
            for i, w in enumerate(open_ways):
                if w[0] == ring[-1]:
                    ring += w[1:]
                elif w[-1] == ring[-1]:
                    ring += w[::-1][1:]
                elif w[-1] == ring[0]:
                    ring = w[:-1] + ring
                elif w[0] == ring[0]:
                    ring = w[::-1][:-1] + ring
                else:
                    continue
                open_ways.pop(i)
                changed = True
                break
        if len(ring) >= 4 and ring[0] == ring[-1]:
            rings.append(ring)
    return rings


def geometry(el):
    return [(p["lon"], p["lat"]) for p in el.get("geometry", []) if p]


def parse(osm):
    """-> dict of feature lists, each item (kind, outer_rings, inner_rings | line | point, tags)."""
    f = {"water": [], "marina": [], "river": [], "structure": [], "bridge": [], "place": [], "coastline": 0}
    for el in osm.get("elements", []):
        t = el.get("tags", {})
        if el["type"] == "node":
            if "name" in t:
                f["place"].append(((el["lon"], el["lat"]), t["name"], t.get("place")))
            continue
        if el["type"] == "relation":
            outer = join_rings([geometry(m) for m in el.get("members", []) if m.get("role") == "outer"])
            inner = join_rings([geometry(m) for m in el.get("members", []) if m.get("role") == "inner"])
        else:
            g = geometry(el)
            outer, inner = ([g], []) if len(g) >= 4 and g[0] == g[-1] else ([], [])
            line = g
        if t.get("natural") == "coastline":
            f["coastline"] += 1
        elif t.get("leisure") == "marina":
            f["marina"] += [(outer, inner)]
        elif t.get("natural") == "water" or t.get("waterway") == "riverbank":
            f["water"] += [(outer, inner)]
        elif t.get("waterway") in ("river", "canal") and el["type"] == "way":
            f["river"].append(line)
        elif t.get("man_made") in ("pier", "breakwater", "groyne") and el["type"] == "way":
            f["structure"].append(line)
        elif t.get("bridge") == "yes" and el["type"] == "way":
            f["bridge"].append(line)
    return f


def load_land_polygons(path, bbox):
    try:
        import shapefile  # pyshp
    except ImportError:
        sys.exit("--land-polygons needs pyshp: pip install pyshp")
    polys = []
    with shapefile.Reader(path) as sf:
        for shape in sf.iterShapes(bbox=list(bbox)):
            parts = list(shape.parts) + [len(shape.points)]
            rings = [[tuple(p) for p in shape.points[parts[i]:parts[i + 1]]] for i in range(len(parts) - 1)]
            # Shapefile: outer rings clockwise, holes counter-clockwise
            outer = [r for r in rings if signed_area(r) < 0]
            inner = [r for r in rings if signed_area(r) >= 0]
            polys.append((outer, inner))
    return polys


def signed_area(ring):
    return sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(ring, ring[1:])) / 2


# ---- rendering ---------------------------------------------------------------

def world(lon, lat, z):
    """Web Mercator world pixel coordinates at zoom z."""
    n = 256 * (1 << z)
    lat = max(-85.0511, min(85.0511, lat))
    s = math.sin(math.radians(lat))
    return (lon + 180) / 360 * n, (0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)) * n


def bounds(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return min(xs), min(ys), max(xs), max(ys)


def overlaps(b, x0, y0, size, margin=0):
    return not (b[2] < x0 - margin or b[0] > x0 + size + margin or b[3] < y0 - margin or b[1] > y0 + size + margin)


class Layer:
    """Features projected to one zoom level, with bounding boxes for culling."""

    def __init__(self, f, land, z):
        def proj(ring):
            return [world(lon, lat, z) for lon, lat in ring]

        def polys(items):
            out = []
            for outer, inner in items:
                o = [proj(r) for r in outer]
                if o:
                    out.append((bounds([p for r in o for p in r]), o, [proj(r) for r in inner]))
            return out

        self.land = polys(land)
        self.water = polys(f["water"])
        self.marina = polys(f["marina"])
        self.lines = {k: [(bounds(p), p) for p in (proj(l) for l in f[k]) if len(p) >= 2]
                      for k in ("river", "structure", "bridge")}
        self.places = place_labels(f["place"], z)


RANK = {"city": 0, "town": 1, "village": 2, "hamlet": 3}
MIN_ZOOM = {"city": 8, "town": 10, "village": 12, "hamlet": 14}


def label_size(kind):
    return 16 if kind in ("city", "town") else 13


def place_labels(places, z):
    """Greedy label placement: important places first, overlapping labels dropped."""
    placed, boxes = [], []
    for (lon, lat), name, kind in sorted(places, key=lambda p: RANK.get(p[2], 9)):
        if z < MIN_ZOOM.get(kind, 14):
            continue
        wx, wy = world(lon, lat, z)
        l, t, r, b = font(label_size(kind)).getbbox(name)
        w, h = r - l + 6, b - t + 6
        box = (wx - w / 2, wy - h / 2, wx + w / 2, wy + h / 2)
        if any(not (box[2] < o[0] or box[0] > o[2] or box[3] < o[1] or box[1] > o[3]) for o in boxes):
            continue
        boxes.append(box)
        placed.append(((wx, wy), name, kind))
    return placed


_fonts = {}


def font(size):
    if size in _fonts:
        return _fonts[size]
    _fonts[size] = _load_font(size)
    return _fonts[size]


def _load_font(size):
    for name in ("DejaVuSans.ttf", "arial.ttf", "Arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default(size=size)


def render_tile(layer, z, x, y, sea_background):
    size = 256 * SS
    x0, y0 = x * 256, y * 256
    img = Image.new("RGB", (size, size), WATER if sea_background else LAND)
    d = ImageDraw.Draw(img)

    def px(points):
        return [((wx - x0) * SS, (wy - y0) * SS) for wx, wy in points]

    def fill(polys, colour, hole_colour, outline=None):
        for b, outer, inner in polys:
            if not overlaps(b, x0, y0, 256):
                continue
            for r in outer:
                d.polygon(px(r), fill=colour, outline=outline, width=SS)
            for r in inner:
                d.polygon(px(r), fill=hole_colour, outline=outline, width=SS)

    fill(layer.land, LAND, WATER, SHORE)
    fill(layer.water, WATER, LAND, SHORE)
    fill(layer.marina, MARINA, LAND)

    river_w = max(1, int(1.5 * 2 ** (z - 12))) * SS
    for b, line in layer.lines["river"]:
        if overlaps(b, x0, y0, 256, river_w):
            d.line(px(line), fill=WATER, width=min(river_w, 14 * SS), joint="curve")
    if z >= 13:
        for b, line in layer.lines["structure"]:
            if overlaps(b, x0, y0, 256, 4):
                d.line(px(line), fill=STRUCTURE, width=2 * SS)
        for b, line in layer.lines["bridge"]:
            if overlaps(b, x0, y0, 256, 4):
                d.line(px(line), fill=(0, 0, 0), width=3 * SS)

    # Labels: drawn into every tile they touch, at the same world position -> seamless
    for (wx, wy), name, kind in layer.places:
        fs = label_size(kind) * SS
        if not (x0 - 300 < wx < x0 + 556 and y0 - 40 < wy < y0 + 296):
            continue
        d.text(((wx - x0) * SS, (wy - y0) * SS), name, fill=TEXT, font=font(fs), anchor="mm",
               stroke_width=2 * SS, stroke_fill=LAND)

    img = img.resize((256, 256), Image.LANCZOS)
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bbox", required=True, help="lon_min,lat_min,lon_max,lat_max (WGS84)")
    p.add_argument("--zooms", default="10-16")
    p.add_argument("--out", required=True)
    p.add_argument("--osm-json", help="use a saved Overpass JSON result instead of querying")
    p.add_argument("--save-osm-json", help="save the Overpass result for later runs")
    p.add_argument("--land-polygons", help="land-polygons-split-4326/land_polygons.shp (for coasts)")
    p.add_argument("--max-tiles", type=int, default=20000)
    args = p.parse_args()

    bbox = tuple(float(v) for v in args.bbox.split(","))
    args.zmin, args.zmax = (int(v) for v in args.zooms.split("-"))
    args.name, args.overlay = "OpenBoat base chart (OSM)", False
    args.attribution = "© OpenStreetMap contributors (ODbL)"

    if args.osm_json:
        with open(args.osm_json, encoding="utf-8") as fh:
            osm = json.load(fh)
    else:
        area = (bbox[2] - bbox[0]) * (bbox[3] - bbox[1])
        if area > MAX_AREA_DEG2:
            sys.exit(f"area {area:.2f} deg² is too large for the public Overpass API (max {MAX_AREA_DEG2}); "
                     "split it or use --osm-json")
        print("querying Overpass ...")
        osm = fetch_overpass(bbox)
        if args.save_osm_json:
            with open(args.save_osm_json, "w", encoding="utf-8") as fh:
                json.dump(osm, fh)
    f = parse(osm)
    land = load_land_polygons(args.land_polygons, bbox) if args.land_polygons else []
    if f["coastline"] and not land:
        print("warning: the area contains sea coast - without --land-polygons the sea is drawn as land!")
    print(f"{len(f['water'])} water areas, {len(f['river'])} rivers, {len(f['place'])} places, "
          f"{len(land)} land polygons")

    jobs = []
    for z in range(args.zmin, args.zmax + 1):
        x0, x1, y0, y1 = tile_range(*bbox, z)
        jobs += [(z, x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)]
    if len(jobs) > args.max_tiles:
        sys.exit(f"{len(jobs)} tiles exceed --max-tiles {args.max_tiles}")

    db = open_mbtiles(args.out, args)
    layer, layer_z = None, None
    for i, (z, x, y) in enumerate(jobs, 1):
        if z != layer_z:
            layer, layer_z = Layer(f, land, z), z
        db.execute("INSERT OR REPLACE INTO tiles VALUES (?, ?, ?, ?)",
                   (z, x, (1 << z) - 1 - y, render_tile(layer, z, x, y, bool(land))))
        if i % 200 == 0:
            db.commit()
            print(f"{i}/{len(jobs)}")
    db.commit()
    db.close()
    print(f"done: {len(jobs)} tiles -> {args.out}")


if __name__ == "__main__":
    main()
