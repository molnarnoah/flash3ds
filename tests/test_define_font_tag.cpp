#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/DefineFontTag.h"
#include "swf/TagCode.h"

using flash3ds::swf::parseDefineFont;
using flash3ds::swf::parseDefineFont2;
using flash3ds::swf::SwfReader;
using flash3ds::swf::TagCode;
namespace fixtures = flash3ds::test::fixtures;

TEST_CASE(DefineFont_V1_ParsesGlyphCountAndOutline) {
    auto glyphA = fixtures::buildGlyphShapeBytes(700, 700);
    auto glyphB = fixtures::buildGlyphShapeBytes(500, 900);
    auto bytes = fixtures::buildDefineFontV1Bytes(5, {glyphA, glyphB});

    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineFont(r);
    CHECK(def.has_value());
    CHECK_EQ(def->fontId, static_cast<uint16_t>(5));
    CHECK_EQ(def->glyphCount(), static_cast<size_t>(2));
    // Each glyph should have decoded a non-empty SHAPERECORD stream (the
    // MoveTo + 3 edges buildGlyphShapeBytes emits).
    CHECK(!def->glyphShapes[0].records.empty());
    CHECK(!def->glyphShapes[1].records.empty());
    // v1 has no code table.
    CHECK(def->codeTable.empty());
}

TEST_CASE(DefineFont_V1_EmptyFont_ParsesWithZeroGlyphs) {
    auto bytes = fixtures::buildDefineFontV1Bytes(9, {});
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineFont(r);
    CHECK(def.has_value());
    CHECK_EQ(def->glyphCount(), static_cast<size_t>(0));
}

TEST_CASE(DefineFont2_NoLayout_ParsesGlyphsAndCodeTable) {
    auto glyphA = fixtures::buildGlyphShapeBytes(600, 600);
    auto glyphB = fixtures::buildGlyphShapeBytes(600, 600);
    auto bytes = fixtures::buildDefineFont2Bytes(7, "TestFont", {glyphA, glyphB},
                                                   {'A', 'B'}, /*wideCodes=*/false, 0, 0, 0, {});

    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineFont2(r, static_cast<uint16_t>(TagCode::DefineFont2));
    CHECK(def.has_value());
    CHECK_EQ(def->fontId, static_cast<uint16_t>(7));
    CHECK_EQ(def->fontName, std::string("TestFont"));
    CHECK_EQ(def->glyphCount(), static_cast<size_t>(2));
    CHECK(!def->hasLayout);
    CHECK_EQ(def->codeTable.size(), static_cast<size_t>(2));
    CHECK_EQ(def->glyphIndexForCode('A'), 0);
    CHECK_EQ(def->glyphIndexForCode('B'), 1);
    CHECK_EQ(def->glyphIndexForCode('Z'), -1);
}

TEST_CASE(DefineFont2_WithLayout_ParsesMetricsAdvancesAndBounds) {
    auto glyphA = fixtures::buildGlyphShapeBytes(700, 700);
    fixtures::GlyphLayoutFixture layoutA;
    layoutA.advance = 750;
    layoutA.boundsXMin = 0;
    layoutA.boundsXMax = 700;
    layoutA.boundsYMin = -700;
    layoutA.boundsYMax = 0;

    auto bytes = fixtures::buildDefineFont2Bytes(11, "Layout", {glyphA}, {'X'},
                                                   /*wideCodes=*/false, 900, -200, 100, {layoutA});

    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineFont2(r, static_cast<uint16_t>(TagCode::DefineFont2));
    CHECK(def.has_value());
    CHECK(def->hasLayout);
    CHECK_EQ(def->ascent, static_cast<int16_t>(900));
    CHECK_EQ(def->descent, static_cast<int16_t>(-200));
    CHECK_EQ(def->leading, static_cast<int16_t>(100));
    CHECK_EQ(def->glyphAdvances.size(), static_cast<size_t>(1));
    CHECK_EQ(def->glyphAdvances[0], static_cast<int16_t>(750));
    CHECK_EQ(def->glyphBounds.size(), static_cast<size_t>(1));
    CHECK_EQ(def->glyphBounds[0].xMax, 700);
}

TEST_CASE(DefineFont2_WideCodes_ResolvesGlyphIndexForCode) {
    auto glyphA = fixtures::buildGlyphShapeBytes(600, 600);
    auto bytes = fixtures::buildDefineFont2Bytes(13, "Wide", {glyphA}, {0x1234},
                                                   /*wideCodes=*/true, 0, 0, 0, {});
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineFont2(r, static_cast<uint16_t>(TagCode::DefineFont2));
    CHECK(def.has_value());
    CHECK_EQ(def->glyphIndexForCode(0x1234), 0);
}

TEST_CASE(DefineFont2_DefineFont3TagCode_Rejected) {
    auto glyphA = fixtures::buildGlyphShapeBytes(600, 600);
    auto bytes = fixtures::buildDefineFont2Bytes(15, "F3", {glyphA}, {'A'}, false, 0, 0, 0, {});
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineFont2(r, 75 /* DefineFont3 */);
    CHECK(!def.has_value());
}
