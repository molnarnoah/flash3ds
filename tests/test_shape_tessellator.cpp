#include <memory>

#include "TestFramework.h"
#include "renderer/ShapeTessellator.h"

using flash3ds::renderer::buildGradientRamp;
using flash3ds::renderer::PaintKind;
using flash3ds::renderer::tessellateShape;
using flash3ds::renderer::toFlatColor;
using flash3ds::swf::FillStyle;
using flash3ds::swf::FillStyleType;
using flash3ds::swf::Gradient;
using flash3ds::swf::GradientRecord;
using flash3ds::swf::LineStyle;
using flash3ds::swf::RgbaColor;
using flash3ds::swf::Shape;
using flash3ds::swf::ShapeRecord;
using flash3ds::swf::ShapeRecordType;
using flash3ds::swf::ShapeStyleChange;

namespace {

// Builds the same "rectangle" shape test_shape_records.cpp's fixture bytes
// decode to, but directly as a Shape struct — this test is about the
// tessellation algorithm, not the byte-level parser (that's covered by
// test_shape_records.cpp / test_define_shape_tag.cpp).
Shape makeRectangleShape(int32_t widthTwips, int32_t heightTwips, RgbaColor fillColor) {
    Shape shape;
    FillStyle fill;
    fill.type = FillStyleType::kSolid;
    fill.solidColor = fillColor;
    shape.fillStyles.push_back(fill);

    ShapeRecord moveTo;
    moveTo.type = ShapeRecordType::kStyleChange;
    moveTo.styleChange = std::make_shared<ShapeStyleChange>();
    moveTo.styleChange->hasMoveTo = true;
    moveTo.styleChange->moveToXTwips = 0;
    moveTo.styleChange->moveToYTwips = 0;
    moveTo.styleChange->fillStyle1 = 1;
    shape.records.push_back(moveTo);

    ShapeRecord right;
    right.type = ShapeRecordType::kStraightEdge;
    right.edge.straightEdge.deltaXTwips = widthTwips;
    shape.records.push_back(right);

    ShapeRecord down;
    down.type = ShapeRecordType::kStraightEdge;
    down.edge.straightEdge.deltaYTwips = heightTwips;
    shape.records.push_back(down);

    ShapeRecord left;
    left.type = ShapeRecordType::kStraightEdge;
    left.edge.straightEdge.deltaXTwips = -widthTwips;
    shape.records.push_back(left);

    return shape;
}

// Same rectangle outline as makeRectangleShape(), but with fill style 1 a
// linear gradient (two stops, ratio 0 and 255) instead of a solid color —
// graphics/gradients task (2026-08-28).
Shape makeRectangleShapeWithLinearGradient(int32_t widthTwips, int32_t heightTwips,
                                            RgbaColor stopColorAt0, RgbaColor stopColorAt255) {
    Shape shape;
    FillStyle fill;
    fill.type = FillStyleType::kLinearGradient;
    fill.gradient.records.push_back(GradientRecord{0, stopColorAt0});
    fill.gradient.records.push_back(GradientRecord{255, stopColorAt255});
    shape.fillStyles.push_back(fill);

    ShapeRecord moveTo;
    moveTo.type = ShapeRecordType::kStyleChange;
    moveTo.styleChange = std::make_shared<ShapeStyleChange>();
    moveTo.styleChange->hasMoveTo = true;
    moveTo.styleChange->moveToXTwips = 0;
    moveTo.styleChange->moveToYTwips = 0;
    moveTo.styleChange->fillStyle1 = 1;
    shape.records.push_back(moveTo);

    ShapeRecord right;
    right.type = ShapeRecordType::kStraightEdge;
    right.edge.straightEdge.deltaXTwips = widthTwips;
    shape.records.push_back(right);

    ShapeRecord down;
    down.type = ShapeRecordType::kStraightEdge;
    down.edge.straightEdge.deltaYTwips = heightTwips;
    shape.records.push_back(down);

    ShapeRecord left;
    left.type = ShapeRecordType::kStraightEdge;
    left.edge.straightEdge.deltaXTwips = -widthTwips;
    shape.records.push_back(left);

    return shape;
}

}  // namespace

TEST_CASE(ToFlatColor_Solid_ReturnsColorUnchanged) {
    FillStyle fill;
    fill.type = FillStyleType::kSolid;
    fill.solidColor = RgbaColor{10, 20, 30, 255};
    auto color = toFlatColor(fill);
    CHECK_EQ(color.r, 10);
    CHECK_EQ(color.g, 20);
    CHECK_EQ(color.b, 30);
}

TEST_CASE(ToFlatColor_Gradient_AveragesStopColors) {
    FillStyle fill;
    fill.type = FillStyleType::kLinearGradient;
    fill.gradient.records.push_back({0, RgbaColor{0, 0, 0, 255}});
    fill.gradient.records.push_back({255, RgbaColor{200, 100, 50, 255}});
    auto color = toFlatColor(fill);
    CHECK_EQ(color.r, 100);
    CHECK_EQ(color.g, 50);
    CHECK_EQ(color.b, 25);
}

TEST_CASE(ToFlatColor_Bitmap_ReturnsGrayPlaceholder) {
    FillStyle fill;
    fill.type = FillStyleType::kClippedBitmap;
    fill.bitmapCharacterId = 42;
    auto color = toFlatColor(fill);
    // Not asserting an exact shade (implementation detail) — just that it's
    // a genuine flat gray (r==g==b) rather than defaulting to black/solid.
    CHECK(color.r == color.g && color.g == color.b);
}

TEST_CASE(TessellateShape_Rectangle_ProducesOneClosedPolygon) {
    Shape shape = makeRectangleShape(200 * 20, 100 * 20, RgbaColor{255, 0, 0, 255});
    auto tess = tessellateShape(shape);

    CHECK_EQ(tess.polygons.size(), static_cast<size_t>(1));
    CHECK_EQ(tess.strokes.size(), static_cast<size_t>(0));

    const auto& poly = tess.polygons[0];
    CHECK_EQ(poly.color.r, 255);
    // MoveTo + 3 edges = 4 points; the closing edge back to points[0] is
    // implicit (IRenderer::fillPolygon contract), not a 5th stored point.
    CHECK_EQ(poly.points.size(), static_cast<size_t>(4));
    CHECK_EQ(poly.points[0].x, 0);
    CHECK_EQ(poly.points[0].y, 0);
    CHECK_EQ(poly.points[1].x, 200 * 20);
    CHECK_EQ(poly.points[1].y, 0);
    CHECK_EQ(poly.points[2].x, 200 * 20);
    CHECK_EQ(poly.points[2].y, 100 * 20);
    CHECK_EQ(poly.points[3].x, 0);
    CHECK_EQ(poly.points[3].y, 100 * 20);
}

TEST_CASE(TessellateShape_LineOnly_ProducesStrokeNoFill) {
    Shape shape;
    LineStyle line;
    line.widthTwips = 20;
    line.color = RgbaColor{0, 0, 255, 255};
    shape.lineStyles.push_back(line);

    ShapeRecord moveTo;
    moveTo.type = ShapeRecordType::kStyleChange;
    moveTo.styleChange = std::make_shared<ShapeStyleChange>();
    moveTo.styleChange->hasMoveTo = true;
    moveTo.styleChange->lineStyleIndex = 1;
    shape.records.push_back(moveTo);

    ShapeRecord edge;
    edge.type = ShapeRecordType::kStraightEdge;
    edge.edge.straightEdge.deltaXTwips = 500;
    shape.records.push_back(edge);

    auto tess = tessellateShape(shape);
    CHECK_EQ(tess.polygons.size(), static_cast<size_t>(0));  // no fill style active
    CHECK_EQ(tess.strokes.size(), static_cast<size_t>(1));
    CHECK_EQ(tess.strokes[0].widthTwips, 20);
    CHECK_EQ(tess.strokes[0].points.size(), static_cast<size_t>(2));
}

TEST_CASE(TessellateShape_CurvedEdge_SubdividesIntoMultiplePoints) {
    // A shape with a single fill style and one MoveTo + one curved edge:
    // the flattened polygon should contain the MoveTo point plus exactly
    // `curveSubdivisions` more points approximating the bezier.
    Shape shape;
    FillStyle fill;
    fill.type = FillStyleType::kSolid;
    fill.solidColor = RgbaColor{0, 255, 0, 255};
    shape.fillStyles.push_back(fill);

    ShapeRecord moveTo;
    moveTo.type = ShapeRecordType::kStyleChange;
    moveTo.styleChange = std::make_shared<ShapeStyleChange>();
    moveTo.styleChange->hasMoveTo = true;
    moveTo.styleChange->fillStyle1 = 1;
    shape.records.push_back(moveTo);

    ShapeRecord curve;
    curve.type = ShapeRecordType::kCurvedEdge;
    curve.edge.curvedEdge.controlDeltaXTwips = 100;
    curve.edge.curvedEdge.controlDeltaYTwips = 0;
    curve.edge.curvedEdge.anchorDeltaXTwips = 100;
    curve.edge.curvedEdge.anchorDeltaYTwips = 100;
    shape.records.push_back(curve);

    auto tess = tessellateShape(shape, /*curveSubdivisions=*/4);
    CHECK_EQ(tess.polygons.size(), static_cast<size_t>(1));
    // 1 MoveTo point + 4 subdivided bezier points.
    CHECK_EQ(tess.polygons[0].points.size(), static_cast<size_t>(5));
    // The anchor (final) point should land exactly at control+anchorDelta.
    const auto& last = tess.polygons[0].points.back();
    CHECK_EQ(last.x, 200);
    CHECK_EQ(last.y, 100);
}

// Graphics/gradients task (2026-08-28) — see IRenderer.h's
// DeviceGradientFill doc comment for the full design and real-corpus
// evidence (161 linear gradient fills, hobo.swf) this scope decision is
// based on.

TEST_CASE(BuildGradientRamp_TwoStops_InterpolatesEndpointsAndMidpointLinearly) {
    Gradient gradient;
    gradient.records.push_back(GradientRecord{0, RgbaColor{0, 0, 0, 255}});
    gradient.records.push_back(GradientRecord{255, RgbaColor{200, 100, 50, 255}});

    auto ramp = buildGradientRamp(gradient);
    CHECK_EQ(ramp[0].r, 0);
    CHECK_EQ(ramp[0].g, 0);
    CHECK_EQ(ramp[0].b, 0);
    CHECK_EQ(ramp[255].r, 200);
    CHECK_EQ(ramp[255].g, 100);
    CHECK_EQ(ramp[255].b, 50);
    // Midpoint (ratio 127/128, index 127 or 128): linear interpolation at
    // t~0.5 should land close to half of each channel — not asserting an
    // exact value (rounding depends on which index falls on which side of
    // 127.5), just that it's genuinely between the two endpoints, not equal
    // to either.
    CHECK(ramp[128].r > 0 && ramp[128].r < 200);
}

TEST_CASE(BuildGradientRamp_EmptyGradient_ReturnsGrayPlaceholderMatchingToFlatColor) {
    Gradient gradient;  // no records
    auto ramp = buildGradientRamp(gradient);
    FillStyle fill;
    fill.type = FillStyleType::kLinearGradient;
    fill.gradient = gradient;
    auto flat = toFlatColor(fill);
    CHECK_EQ(ramp[0].r, flat.r);
    CHECK_EQ(ramp[0].g, flat.g);
    CHECK_EQ(ramp[0].b, flat.b);
    CHECK_EQ(ramp[255].r, flat.r);
}

TEST_CASE(BuildGradientRamp_UnsortedStops_StillInterpolatesCorrectly) {
    // Real encoders are required to emit stops in non-decreasing ratio
    // order, but buildGradientRamp() defensively sorts a local copy rather
    // than assuming it — this exercises that path directly.
    Gradient gradient;
    gradient.records.push_back(GradientRecord{255, RgbaColor{200, 100, 50, 255}});
    gradient.records.push_back(GradientRecord{0, RgbaColor{0, 0, 0, 255}});
    auto ramp = buildGradientRamp(gradient);
    CHECK_EQ(ramp[0].r, 0);
    CHECK_EQ(ramp[255].r, 200);
}

TEST_CASE(TessellateShape_LinearGradientFill_SetsGradientPaintKindAndRamp) {
    Shape shape = makeRectangleShapeWithLinearGradient(200 * 20, 100 * 20, RgbaColor{255, 0, 0, 255},
                                                         RgbaColor{0, 0, 255, 255});
    auto tess = tessellateShape(shape);

    CHECK_EQ(tess.polygons.size(), static_cast<size_t>(1));
    const auto& poly = tess.polygons[0];
    CHECK(poly.paintKind == PaintKind::kLinearGradient);
    CHECK_EQ(poly.gradient.ramp[0].r, 255);
    CHECK_EQ(poly.gradient.ramp[0].b, 0);
    CHECK_EQ(poly.gradient.ramp[255].r, 0);
    CHECK_EQ(poly.gradient.ramp[255].b, 255);
}

TEST_CASE(TessellateShape_RadialGradientFill_StaysFlatByEvidenceDrivenScope) {
    // Real hobo.swf content has zero radial/focal-radial gradient fills
    // (/tmp/gradient_scan.cpp, 2026-08-28) — per this project's
    // evidence-driven-scope discipline, radial gradients deliberately stay
    // on the pre-existing toFlatColor() averaging path rather than being
    // implemented against no corpus evidence. This test locks that
    // decision in as an explicit, intentional behavior, not an oversight.
    Shape shape;
    FillStyle fill;
    fill.type = FillStyleType::kRadialGradient;
    fill.gradient.records.push_back(GradientRecord{0, RgbaColor{255, 0, 0, 255}});
    fill.gradient.records.push_back(GradientRecord{255, RgbaColor{0, 0, 255, 255}});
    shape.fillStyles.push_back(fill);

    ShapeRecord moveTo;
    moveTo.type = ShapeRecordType::kStyleChange;
    moveTo.styleChange = std::make_shared<ShapeStyleChange>();
    moveTo.styleChange->hasMoveTo = true;
    moveTo.styleChange->fillStyle1 = 1;
    shape.records.push_back(moveTo);
    ShapeRecord right;
    right.type = ShapeRecordType::kStraightEdge;
    right.edge.straightEdge.deltaXTwips = 100;
    shape.records.push_back(right);
    ShapeRecord down;
    down.type = ShapeRecordType::kStraightEdge;
    down.edge.straightEdge.deltaYTwips = 100;
    shape.records.push_back(down);
    ShapeRecord left;
    left.type = ShapeRecordType::kStraightEdge;
    left.edge.straightEdge.deltaXTwips = -100;
    shape.records.push_back(left);

    auto tess = tessellateShape(shape);
    CHECK_EQ(tess.polygons.size(), static_cast<size_t>(1));
    CHECK(tess.polygons[0].paintKind == PaintKind::kFlat);
    // toFlatColor() averages the two stops: (255+0)/2=127, (0+0)/2=0, (0+255)/2=127.
    CHECK_EQ(tess.polygons[0].color.r, 127);
    CHECK_EQ(tess.polygons[0].color.b, 127);
}
