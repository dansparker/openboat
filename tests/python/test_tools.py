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


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok ", name)
