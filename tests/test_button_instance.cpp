// test_button_instance.cpp
//
// ButtonInstance phase (2026-08-19) regression tests: ButtonDef ->
// ButtonInstance runtime-instance creation, independent placements,
// transforms (including nested MovieClip -> Button composition),
// visibility, depth ordering, HitTest-state-vs-visual-state hit testing,
// UP/OVER/DOWN state transitions, removal, replacement, and duplicate
// placements. See docs/buttons.md for the design this backs.
//
// Deliberately does NOT test any ActionScript event dispatch
// (onPress/onRelease/onRollOver/onRollOut/onClipEvent(mouse*)) — see
// ButtonInstance.h's "SCOPE OF THIS PHASE": that dispatch doesn't exist
// yet, on purpose.

#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "runtime/ButtonInstance.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/MovieClipInstance.h"
#include "swf/SwfLoader.h"
#include "swf/SwfRecords.h"
#include "swf/TagCode.h"

namespace swf_fixtures = flash3ds::test::fixtures;
using flash3ds::runtime::ButtonInstance;
using flash3ds::runtime::CharacterDictionary;
using flash3ds::runtime::MovieClipInstance;
using flash3ds::runtime::ScriptEnvironment;
using flash3ds::swf::SwfLoader;
using flash3ds::swf::TagCode;

namespace {

// {DefineShape2(shapeId, a widthTwips x heightTwips solid rect),
// DefineButton2(buttonId, one record: Up+HitTest referencing shapeId at
// offset (0,0))} -- a minimal one-state button whose HitTest area exactly
// matches its Up-state visual. Real, legal SWF authoring: many buttons
// never define a separate invisible HitTest shape and just reuse Up (see
// characterOwnBoundsRect()'s documented fallback for this exact case,
// reused unchanged by ButtonInstance::hitTestLocal()).
std::vector<swf_fixtures::FixtureTag> buildSimpleButtonDefTags(uint16_t buttonId,
                                                                 uint16_t shapeId,
                                                                 int32_t widthTwips,
                                                                 int32_t heightTwips) {
    auto shapeBody = swf_fixtures::buildDefineShapeBytes(2, shapeId, widthTwips, heightTwips,
                                                          0x00, 0xFF, 0x00, 0xFF);
    swf_fixtures::ButtonRecordV1Fixture rec;
    rec.up = true;
    rec.hitTest = true;
    rec.characterId = shapeId;
    rec.depth = 1;
    rec.matrixBytes = swf_fixtures::buildMatrixBytes(0, 0);
    auto buttonBody = swf_fixtures::buildDefineButtonV2Bytes(buttonId, {rec}, 0, std::nullopt, {});
    return {
        {static_cast<uint16_t>(TagCode::DefineShape2), shapeBody},
        {static_cast<uint16_t>(TagCode::DefineButton2), buttonBody},
    };
}

// A button (buildSimpleButtonDefTags) whose Up-state VISUAL shape and
// HitTest-state shape are DIFFERENT sizes -- Up: 20x20px at origin;
// HitTest: 60x60px at origin. Used to prove hit-testing uses the HitTest
// geometry, not the (smaller) visual Up-state geometry.
std::vector<swf_fixtures::FixtureTag> buildButtonWithDistinctHitAreaTags(uint16_t buttonId,
                                                                          uint16_t upShapeId,
                                                                          uint16_t hitShapeId) {
    auto upShapeBody = swf_fixtures::buildDefineShapeBytes(2, upShapeId, 20 * 20, 20 * 20, 0xFF,
                                                            0x00, 0x00, 0xFF);
    auto hitShapeBody = swf_fixtures::buildDefineShapeBytes(2, hitShapeId, 60 * 20, 60 * 20, 0x00,
                                                             0x00, 0xFF, 0xFF);
    swf_fixtures::ButtonRecordV1Fixture upRec;
    upRec.up = true;
    upRec.characterId = upShapeId;
    upRec.depth = 1;
    upRec.matrixBytes = swf_fixtures::buildMatrixBytes(0, 0);
    swf_fixtures::ButtonRecordV1Fixture hitRec;
    hitRec.hitTest = true;
    hitRec.characterId = hitShapeId;
    hitRec.depth = 2;
    hitRec.matrixBytes = swf_fixtures::buildMatrixBytes(0, 0);
    auto buttonBody =
        swf_fixtures::buildDefineButtonV2Bytes(buttonId, {upRec, hitRec}, 0, std::nullopt, {});
    return {
        {static_cast<uint16_t>(TagCode::DefineShape2), upShapeBody},
        {static_cast<uint16_t>(TagCode::DefineShape2), hitShapeBody},
        {static_cast<uint16_t>(TagCode::DefineButton2), buttonBody},
    };
}

// Places a single button (buildSimpleButtonDefTags, 40x40px) named "btn"
// at the ROOT level, at the given placement matrix bytes and depth.
std::vector<uint8_t> buildMovieWithNamedButton(const std::vector<uint8_t>& matrixBytes,
                                                uint16_t buttonId = 50, uint16_t shapeId = 51,
                                                int32_t depth = 1) {
    auto tags = buildSimpleButtonDefTags(buttonId, shapeId, 40 * 20, 40 * 20);
    tags.push_back({26 /* PlaceObject2 */,
                    swf_fixtures::buildPlaceObject2Bytes(static_cast<uint16_t>(depth), false,
                                                          buttonId, matrixBytes,
                                                          std::string("btn"))});
    tags.push_back({1 /* ShowFrame */, {}});
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    return swf_fixtures::wrapFws(6, body);
}

// root -> "mc" (MovieClip, offset mcOffsetTwips) -> "btn" (Button, offset
// buttonOffsetTwips) -- for nested-button world-transform tests.
std::vector<uint8_t> buildMovieWithNestedButton(int32_t mcOffsetXTwips, int32_t mcOffsetYTwips,
                                                 int32_t buttonOffsetXTwips,
                                                 int32_t buttonOffsetYTwips) {
    uint16_t buttonId = 60, shapeId = 61, mcId = 62;
    auto nestedTags = buildSimpleButtonDefTags(buttonId, shapeId, 40 * 20, 40 * 20);
    nestedTags.push_back(
        {26, swf_fixtures::buildPlaceObject2Bytes(
                 1, false, buttonId,
                 swf_fixtures::buildMatrixBytes(buttonOffsetXTwips, buttonOffsetYTwips),
                 std::string("btn"))});
    nestedTags.push_back({1, {}});
    auto spriteBody = swf_fixtures::buildDefineSpriteBytes(mcId, 1, nestedTags);

    std::vector<swf_fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineSprite), spriteBody},
        {26, swf_fixtures::buildPlaceObject2Bytes(
                 1, false, mcId, swf_fixtures::buildMatrixBytes(mcOffsetXTwips, mcOffsetYTwips),
                 std::string("mc"))},
        {1, {}},
    };
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    return swf_fixtures::wrapFws(6, body);
}

// Two independent placements of the SAME ButtonDef (characterId=70): "a"
// at depth 1 offset (0,0), "b" at depth 2 offset (50,50)px.
std::vector<uint8_t> buildMovieWithTwoButtonPlacements() {
    uint16_t buttonId = 70, shapeId = 71;
    auto tags = buildSimpleButtonDefTags(buttonId, shapeId, 40 * 20, 40 * 20);
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(
                             1, false, buttonId, swf_fixtures::buildMatrixBytes(0, 0),
                             std::string("a"))});
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(
                             2, false, buttonId,
                             swf_fixtures::buildMatrixBytes(50 * 20, 50 * 20), std::string("b"))});
    tags.push_back({1, {}});
    auto body = swf_fixtures::buildMovieBody(200 * 20, 200 * 20, 12.0, 1, tags);
    return swf_fixtures::wrapFws(6, body);
}

// 3-frame movie: frame 1 places button (id=80) at depth 1; frame 2 REPLACES
// depth 1 with a plain shape (id=81); frame 3 removes depth 1 entirely.
std::vector<uint8_t> buildButtonRemovalReplacementMovie() {
    uint16_t buttonId = 80, buttonShapeId = 81, replacementShapeId = 82;
    auto buttonTags = buildSimpleButtonDefTags(buttonId, buttonShapeId, 40 * 20, 40 * 20);
    auto replacementShapeBody = swf_fixtures::buildDefineShapeBytes(
        2, replacementShapeId, 20 * 20, 20 * 20, 0x00, 0x00, 0xFF, 0xFF);

    std::vector<swf_fixtures::FixtureTag> tags = buttonTags;
    tags.push_back({static_cast<uint16_t>(TagCode::DefineShape2), replacementShapeBody});
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(
                             1, false, buttonId, swf_fixtures::buildMatrixBytes(0, 0),
                             std::string("btn"))});
    tags.push_back({1, {}});
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(
                             1, true, replacementShapeId, swf_fixtures::buildMatrixBytes(0, 0))});
    tags.push_back({1, {}});
    tags.push_back({28 /* RemoveObject2 */, swf_fixtures::buildRemoveObject2Bytes(1)});
    tags.push_back({1, {}});
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 3, tags);
    return swf_fixtures::wrapFws(6, body);
}

}  // namespace

// --- Creation / architecture --------------------------------------------

TEST_CASE(ButtonInstance_Placement_CreatesRuntimeInstance) {
    auto bytes = buildMovieWithNamedButton(swf_fixtures::buildMatrixBytes(0, 0));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto it = root->buttonInstances().find(1);
    CHECK(it != root->buttonInstances().end());
    if (it != root->buttonInstances().end()) {
        CHECK_EQ(it->second->characterId(), static_cast<uint16_t>(50));
        CHECK_EQ(it->second->depthInParent(), static_cast<int32_t>(1));
        CHECK_EQ(it->second->name(), std::string("btn"));
        CHECK_EQ(it->second->parent(), root.get());
    }
}

TEST_CASE(ButtonInstance_Placement_DoesNotCreateMovieClipChild) {
    // The core architectural distinction this phase establishes: a button
    // placement gets a ButtonInstance in buttonInstances_, NOT a
    // MovieClipInstance in children_ (which SceneRenderer walks) — see
    // ButtonInstance.h's "DISPLAY-LIST / RENDERING INTEGRATION" section.
    auto bytes = buildMovieWithNamedButton(swf_fixtures::buildMatrixBytes(0, 0));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK(root->children().find(1) == root->children().end());
    CHECK(root->buttonInstances().find(1) != root->buttonInstances().end());
}

TEST_CASE(ButtonInstance_DefaultState_IsUp) {
    auto bytes = buildMovieWithNamedButton(swf_fixtures::buildMatrixBytes(0, 0));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);

    auto it = root->buttonInstances().find(1);
    CHECK(it != root->buttonInstances().end());
    if (it != root->buttonInstances().end()) {
        CHECK(it->second->state() == ButtonInstance::State::kUp);
        CHECK(it->second->visible());
    }
}

// --- Independent instances / duplicate placements ------------------------

TEST_CASE(ButtonInstance_TwoPlacementsOfSameDef_AreIndependentInstances) {
    auto bytes = buildMovieWithTwoButtonPlacements();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto aIt = root->buttonInstances().find(1);
    auto bIt = root->buttonInstances().find(2);
    CHECK(aIt != root->buttonInstances().end());
    CHECK(bIt != root->buttonInstances().end());
    if (aIt == root->buttonInstances().end() || bIt == root->buttonInstances().end()) return;

    auto& a = aIt->second;
    auto& b = bIt->second;
    CHECK(a.get() != b.get());  // genuinely distinct objects
    CHECK_EQ(a->characterId(), b->characterId());  // same underlying ButtonDef (id 70)
    CHECK(&a->def() == &b->def());  // literally the SAME shared immutable definition

    // Mutating "a"'s state/visibility must never affect "b".
    a->updateState(/*isOver=*/true, /*mouseDown=*/true);
    a->setVisible(false);
    CHECK(a->state() == ButtonInstance::State::kDown);
    CHECK(!a->visible());
    CHECK(b->state() == ButtonInstance::State::kUp);
    CHECK(b->visible());
}

// --- Transforms ------------------------------------------------------------

TEST_CASE(ButtonInstance_Transform_Origin_IdentityMatrix) {
    auto bytes = buildMovieWithNamedButton(swf_fixtures::buildMatrixBytes(0, 0));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);

    auto it = root->buttonInstances().find(1);
    CHECK(it != root->buttonInstances().end());
    if (it != root->buttonInstances().end()) {
        CHECK_EQ(it->second->localMatrix().translateXTwips, 0);
        CHECK_EQ(it->second->localMatrix().translateYTwips, 0);
        CHECK_EQ(it->second->worldMatrix().translateXTwips, 0);
        CHECK_EQ(it->second->worldMatrix().translateYTwips, 0);
    }
}

TEST_CASE(ButtonInstance_Transform_Translated_MatchesPlacementMatrix) {
    auto bytes = buildMovieWithNamedButton(swf_fixtures::buildMatrixBytes(30 * 20, 20 * 20));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);

    auto it = root->buttonInstances().find(1);
    CHECK(it != root->buttonInstances().end());
    if (it != root->buttonInstances().end()) {
        CHECK_EQ(it->second->localMatrix().translateXTwips, static_cast<int32_t>(30 * 20));
        CHECK_EQ(it->second->localMatrix().translateYTwips, static_cast<int32_t>(20 * 20));
        // Placed directly at the root -- worldMatrix() equals localMatrix()
        // (root's own matrix_ is identity).
        CHECK_EQ(it->second->worldMatrix().translateXTwips, static_cast<int32_t>(30 * 20));
        CHECK_EQ(it->second->worldMatrix().translateYTwips, static_cast<int32_t>(20 * 20));
    }
}

TEST_CASE(ButtonInstance_Transform_Scaled_WorldMatrixReflectsScale) {
    // Not directly expressible via the translate-only MATRIX fixture
    // builder -- set a scaled matrix directly on the ButtonInstance
    // (exactly what a future SetProperty/_xscale bridge would eventually
    // do; see ButtonInstance.h's documented AS2-identity gap) and confirm
    // worldMatrix() reflects it verbatim (no hidden re-derivation).
    auto bytes = buildMovieWithNamedButton(swf_fixtures::buildMatrixBytes(0, 0));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);

    auto it = root->buttonInstances().find(1);
    CHECK(it != root->buttonInstances().end());
    if (it == root->buttonInstances().end()) return;

    flash3ds::swf::Matrix scaled;
    scaled.scaleX = 2.0;
    scaled.scaleY = 2.0;
    scaled.translateXTwips = 10 * 20;
    scaled.translateYTwips = 10 * 20;
    it->second->setLocalMatrix(scaled);

    CHECK_EQ(it->second->worldMatrix().scaleX, 2.0);
    CHECK_EQ(it->second->worldMatrix().scaleY, 2.0);
    CHECK_EQ(it->second->worldMatrix().translateXTwips, static_cast<int32_t>(10 * 20));
}

TEST_CASE(ButtonInstance_Transform_NestedInMovieClip_ComposesWithParent) {
    // root -> "mc" (offset 30,30) -> "btn" (offset 5,5) -- world offset
    // must be (35,35), exactly mirroring MovieClipInstance::worldMatrix()'s
    // own nested-composition contract.
    auto bytes = buildMovieWithNestedButton(30 * 20, 30 * 20, 5 * 20, 5 * 20);
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto mcIt = root->children().find(1);
    CHECK(mcIt != root->children().end());
    if (mcIt == root->children().end()) return;

    auto btnIt = mcIt->second->buttonInstances().find(1);
    CHECK(btnIt != mcIt->second->buttonInstances().end());
    if (btnIt == mcIt->second->buttonInstances().end()) return;

    CHECK_EQ(btnIt->second->localMatrix().translateXTwips, static_cast<int32_t>(5 * 20));
    CHECK_EQ(btnIt->second->worldMatrix().translateXTwips, static_cast<int32_t>(35 * 20));
    CHECK_EQ(btnIt->second->worldMatrix().translateYTwips, static_cast<int32_t>(35 * 20));
}

// --- Depth ordering / multiple buttons -------------------------------------

TEST_CASE(ButtonInstance_HitTest_OverlappingButtons_HigherDepthWins) {
    // Button A (id=90) at depth 1, (0,0)-(40,40)px. Button B (id=91) at
    // depth 2, offset (10,10) -- (10,10)-(50,50)px, painted on top. Point
    // (30,30) falls inside both; B must win (matches
    // MovieClipInstance_HitTestPoint_OverlappingShapes_TopmostDepthWins's
    // existing shape-only precedent, now exercised for buttons).
    uint16_t buttonAId = 90, shapeAId = 92, buttonBId = 91, shapeBId = 93;
    auto tags = buildSimpleButtonDefTags(buttonAId, shapeAId, 40 * 20, 40 * 20);
    auto bTags = buildSimpleButtonDefTags(buttonBId, shapeBId, 40 * 20, 40 * 20);
    tags.insert(tags.end(), bTags.begin(), bTags.end());
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(
                             1, false, buttonAId, swf_fixtures::buildMatrixBytes(0, 0),
                             std::string("a"))});
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(
                             2, false, buttonBId,
                             swf_fixtures::buildMatrixBytes(10 * 20, 10 * 20), std::string("b"))});
    tags.push_back({1, {}});
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    auto bytes = swf_fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto result = root->hitTestPoint(30.0, 30.0);
    CHECK(result.has_value());
    if (result) {
        CHECK(result->button != nullptr);
        CHECK_EQ(result->characterId, buttonBId);
        CHECK_EQ(result->depth, static_cast<int32_t>(2));
        auto bIt = root->buttonInstances().find(2);
        CHECK(bIt != root->buttonInstances().end());
        if (bIt != root->buttonInstances().end()) CHECK_EQ(result->button, bIt->second.get());
    }

    // A point only A covers still resolves to A.
    auto underOnly = root->hitTestPoint(5.0, 5.0);
    CHECK(underOnly.has_value());
    if (underOnly) CHECK_EQ(underOnly->characterId, buttonAId);
}

// --- HitTest-state vs. visual-state geometry -------------------------------

TEST_CASE(ButtonInstance_HitTest_UsesHitTestGeometry_NotSmallerVisualUpState) {
    // Up-state visual is a 20x20px shape at the origin; HitTest-state is a
    // separate, LARGER 60x60px shape. A point at (50,50) -- outside the
    // 20x20 Up box but inside the 60x60 HitTest box -- must still hit.
    uint16_t buttonId = 100, upShapeId = 101, hitShapeId = 102;
    auto tags = buildButtonWithDistinctHitAreaTags(buttonId, upShapeId, hitShapeId);
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(
                             1, false, buttonId, swf_fixtures::buildMatrixBytes(0, 0),
                             std::string("btn"))});
    tags.push_back({1, {}});
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    auto bytes = swf_fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto hit = root->hitTestPoint(50.0, 50.0);
    CHECK(hit.has_value());
    if (hit) CHECK(hit->button != nullptr);

    // Outside the 60x60 HitTest box entirely -- must miss.
    CHECK(!root->hitTestPoint(90.0, 90.0).has_value());
}

TEST_CASE(ButtonInstance_HitTest_InvisibleButton_NeverHits) {
    auto bytes = buildMovieWithNamedButton(swf_fixtures::buildMatrixBytes(0, 0));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);

    auto it = root->buttonInstances().find(1);
    CHECK(it != root->buttonInstances().end());
    if (it == root->buttonInstances().end()) return;

    CHECK(root->hitTestPoint(20.0, 20.0).has_value());  // sanity: hits while visible
    it->second->setVisible(false);
    CHECK(!root->hitTestPoint(20.0, 20.0).has_value());
}

// --- UP/OVER/DOWN state transitions (driven via advanceFrame()) -----------

TEST_CASE(ButtonInstance_StateTransitions_FullMatrix_MatchesDocumentedSemantics) {
    // Exercises the exact example transition table from the task charter:
    // outside -> UP; enters -> OVER; presses while over -> DOWN; remains
    // pressed -> DOWN; releases over -> OVER; leaves -> UP.
    auto bytes = buildMovieWithNamedButton(swf_fixtures::buildMatrixBytes(0, 0));  // 40x40 @ origin
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto it = root->buttonInstances().find(1);
    CHECK(it != root->buttonInstances().end());
    if (it == root->buttonInstances().end()) return;
    auto& button = it->second;

    // Pointer outside -> UP.
    env.inputState().setMousePosition(90.0, 90.0);
    env.inputState().setMouseDown(false);
    root->advanceFrame();
    CHECK(button->state() == ButtonInstance::State::kUp);

    // Pointer enters -> OVER.
    env.inputState().setMousePosition(20.0, 20.0);
    root->advanceFrame();
    CHECK(button->state() == ButtonInstance::State::kOver);
    CHECK(button->previousState() == ButtonInstance::State::kUp);

    // Presses while over -> DOWN.
    env.inputState().setMouseDown(true);
    root->advanceFrame();
    CHECK(button->state() == ButtonInstance::State::kDown);
    CHECK(button->previousState() == ButtonInstance::State::kOver);

    // Remains pressed, still over -> stays DOWN (no spurious transition).
    root->advanceFrame();
    CHECK(button->state() == ButtonInstance::State::kDown);
    CHECK(button->previousState() == ButtonInstance::State::kDown);

    // Releases over the button -> OVER.
    env.inputState().setMouseDown(false);
    root->advanceFrame();
    CHECK(button->state() == ButtonInstance::State::kOver);

    // Leaves -> UP.
    env.inputState().setMousePosition(90.0, 90.0);
    root->advanceFrame();
    CHECK(button->state() == ButtonInstance::State::kUp);
}

TEST_CASE(ButtonInstance_StateTransitions_MultipleButtons_OnlyTopmostGetsOver) {
    auto bytes = buildMovieWithTwoButtonPlacements();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto aIt = root->buttonInstances().find(1);
    auto bIt = root->buttonInstances().find(2);
    CHECK(aIt != root->buttonInstances().end());
    CHECK(bIt != root->buttonInstances().end());
    if (aIt == root->buttonInstances().end() || bIt == root->buttonInstances().end()) return;

    // "a" occupies (0,0)-(40,40)px; "b" occupies (50,50)-(90,90)px --
    // non-overlapping. Point inside "a" only.
    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().setMouseDown(false);
    root->advanceFrame();
    CHECK(aIt->second->state() == ButtonInstance::State::kOver);
    CHECK(bIt->second->state() == ButtonInstance::State::kUp);
}

// --- Lifetime: removal / replacement / duplicate placements ----------------

TEST_CASE(ButtonInstance_Removal_DisplayListDriven_ErasesInstance) {
    auto bytes = buildButtonRemovalReplacementMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    // Frame 1: button instance exists.
    CHECK(root->buttonInstances().find(1) != root->buttonInstances().end());

    // Frame 2: depth 1 is REPLACED with a plain shape (not a button) --
    // the button instance must be erased, and no MovieClip child appears
    // either (a plain leaf shape gets neither).
    root->advanceFrame();
    CHECK(root->buttonInstances().find(1) == root->buttonInstances().end());
    CHECK(root->children().find(1) == root->children().end());

    // Frame 3: depth 1 removed entirely -- still nothing.
    root->advanceFrame();
    CHECK(root->buttonInstances().find(1) == root->buttonInstances().end());
}

TEST_CASE(ButtonInstance_Replacement_SameDepthDifferentButton_CreatesFreshInstance) {
    // Frame 1: button A (id=110) at depth 1. Frame 2: depth 1 replaced by
    // DIFFERENT button B (id=111) -- must get a NEW ButtonInstance (not
    // the same object continuing), matching real Flash "characterId
    // changed at this depth -> genuine replacement" semantics (mirrors
    // children_'s own replacement rule exactly).
    uint16_t buttonAId = 110, shapeAId = 111, buttonBId = 112, shapeBId = 113;
    auto tagsA = buildSimpleButtonDefTags(buttonAId, shapeAId, 40 * 20, 40 * 20);
    auto tagsB = buildSimpleButtonDefTags(buttonBId, shapeBId, 40 * 20, 40 * 20);
    std::vector<swf_fixtures::FixtureTag> tags = tagsA;
    tags.insert(tags.end(), tagsB.begin(), tagsB.end());
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(
                             1, false, buttonAId, swf_fixtures::buildMatrixBytes(0, 0),
                             std::string("btn"))});
    tags.push_back({1, {}});
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(
                             1, true, buttonBId, swf_fixtures::buildMatrixBytes(0, 0))});
    tags.push_back({1, {}});
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 2, tags);
    auto bytes = swf_fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto firstIt = root->buttonInstances().find(1);
    CHECK(firstIt != root->buttonInstances().end());
    // A weak_ptr, not a raw pointer -- proves the OLD object was actually
    // DESTROYED (use_count reaches 0), not just relabeled in place. A raw
    // address comparison would be unreliable here: the allocator is free
    // to reuse the just-freed slot for the new instance, which would make
    // "new pointer != old pointer" a false negative even though a
    // genuinely fresh object was created.
    std::weak_ptr<ButtonInstance> firstWeak =
        firstIt != root->buttonInstances().end() ? firstIt->second : std::weak_ptr<ButtonInstance>();
    CHECK_EQ(firstIt->second->characterId(), buttonAId);

    root->advanceFrame();

    CHECK(firstWeak.expired());  // the old instance is genuinely gone

    auto secondIt = root->buttonInstances().find(1);
    CHECK(secondIt != root->buttonInstances().end());
    if (secondIt != root->buttonInstances().end()) {
        CHECK_EQ(secondIt->second->characterId(), buttonBId);
    }
}

TEST_CASE(ButtonInstance_DuplicatePlacements_BothResolveByName) {
    auto bytes = buildMovieWithTwoButtonPlacements();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    // AS2 object identity (see ButtonInstance.h's "AS2 OBJECT IDENTITY"
    // section) -- `_root.a` and `_root.b` resolve to distinct, real
    // objects via the SAME childNameToDepth_ + handleNativeGet() fallback
    // path a named MovieClip child would use.
    flash3ds::avm1::Value outA, outB;
    CHECK(root->scriptObject()->nativeGet("a", outA));
    CHECK(root->scriptObject()->nativeGet("b", outB));
    CHECK(outA.isObject());
    CHECK(outB.isObject());
    CHECK(outA.asObject().get() != outB.asObject().get());
}
