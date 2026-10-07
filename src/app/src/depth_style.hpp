#pragma once

// Colours a depth-encoded tile (tools/make_depth.py: R,G = depth in dm,
// B = 1 surface / 2 contour line, A = 0 no data) for a safety depth:
//   shallower than the safety depth  -> blue
//   deeper than `deep`               -> light (lightens the base chart water)
//   contour lines at standard depths -> thin; the safety contour -> heavy
// Done per tile when it is loaded, so changing the safety depth on the settings
// page takes effect without re-generating the chart.

#include <QImage>

[[nodiscard]] QImage styleDepthTile(const QImage& encoded, double safety_m, double deep_m);

// Deep-water shading limit for a safety depth (paper charts: around 30 m)
[[nodiscard]] inline double deepDepthFor(double safety_m) { return safety_m * 3.0 > 30.0 ? safety_m * 3.0 : 30.0; }
