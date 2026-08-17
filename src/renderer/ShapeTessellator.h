// ShapeTessellator.h
//
// Converts a parsed swf::Shape (fill/line styles + SHAPERECORD stream) into
// flat, renderable geometry: closed polygons (for fills) and polylines
// (for strokes), in the shape's own local twip coordinate space.
//
// IMPORTANT — this is a deliberately SIMPLIFIED tessellator, not a
// spec-correct one:
//
//   Real SWF shapes describe fills via pairs of edges with a "left" and
//   "right" fill style (fillStyle0/fillStyle1), and a proper renderer joins
//   same-style edges from potentially many StyleChangeRecords into
//   arbitrary-topology fill regions (including holes — e.g. the letter
//   "O"). That requires a real polygon-boundary-merging algorithm.
//
//   This tessellator instead treats each contiguous run of edges starting
//   at a StyleChangeRecord's MoveTo as ONE closed polygon, filled with
//   whichever of fillStyle1/fillStyle0 is active (fillStyle1 preferred,
//   matching the common authoring-tool convention for the "outer" fill
//   direction). This renders simple, single-contour shapes (rectangles,
//   circles, stars, most vector-art fills exported by simple tools)
//   correctly, but will NOT correctly render shapes with holes or shapes
//   that rely on multiple StyleChangeRecords sharing one fill region —
//   those render as overlapping solid fills instead. Good enough for a
//   "basic renderer" (Phase 3); revisit with real edge-merging if/when
//   target content needs it (see docs/renderer.md).
//
// Gradient and bitmap fills are reduced to a single representative flat
// color (see FillStyle::toFlatColor below) — proper gradient/bitmap
// rendering is future work.

#pragma once

#include <cstdint>
#include <vector>

#include "swf/ShapeRecords.h"

namespace flash3ds::renderer {

struct PointTwips {
    int32_t x = 0;
    int32_t y = 0;
};

struct TessellatedPolygon {
    swf::RgbaColor color;  // already resolved to a flat color (see toFlatColor)
    std::vector<PointTwips> points;  // closed polygon; edge back to points[0] is implicit
};

struct TessellatedStroke {
    swf::RgbaColor color;
    uint16_t widthTwips = 0;
    std::vector<PointTwips> points;  // open polyline
};

struct TessellatedShape {
    std::vector<TessellatedPolygon> polygons;
    std::vector<TessellatedStroke> strokes;
};

// Reduces a FillStyle to a single flat color: the solid color as-is, the
// average of a gradient's stop colors for gradient fills, or a neutral gray
// placeholder for bitmap fills (bitmap decoding isn't implemented yet).
swf::RgbaColor toFlatColor(const swf::FillStyle& fill);

// `curveSubdivisions` controls how many line segments approximate each
// quadratic-bezier CurvedEdgeRecord (higher = smoother, more points).
TessellatedShape tessellateShape(const swf::Shape& shape, int curveSubdivisions = 8);

}  // namespace flash3ds::renderer
