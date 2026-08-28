#include <cstdio>

#include "TestFramework.h"
#include "renderer/SoftwareRenderer.h"

using flash3ds::renderer::PointTwips;
using flash3ds::renderer::SoftwareRenderer;
using flash3ds::swf::RgbaColor;

TEST_CASE(SoftwareRenderer_BeginFrame_ClearsToBackgroundColor) {
    SoftwareRenderer renderer(10, 10);
    renderer.beginFrame(RgbaColor{200, 200, 200, 255});
    auto p = renderer.pixelAt(5, 5);
    CHECK_EQ(p.r, 200);
    CHECK_EQ(p.g, 200);
    CHECK_EQ(p.b, 200);
}

TEST_CASE(SoftwareRenderer_FillPolygon_FillsInsideLeavesOutsideUntouched) {
    SoftwareRenderer renderer(20, 20);
    renderer.beginFrame(RgbaColor{255, 255, 255, 255});

    // A 10x10 square from (2,2) to (12,12).
    std::vector<PointTwips> square = {
        {2, 2}, {12, 2}, {12, 12}, {2, 12},
    };
    renderer.fillPolygon(square, RgbaColor{255, 0, 0, 255});

    auto inside = renderer.pixelAt(6, 6);
    CHECK_EQ(inside.r, 255);
    CHECK_EQ(inside.g, 0);
    CHECK_EQ(inside.b, 0);

    auto outside = renderer.pixelAt(0, 0);
    CHECK_EQ(outside.r, 255);
    CHECK_EQ(outside.g, 255);
    CHECK_EQ(outside.b, 255);
}

TEST_CASE(SoftwareRenderer_FillPolygon_OutOfBoundsPointsDoNotCrash) {
    SoftwareRenderer renderer(10, 10);
    renderer.beginFrame(RgbaColor{255, 255, 255, 255});
    std::vector<PointTwips> square = {
        {-5, -5}, {50, -5}, {50, 50}, {-5, 50},
    };
    renderer.fillPolygon(square, RgbaColor{0, 255, 0, 255});
    // Should clamp to the framebuffer; center should now be green.
    auto center = renderer.pixelAt(5, 5);
    CHECK_EQ(center.g, 255);
}

// Diagnostic instrumentation (2026-08-28, "resolve the 7-12 FPS pacing"
// task -- see docs/performance-pacing.md): after the active-edge-table
// fix only produced a modest improvement, the next hypothesis is that
// blendPixel()'s alpha-blend divide path (hit by any fill with alpha <
// 255) -- not the edge-testing the previous fix targeted -- is a bigger
// factor. These counters measure that composition before any further
// fix is attempted. This test just confirms the counters classify each
// path correctly and that beginFrame() resets them for the next frame.
TEST_CASE(SoftwareRenderer_PixelWriteCounters_ClassifyOpaqueVsBlendedAndResetPerFrame) {
    SoftwareRenderer renderer(20, 20);
    renderer.beginFrame(RgbaColor{255, 255, 255, 255});
    CHECK_EQ(renderer.lastOpaquePixelWrites(), static_cast<size_t>(0));
    CHECK_EQ(renderer.lastBlendedPixelWrites(), static_cast<size_t>(0));

    // A 10x10 opaque square: every one of its interior pixels should
    // count as an opaque write, none as blended.
    std::vector<PointTwips> square = {
        {2, 2}, {12, 2}, {12, 12}, {2, 12},
    };
    renderer.fillPolygon(square, RgbaColor{255, 0, 0, 255});
    CHECK(renderer.lastOpaquePixelWrites() > 0);
    CHECK_EQ(renderer.lastBlendedPixelWrites(), static_cast<size_t>(0));
    size_t opaqueAfterFirstFill = renderer.lastOpaquePixelWrites();

    // A second, semi-transparent square overlapping the first: those
    // pixels should count as blended, on top of (not replacing) the
    // opaque count already tallied this frame.
    std::vector<PointTwips> translucentSquare = {
        {5, 5}, {15, 5}, {15, 15}, {5, 15},
    };
    renderer.fillPolygon(translucentSquare, RgbaColor{0, 0, 255, 128});
    CHECK(renderer.lastBlendedPixelWrites() > 0);
    CHECK_EQ(renderer.lastOpaquePixelWrites(), opaqueAfterFirstFill);  // unchanged by the second fill

    // A fully-transparent fill (alpha=0) is a real no-op -- it must not
    // be counted as either kind of write.
    size_t blendedBeforeNoop = renderer.lastBlendedPixelWrites();
    std::vector<PointTwips> invisibleSquare = {
        {0, 0}, {19, 0}, {19, 19}, {0, 19},
    };
    renderer.fillPolygon(invisibleSquare, RgbaColor{0, 0, 0, 0});
    CHECK_EQ(renderer.lastOpaquePixelWrites(), opaqueAfterFirstFill);
    CHECK_EQ(renderer.lastBlendedPixelWrites(), blendedBeforeNoop);

    // beginFrame() starts the next frame's tally at zero.
    renderer.beginFrame(RgbaColor{255, 255, 255, 255});
    CHECK_EQ(renderer.lastOpaquePixelWrites(), static_cast<size_t>(0));
    CHECK_EQ(renderer.lastBlendedPixelWrites(), static_cast<size_t>(0));
}

// Performance fix (2026-08-28, "resolve the 7-12 FPS pacing" task -- see
// docs/performance-pacing.md): fillPolygon() now builds an active-edge
// table instead of testing every edge on every scanline. A plain convex
// square (the test above) can't distinguish a correct active-edge sweep
// from a broken one, since a square only ever has exactly 2 active edges
// on any row for its whole height -- nothing gets added or removed
// mid-sweep. This test uses a concave "U" shape (a notch cut out of the
// top-middle of an otherwise-solid square) specifically so that two of
// its four vertical edges become active only from y=2 (the top) and
// inactive again at y=15 (partway down), forcing the active-edge list to
// actually add edges partway through AND drop them partway through --
// exactly the bug-prone part of an active-edge-table implementation.
TEST_CASE(SoftwareRenderer_FillPolygon_ConcaveShapeExercisesActiveEdgeAddAndRemove) {
    SoftwareRenderer renderer(30, 30);
    renderer.beginFrame(RgbaColor{255, 255, 255, 255});

    // A square from (2,2)-(28,28) with a notch cut from (12,2)-(18,15).
    std::vector<PointTwips> uShape = {
        {2, 2}, {12, 2}, {12, 15}, {18, 15}, {18, 2}, {28, 2}, {28, 28}, {2, 28},
    };
    renderer.fillPolygon(uShape, RgbaColor{255, 0, 0, 255});

    // Inside the notch's row range (y=8): filled to the left of the
    // notch, NOT filled inside the notch gap (two separate spans on this
    // scanline -- exactly what an active-edge sweep with 4 active edges
    // must produce), filled again to the right of the notch.
    auto leftOfNotch = renderer.pixelAt(6, 8);
    CHECK_EQ(leftOfNotch.r, 255);
    CHECK_EQ(leftOfNotch.g, 0);

    auto insideNotch = renderer.pixelAt(15, 8);
    CHECK_EQ(insideNotch.r, 255);
    CHECK_EQ(insideNotch.g, 255);  // still background white -- the notch gap

    auto rightOfNotch = renderer.pixelAt(22, 8);
    CHECK_EQ(rightOfNotch.r, 255);
    CHECK_EQ(rightOfNotch.g, 0);

    // Below the notch (y=20), the two notch-only edges have already been
    // dropped from the active list, so the row is one solid span again --
    // including the x range that was the notch gap higher up.
    auto belowNotchWhereGapWas = renderer.pixelAt(15, 20);
    CHECK_EQ(belowNotchWhereGapWas.r, 255);
    CHECK_EQ(belowNotchWhereGapWas.g, 0);

    auto belowNotchMainBody = renderer.pixelAt(6, 25);
    CHECK_EQ(belowNotchMainBody.r, 255);
    CHECK_EQ(belowNotchMainBody.g, 0);

    // Untouched outside the whole shape.
    auto outside = renderer.pixelAt(0, 0);
    CHECK_EQ(outside.r, 255);
    CHECK_EQ(outside.g, 255);
}

TEST_CASE(SoftwareRenderer_StrokePolyline_PlotsPointsAlongLine) {
    SoftwareRenderer renderer(20, 20);
    renderer.beginFrame(RgbaColor{255, 255, 255, 255});
    std::vector<PointTwips> line = {{2, 10}, {17, 10}};
    renderer.strokePolyline(line, RgbaColor{0, 0, 255, 255}, /*widthPixels=*/1);

    auto onLine = renderer.pixelAt(10, 10);
    CHECK_EQ(onLine.b, 255);
    CHECK_EQ(onLine.r, 0);

    auto offLine = renderer.pixelAt(10, 0);
    CHECK_EQ(offLine.r, 255);  // untouched background
}

TEST_CASE(SoftwareRenderer_WritePpm_ProducesValidP6Header) {
    SoftwareRenderer renderer(4, 3);
    renderer.beginFrame(RgbaColor{10, 20, 30, 255});
    std::string path = "/tmp/flash3ds_test_output.ppm";
    CHECK(renderer.writePpm(path));

    std::FILE* f = std::fopen(path.c_str(), "rb");
    CHECK(f != nullptr);
    char magic[2] = {0, 0};
    int width = 0, height = 0, maxVal = 0;
    int scanned = std::fscanf(f, "%2c %d %d %d", magic, &width, &height, &maxVal);
    CHECK_EQ(scanned, 4);
    CHECK_EQ(magic[0], 'P');
    CHECK_EQ(magic[1], '6');
    CHECK_EQ(width, 4);
    CHECK_EQ(height, 3);
    CHECK_EQ(maxVal, 255);
    std::fclose(f);
}
