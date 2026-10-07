#!/usr/bin/env python3
"""Render a depth overlay (MBTiles): depth contours, depth labels and depth shading.

Sources (one or both):
  --grid FILE.asc       bathymetry grid in ESRI ASCII format, e.g.
                        EMODnet Bathymetry (European seas incl. the Mediterranean, CC BY 4.0)
                        or GEBCO (worldwide, coarse). Values are elevations (negative =
                        below water) unless --depth-positive is given.
  --contours FILE.json  GeoJSON with depth contours (LineString, property VALDCO / depth /
                        DEPTH / tiefe) and/or depth areas (Polygon, property DRVAL1), e.g.
                        from an Inland ENC / S-57 cell:
                          ogr2ogr -f GeoJSON depcnt.json CELL.000 DEPCNT
                          ogr2ogr -f GeoJSON depare.json CELL.000 DEPARE

Shading follows paper/electronic chart conventions: water shallower than the
safety depth is blue, deep water (>= --deep-depth) lighter, the safety contour
is drawn heavier.

Example:
    python tools/make_depth.py --grid emodnet_D6.asc --bbox 13.5,44.8,14.0,45.2 \\
        --zooms 9-14 --safety-depth 3 --out charts/depth.mbtiles

Needs numpy and Pillow (`pip install numpy pillow`). The result is only as good
as the source: EMODnet cells are ~115 m, GEBCO ~450 m - fine for overview and
safety shading, NOT for close-quarters pilotage. The resolution is written into
the chart attribution.
"""

import argparse
import io
import json
import math
import sys

from fetch_tiles import open_mbtiles, tile_range

try:
    import numpy as np
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("numpy and Pillow are required: pip install numpy pillow")

SS = 2  # supersampling
SHALLOW = (110, 165, 220, 170)   # 0 .. safety depth
DEEP = (255, 255, 255, 110)      # >= deep depth (lightens the base map water)
CONTOUR = (60, 90, 130, 255)
SAFETY = (20, 40, 90, 255)
LABEL = (30, 50, 90, 255)
DEFAULT_LEVELS = [2, 3, 5, 10, 15, 20, 30, 50, 100, 200, 500, 1000]


# ---- input -------------------------------------------------------------------

def read_asc(path, depth_positive):
    header = {}
    with open(path, encoding="ascii", errors="replace") as fh:
        for _ in range(6):
            pos = fh.tell()
            parts = fh.readline().split()
            if len(parts) != 2 or parts[0][0].isdigit() or parts[0][0] == "-":
                fh.seek(pos)
                break
            header[parts[0].lower()] = float(parts[1])
        data = np.loadtxt(fh, dtype=np.float32)
    ncols, nrows, cell = int(header["ncols"]), int(header["nrows"]), header["cellsize"]
    if data.shape != (nrows, ncols):
        sys.exit(f"{path}: grid is {data.shape}, header says {nrows}x{ncols}")
    x0 = header.get("xllcorner", header.get("xllcenter", 0) - cell / 2)
    y0 = header.get("yllcorner", header.get("yllcenter", 0) - cell / 2)
    nodata = header.get("nodata_value")
    if nodata is not None:
        data[data == nodata] = np.nan
    depth = data if depth_positive else -data
    # cell centres: lon = x0 + (c + 0.5) * cell, lat = ytop - (r + 0.5) * cell
    return {"depth": depth, "lon0": x0 + cell / 2, "lat0": y0 + nrows * cell - cell / 2, "cell": cell}


def depth_property(props):
    for key in ("VALDCO", "valdco", "depth", "DEPTH", "tiefe", "Tiefe", "ELEV", "elev"):
        if key in props and props[key] is not None:
            try:
                v = float(props[key])
            except (TypeError, ValueError):
                continue
            return -v if key.lower() == "elev" else v
    return None


def read_geojson(path):
    """-> (lines [(depth, [(lon, lat), ...])], areas [(drval1, [outer ring], [holes])])"""
    with open(path, encoding="utf-8") as fh:
        gj = json.load(fh)
    lines, areas = [], []
    for f in gj.get("features", []):
        g, props = f.get("geometry") or {}, f.get("properties") or {}
        t, coords = g.get("type"), g.get("coordinates")
        if t in ("LineString", "MultiLineString"):
            d = depth_property(props)
            if d is None:
                continue
            for part in ([coords] if t == "LineString" else coords):
                lines.append((d, [tuple(p[:2]) for p in part]))
        elif t in ("Polygon", "MultiPolygon") and props.get("DRVAL1") is not None:
            for poly in ([coords] if t == "Polygon" else coords):
                areas.append((float(props["DRVAL1"]), [tuple(p[:2]) for p in poly[0]],
                              [[tuple(p[:2]) for p in ring] for ring in poly[1:]]))
    return lines, areas


# ---- marching squares ----------------------------------------------------------

# For each of the 16 cases: list of edge pairs. Edges: 0 top, 1 right, 2 bottom, 3 left.
CASES = {1: [(3, 2)], 2: [(2, 1)], 3: [(3, 1)], 4: [(0, 1)], 5: [(3, 0), (2, 1)], 6: [(0, 2)],
         7: [(3, 0)], 8: [(3, 0)], 9: [(0, 2)], 10: [(0, 1), (3, 2)], 11: [(0, 1)], 12: [(3, 1)],
         13: [(2, 1)], 14: [(3, 2)]}


def contour_segments(grid, level):
    """Segments [(lon1, lat1, lon2, lat2)] of the iso-line depth == level."""
    d = grid["depth"]
    tl, tr, br, bl = d[:-1, :-1], d[:-1, 1:], d[1:, 1:], d[1:, :-1]
    valid = ~(np.isnan(tl) | np.isnan(tr) | np.isnan(br) | np.isnan(bl))
    case = ((tl > level) * 8 + (tr > level) * 4 + (br > level) * 2 + (bl > level) * 1).astype(np.int8)
    rows, cols = np.nonzero(valid & (case > 0) & (case < 15))
    cell, lon0, lat0 = grid["cell"], grid["lon0"], grid["lat0"]

    def t(a, b):
        return 0.5 if a == b else (level - a) / (b - a)

    out = []
    for r, c in zip(rows.tolist(), cols.tolist()):
        a, b, cc, e = float(tl[r, c]), float(tr[r, c]), float(br[r, c]), float(bl[r, c])
        # edge points in (col, row) cell coordinates
        pts = {0: (c + t(a, b), r), 1: (c + 1, r + t(b, cc)), 2: (c + t(e, cc), r + 1), 3: (c, r + t(a, e))}
        for e1, e2 in CASES[int(case[r, c])]:
            (x1, y1), (x2, y2) = pts[e1], pts[e2]
            out.append((lon0 + x1 * cell, lat0 - y1 * cell, lon0 + x2 * cell, lat0 - y2 * cell))
    return out


# ---- projection & rendering ------------------------------------------------------

def world(lon, lat, z):
    n = 256 * (1 << z)
    lat = np.clip(lat, -85.0511, 85.0511)
    s = np.sin(np.radians(lat))
    return (np.asarray(lon) + 180) / 360 * n, (0.5 - np.log((1 + s) / (1 - s)) / (4 * math.pi)) * n


def sample_grid(grid, lon, lat):
    """Bilinear depth at arrays of lon/lat (NaN outside / nodata)."""
    d = grid["depth"]
    fc = (lon - grid["lon0"]) / grid["cell"]
    fr = (grid["lat0"] - lat) / grid["cell"]
    c0, r0 = np.floor(fc).astype(int), np.floor(fr).astype(int)
    inside = (c0 >= 0) & (r0 >= 0) & (c0 < d.shape[1] - 1) & (r0 < d.shape[0] - 1)
    c0c, r0c = np.clip(c0, 0, d.shape[1] - 2), np.clip(r0, 0, d.shape[0] - 2)
    wx, wy = fc - c0c, fr - r0c
    v = (d[r0c, c0c] * (1 - wx) * (1 - wy) + d[r0c, c0c + 1] * wx * (1 - wy) +
         d[r0c + 1, c0c] * (1 - wx) * wy + d[r0c + 1, c0c + 1] * wx * wy)
    return np.where(inside, v, np.nan)


def font(size):
    for name in ("DejaVuSans.ttf", "arial.ttf", "Arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default(size=size)


def fmt_depth(d):
    return f"{d:g}"


class Zoom:
    """Contours projected to one zoom level, with label positions."""

    def __init__(self, segments_by_level, z, safety):
        self.levels = []
        for level, segs in segments_by_level:
            if not segs:
                continue
            a = np.array(segs, dtype=np.float64)
            x1, y1 = world(a[:, 0], a[:, 1], z)
            x2, y2 = world(a[:, 2], a[:, 3], z)
            px = np.stack([x1, y1, x2, y2], axis=1)
            self.levels.append((level, px, level == safety))
        # Labels: the same depth repeats every ~300 px along its line; labels of
        # different depths keep 40 px apart (contours near a steep shore are dense)
        self.labels = []
        for level, px, _ in self.levels:
            mids = (px[:, :2] + px[:, 2:]) / 2
            same = []
            for mx, my in mids[:: max(1, len(mids) // 600)]:
                if any((mx - tx) ** 2 + (my - ty) ** 2 < 300 ** 2 for tx, ty in same):
                    continue
                if any((mx - tx) ** 2 + (my - ty) ** 2 < 40 ** 2 for tx, ty, _ in self.labels):
                    continue
                same.append((mx, my))
                self.labels.append((mx, my, fmt_depth(level)))


def render_tile(z, x, y, zoom_data, grid, areas, safety, deep):
    size = 256 * SS
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    x0, y0 = x * 256, y * 256

    # Shading from the grid (vectorised per pixel)
    if grid is not None:
        n = 256 * (1 << z)
        px = (x0 + (np.arange(size) + 0.5) / SS) / n
        py = (y0 + (np.arange(size) + 0.5) / SS) / n
        lon = px * 360 - 180
        lat = np.degrees(np.arctan(np.sinh(math.pi * (1 - 2 * py))))
        lon_g, lat_g = np.meshgrid(lon, lat)
        d = sample_grid(grid, lon_g, lat_g)
        rgba = np.zeros((size, size, 4), dtype=np.uint8)
        rgba[(d > 0) & (d < safety)] = SHALLOW
        rgba[d >= deep] = DEEP
        img = Image.fromarray(rgba, "RGBA")

    draw = ImageDraw.Draw(img)

    def tp(wx, wy):
        return ((wx - x0) * SS, (wy - y0) * SS)

    # Depth areas from S-57 (DEPARE)
    for drval1, outer, holes in areas:
        colour = SHALLOW if drval1 < safety else DEEP if drval1 >= deep else None
        if colour is None:
            continue
        ox, oy = world(np.array([p[0] for p in outer]), np.array([p[1] for p in outer]), z)
        if ox.max() < x0 or ox.min() > x0 + 256 or oy.max() < y0 or oy.min() > y0 + 256:
            continue
        draw.polygon([tp(a, b) for a, b in zip(ox, oy)], fill=colour)
        for h in holes:
            hx, hy = world(np.array([p[0] for p in h]), np.array([p[1] for p in h]), z)
            draw.polygon([tp(a, b) for a, b in zip(hx, hy)], fill=(0, 0, 0, 0))

    # Contour lines
    for level, px, is_safety in zoom_data.levels:
        m = ((np.maximum(px[:, 0], px[:, 2]) >= x0 - 2) & (np.minimum(px[:, 0], px[:, 2]) <= x0 + 258) &
             (np.maximum(px[:, 1], px[:, 3]) >= y0 - 2) & (np.minimum(px[:, 1], px[:, 3]) <= y0 + 258))
        width = (3 if is_safety else 1) * SS
        colour = SAFETY if is_safety else CONTOUR
        for sx1, sy1, sx2, sy2 in px[m]:
            draw.line([tp(sx1, sy1), tp(sx2, sy2)], fill=colour, width=width)

    # Labels (drawn into every tile they touch: seamless across tiles)
    f = font(11 * SS)
    for lx, ly, text in zoom_data.labels:
        if x0 - 30 < lx < x0 + 286 and y0 - 20 < ly < y0 + 276:
            draw.text(tp(lx, ly), text, fill=LABEL, font=f, anchor="mm", stroke_width=2 * SS,
                      stroke_fill=(235, 242, 250, 255))

    img = img.resize((256, 256), Image.LANCZOS)
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--grid", help="bathymetry grid (ESRI ASCII .asc)")
    p.add_argument("--depth-positive", action="store_true", help="grid values are depths (positive down)")
    p.add_argument("--contours", help="GeoJSON with depth contours / depth areas")
    p.add_argument("--bbox", help="lon_min,lat_min,lon_max,lat_max (default: grid extent)")
    p.add_argument("--zooms", default="9-14")
    p.add_argument("--levels", help="contour depths in metres, comma separated")
    p.add_argument("--safety-depth", type=float, default=3.0, help="safety contour / shading limit (m)")
    p.add_argument("--deep-depth", type=float, default=30.0, help="deep water shading from (m)")
    p.add_argument("--name", default="Tiefen")
    p.add_argument("--attribution", default="", help="source and licence of the depth data")
    p.add_argument("--out", required=True)
    p.add_argument("--max-tiles", type=int, default=20000)
    args = p.parse_args()

    if not args.grid and not args.contours:
        sys.exit("give --grid and/or --contours")
    grid = read_asc(args.grid, args.depth_positive) if args.grid else None
    lines, areas = read_geojson(args.contours) if args.contours else ([], [])

    if args.bbox:
        bbox = tuple(float(v) for v in args.bbox.split(","))
    elif grid is not None:
        h, w = grid["depth"].shape
        bbox = (grid["lon0"], grid["lat0"] - (h - 1) * grid["cell"], grid["lon0"] + (w - 1) * grid["cell"], grid["lat0"])
    else:
        xs = [p[0] for _, pts in lines for p in pts]
        ys = [p[1] for _, pts in lines for p in pts]
        bbox = (min(xs), min(ys), max(xs), max(ys))

    levels = sorted({float(v) for v in args.levels.split(",")} if args.levels else set(DEFAULT_LEVELS))
    levels = sorted(set(levels) | {args.safety_depth})
    segments = []
    for level in levels:
        segs = contour_segments(grid, level) if grid is not None else []
        segs += [(a[0], a[1], b[0], b[1]) for d, pts in lines if d == level for a, b in zip(pts, pts[1:])]
        segments.append((level, segs))
    # GeoJSON contours at depths not in `levels` are drawn too
    extra = sorted({d for d, _ in lines} - set(levels))
    for level in extra:
        segments.append((level, [(a[0], a[1], b[0], b[1]) for d, pts in lines if d == level for a, b in zip(pts, pts[1:])]))
    print(f"{sum(len(s) for _, s in segments)} contour segments, {len(areas)} depth areas")

    args.zmin, args.zmax = (int(v) for v in args.zooms.split("-"))
    args.bbox = ",".join(f"{v:.6f}" for v in bbox)
    args.overlay = True
    if grid is not None:
        res_m = grid["cell"] * 111320
        args.attribution = (args.attribution + f" · Raster ~{res_m:.0f} m").strip(" ·")
    jobs = []
    for z in range(args.zmin, args.zmax + 1):
        x0, x1, y0, y1 = tile_range(*bbox, z)
        jobs += [(z, x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)]
    if len(jobs) > args.max_tiles:
        sys.exit(f"{len(jobs)} tiles exceed --max-tiles {args.max_tiles}")

    db = open_mbtiles(args.out, args)
    zoom_data, current = None, None
    for i, (z, x, y) in enumerate(jobs, 1):
        if z != current:
            zoom_data, current = Zoom(segments, z, args.safety_depth), z
        db.execute("INSERT OR REPLACE INTO tiles VALUES (?, ?, ?, ?)",
                   (z, x, (1 << z) - 1 - y, render_tile(z, x, y, zoom_data, grid, areas, args.safety_depth, args.deep_depth)))
        if i % 200 == 0:
            db.commit()
            print(f"{i}/{len(jobs)}")
    db.commit()
    db.close()
    print(f"done: {len(jobs)} tiles -> {args.out}")


if __name__ == "__main__":
    main()
