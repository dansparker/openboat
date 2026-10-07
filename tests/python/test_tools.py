"""Checks for the chart tools (run: python tests/python/test_tools.py)."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))

import numpy as np  # noqa: E402

import make_depth  # noqa: E402


def grid(values):
    return {"depth": np.array(values, dtype=np.float32), "lon0": 10.0, "lat0": 50.0, "cell": 0.01}


def test_contour_lies_between_samples():
    # depth grows to the east: 0, 10, 20 -> the 5 m line runs north-south between columns 0 and 1
    g = grid([[0, 10, 20], [0, 10, 20], [0, 10, 20]])
    segs = make_depth.contour_segments(g, 5.0)
    assert segs, "no contour"
    for lon1, _, lon2, _ in segs:
        assert abs(lon1 - 10.005) < 1e-9 and abs(lon2 - 10.005) < 1e-9


def test_nodata_cells_produce_no_contour():
    g = grid([[0, np.nan], [0, 10]])
    assert make_depth.contour_segments(g, 5.0) == []


def test_bilinear_sampling():
    g = grid([[0, 10], [20, 30]])
    v = make_depth.sample_grid(g, np.array([10.005]), np.array([49.995]))
    assert abs(float(v[0]) - 15.0) < 1e-4
    outside = make_depth.sample_grid(g, np.array([11.0]), np.array([49.995]))
    assert np.isnan(outside[0])


def test_geojson_depth_properties():
    assert make_depth.depth_property({"VALDCO": "5"}) == 5.0
    assert make_depth.depth_property({"ELEV": -12}) == 12.0
    assert make_depth.depth_property({"name": "x"}) is None


def test_depth_encoding_round_trip():
    d = np.array([[0.4, 12.3], [np.nan, 6553.5]], dtype=np.float32)
    k = np.array([[1, 2], [0, 1]], dtype=np.uint8)
    back, kind = make_depth.decode(make_depth.encode(d, k))
    assert abs(back[0, 0] - 0.4) < 1e-4 and abs(back[0, 1] - 12.3) < 1e-4 and abs(back[1, 1] - 6553.5) < 1e-3
    assert np.isnan(back[1, 0])
    assert kind[0, 1] == 2


def test_labels_sidecar_merges_zoom_levels(tmp="labels-test.json"):
    n = make_depth.write_labels(tmp, {12: [(13.5, 47.9, "5")], 13: [(13.5, 47.9, "5"), (13.6, 47.9, "10")]})
    import json
    data = json.load(open(tmp, encoding="utf-8"))
    os.remove(tmp)
    assert n == 2
    five = [l for l in data["labels"] if l["text"] == "5"][0]
    assert five["z"] == [12, 13]


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok ", name)
