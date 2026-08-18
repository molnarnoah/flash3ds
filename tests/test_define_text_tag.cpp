#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/DefineTextTag.h"
#include "swf/TagCode.h"

using flash3ds::swf::parseDefineText;
using flash3ds::swf::SwfReader;
using flash3ds::swf::TagCode;
namespace fixtures = flash3ds::test::fixtures;

TEST_CASE(DefineText_SingleRecord_ParsesFontColorAndGlyphs) {
    fixtures::TextRecordFixture rec;
    rec.fontId = 7;
    rec.textHeightTwips = 240;
    rec.colorRgba = std::array<uint8_t, 4>{0x11, 0x22, 0x33, 0xFF};
    rec.xOffsetTwips = 10;
    rec.yOffsetTwips = 20;
    rec.glyphs = {{0, 150}, {1, 200}};

    auto matrixBytes = fixtures::buildMatrixBytes(0, 0);
    auto bytes = fixtures::buildDefineTextBytes(3, matrixBytes, /*glyphBits=*/8,
                                                  /*advanceBits=*/10, {rec}, /*withAlpha=*/false);

    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineText(r, static_cast<uint16_t>(TagCode::DefineText));
    CHECK(def.has_value());
    CHECK_EQ(def->characterId, static_cast<uint16_t>(3));
    CHECK_EQ(def->records.size(), static_cast<size_t>(1));

    const auto& r0 = def->records[0];
    CHECK(r0.fontId.has_value());
    CHECK_EQ(*r0.fontId, static_cast<uint16_t>(7));
    CHECK_EQ(*r0.textHeightTwips, static_cast<uint16_t>(240));
    CHECK(r0.color.has_value());
    CHECK_EQ(r0.color->r, 0x11);
    CHECK_EQ(r0.color->g, 0x22);
    CHECK_EQ(r0.color->b, 0x33);
    // DefineText (v1, withAlpha=false) always reads alpha as opaque.
    CHECK_EQ(r0.color->a, 255);
    CHECK_EQ(*r0.xOffsetTwips, 10);
    CHECK_EQ(*r0.yOffsetTwips, 20);
    CHECK_EQ(r0.glyphs.size(), static_cast<size_t>(2));
    CHECK_EQ(r0.glyphs[0].glyphIndex, static_cast<uint32_t>(0));
    CHECK_EQ(r0.glyphs[0].advance, 150);
    CHECK_EQ(r0.glyphs[1].glyphIndex, static_cast<uint32_t>(1));
    CHECK_EQ(r0.glyphs[1].advance, 200);
}

TEST_CASE(DefineText2_ReadsRgbaColorWithAlpha) {
    fixtures::TextRecordFixture rec;
    rec.fontId = 1;
    rec.textHeightTwips = 100;
    rec.colorRgba = std::array<uint8_t, 4>{0x00, 0x00, 0x00, 0x80};
    rec.glyphs = {{0, 100}};

    auto matrixBytes = fixtures::buildMatrixBytes(0, 0);
    auto bytes = fixtures::buildDefineTextBytes(4, matrixBytes, 8, 8, {rec}, /*withAlpha=*/true);

    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineText(r, static_cast<uint16_t>(TagCode::DefineText2));
    CHECK(def.has_value());
    CHECK_EQ(def->records[0].color->a, 0x80);
}

TEST_CASE(DefineText_MultipleRecords_CarriesForwardUnsetFields) {
    fixtures::TextRecordFixture rec1;
    rec1.fontId = 2;
    rec1.textHeightTwips = 200;
    rec1.colorRgba = std::array<uint8_t, 4>{0xFF, 0, 0, 0xFF};
    rec1.xOffsetTwips = 0;
    rec1.yOffsetTwips = 0;
    rec1.glyphs = {{0, 100}};

    // Second record sets only YOffset — font/color/xOffset should be
    // std::nullopt (SceneRenderer is responsible for carrying the previous
    // value forward, not the parser).
    fixtures::TextRecordFixture rec2;
    rec2.yOffsetTwips = 240;
    rec2.glyphs = {{1, 100}};

    auto matrixBytes = fixtures::buildMatrixBytes(0, 0);
    auto bytes = fixtures::buildDefineTextBytes(5, matrixBytes, 8, 8, {rec1, rec2}, false);

    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineText(r, static_cast<uint16_t>(TagCode::DefineText));
    CHECK(def.has_value());
    CHECK_EQ(def->records.size(), static_cast<size_t>(2));
    CHECK(!def->records[1].fontId.has_value());
    CHECK(!def->records[1].color.has_value());
    CHECK(!def->records[1].xOffsetTwips.has_value());
    CHECK(def->records[1].yOffsetTwips.has_value());
    CHECK_EQ(*def->records[1].yOffsetTwips, 240);
}

TEST_CASE(DefineText_NoRecords_ParsesEmptyRecordList) {
    auto matrixBytes = fixtures::buildMatrixBytes(0, 0);
    auto bytes = fixtures::buildDefineTextBytes(6, matrixBytes, 4, 4, {}, false);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineText(r, static_cast<uint16_t>(TagCode::DefineText));
    CHECK(def.has_value());
    CHECK(def->records.empty());
}
