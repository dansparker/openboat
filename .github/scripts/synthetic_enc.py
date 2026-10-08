"""SYNTHETIC ENC layers around the simulator circle on the Attersee - only to show
sector lights, buoys and vertical clearances in the CI screenshots. The Attersee
has no such bridge; every name says SYNTHETISCH. Never use for navigation.

    python3 synthetic_enc.py OUT_DIR   -> OUT_DIR/<S-57 class>.geojson
"""

import json
import os
import sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def pt(lon, lat):
    return {"type": "Point", "coordinates": [lon, lat]}


layers = {
    # Sector light on the east shore: red / white / green (bearings from seaward)
    "LIGHTS": [(pt(13.5585, 47.8735), {"SECTR1": 200.0, "SECTR2": 250.0, "COLOUR": "3", "LITCHR": "8", "SIGPER": 4.0, "VALNMR": 3.0}),
               (pt(13.5585, 47.8735), {"SECTR1": 250.0, "SECTR2": 260.0, "COLOUR": "1", "LITCHR": "8", "SIGPER": 4.0, "VALNMR": 4.0}),
               (pt(13.5585, 47.8735), {"SECTR1": 260.0, "SECTR2": 310.0, "COLOUR": "4", "LITCHR": "8", "SIGPER": 4.0, "VALNMR": 3.0})],
    "BOYLAT": [(pt(13.5480, 47.8775), {"BOYSHP": 2, "COLOUR": "3", "OBJNAM": "SYNTH 1"}),
               (pt(13.5500, 47.8772), {"BOYSHP": 1, "COLOUR": "4", "OBJNAM": "SYNTH 2"})],
    "BOYCAR": [(pt(13.5390, 47.8660), {"BOYSHP": 4, "COLOUR": "2,6", "CATCAM": 1})],
    # Bridge across the north of the circle, an overhead cable to the south
    "BRIDGE": [({"type": "LineString", "coordinates": [[13.528, 47.8782], [13.562, 47.8782]]},
                {"VERCLR": 9.5, "OBJNAM": "SYNTHETISCH"})],
    "CBLOHD": [({"type": "LineString", "coordinates": [[13.530, 47.8625], [13.560, 47.8625]]},
                {"VERCLR": 22.0})],
}

for name, feats in layers.items():
    with open(os.path.join(out, name + ".geojson"), "w", encoding="utf-8") as fh:
        json.dump({"type": "FeatureCollection", "name": name,
                   "features": [{"type": "Feature", "geometry": g, "properties": p} for g, p in feats]}, fh)
print(f"synthetic ENC layers -> {out}")
