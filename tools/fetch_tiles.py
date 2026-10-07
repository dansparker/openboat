#!/usr/bin/env python3
"""Download XYZ raster tiles for an area into an MBTiles file (offline chart).

Example (OpenSeaMap seamark overlay for the Attersee, zoom 10-16):

    python tools/fetch_tiles.py --url "https://tiles.openseamap.org/seamark/{z}/{x}/{y}.png" \
        --bbox 13.47,47.78,13.62,47.96 --zooms 10-16 --out charts/seamarks.mbtiles \
        --name "OpenSeaMap seamarks" --attribution "OpenSeaMap, CC BY-SA"

Be a good citizen: tile servers are run by volunteers. This tool limits the
number of tiles and the request rate, resumes interrupted downloads and
refuses servers whose usage policy forbids bulk downloading. Check the terms
of the server you use - see docs/charts.md.
"""

import argparse
import math
import sqlite3
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

USER_AGENT = "OpenBoat-fetch_tiles/0.1 (+https://github.com/dansparker/openboat)"

# Servers whose usage policy forbids offline/bulk downloads.
FORBIDDEN_HOSTS = (
    "tile.openstreetmap.org",
    "openstreetmap.org",
    "tile.osm.org",
    "google.com",
    "googleapis.com",
    "navionics.com",
    "garmin.com",
)


def tile_range(lon_min, lat_min, lon_max, lat_max, z):
    def tx(lon):
        return int((lon + 180.0) / 360.0 * (1 << z))

    def ty(lat):
        lat = max(-85.0511, min(85.0511, lat))
        s = math.sin(math.radians(lat))
        return int((0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)) * (1 << z))

    n = (1 << z) - 1
    return (max(0, tx(lon_min)), min(n, tx(lon_max)), max(0, ty(lat_max)), min(n, ty(lat_min)))


def open_mbtiles(path, args):
    db = sqlite3.connect(path)
    db.execute("CREATE TABLE IF NOT EXISTS metadata (name TEXT PRIMARY KEY, value TEXT)")
    db.execute(
        "CREATE TABLE IF NOT EXISTS tiles (zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, "
        "tile_data BLOB, PRIMARY KEY (zoom_level, tile_column, tile_row))"
    )
    meta = {
        "name": args.name,
        "format": "png",
        "type": "overlay" if args.overlay else "baselayer",
        "minzoom": str(args.zmin),
        "maxzoom": str(args.zmax),
        "bounds": args.bbox,
        "attribution": args.attribution,
    }
    db.executemany("INSERT OR REPLACE INTO metadata VALUES (?, ?)", meta.items())
    db.commit()
    return db


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--url", required=True, help="tile URL template with {z} {x} {y}")
    p.add_argument("--bbox", required=True, help="lon_min,lat_min,lon_max,lat_max (WGS84)")
    p.add_argument("--zooms", default="8-15", help="zoom range, e.g. 8-15")
    p.add_argument("--out", required=True, help="output .mbtiles file (resumed if it exists)")
    p.add_argument("--name", default="chart")
    p.add_argument("--attribution", default="")
    p.add_argument("--overlay", action="store_true", help="mark as overlay (transparent tiles)")
    p.add_argument("--max-tiles", type=int, default=5000, help="safety limit (default 5000)")
    p.add_argument("--delay", type=float, default=0.25, help="seconds between requests (default 0.25)")
    args = p.parse_args()

    host = urllib.parse.urlparse(args.url).hostname or ""
    if any(host == h or host.endswith("." + h) for h in FORBIDDEN_HOSTS):
        sys.exit(f"{host}: usage policy forbids bulk downloads - see docs/charts.md for alternatives")

    lon_min, lat_min, lon_max, lat_max = (float(v) for v in args.bbox.split(","))
    args.zmin, args.zmax = (int(v) for v in args.zooms.split("-"))
    jobs = []
    for z in range(args.zmin, args.zmax + 1):
        x0, x1, y0, y1 = tile_range(lon_min, lat_min, lon_max, lat_max, z)
        jobs += [(z, x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)]
    if len(jobs) > args.max_tiles:
        sys.exit(f"{len(jobs)} tiles exceed --max-tiles {args.max_tiles}: reduce the area or zoom range")

    db = open_mbtiles(args.out, args)
    have = {(z, x, (1 << z) - 1 - r) for z, x, r in db.execute("SELECT zoom_level, tile_column, tile_row FROM tiles")}
    todo = [j for j in jobs if j not in have]
    print(f"{len(jobs)} tiles, {len(todo)} to download")

    for i, (z, x, y) in enumerate(todo, 1):
        url = args.url.format(z=z, x=x, y=y)
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                data = r.read()
        except urllib.error.HTTPError as e:
            if e.code == 404:
                data = None  # no data there (normal for overlays)
            elif e.code in (429, 503):
                print("server asks to slow down - stopping; run again later to resume")
                break
            else:
                print(f"{url}: HTTP {e.code}")
                continue
        except (urllib.error.URLError, TimeoutError) as e:
            print(f"{url}: {e}")
            continue
        if data:
            db.execute("INSERT OR REPLACE INTO tiles VALUES (?, ?, ?, ?)", (z, x, (1 << z) - 1 - y, data))
        if i % 50 == 0:
            db.commit()
            print(f"{i}/{len(todo)}")
        time.sleep(args.delay)
    db.commit()
    db.close()
    print("done")


if __name__ == "__main__":
    main()
