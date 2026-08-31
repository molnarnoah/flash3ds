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

// Phase 3 investigation (2026-08-30): reproduces the exact real-content
// pattern that was silently losing fill regions -- two adjacent,
// differently-colored squares sharing a boundary, authored as ONE
// continuous edge run with a style-only StyleChangeRecord (no MoveTo)
// between them, exactly like hobo.swf's title/menu vector artwork (the
// user-reported "only base colors layer shows not everything" bug). The
// OLD tessellator (one polygon per pen-drawn subpath, style captured only
// at the subpath's start) merged both squares into a single 8-point blob
// colored entirely with the FIRST square's style, silently dropping the
// second square as its own region. See ShapeTessellator.h/.cpp's
// 2026-08-30 doc comments for the real edge-merging fix.
TEST_CASE(TessellateShape_TwoAdjacentSquaresNoMoveToBetween_ProducesTwoSeparatePolygons) {
    Shape shape;
    FillStyle fillA;
    fillA.type = FillStyleType::kSolid;
    fillA.solidColor = RgbaColor{255, 0, 0, 255};  // red
    shape.fillStyles.push_back(fillA);
    FillStyle fillB;
    fillB.type = FillStyleType::kSolid;
    fillB.solidColor = RgbaColor{0, 0, 255, 255};  // blue
    shape.fillStyles.push_back(fillB);

    // MoveTo (0,0), fillStyle1 = A (index 1).
    ShapeRecord moveTo;
    moveTo.type = ShapeRecordType::kStyleChange;
    moveTo.styleChange = std::make_shared<ShapeStyleChange>();
    moveTo.styleChange->hasMoveTo = true;
    moveTo.styleChange->moveToXTwips = 0;
    moveTo.styleChange->moveToYTwips = 0;
    moveTo.styleChange->fillStyle1 = 1;
    shape.records.push_back(moveTo);

    // Right square, (0,0) -> (100,0) -> (100,100) -> (0,100), relying on
    // the same IMPLICIT closing edge back to (0,0) every other fixture in
    // this file uses (TessellatedPolygon::points is an open polyline —
    // see its own doc comment) rather than authoring a 4th closing edge:
    // the pen is left sitting at (0,100), NOT back at the MoveTo point,
    // which is exactly what lets the very next style-only change below
    // continue the SAME pen-drawn run without a MoveTo, matching how real
    // hobo.swf content was found to author adjacent regions.
    auto addEdge = [&](int32_t dx, int32_t dy) {
        ShapeRecord edge;
        edge.type = ShapeRecordType::kStraightEdge;
        edge.edge.straightEdge.deltaXTwips = dx;
        edge.edge.straightEdge.deltaYTwips = dy;
        shape.records.push_back(edge);
    };
    addEdge(100, 0);
    addEdge(0, 100);
    addEdge(-100, 0);

    // Style-only change, deliberately NO MoveTo -- the pen is still
    // exactly at (0,100) after the last edge above. Switches to fillStyle
    // B (index 2) for the next loop.
    ShapeRecord styleOnly;
    styleOnly.type = ShapeRecordType::kStyleChange;
    styleOnly.styleChange = std::make_shared<ShapeStyleChange>();
    styleOnly.styleChange->fillStyle1 = 2;
    shape.records.push_back(styleOnly);

    // Left square (to the left of x=0, so it doesn't overlap the first),
    // continuing from (0,100): -> (-100,100) -> (-100,0) -> (0,0), again
    // relying on the implicit closing edge back to (0,100).
    addEdge(-100, 0);
    addEdge(0, -100);
    addEdge(100, 0);

    auto tess = tessellateShape(shape);

    CHECK_EQ(tess.polygons.size(), static_cast<size_t>(2));
    // Both squares' full geometry must survive -- 4 points each, not
    // merged into one 8-point (or fewer, if some edges were dropped) blob.
    CHECK_EQ(tess.polygons[0].points.size(), static_cast<size_t>(4));
    CHECK_EQ(tess.polygons[1].points.size(), static_cast<size_t>(4));
    // Each polygon keeps its OWN style's color -- this is the actual bug:
    // the old tessellator colored the whole merged blob with the first
    // style only.
    bool sawRed = false, sawBlue = false;
    for (const auto& poly : tess.polygons) {
        if (poly.color.r == 255 && poly.color.b == 0) sawRed = true;
        if (poly.color.r == 0 && poly.color.b == 255) sawBlue = true;
    }
    CHECK(sawRed);
    CHECK(sawBlue);
    // The two squares use DIFFERENT fill styles (A vs. B) — Priority Fix
    // List item #1's grouping must keep them as separate fillGroupIds, not
    // combine them into one even-odd group (that would be wrong: they're
    // unrelated adjacent regions, not an outer/hole pair of the same
    // style).
    CHECK(tess.polygons[0].fillGroupId != tess.polygons[1].fillGroupId);
}

TEST_CASE(TessellateShape_HoleLikeContourSameFillStyleTwoMoveToRuns_ShareFillGroupId) {
    // Priority Fix List item #1 (2026-08-31) — hole/counter rendering. This
    // builds the letter-"O"-shaped case the tessellator's own header
    // comment describes: an outer boundary contour and a separate,
    // disjoint inner contour, BOTH authored under the SAME fill style
    // (unlike the adjacent-squares test above, which deliberately uses two
    // DIFFERENT styles). Real font glyph data authors exactly this pattern
    // (see SceneRenderer::renderGlyph, which synthesizes a single-entry
    // FillStyle array for every glyph — so any letterform with a counter
    // naturally produces this shape). The fix is entirely about what the
    // TESSELLATOR reports (same fillGroupId for both contours, so the
    // renderer can combine-fill them) — the actual even-odd/hole pixel
    // behavior is verified separately in test_software_renderer.cpp
    // (SoftwareRenderer::fillPolygonGroup) and test_scene_renderer.cpp
    // (end-to-end through SceneRenderer).
    Shape shape;
    FillStyle fill;
    fill.type = FillStyleType::kSolid;
    fill.solidColor = RgbaColor{0, 200, 0, 255};  // green
    shape.fillStyles.push_back(fill);

    // Outer boundary: a 200x200 square, MoveTo'd, fillStyle1 = 1.
    ShapeRecord outerMoveTo;
    outerMoveTo.type = ShapeRecordType::kStyleChange;
    outerMoveTo.styleChange = std::make_shared<ShapeStyleChange>();
    outerMoveTo.styleChange->hasMoveTo = true;
    outerMoveTo.styleChange->moveToXTwips = 0;
    outerMoveTo.styleChange->moveToYTwips = 0;
    outerMoveTo.styleChange->fillStyle1 = 1;
    shape.records.push_back(outerMoveTo);

    auto addEdge = [&](int32_t dx, int32_t dy) {
        ShapeRecord edge;
        edge.type = ShapeRecordType::kStraightEdge;
        edge.edge.straightEdge.deltaXTwips = dx;
        edge.edge.straightEdge.deltaYTwips = dy;
        shape.records.push_back(edge);
    };
    addEdge(200, 0);
    addEdge(0, 200);
    addEdge(-200, 0);
    addEdge(0, -200);  // back to (0, 0), closing the outer square explicitly

    // Inner "hole" contour: a disjoint 60x60 square in the middle
    // (50,50)-(110,110), a fresh MoveTo, SAME fillStyle1 = 1 as the outer
    // boundary — this is exactly what makes it a hole/counter case rather
    // than an unrelated separate region.
    ShapeRecord innerMoveTo;
    innerMoveTo.type = ShapeRecordType::kStyleChange;
    innerMoveTo.styleChange = std::make_shared<ShapeStyleChange>();
    innerMoveTo.styleChange->hasMoveTo = true;
    innerMoveTo.styleChange->moveToXTwips = 50;
    innerMoveTo.styleChange->moveToYTwips = 50;
    innerMoveTo.styleChange->fillStyle1 = 1;
    shape.records.push_back(innerMoveTo);
    addEdge(60, 0);
    addEdge(0, 60);
    addEdge(-60, 0);
    addEdge(0, -60);

    auto tess = tessellateShape(shape);

    // Two genuinely separate, fully-preserved contours — NOT welded into
    // one polygon (that was the earlier, rejected whole-shape-chaining
    // design's failure mode) and NOT dropped.
    CHECK_EQ(tess.polygons.size(), static_cast<size_t>(2));
    // Same resolved FillStyle -> same fillGroupId, so the renderer knows to
    // combine-fill them via IRenderer::fillPolygonGroup() instead of
    // painting each as an independently solid polygon.
    CHECK_EQ(tess.polygons[0].fillGroupId, tess.polygons[1].fillGroupId);
    CHECK(tess.polygons[0].fillGroupId >= 0);
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
