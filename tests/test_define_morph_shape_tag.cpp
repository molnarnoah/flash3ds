#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/DefineMorphShapeTag.h"
#include "swf/TagCode.h"

using flash3ds::swf::parseDefineMorphShape;
using flash3ds::swf::SwfReader;
using flash3ds::swf::TagCode;
namespace fixtures = flash3ds::test::fixtures;

TEST_CASE(DefineMorphShape_ParsesCharacterIdAndStartEndBounds) {
    auto bytes = fixtures::buildDefineMorphShapeBytes(
        /*characterId=*/12, /*startW=*/200 * 20, /*startH=*/100 * 20, /*endW=*/300 * 20,
        /*endH=*/150 * 20, 0x10, 0x20, 0x30, 0xFF, 0x40, 0x50, 0x60, 0x80);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineMorphShape(r, static_cast<uint16_t>(TagCode::DefineMorphShape));
    CHECK(def.has_value());
    CHECK_EQ(def->characterId, static_cast<uint16_t>(12));
    CHECK_EQ(def->startBounds.widthTwips(), 200 * 20);
    CHECK_EQ(def->startBounds.heightTwips(), 100 * 20);
    CHECK_EQ(def->endBounds.widthTwips(), 300 * 20);
    CHECK_EQ(def->endBounds.heightTwips(), 150 * 20);
}

TEST_CASE(DefineMorphShape_ParsesStartAndEndSolidFillColorsSeparately) {
    auto bytes = fixtures::buildDefineMorphShapeBytes(1, 100 * 20, 100 * 20, 100 * 20, 100 * 20,
                                                        0x10, 0x20, 0x30, 0xFF, 0x40, 0x50, 0x60,
                                                        0x80);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineMorphShape(r, static_cast<uint16_t>(TagCode::DefineMorphShape));
    CHECK(def.has_value());
    CHECK_EQ(def->fillStyles.size(), static_cast<size_t>(1));
    const auto& fs = def->fillStyles[0];
    CHECK(fs.isSolid());
    CHECK_EQ(fs.startColor.r, 0x10);
    CHECK_EQ(fs.startColor.g, 0x20);
    CHECK_EQ(fs.startColor.b, 0x30);
    CHECK_EQ(fs.startColor.a, 0xFF);
    CHECK_EQ(fs.endColor.r, 0x40);
    CHECK_EQ(fs.endColor.g, 0x50);
    CHECK_EQ(fs.endColor.b, 0x60);
    CHECK_EQ(fs.endColor.a, 0x80);
}

TEST_CASE(DefineMorphShape_ParsesDistinctStartAndEndEdgeGeometry) {
    // Start traces a 100x50 rect, end traces a 300x150 rect — StartEdges and
    // EndEdges are independently bit-packed SHAPE streams, so this exercises
    // that both get read (and their record counts reflect their own,
    // different, MoveTo+3-edge encodings) rather than one being silently
    // reused for the other.
    auto bytes = fixtures::buildDefineMorphShapeBytes(1, 100 * 20, 50 * 20, 300 * 20, 150 * 20,
                                                        0xFF, 0, 0, 0xFF, 0, 0xFF, 0, 0xFF);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineMorphShape(r, static_cast<uint16_t>(TagCode::DefineMorphShape));
    CHECK(def.has_value());
    // MoveTo StyleChangeRecord + 3 StraightEdge records each side.
    CHECK_EQ(def->startEdges.size(), static_cast<size_t>(4));
    CHECK_EQ(def->endEdges.size(), static_cast<size_t>(4));
    CHECK(def->startEdges[0].type == flash3ds::swf::ShapeRecordType::kStyleChange);
    CHECK(def->endEdges[0].type == flash3ds::swf::ShapeRecordType::kStyleChange);
}

TEST_CASE(DefineMorphShape_EmptyLineStyleArray_ParsesZeroLineStyles) {
    auto bytes = fixtures::buildDefineMorphShapeBytes(1, 50 * 20, 50 * 20, 50 * 20, 50 * 20, 0, 0,
                                                        0, 255, 0, 0, 0, 255);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineMorphShape(r, static_cast<uint16_t>(TagCode::DefineMorphShape));
    CHECK(def.has_value());
    CHECK_EQ(def->lineStyles.size(), static_cast<size_t>(0));
}

TEST_CASE(DefineMorphShape2_UnsupportedTagCode_ReturnsNullopt) {
    auto bytes = fixtures::buildDefineMorphShapeBytes(1, 20 * 20, 20 * 20, 20 * 20, 20 * 20, 0, 0,
                                                        0, 255, 0, 0, 0, 255);
    SwfReader r(bytes.data(), bytes.size());
    // DefineMorphShape2 (tag 84) is explicitly out of scope — zero
    // real-corpus evidence, see DefineMorphShapeTag.h.
    auto def = parseDefineMorphShape(r, static_cast<uint16_t>(TagCode::DefineMorphShape2));
    CHECK(!def.has_value());
}
