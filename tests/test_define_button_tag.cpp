#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/DefineButtonTag.h"
#include "swf/TagCode.h"

using flash3ds::swf::ButtonCondition;
using flash3ds::swf::parseDefineButton;
using flash3ds::swf::SwfReader;
using flash3ds::swf::TagCode;
namespace fixtures = flash3ds::test::fixtures;

TEST_CASE(DefineButton_V1_ParsesStatesAndActions) {
    fixtures::ButtonRecordV1Fixture up;
    up.up = true;
    up.hitTest = true;
    up.characterId = 10;
    up.depth = 1;
    up.matrixBytes = fixtures::buildMatrixBytes(0, 0);

    fixtures::ButtonRecordV1Fixture down;
    down.down = true;
    down.characterId = 11;
    down.depth = 1;
    down.matrixBytes = fixtures::buildMatrixBytes(5 * 20, 5 * 20);

    std::vector<uint8_t> actions = {0x00};  // a single ActionEnd byte
    auto bytes = fixtures::buildDefineButtonV1Bytes(1, {up, down}, actions);

    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineButton(r, static_cast<uint16_t>(TagCode::DefineButton));
    CHECK(def.has_value());
    CHECK_EQ(def->characterId, static_cast<uint16_t>(1));
    CHECK_EQ(def->records.size(), static_cast<size_t>(2));

    CHECK(def->records[0].stateUp);
    CHECK(def->records[0].stateHitTest);
    CHECK(!def->records[0].stateOver);
    CHECK(!def->records[0].stateDown);
    CHECK_EQ(def->records[0].characterId, static_cast<uint16_t>(10));

    CHECK(def->records[1].stateDown);
    CHECK(!def->records[1].stateUp);
    CHECK_EQ(def->records[1].characterId, static_cast<uint16_t>(11));

    CHECK_EQ(def->actionsV1.size(), static_cast<size_t>(1));
    CHECK_EQ(def->actionsV1[0], static_cast<uint8_t>(0x00));
    CHECK(def->condActionsV2.empty());
}

TEST_CASE(DefineButton2_ParsesColorTransformAndCondActions) {
    fixtures::ButtonRecordV1Fixture up;
    up.up = true;
    up.characterId = 20;
    up.depth = 1;
    up.matrixBytes = fixtures::buildMatrixBytes(0, 0);

    uint16_t conditions = static_cast<uint16_t>(ButtonCondition::kOverDownToOverUp);
    std::vector<uint8_t> actions = {0x00};
    auto bytes = fixtures::buildDefineButtonV2Bytes(2, {up}, conditions, std::nullopt, actions);

    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineButton(r, static_cast<uint16_t>(TagCode::DefineButton2));
    CHECK(def.has_value());
    CHECK_EQ(def->characterId, static_cast<uint16_t>(2));
    CHECK_EQ(def->records.size(), static_cast<size_t>(1));
    CHECK(def->records[0].colorTransform.has_value());
    CHECK_EQ(def->records[0].colorTransform->redMult, 1.0);

    CHECK_EQ(def->condActionsV2.size(), static_cast<size_t>(1));
    CHECK_EQ(def->condActionsV2[0].conditions,
             static_cast<uint16_t>(ButtonCondition::kOverDownToOverUp));
    CHECK(!def->condActionsV2[0].keyCode.has_value());
    CHECK_EQ(def->condActionsV2[0].actionBytes.size(), static_cast<size_t>(1));
    CHECK(def->actionsV1.empty());
}

TEST_CASE(DefineButton2_KeyPressCondition_CapturesKeyCode) {
    fixtures::ButtonRecordV1Fixture up;
    up.up = true;
    up.characterId = 30;
    up.matrixBytes = fixtures::buildMatrixBytes(0, 0);

    std::vector<uint8_t> actions = {0x00};
    auto bytes = fixtures::buildDefineButtonV2Bytes(3, {up}, 0, /*keyCode=*/13, actions);

    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineButton(r, static_cast<uint16_t>(TagCode::DefineButton2));
    CHECK(def.has_value());
    CHECK_EQ(def->condActionsV2.size(), static_cast<size_t>(1));
    CHECK(def->condActionsV2[0].keyCode.has_value());
    CHECK_EQ(*def->condActionsV2[0].keyCode, static_cast<uint8_t>(13));
}

TEST_CASE(DefineButton2_NoActions_EmptyCondActionsList) {
    fixtures::ButtonRecordV1Fixture up;
    up.up = true;
    up.characterId = 40;
    up.matrixBytes = fixtures::buildMatrixBytes(0, 0);

    auto bytes = fixtures::buildDefineButtonV2Bytes(4, {up}, 0, std::nullopt, {});
    SwfReader r(bytes.data(), bytes.size());
    auto def = parseDefineButton(r, static_cast<uint16_t>(TagCode::DefineButton2));
    CHECK(def.has_value());
    CHECK(def->condActionsV2.empty());
}
