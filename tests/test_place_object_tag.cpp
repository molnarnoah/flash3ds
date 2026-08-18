#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/PlaceObjectTag.h"
#include "swf/TagCode.h"

using flash3ds::swf::parseFrameLabel;
using flash3ds::swf::parsePlaceObject;
using flash3ds::swf::parseRemoveObject;
using flash3ds::swf::SwfReader;
using flash3ds::swf::TagCode;
namespace fixtures = flash3ds::test::fixtures;

TEST_CASE(PlaceObjectV1_ParsesCharacterDepthMatrix) {
    auto matrix = fixtures::buildMatrixBytes(200, 300);
    auto bytes = fixtures::buildPlaceObjectV1Bytes(/*characterId=*/7, /*depth=*/1, matrix);
    SwfReader r(bytes.data(), bytes.size());

    auto rec = parsePlaceObject(r, static_cast<uint16_t>(TagCode::PlaceObject));
    CHECK(rec.has_value());
    CHECK_EQ(rec->version, 1);
    CHECK(!rec->move);
    CHECK(rec->characterId.has_value());
    CHECK_EQ(*rec->characterId, 7);
    CHECK_EQ(rec->depth, 1);
    CHECK(rec->matrix.has_value());
    CHECK_EQ(rec->matrix->translateXTwips, 200);
    CHECK_EQ(rec->matrix->translateYTwips, 300);
    CHECK(!rec->colorTransform.has_value());  // no trailing bytes -> absent
}

TEST_CASE(PlaceObject2_AddNew_MoveFalse_HasCharacterTrue) {
    auto matrix = fixtures::buildMatrixBytes(10, 20);
    auto bytes = fixtures::buildPlaceObject2Bytes(/*depth=*/5, /*move=*/false,
                                                    /*characterId=*/42, matrix, "hero");
    SwfReader r(bytes.data(), bytes.size());

    auto rec = parsePlaceObject(r, static_cast<uint16_t>(TagCode::PlaceObject2));
    CHECK(rec.has_value());
    CHECK_EQ(rec->version, 2);
    CHECK(!rec->move);
    CHECK(rec->characterId.has_value());
    CHECK_EQ(*rec->characterId, 42);
    CHECK_EQ(rec->depth, 5);
    CHECK(rec->matrix.has_value());
    CHECK(rec->name.has_value());
    CHECK_EQ(*rec->name, std::string("hero"));
}

TEST_CASE(PlaceObject2_UpdateOnly_MoveTrue_NoCharacter) {
    auto matrix = fixtures::buildMatrixBytes(99, 0);
    auto bytes =
        fixtures::buildPlaceObject2Bytes(/*depth=*/5, /*move=*/true,
                                           /*characterId=*/std::nullopt, matrix);
    SwfReader r(bytes.data(), bytes.size());

    auto rec = parsePlaceObject(r, static_cast<uint16_t>(TagCode::PlaceObject2));
    CHECK(rec.has_value());
    CHECK(rec->move);
    CHECK(!rec->characterId.has_value());
    CHECK_EQ(rec->depth, 5);
    CHECK(rec->matrix.has_value());
    CHECK_EQ(rec->matrix->translateXTwips, 99);
}

TEST_CASE(PlaceObject2_Replace_MoveTrue_HasCharacterTrue) {
    auto bytes = fixtures::buildPlaceObject2Bytes(/*depth=*/5, /*move=*/true,
                                                    /*characterId=*/43, std::nullopt);
    SwfReader r(bytes.data(), bytes.size());

    auto rec = parsePlaceObject(r, static_cast<uint16_t>(TagCode::PlaceObject2));
    CHECK(rec.has_value());
    CHECK(rec->move);
    CHECK(rec->characterId.has_value());
    CHECK_EQ(*rec->characterId, 43);
    CHECK(!rec->matrix.has_value());
}

TEST_CASE(RemoveObject2_ParsesDepth) {
    auto bytes = fixtures::buildRemoveObject2Bytes(9);
    SwfReader r(bytes.data(), bytes.size());
    auto rec = parseRemoveObject(r, static_cast<uint16_t>(TagCode::RemoveObject2));
    CHECK(rec.has_value());
    CHECK_EQ(rec->depth, 9);
    CHECK(!rec->characterId.has_value());
}

TEST_CASE(FrameLabel_ParsesName) {
    auto bytes = fixtures::buildFrameLabelBytes("start");
    SwfReader r(bytes.data(), bytes.size());
    auto label = parseFrameLabel(r);
    CHECK(label.has_value());
    CHECK_EQ(*label, std::string("start"));
}

// --- Phase 6: PlaceObject2 ClipActionRecord parsing --------------------

TEST_CASE(PlaceObject2_ClipActions_ParsesLoadAndEnterFrameRecords) {
    using flash3ds::swf::ClipEventFlag;
    std::vector<fixtures::ClipActionFixture> clipActions;
    clipActions.push_back({static_cast<uint32_t>(ClipEventFlag::kLoad), std::nullopt,
                            {0x00} /* single ActionEnd byte */});
    clipActions.push_back({static_cast<uint32_t>(ClipEventFlag::kEnterFrame), std::nullopt,
                            {0x07, 0x00} /* ActionStop, ActionEnd */});

    auto bytes = fixtures::buildPlaceObject2WithClipActionsBytes(
        /*depth=*/3, /*characterId=*/11, std::nullopt, std::nullopt, clipActions);
    SwfReader r(bytes.data(), bytes.size());

    auto rec = parsePlaceObject(r, static_cast<uint16_t>(TagCode::PlaceObject2), /*swfVersion=*/6);
    CHECK(rec.has_value());
    CHECK_EQ(rec->clipActions.size(), static_cast<size_t>(2));
    CHECK_EQ(rec->clipActions[0].eventFlags, static_cast<uint32_t>(ClipEventFlag::kLoad));
    CHECK(!rec->clipActions[0].keyCode.has_value());
    CHECK_EQ(rec->clipActions[0].actionBytes.size(), static_cast<size_t>(1));
    CHECK_EQ(rec->clipActions[1].eventFlags, static_cast<uint32_t>(ClipEventFlag::kEnterFrame));
    CHECK_EQ(rec->clipActions[1].actionBytes.size(), static_cast<size_t>(2));
}

TEST_CASE(PlaceObject2_ClipActions_KeyPressRecordCapturesKeyCode) {
    using flash3ds::swf::ClipEventFlag;
    std::vector<fixtures::ClipActionFixture> clipActions;
    clipActions.push_back({static_cast<uint32_t>(ClipEventFlag::kKeyPress), uint8_t{65} /* 'A' */,
                            {0x00}});

    auto bytes = fixtures::buildPlaceObject2WithClipActionsBytes(
        /*depth=*/3, /*characterId=*/11, std::nullopt, std::nullopt, clipActions);
    SwfReader r(bytes.data(), bytes.size());

    auto rec = parsePlaceObject(r, static_cast<uint16_t>(TagCode::PlaceObject2), /*swfVersion=*/6);
    CHECK(rec.has_value());
    CHECK_EQ(rec->clipActions.size(), static_cast<size_t>(1));
    CHECK(rec->clipActions[0].keyCode.has_value());
    CHECK_EQ(*rec->clipActions[0].keyCode, static_cast<uint8_t>(65));
}

TEST_CASE(PlaceObject2_NoClipActions_EmptyClipActionsList) {
    auto bytes = fixtures::buildPlaceObject2Bytes(/*depth=*/5, /*move=*/false,
                                                    /*characterId=*/42, std::nullopt);
    SwfReader r(bytes.data(), bytes.size());
    auto rec = parsePlaceObject(r, static_cast<uint16_t>(TagCode::PlaceObject2));
    CHECK(rec.has_value());
    CHECK(rec->clipActions.empty());
}
