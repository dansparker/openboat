"""End-to-end test of tools/make_enc.py through GDAL/OGR (CI: apt python3-gdal).

There are no freely redistributable S-57 test cells, so the cell content is
written as GeoJSON layers named like the S-57 object classes; OGR reads them
through the same code path (layer name, attributes, geometries).
"""

import json
import os
import sqlite3
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))

import make_enc  # noqa: E402

LAYERS = {
    "DEPARE": [({"type": "Polygon", "coordinates": [[[9.07, 47.665], [9.09, 47.665], [9.09, 47.675], [9.07, 47.675], [9.07, 47.665]]]},
                {"DRVAL1": 2.0, "DRVAL2": 5.0}),
               ({"type": "Polygon", "coordinates": [[[9.09, 47.665], [9.11, 47.665], [9.11, 47.675], [9.09, 47.675], [9.09, 47.665]]]},
                {"DRVAL1": 10.0, "DRVAL2": 20.0})],
    "DEPCNT": [({"type": "LineString", "coordinates": [[9.09, 47.665], [9.09, 47.675]]}, {"VALDCO": 10.0})],
    "BOYLAT": [({"type": "Point", "coordinates": [9.08, 47.67]}, {"BOYSHP": 2, "COLOUR": "3", "OBJNAM": "Rot 1"})],
    "BOYCAR": [({"type": "Point", "coordinates": [9.10, 47.67]}, {"BOYSHP": 4, "COLOUR": "2,6", "CATCAM": 1})],
    "LIGHTS": [({"type": "Point", "coordinates": [9.08, 47.67]}, {"COLOUR": "3"})],
    "FAIRWY": [({"type": "Polygon", "coordinates": [[[9.075, 47.668], [9.105, 47.668], [9.105, 47.672], [9.075, 47.668]]]}, {})],
    "SOUNDG": [({"type": "MultiPoint", "coordinates": [[9.08, 47.668, 3.4], [9.1, 47.668, 14.2]]}, {})],
}


def write_layers(tmp):
    paths = []
    for name, feats in LAYERS.items():
        p = os.path.join(tmp, name + ".geojson")
        with open(p, "w", encoding="utf-8") as fh:
            json.dump({"type": "FeatureCollection", "name": name,
                       "features": [{"type": "Feature", "geometry": g, "properties": pr} for g, pr in feats]}, fh)
        paths.append(p)
    return paths


def test_read_enc():
    with tempfile.TemporaryDirectory() as tmp:
        depth, marks, lines, areas, soundings = make_enc.read_enc(write_layers(tmp))
        assert len(depth) == 3
        assert sorted(m["class"] for m in marks) == ["BOYCAR", "BOYLAT", "LIGHTS"]
        assert len(areas) == 1
        assert sorted(d for _, _, d in soundings) == [3.4, 14.2]


def test_end_to_end():
    with tempfile.TemporaryDirectory() as tmp:
        cells = write_layers(tmp)
        out = os.path.join(tmp, "enc")
        tool = os.path.join(os.path.dirname(__file__), "..", "..", "tools", "make_enc.py")
        subprocess.run([sys.executable, tool, "--enc", *cells, "--zooms", "13-14", "--out", out], check=True)
        for f in ("-depth.mbtiles", "-marks.mbtiles"):
            db = sqlite3.connect(out + f)
            assert db.execute("SELECT COUNT(*) FROM tiles").fetchone()[0] > 0
            db.close()
        enc = sqlite3.connect(out + "-depth.mbtiles")
        assert enc.execute("SELECT value FROM metadata WHERE name='encoding'").fetchone()[0] == "openboat-depth-dm-v1"
        enc.close()
        labels = json.load(open(out + "-marks.mbtiles.labels.json", encoding="utf-8"))["labels"]
        assert any(l["text"] == "3.4" for l in labels)


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok ", name)
