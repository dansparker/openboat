"""Checks for the chart tools (run: python tests/python/test_tools.py)."""

import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))

import numpy as np  # noqa: E402

import make_depth  # noqa: E402
import make_enc  # noqa: E402


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


def test_enc_attributes():
    # GDAL returns list attributes as lists or strings depending on the version
    assert make_enc.int_list(["3", "4"]) == [3, 4]
    assert make_enc.int_list("2,6") == [2, 6]
    assert make_enc.int_list(None) == []
    m = make_enc.mark_from_feature("BOYCAR", {"BOYSHP": 4, "COLOUR": "2,6", "CATCAM": 1, "OBJNAM": " N1 "}, 9.08, 47.67)
    assert (m["kind"], m["shape"], m["colours"], m["catcam"], m["name"]) == ("buoy", 4, [2, 6], 1, "N1")
    assert make_enc.mark_from_feature("LIGHTS", {}, 0, 0)["kind"] == "light"
    assert make_enc.mark_from_feature("BCNLAT", {"BCNSHP": "x"}, 0, 0)["shape"] == 0


def test_enc_marks_tile_draws_symbols():
    from PIL import Image
    import io
    lon, lat, z = 9.08, 47.67, 16
    wx, wy = make_enc.world(lon, lat, z)
    marks = [make_enc.mark_from_feature("BOYLAT", {"BOYSHP": 2, "COLOUR": "3"}, lon, lat)]
    png = make_enc.render_marks_tile(z, int(wx // 256), int(wy // 256), marks, [], [])
    img = Image.open(io.BytesIO(png)).convert("RGBA")
    px = [img.getpixel((x, y)) for x in range(256) for y in range(256)]
    assert any(p[3] > 0 and p[0] > 150 and p[1] < 80 for p in px)  # a red can is drawn
    # far away tile: empty
    empty = Image.open(io.BytesIO(make_enc.render_marks_tile(z, 0, 0, marks, [], []))).convert("RGBA")
    assert empty.getextrema()[3] == (0, 0)


def test_enc_labels_soundings_and_names():
    labels = make_enc.labels_sidecar([(9.08, 47.67, 2.4)], [{"lon": 9.09, "lat": 47.67, "name": "Tonne 3"}], 13, 15)
    d = [l for l in labels if l["kind"] == "depth"][0]
    assert d["text"] == "2.4" and d["z"] == [14, 15]
    n = [l for l in labels if l["text"] == "Tonne 3"][0]
    assert n["z"] == [15]


def test_light_characteristic():
    props = {"LITCHR": "2", "SIGGRP": "(2)", "COLOUR": "3", "SIGPER": 6, "HEIGHT": 12, "VALNMR": 5}
    assert make_enc.light_characteristic(props) == "Fl(2) R 6s 12m 5M"
    assert make_enc.light_characteristic({"LITCHR": "1", "COLOUR": "1"}) == "F W"
    m = make_enc.mark_from_feature("LIGHTS", {"SECTR1": "90", "SECTR2": "180", "COLOUR": "4"}, 9.0, 47.0)
    assert m["sector"] == (90.0, 180.0)
    assert make_enc.mark_from_feature("LIGHTS", {"SECTR1": "90"}, 9.0, 47.0)["sector"] is None


def test_light_sector_shines_away_from_seaward_bearing():
    # SECTR 90..180 (bearings from seaward) -> the light shines towards 270..360: the arc
    # lies up-left of the light, nothing down-right
    from PIL import Image
    import io
    lon, lat, z = 9.08, 47.67, 16
    wx, wy = make_enc.world(lon, lat, z)
    tx, ty = int(wx // 256), int(wy // 256)
    lx, ly = wx - tx * 256, wy - ty * 256
    m = make_enc.mark_from_feature("LIGHTS", {"SECTR1": 90, "SECTR2": 180, "COLOUR": "4"}, lon, lat)
    img = Image.open(io.BytesIO(make_enc.render_marks_tile(z, tx, ty, [m], [], []))).convert("RGBA")

    def green_near(bearing):
        dx, dy = make_enc.bearing_xy(bearing, make_enc.SECTOR_RADIUS)
        x, y = int(lx + dx), int(ly + dy)
        if not (0 <= x < 256 and 0 <= y < 256):
            return None
        return any(img.getpixel((min(255, max(0, x + i)), min(255, max(0, y + j))))[1] > 120 and
                   img.getpixel((min(255, max(0, x + i)), min(255, max(0, y + j))))[0] < 100
                   for i in range(-2, 3) for j in range(-2, 3))

    inside = [green_near(b) for b in (290, 315, 340)]
    outside = [green_near(b) for b in (110, 135, 160)]
    assert any(v for v in inside if v is not None), inside
    assert not any(v for v in outside if v is not None), outside


def test_clearances_labels_and_sidecar(tmp="clearances-test.json"):
    bridge = make_enc.clearance_from_feature("BRIDGE", {"VERCLR": 6.2, "VERCCL": 4.5, "VERCOP": 30, "OBJNAM": "Br"},
                                             [[(9.07, 47.67), (9.08, 47.67), (9.09, 47.67)]])
    cable = make_enc.clearance_from_feature("CBLOHD", {"VERCLR": 18}, [[(9.1, 47.6), (9.1, 47.7)]])
    unknown = make_enc.clearance_from_feature("PIPOHD", {}, [[(9.2, 47.6), (9.2, 47.7)]])
    assert make_enc.clearance_value(bridge) == 4.5  # an opening bridge counts closed
    labels = make_enc.labels_sidecar([], [], 13, 13, [bridge, cable])
    b = [l for l in labels if l["value"] == 4.5][0]
    assert b["kind"] == "clearance" and "offen 30" in b["text"] and (b["lon"], b["lat"]) == (9.08, 47.67)
    n = make_enc.write_clearances(tmp, [bridge, cable, unknown])
    data = json.load(open(tmp, encoding="utf-8"))["clearances"]
    os.remove(tmp)
    assert n == 2 and data[0]["clearance"] == 4.5 and data[0]["open"] == 30


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok ", name)
