#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/DefineEditTextTag.h"

using flash3ds::swf::parseDefineEditText;
using flash3ds::swf::SwfReader;
namespace fixtures = flash3ds::test::fixtures;

TEST_CASE(DefineEditText_FullFields_ParsesFontColorAndText) {
    auto bytes = fixtures::buildDefineEditTextBytes(
        8, 100 * 20, 30 * 20, /*fontId=*/3, /*fontHeight=*/240,
        std::array<uint8_t, 4>{0x10, 0x20, 0x30, 0xFF}, "score", std::string("Hello"));

    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineEditText(r);
    CHECK(def.has_value());
    CHECK_EQ(def->characterId, static_cast<uint16_t>(8));
    CHECK_EQ(def->bounds.widthTwips(), 100 * 20);
    CHECK(def->fontId.has_value());
    CHECK_EQ(*def->fontId, static_cast<uint16_t>(3));
    CHECK_EQ(*def->fontHeightTwips, static_cast<uint16_t>(240));
    CHECK(def->textColor.has_value());
    CHECK_EQ(def->textColor->r, 0x10);
    CHECK_EQ(def->textColor->a, 0xFF);
    CHECK_EQ(def->variableName, std::string("score"));
    CHECK(def->initialText.has_value());
    CHECK_EQ(*def->initialText, std::string("Hello"));
}

TEST_CASE(DefineEditText_NoFontNoText_OptionalFieldsAbsent) {
    auto bytes = fixtures::buildDefineEditTextBytes(9, 50 * 20, 20 * 20, std::nullopt,
                                                       std::nullopt, std::nullopt, "", std::nullopt);
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineEditText(r);
    CHECK(def.has_value());
    CHECK(!def->fontId.has_value());
    CHECK(!def->fontHeightTwips.has_value());
    CHECK(!def->textColor.has_value());
    CHECK(!def->initialText.has_value());
    CHECK_EQ(def->variableName, std::string(""));
}

TEST_CASE(DefineEditText_EmptyVariableName_ParsesAsEmptyString) {
    auto bytes = fixtures::buildDefineEditTextBytes(10, 10 * 20, 10 * 20, std::nullopt,
                                                       std::nullopt, std::nullopt, "",
                                                       std::string("static text"));
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineEditText(r);
    CHECK(def.has_value());
    CHECK(def->variableName.empty());
    CHECK_EQ(*def->initialText, std::string("static text"));
}
