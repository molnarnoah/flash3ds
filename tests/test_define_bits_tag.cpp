#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/DefineBitsTag.h"
#include "swf/TagCode.h"

using flash3ds::swf::parseDefineBits;
using flash3ds::swf::SwfReader;
using flash3ds::swf::TagCode;
namespace fixtures = flash3ds::test::fixtures;

// --- DefineBitsLossless (tag 20), BitmapFormat=5, PIX24 -------------------

TEST_CASE(DefineBitsLossless_Pix24_ParsesDimensionsAndOpaqueRgb) {
    // 2x1 image: left pixel red, right pixel green. PIX24 carries no alpha
    // of its own — parseLossless() must fill alpha=255 for every pixel.
    std::vector<std::vector<fixtures::Pix24Fixture>> rows = {
        {{255, 0, 0}, {0, 255, 0}},
    };
    auto bytes = fixtures::buildDefineBitsLosslessRgbBytes(/*characterId=*/7, /*width=*/2,
                                                             /*height=*/1, rows);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineBits(r, static_cast<uint16_t>(TagCode::DefineBitsLossless));
    CHECK(def.has_value());
    CHECK_EQ(def->characterId, static_cast<uint16_t>(7));
    CHECK_EQ(def->width, 2);
    CHECK_EQ(def->height, 1);
    CHECK_EQ(def->pixels.size(), static_cast<size_t>(2));
    CHECK_EQ(def->pixels[0].r, static_cast<uint8_t>(255));
    CHECK_EQ(def->pixels[0].g, static_cast<uint8_t>(0));
    CHECK_EQ(def->pixels[0].b, static_cast<uint8_t>(0));
    CHECK_EQ(def->pixels[0].a, static_cast<uint8_t>(255));
    CHECK_EQ(def->pixels[1].r, static_cast<uint8_t>(0));
    CHECK_EQ(def->pixels[1].g, static_cast<uint8_t>(255));
    CHECK_EQ(def->pixels[1].b, static_cast<uint8_t>(0));
    CHECK_EQ(def->pixels[1].a, static_cast<uint8_t>(255));
}

TEST_CASE(DefineBitsLossless_Pix24_MultiRow_PreservesRowOrder) {
    // 1x2 image (single column, two rows) — makes sure rows aren't
    // transposed or reversed by the zlib-decompress + row-stride logic.
    std::vector<std::vector<fixtures::Pix24Fixture>> rows = {
        {{10, 20, 30}},
        {{40, 50, 60}},
    };
    auto bytes = fixtures::buildDefineBitsLosslessRgbBytes(1, 1, 2, rows);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineBits(r, static_cast<uint16_t>(TagCode::DefineBitsLossless));
    CHECK(def.has_value());
    CHECK_EQ(def->pixels.size(), static_cast<size_t>(2));
    CHECK_EQ(def->pixels[0].r, static_cast<uint8_t>(10));
    CHECK_EQ(def->pixels[0].g, static_cast<uint8_t>(20));
    CHECK_EQ(def->pixels[0].b, static_cast<uint8_t>(30));
    CHECK_EQ(def->pixels[1].r, static_cast<uint8_t>(40));
    CHECK_EQ(def->pixels[1].g, static_cast<uint8_t>(50));
    CHECK_EQ(def->pixels[1].b, static_cast<uint8_t>(60));
}

// --- DefineBitsLossless2 (tag 36), BitmapFormat=5, premultiplied ARGB32 ---

TEST_CASE(DefineBitsLossless2_Argb32_UnpremultipliesBackToStraightAlpha) {
    // Half-transparent red, fully-opaque blue, fully-transparent (should
    // come back as (0,0,0,0) per parseLossless()'s unpremultiply() a==0
    // special case regardless of what "straight" color was requested).
    std::vector<std::vector<fixtures::StraightRgbaFixture>> rows = {
        {{255, 0, 0, 128}, {0, 0, 255, 255}, {99, 99, 99, 0}},
    };
    auto bytes = fixtures::buildDefineBitsLossless2ArgbBytes(3, 3, 1, rows);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineBits(r, static_cast<uint16_t>(TagCode::DefineBitsLossless2));
    CHECK(def.has_value());
    CHECK_EQ(def->pixels.size(), static_cast<size_t>(3));

    // Half-alpha red: premultiply/unpremultiply round-trip should recover a
    // value very close to the original (rounding tolerance of a couple of
    // levels, same as the JPEG tolerance below).
    const auto& p0 = def->pixels[0];
    CHECK(p0.r >= 250);
    CHECK_EQ(p0.g, static_cast<uint8_t>(0));
    CHECK_EQ(p0.b, static_cast<uint8_t>(0));
    CHECK_EQ(p0.a, static_cast<uint8_t>(128));

    // Fully opaque blue round-trips exactly (alpha=255 means no rounding
    // loss in premultiply/unpremultiply).
    const auto& p1 = def->pixels[1];
    CHECK_EQ(p1.r, static_cast<uint8_t>(0));
    CHECK_EQ(p1.g, static_cast<uint8_t>(0));
    CHECK_EQ(p1.b, static_cast<uint8_t>(255));
    CHECK_EQ(p1.a, static_cast<uint8_t>(255));

    // Fully transparent -> (0,0,0,0), regardless of requested color.
    const auto& p2 = def->pixels[2];
    CHECK_EQ(p2.r, static_cast<uint8_t>(0));
    CHECK_EQ(p2.g, static_cast<uint8_t>(0));
    CHECK_EQ(p2.b, static_cast<uint8_t>(0));
    CHECK_EQ(p2.a, static_cast<uint8_t>(0));
}

// --- DefineBitsLossless/2 (tag 20/36), BitmapFormat=3, colormapped -------

TEST_CASE(DefineBitsLossless_Colormap_V1_ResolvesIndicesThroughRgbTable) {
    std::vector<fixtures::StraightRgbaFixture> colorTable = {
        {255, 0, 0, 255},
        {0, 255, 0, 255},
    };
    std::vector<std::vector<uint8_t>> rowIndices = {{0, 1, 0}};
    auto bytes = fixtures::buildDefineBitsLosslessColormapBytes(
        /*characterId=*/9, /*width=*/3, /*height=*/1, colorTable, rowIndices,
        /*version2=*/false);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineBits(r, static_cast<uint16_t>(TagCode::DefineBitsLossless));
    CHECK(def.has_value());
    CHECK_EQ(def->width, 3);
    CHECK_EQ(def->height, 1);
    CHECK_EQ(def->pixels.size(), static_cast<size_t>(3));
    CHECK_EQ(def->pixels[0].r, static_cast<uint8_t>(255));
    CHECK_EQ(def->pixels[0].g, static_cast<uint8_t>(0));
    CHECK_EQ(def->pixels[0].a, static_cast<uint8_t>(255));
    CHECK_EQ(def->pixels[1].r, static_cast<uint8_t>(0));
    CHECK_EQ(def->pixels[1].g, static_cast<uint8_t>(255));
    CHECK_EQ(def->pixels[2].r, static_cast<uint8_t>(255));
    CHECK_EQ(def->pixels[2].g, static_cast<uint8_t>(0));
}

TEST_CASE(DefineBitsLossless2_Colormap_V2_ResolvesIndicesThroughRgbaTable) {
    // v2 colormap tables carry a real alpha byte per entry — confirm it's
    // actually read (not just the RGB, which the v1 test above already
    // covers) and passed through untouched (colormapped pixels aren't
    // premultiplied, unlike format-5 ARGB32).
    std::vector<fixtures::StraightRgbaFixture> colorTable = {
        {10, 20, 30, 64},
        {40, 50, 60, 200},
    };
    std::vector<std::vector<uint8_t>> rowIndices = {{1, 0}};
    auto bytes = fixtures::buildDefineBitsLosslessColormapBytes(11, 2, 1, colorTable, rowIndices,
                                                                  /*version2=*/true);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineBits(r, static_cast<uint16_t>(TagCode::DefineBitsLossless2));
    CHECK(def.has_value());
    CHECK_EQ(def->pixels.size(), static_cast<size_t>(2));
    CHECK_EQ(def->pixels[0].r, static_cast<uint8_t>(40));
    CHECK_EQ(def->pixels[0].g, static_cast<uint8_t>(50));
    CHECK_EQ(def->pixels[0].b, static_cast<uint8_t>(60));
    CHECK_EQ(def->pixels[0].a, static_cast<uint8_t>(200));
    CHECK_EQ(def->pixels[1].r, static_cast<uint8_t>(10));
    CHECK_EQ(def->pixels[1].g, static_cast<uint8_t>(20));
    CHECK_EQ(def->pixels[1].b, static_cast<uint8_t>(30));
    CHECK_EQ(def->pixels[1].a, static_cast<uint8_t>(64));
}

// --- DefineBitsJpeg2 (tag 21) ---------------------------------------------

TEST_CASE(DefineBitsJpeg2_DecodesRealJpeg_RecoversApproximateColorsAndOpaqueAlpha) {
    auto bytes = fixtures::buildDefineBitsJpeg2Bytes(5, fixtures::sampleTinyJpegBytes());
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineBits(r, static_cast<uint16_t>(TagCode::DefineBitsJpeg2));
    CHECK(def.has_value());
    CHECK_EQ(def->characterId, static_cast<uint16_t>(5));
    CHECK_EQ(def->width, 8);
    CHECK_EQ(def->height, 8);
    CHECK_EQ(def->pixels.size(), static_cast<size_t>(64));

    // Left half is dark red (200,20,20), right half dark blue (20,20,200) —
    // JPEG is lossy, so allow a generous tolerance (jpgd_probe.cpp's
    // earlier standalone check showed ~1-unit error at quality=100; a
    // ±10 band comfortably covers that plus any minor decoder-version
    // drift without weakening the test into meaninglessness).
    auto closeTo = [](uint8_t actual, int expected) {
        int diff = static_cast<int>(actual) - expected;
        return diff >= -10 && diff <= 10;
    };
    // Top-left pixel (0,0) should be the dark-red side.
    const auto& topLeft = def->pixels[0];
    CHECK(closeTo(topLeft.r, 200));
    CHECK(closeTo(topLeft.g, 20));
    CHECK(closeTo(topLeft.b, 20));
    CHECK_EQ(topLeft.a, static_cast<uint8_t>(255));  // JPEG2 has no alpha channel.

    // Top-right pixel (7,0) should be the dark-blue side.
    const auto& topRight = def->pixels[7];
    CHECK(closeTo(topRight.r, 20));
    CHECK(closeTo(topRight.g, 20));
    CHECK(closeTo(topRight.b, 200));
    CHECK_EQ(topRight.a, static_cast<uint8_t>(255));
}

TEST_CASE(DefineBitsJpeg2_ErroneousEoiSoiMarkerPairMidStream_StillDecodesCorrectly) {
    // Real-corpus finding (2026-08-31): several Hobo/Extreme Pamplona
    // DefineBitsJpeg2/3 tags embed a spurious 4-byte "EOI SOI"
    // (0xFF 0xD9 0xFF 0xD8) marker pair partway through the JPEG stream —
    // see DefineBitsTag.cpp's stripErroneousEoiSoiMarkers() for the full
    // evidence writeup. Splice that exact 4-byte sequence into the middle
    // of an otherwise-valid JPEG and confirm parseDefineBits() still
    // recovers the correct image (rather than failing, or silently
    // decoding the wrong/truncated data) — a direct regression test for
    // the real bug this project's own corpus verification found.
    auto original = fixtures::sampleTinyJpegBytes();
    std::vector<uint8_t> corrupted(original.begin(), original.begin() + original.size() / 2);
    corrupted.push_back(0xFF);
    corrupted.push_back(0xD9);
    corrupted.push_back(0xFF);
    corrupted.push_back(0xD8);
    corrupted.insert(corrupted.end(), original.begin() + original.size() / 2, original.end());

    auto bytes = fixtures::buildDefineBitsJpeg2Bytes(9, corrupted);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineBits(r, static_cast<uint16_t>(TagCode::DefineBitsJpeg2));
    CHECK(def.has_value());
    CHECK_EQ(def->width, 8);
    CHECK_EQ(def->height, 8);
    CHECK_EQ(def->pixels.size(), static_cast<size_t>(64));

    auto closeTo = [](uint8_t actual, int expected) {
        int diff = static_cast<int>(actual) - expected;
        return diff >= -10 && diff <= 10;
    };
    const auto& topLeft = def->pixels[0];
    CHECK(closeTo(topLeft.r, 200));
    CHECK(closeTo(topLeft.g, 20));
    CHECK(closeTo(topLeft.b, 20));
    const auto& topRight = def->pixels[7];
    CHECK(closeTo(topRight.r, 20));
    CHECK(closeTo(topRight.g, 20));
    CHECK(closeTo(topRight.b, 200));
}

// --- DefineBitsJpeg3 (tag 35) ---------------------------------------------

TEST_CASE(DefineBitsJpeg3_WithAlphaChannel_AppliesPerPixelAlpha) {
    // 8x8 image, 64 alpha bytes: left half fully transparent (0), right half
    // fully opaque (255) — independent of the JPEG's own red/blue split, to
    // confirm alpha is read from its own separate zlib record rather than
    // being derived from color somehow.
    std::vector<uint8_t> alpha(64, 0);
    for (int y = 0; y < 8; ++y) {
        for (int x = 4; x < 8; ++x) alpha[y * 8 + x] = 255;
    }
    auto bytes =
        fixtures::buildDefineBitsJpeg3Bytes(6, fixtures::sampleTinyJpegBytes(), alpha);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineBits(r, static_cast<uint16_t>(TagCode::DefineBitsJpeg3));
    CHECK(def.has_value());
    CHECK_EQ(def->characterId, static_cast<uint16_t>(6));
    CHECK_EQ(def->pixels.size(), static_cast<size_t>(64));
    CHECK_EQ(def->pixels[0].a, static_cast<uint8_t>(0));    // top-left, transparent half
    CHECK_EQ(def->pixels[7].a, static_cast<uint8_t>(255));  // top-right, opaque half
}

TEST_CASE(DefineBitsJpeg3_NoAlphaRecord_FallsBackToFullyOpaque) {
    // Empty alphaBytes -> buildDefineBitsJpeg3Bytes writes NO alpha record
    // at all, exercising parseJpeg3()'s documented "optional alpha, falls
    // back to opaque" path.
    std::vector<uint8_t> noAlpha;
    auto bytes =
        fixtures::buildDefineBitsJpeg3Bytes(6, fixtures::sampleTinyJpegBytes(), noAlpha);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineBits(r, static_cast<uint16_t>(TagCode::DefineBitsJpeg3));
    CHECK(def.has_value());
    CHECK_EQ(def->pixels.size(), static_cast<size_t>(64));
    for (const auto& px : def->pixels) {
        CHECK_EQ(px.a, static_cast<uint8_t>(255));
    }
}

// --- Out-of-scope tags ----------------------------------------------------

TEST_CASE(DefineBits_UnsupportedTagCode_ReturnsNullopt) {
    // DefineBits (tag 6) needs an external JPEGTables record this parser
    // doesn't thread through — explicitly out of scope (zero corpus
    // evidence), must return nullopt rather than misparsing.
    auto bytes = fixtures::buildDefineBitsJpeg2Bytes(1, fixtures::sampleTinyJpegBytes());
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineBits(r, static_cast<uint16_t>(TagCode::DefineBits));
    CHECK(!def.has_value());
}

TEST_CASE(DefineBitsJpeg4_UnsupportedTagCode_ReturnsNullopt) {
    auto bytes = fixtures::buildDefineBitsJpeg3Bytes(1, fixtures::sampleTinyJpegBytes(), {});
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineBits(r, static_cast<uint16_t>(TagCode::DefineBitsJpeg4));
    CHECK(!def.has_value());
}
