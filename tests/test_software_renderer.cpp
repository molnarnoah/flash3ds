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
