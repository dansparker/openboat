#!/usr/bin/env python3
"""CI ONLY: writes a SYNTHETIC bathymetry grid (ESRI ASCII) for screenshots.

The shoreline comes from the rendered OSM base chart (water colour), the depth
is invented: it grows with the distance from the shore. NOT REAL DATA - the
overlay made from it is labelled accordingly and never shipped.

usage: synthetic_depth.py base.mbtiles lon_min,lat_min,lon_max,lat_max out.asc
"""

import io
import math
import sqlite3
import sys

import numpy as np
from PIL import Image

WATER = np.array([200, 225, 240])
Z = 14
CELL = 0.0004  # degrees (~30-45 m)


def main():
    base, bbox, out = sys.argv[1], [float(v) for v in sys.argv[2].split(",")], sys.argv[3]
    lon_min, lat_min, lon_max, lat_max = bbox
    db = sqlite3.connect(base)
    n = 1 << Z

    def tile_xy(lon, lat):
        s = math.sin(math.radians(lat))
        return (lon + 180) / 360 * n, (0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)) * n

    cols = int((lon_max - lon_min) / CELL) + 1
    rows = int((lat_max - lat_min) / CELL) + 1
    water = np.zeros((rows, cols), dtype=bool)
    cache = {}
    for r in range(rows):
        lat = lat_max - r * CELL
        for c in range(cols):
            fx, fy = tile_xy(lon_min + c * CELL, lat)
            tx, ty = int(fx), int(fy)
            if (tx, ty) not in cache:
                row = db.execute("SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?",
                                 (Z, tx, n - 1 - ty)).fetchone()
                cache[(tx, ty)] = np.asarray(Image.open(io.BytesIO(row[0])).convert("RGB")) if row else None
            img = cache[(tx, ty)]
            if img is not None:
                px = img[int((fy - ty) * 255), int((fx - tx) * 255)]
                water[r, c] = np.abs(px.astype(int) - WATER).sum() < 30

    # distance to the shore in cells (chamfer-like, by repeated erosion)
    dist = np.zeros(water.shape, dtype=np.float32)
    cur = water.copy()
    step = 0
    while cur.any() and step < 200:
        step += 1
        dist[cur] = step
        shrunk = cur.copy()
        shrunk[1:, :] &= cur[:-1, :]
        shrunk[:-1, :] &= cur[1:, :]
        shrunk[:, 1:] &= cur[:, :-1]
        shrunk[:, :-1] &= cur[:, 1:]
        cur = shrunk
    metres = dist * CELL * 111320 * math.cos(math.radians((lat_min + lat_max) / 2))
    depth = np.where(water, np.minimum(170.0, 0.5 + metres * 0.12), -5.0)  # land: 5 m above water

    with open(out, "w") as fh:
        fh.write(f"ncols {cols}\nnrows {rows}\nxllcenter {lon_min}\nyllcenter {lat_max - (rows - 1) * CELL}\n"
                 f"cellsize {CELL}\nNODATA_value -9999\n")
        for r in range(rows):
            fh.write(" ".join(f"{-v:.1f}" for v in depth[r]) + "\n")  # as elevation (negative = below water)
    print(f"{rows}x{cols} synthetic grid, {water.sum()} water cells -> {out}")


if __name__ == "__main__":
    main()
