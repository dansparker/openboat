#!/usr/bin/env python3
"""Convert S-57 / Inland ENC cells (*.000) into OpenBoat charts.

    python tools/make_enc.py --enc ENC_ROOT/ --zooms 12-17 --out charts/enc

writes
  charts/enc-depth.mbtiles   depth areas (DEPARE) and contours (DEPCNT) as a
                             depth-encoded overlay (same as tools/make_depth.py):
                             the app shades it for the safety depth set on its
                             settings page and draws the heavy safety contour
  charts/enc-marks.mbtiles   buoys, beacons, lights, fairways, navigation lines
  *.labels.json              soundings and names, drawn upright by the app

Symbols follow the S-52 ideas, simplified: shape from BOYSHP/BCNSHP (cone,
can, sphere, pillar, spar), colours from COLOUR, cardinal topmarks from
CATCAM, lights as a magenta flare. This is NOT a certified ECDIS rendering:
check against the official chart.

Where to get cells (free):
  Inland ENC: Danube (Austria: via donau, https://www.doris.bmk.gv.at), Rhine,
              German waterways (ELWIS), Netherlands (Rijkswaterstaat), ...
  S-57 sea:   NOAA (USA, free); many hydrographic offices sell theirs - only
              use cells you are licensed to use.

Needs GDAL with Python bindings (`apt install python3-gdal` / `pip install gdal`),
numpy and Pillow.
"""

import argparse
import glob
import io
import json
import math
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

from fetch_tiles import open_mbtiles, tile_range

# S-57 COLOUR codes -> RGB
COLOURS = {1: (255, 255, 255), 2: (0, 0, 0), 3: (220, 30, 30), 4: (20, 160, 60), 5: (30, 80, 200),
           6: (245, 200, 0), 7: (128, 128, 128), 8: (130, 80, 30), 9: (255, 170, 0), 10: (140, 60, 200),
           11: (255, 130, 0), 12: (220, 0, 220), 13: (255, 150, 180)}

BUOYS = ("BOYLAT", "BOYCAR", "BOYSAW", "BOYSPP", "BOYISD", "BOYINB")
BEACONS = ("BCNLAT", "BCNCAR", "BCNSAW", "BCNSPP", "BCNISD")
# Inland ENC uses lower-case object classes for its own objects; the mark classes are the same
MARK_LAYERS = BUOYS + BEACONS + ("LIGHTS",)
LINE_LAYERS = ("NAVLNE", "RECTRC")
AREA_LAYERS = ("FAIRWY",)
DEPTH_LAYERS = ("DEPARE", "DEPCNT", "DRGARE")


# ---- attributes ------------------------------------------------------------------

def int_list(value):
    """S-57 list attributes come as a list, '3,4' or '3' depending on the GDAL version."""
    if value is None:
        return []
    if isinstance(value, (list, tuple)):
        items = value
    else:
        items = str(value).replace(";", ",").split(",")
    out = []
    for v in items:
        try:
            out.append(int(str(v).strip()))
        except ValueError:
            pass
    return out


def mark_from_feature(layer, props, lon, lat):
    """Feature attributes -> mark dict for rendering (pure, unit tested)."""
    kind = "light" if layer == "LIGHTS" else "buoy" if layer in BUOYS else "beacon"
    shape = props.get("BOYSHP") if kind == "buoy" else props.get("BCNSHP")
    try:
        shape = int(shape) if shape is not None else 0
    except (TypeError, ValueError):
        shape = 0
    return {
        "kind": kind,
        "class": layer,
        "lon": lon,
        "lat": lat,
        "shape": shape,
        "colours": int_list(props.get("COLOUR")),
        "catcam": int(props.get("CATCAM") or 0) if str(props.get("CATCAM") or "0").isdigit() else 0,
        "name": (props.get("OBJNAM") or "").strip(),
    }


# ---- projection --------------------------------------------------------------------

def world(lon, lat, z):
    n = 256 * (1 << z)
    lat = max(-85.0511, min(85.0511, lat))
    s = math.sin(math.radians(lat))
    return (lon + 180) / 360 * n, (0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)) * n


# ---- symbols -----------------------------------------------------------------------

def symbol_size(z):
    return max(5, min(12, 2 * (z - 10)))


def draw_mark(d, x, y, m, z, ss=1):
    s = symbol_size(z) * ss
    cols = [COLOURS.get(c, (128, 128, 128)) for c in m["colours"]] or [(128, 128, 128)]
    fill, outline = cols[0], (0, 0, 0)
    if m["kind"] == "light":
        # magenta flare pointing up-right from the position
        d.polygon([(x, y), (x + s * 1.6, y - s * 2.2), (x + s * 0.4, y - s * 2.6)], fill=(220, 0, 220))
        return
    if m["kind"] == "beacon":
        # stake with a small top shape
        d.line([(x, y), (x, y - 2.2 * s)], fill=outline, width=max(2, int(s // 4)))
        top = y - 2.2 * s
        d.rectangle([x - s * 0.45, top - s * 0.45, x + s * 0.45, top + s * 0.45], fill=fill, outline=outline)
    else:
        shape = m["shape"]
        if shape == 1:  # conical
            d.polygon([(x - s * 0.7, y), (x + s * 0.7, y), (x, y - 1.6 * s)], fill=fill, outline=outline)
        elif shape == 2:  # can
            d.rectangle([x - s * 0.6, y - 1.3 * s, x + s * 0.6, y], fill=fill, outline=outline)
        elif shape in (4, 5):  # pillar / spar: slim, with bands of the further colours
            w = s * (0.45 if shape == 4 else 0.25)
            h = 1.8 * s
            bands = len(cols)
            for i, c in enumerate(cols):
                d.rectangle([x - w, y - h + i * h / bands, x + w, y - h + (i + 1) * h / bands], fill=c)
            d.rectangle([x - w, y - h, x + w, y], outline=outline)
        else:  # spherical, barrel, unknown
            d.ellipse([x - s * 0.7, y - 1.4 * s, x + s * 0.7, y], fill=fill, outline=outline)
        top = y - 1.9 * s
    # Cardinal topmarks: two cones (N up-up, E up-down, S down-down, W down-up)
    if m["catcam"] in (1, 2, 3, 4):
        t = s * 0.45
        ups = {1: (True, True), 2: (True, False), 3: (False, False), 4: (False, True)}[m["catcam"]]
        for i, up in enumerate(ups):
            cy = top - t - i * 2.2 * t
            pts = [(x - t, cy + t), (x + t, cy + t), (x, cy - t)] if up else [(x - t, cy - t), (x + t, cy - t), (x, cy + t)]
            d.polygon(pts, fill=(0, 0, 0))
    # small position circle (the real position is the bottom centre)
    d.ellipse([x - 2 * ss, y - 2 * ss, x + 2 * ss, y + 2 * ss], outline=outline)


def render_marks_tile(z, x, y, marks, lines, areas, ss=2):
    """One 256 px overlay tile (transparent). Pure: unit tested without GDAL."""
    size = 256 * ss
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    x0, y0 = x * 256, y * 256

    def tp(lon, lat):
        wx, wy = world(lon, lat, z)
        return (wx - x0) * ss, (wy - y0) * ss

    for ring in areas:  # fairways: dashed magenta-grey outline
        pts = [tp(*p) for p in ring]
        for a, b in zip(pts, pts[1:]):
            draw_dashed(d, a, b, (160, 60, 160, 200), 2 * ss, 10 * ss)
    for pts in lines:  # navigation lines / recommended tracks
        sp = [tp(*p) for p in pts]
        for a, b in zip(sp, sp[1:]):
            draw_dashed(d, a, b, (60, 60, 60, 220), 2 * ss, 14 * ss)
    if z >= 12:
        for m in marks:
            px, py = tp(m["lon"], m["lat"])
            if -40 * ss < px < size + 40 * ss and -40 * ss < py < size + 40 * ss:
                draw_mark(d, px, py, m, z, ss)
    img = img.resize((256, 256), Image.LANCZOS)
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def draw_dashed(d, a, b, colour, width, dash):
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    if length == 0:
        return
    n = int(length // dash)
    for i in range(0, n + 1, 2):
        t0, t1 = i * dash / length, min(1.0, (i + 1) * dash / length)
        d.line([(a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0),
                (a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1)], fill=colour, width=width)


# ---- labels ------------------------------------------------------------------------

def place_labels(items, z, spacing_px):
    """Greedy: keep a label only if no kept one is closer than spacing_px at zoom z."""
    kept = []
    for lon, lat, text, kind in items:
        wx, wy = world(lon, lat, z)
        if all((wx - kx) ** 2 + (wy - ky) ** 2 >= spacing_px ** 2 for kx, ky in kept):
            kept.append((wx, wy))
            yield lon, lat, text, kind


def labels_sidecar(soundings, marks, zmin, zmax):
    merged = {}
    for z in range(zmin, zmax + 1):
        items = []
        if z >= 14:
            items += [(lon, lat, f"{d:g}", "depth") for lon, lat, d in soundings]
        if z >= 15:
            items += [(m["lon"], m["lat"], m["name"], "place") for m in marks if m["name"]]
        for lon, lat, text, kind in place_labels(items, z, 40):
            merged.setdefault((round(lon, 6), round(lat, 6), text, kind), []).append(z)
    return [{"lon": k[0], "lat": k[1], "text": k[2], "kind": k[3], "z": v} for k, v in merged.items()]


# ---- reading (GDAL) ------------------------------------------------------------------

def read_enc(paths):
    try:
        from osgeo import ogr
    except ImportError:
        sys.exit("GDAL Python bindings missing: apt install python3-gdal (or pip install gdal)")
    os.environ.setdefault("OGR_S57_OPTIONS", "RETURN_PRIMITIVES=OFF,SPLIT_MULTIPOINT=ON,ADD_SOUNDG_DEPTH=ON,"
                                             "LNAM_REFS=OFF,UPDATES=APPLY")
    depth_features, marks, lines, areas, soundings = [], [], [], [], []
    for path in paths:
        ds = ogr.Open(path)
        if ds is None:
            print(f"skipped (not readable): {path}")
            continue
        for i in range(ds.GetLayerCount()):
            layer = ds.GetLayerByIndex(i)
            name = layer.GetName().upper()
            if name not in MARK_LAYERS + LINE_LAYERS + AREA_LAYERS + DEPTH_LAYERS + ("SOUNDG",):
                continue
            for f in layer:
                g = f.GetGeometryRef()
                if g is None:
                    continue
                props = {k: f.GetField(k) for k in f.keys()}
                if name in DEPTH_LAYERS:
                    gj = json.loads(g.ExportToJson())
                    if name == "DRGARE" and props.get("DRVAL1") is None:
                        continue
                    depth_features.append({"type": "Feature", "geometry": gj,
                                           "properties": {k: props.get(k) for k in ("DRVAL1", "DRVAL2", "VALDCO")}})
                elif name == "SOUNDG":
                    for k in range(max(1, g.GetGeometryCount())):
                        p = g.GetGeometryRef(k) if g.GetGeometryCount() else g
                        if p.GetCoordinateDimension() >= 3:
                            soundings.append((p.GetX(), p.GetY(), round(p.GetZ(), 1)))
                elif name in MARK_LAYERS:
                    marks.append(mark_from_feature(name, props, g.GetX(), g.GetY()))
                elif name in LINE_LAYERS:
                    lines.append([(g.GetX(k), g.GetY(k)) for k in range(g.GetPointCount())])
                elif name in AREA_LAYERS:
                    for k in range(g.GetGeometryCount()):
                        ring = g.GetGeometryRef(k)
                        if ring.GetGeometryName() == "LINEARRING":
                            areas.append([(ring.GetX(j), ring.GetY(j)) for j in range(ring.GetPointCount())])
    return depth_features, marks, lines, areas, soundings


def enc_files(spec):
    if os.path.isdir(spec):
        return sorted(glob.glob(os.path.join(spec, "**", "*.000"), recursive=True))
    return [spec]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--enc", required=True, nargs="+", help="*.000 cells or directories with them")
    p.add_argument("--zooms", default="12-17")
    p.add_argument("--out", required=True, help="output prefix, e.g. charts/enc")
    p.add_argument("--name", default="ENC")
    p.add_argument("--attribution", default="", help="source and licence of the cells")
    p.add_argument("--max-tiles", type=int, default=40000)
    args = p.parse_args()

    paths = [f for spec in args.enc for f in enc_files(spec)]
    if not paths:
        sys.exit("no *.000 cells found")
    depth_features, marks, lines, areas, soundings = read_enc(paths)
    print(f"{len(paths)} cells: {len(depth_features)} depth features, {len(marks)} marks, "
          f"{len(lines)} lines, {len(areas)} fairways, {len(soundings)} soundings")

    pts = [(m["lon"], m["lat"]) for m in marks] + [p for l in lines for p in l] + [p for a in areas for p in a]
    for f in depth_features:
        coords = json.dumps(f["geometry"]["coordinates"])
        nums = [float(v) for v in coords.replace("[", " ").replace("]", " ").replace(",", " ").split()]
        pts += list(zip(nums[0::2], nums[1::2])) if f["geometry"]["type"] != "Point" else []
    if not pts:
        sys.exit("nothing to draw in these cells")
    bbox = (min(x for x, _ in pts), min(y for _, y in pts), max(x for x, _ in pts), max(y for _, y in pts))
    args.zmin, args.zmax = (int(v) for v in args.zooms.split("-"))

    # 1. depths: through make_depth (same encoding, safety depth chosen in the app)
    if depth_features:
        with tempfile.NamedTemporaryFile("w", suffix=".geojson", delete=False, encoding="utf-8") as fh:
            json.dump({"type": "FeatureCollection", "features": depth_features}, fh)
            tmp = fh.name
        try:
            subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "make_depth.py"),
                            "--contours", tmp, "--zooms", args.zooms, "--name", args.name + " Tiefen",
                            "--attribution", args.attribution, "--out", args.out + "-depth.mbtiles",
                            "--max-tiles", str(args.max_tiles)], check=True)
        finally:
            os.unlink(tmp)

    # 2. marks overlay
    jobs = []
    for z in range(args.zmin, args.zmax + 1):
        x0, x1, y0, y1 = tile_range(*bbox, z)
        jobs += [(z, x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)]
    if len(jobs) > args.max_tiles:
        sys.exit(f"{len(jobs)} tiles exceed --max-tiles {args.max_tiles}")
    args.bbox = ",".join(f"{v:.6f}" for v in bbox)
    args.overlay = True
    name = args.name
    args.name = name + " Seezeichen"
    db = open_mbtiles(args.out + "-marks.mbtiles", args)
    for i, (z, x, y) in enumerate(jobs, 1):
        db.execute("INSERT OR REPLACE INTO tiles VALUES (?, ?, ?, ?)",
                   (z, x, (1 << z) - 1 - y, render_marks_tile(z, x, y, marks, lines, areas)))
        if i % 500 == 0:
            db.commit()
            print(f"{i}/{len(jobs)}")
    db.commit()
    db.close()
    labels = labels_sidecar(soundings, marks, args.zmin, args.zmax)
    with open(args.out + "-marks.mbtiles.labels.json", "w", encoding="utf-8") as fh:
        json.dump({"version": 1, "labels": labels}, fh, ensure_ascii=False)
    print(f"done: {args.out}-depth.mbtiles, {args.out}-marks.mbtiles ({len(jobs)} tiles, {len(labels)} labels)")


if __name__ == "__main__":
    main()
