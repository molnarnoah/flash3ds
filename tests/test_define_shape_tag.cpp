#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/DefineShapeTag.h"
#include "swf/TagCode.h"

using flash3ds::swf::parseDefineShape;
using flash3ds::swf::SwfReader;
using flash3ds::swf::TagCode;
namespace fixtures = flash3ds::test::fixtures;

TEST_CASE(DefineShape_Version1_ParsesCharacterIdBoundsAndShape) {
    auto bytes = fixtures::buildDefineShapeBytes(1, /*characterId=*/7, 300 * 20, 150 * 20, 0x10,
                                                   0x20, 0x30, 0xFF);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineShape(r, static_cast<uint16_t>(TagCode::DefineShape));
    CHECK(def.has_value());
    CHECK_EQ(def->characterId, static_cast<uint16_t>(7));
    CHECK_EQ(def->bounds.widthTwips(), 300 * 20);
    CHECK_EQ(def->bounds.heightTwips(), 150 * 20);
    CHECK_EQ(def->shape.fillStyles.size(), static_cast<size_t>(1));
    CHECK_EQ(def->shape.fillStyles[0].solidColor.r, 0x10);
}

TEST_CASE(DefineShape_Version3_ReadsRgbaSolidColor) {
    auto bytes = fixtures::buildDefineShapeBytes(3, /*characterId=*/9, 100 * 20, 100 * 20, 0x01,
                                                   0x02, 0x03, 0x80);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineShape(r, static_cast<uint16_t>(TagCode::DefineShape3));
    CHECK(def.has_value());
    CHECK_EQ(def->characterId, static_cast<uint16_t>(9));
    CHECK_EQ(def->shape.fillStyles[0].solidColor.a, 0x80);
}

TEST_CASE(DefineShape_UnsupportedTagCode_ReturnsNullopt) {
    auto bytes = fixtures::buildDefineShapeBytes(3, 1, 20, 20, 0, 0, 0, 255);
    SwfReader r(bytes.data(), bytes.size());
    // DefineShape4 (tag 83) is explicitly out of scope.
    auto def = parseDefineShape(r, 83);
    CHECK(!def.has_value());
}
