// test_movieclip_instance.cpp
//
// Phase 5 integration tests: AVM1 bytecode actually driving a real
// MovieClipInstance tree (DoAction dispatch, GetProperty/SetProperty,
// GetMember/SetMember on _x/_y-style intrinsic properties, independent
// per-instance playheads, CloneSprite/RemoveSprite, SetTarget, and
// _root/_parent/named-child resolution) — as opposed to test_avm1_*.cpp,
// which exercises the interpreter purely in isolation against raw bytecode
// buffers with no HostBindings wired up.

#include <cmath>

#include "Avm1TestFixtures.h"
#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "audio/IAudioBackend.h"
#include "avm1/Value.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/MovieClipInstance.h"
#include "swf/PlaceObjectTag.h"
#include "swf/SwfLoader.h"
#include "swf/TagCode.h"

namespace swf_fixtures = flash3ds::test::fixtures;
using flash3ds::avm1::Value;
using flash3ds::runtime::CharacterDictionary;
using flash3ds::runtime::MovieClipInstance;
using flash3ds::runtime::ScriptEnvironment;
using flash3ds::swf::SwfLoader;
using flash3ds::swf::TagCode;
using Asm = flash3ds::test::fixtures::Avm1Assembler;

namespace {

// A 100x100px, 1-frame movie whose single DoAction body is `actionBytes`.
// No characters are placed — used for tests that only touch the root's own
// scriptObject()/scope, not a child clip.
std::vector<uint8_t> buildRootScriptMovie(const std::vector<uint8_t>& actionBytes) {
    std::vector<swf_fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DoAction), actionBytes},
        {1 /* ShowFrame */, {}},
    };
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    return swf_fixtures::wrapFws(6, body);
}

// Like buildRootScriptMovie(), but with a caller-chosen stage size instead
// of the fixed 100x100px — used by the _xmouse/_ymouse coordinate-space
// conversion tests (interactivity phase, 2026-08-18), which need to
// exercise a stage size that DIFFERS from the input viewport size (e.g.
// hobo.swf's real 600x450 stage) to prove the scaling math, not just the
// "no viewport set" identity path the pre-existing 100x100px tests cover.
std::vector<uint8_t> buildRootScriptMovieWithStage(int32_t stageWidthTwips,
                                                    int32_t stageHeightTwips,
                                                    const std::vector<uint8_t>& actionBytes) {
    std::vector<swf_fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DoAction), actionBytes},
        {1 /* ShowFrame */, {}},
    };
    auto body = swf_fixtures::buildMovieBody(stageWidthTwips, stageHeightTwips, 12.0, 1, tags);
    return swf_fixtures::wrapFws(6, body);
}

// A 100x100px, `frameCount`-frame movie whose frame 1 DoAction body is
// `actionBytes`; `labels[i]` (if non-empty) becomes a FrameLabel tag on
// frame `i+1`. Used for Phase 9's OOP MovieClip-method tests
// (gotoAndStop/gotoAndPlay/stop/play/getBytesLoaded/getBytesTotal), which
// need more than one frame to observe playhead movement.
std::vector<uint8_t> buildMultiFrameRootScriptMovie(uint16_t frameCount,
                                                     const std::vector<uint8_t>& actionBytes,
                                                     const std::vector<std::string>& labels = {}) {
    std::vector<swf_fixtures::FixtureTag> tags;
    auto labelFor = [&](uint16_t frame1Based) -> std::string {
        size_t idx = frame1Based - 1;
        return idx < labels.size() ? labels[idx] : std::string();
    };
    for (uint16_t f = 1; f <= frameCount; ++f) {
        std::string label = labelFor(f);
        if (!label.empty()) {
            tags.push_back({43 /* FrameLabel */, swf_fixtures::buildFrameLabelBytes(label)});
        }
        if (f == 1 && !actionBytes.empty()) {
            tags.push_back({static_cast<uint16_t>(TagCode::DoAction), actionBytes});
        }
        tags.push_back({1 /* ShowFrame */, {}});
    }
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, frameCount, tags);
    return swf_fixtures::wrapFws(6, body);
}

// A 100x100px, `rootFrames`-frame movie that places a 1-depth, `spriteFrames`
// -frame sprite character (id=20, no visual content — an empty MovieClip is
// fine for scripting tests) named "mc" on root frame 1, with `actionBytes`
// as root frame 1's DoAction body (may be empty).
std::vector<uint8_t> buildMovieWithNamedChild(uint16_t rootFrames, uint16_t spriteFrames,
                                               const std::vector<uint8_t>& actionBytes) {
    std::vector<swf_fixtures::FixtureTag> nestedTags;
    for (uint16_t i = 0; i < spriteFrames; ++i) nestedTags.push_back({1 /* ShowFrame */, {}});
    auto spriteBody = swf_fixtures::buildDefineSpriteBytes(/*characterId=*/20, spriteFrames,
                                                             nestedTags);

    std::vector<swf_fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineSprite), spriteBody},
        {26 /* PlaceObject2 */,
         swf_fixtures::buildPlaceObject2Bytes(1, false, 20, std::nullopt, std::string("mc"))},
    };
    if (!actionBytes.empty()) {
        tags.push_back({static_cast<uint16_t>(TagCode::DoAction), actionBytes});
    }
    tags.push_back({1 /* ShowFrame */, {}});
    for (uint16_t f = 1; f < rootFrames; ++f) {
        tags.push_back({1 /* ShowFrame */, {}});
    }
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, rootFrames, tags);
    return swf_fixtures::wrapFws(6, body);
}

// A 100x100px, 1-frame movie placing a named sprite "mc" (id=20) at local
// (0,0) on the root; the sprite's own nested tag stream places one or more
// shapes (id 10, 11, 12, ... in `shapes` order) at the given per-shape
// offsets — used for the interactivity-audit phase's _width/_height
// bounds-computation regression tests. `rootActionBytes` (if non-empty)
// becomes root frame 1's DoAction body.
struct ShapePlacement {
    int32_t widthTwips;
    int32_t heightTwips;
    int32_t offsetXTwips;
    int32_t offsetYTwips;
};

std::vector<uint8_t> buildMovieWithNamedChildContainingShapes(
    const std::vector<ShapePlacement>& shapes, const std::vector<uint8_t>& rootActionBytes = {}) {
    std::vector<swf_fixtures::FixtureTag> nestedTags;
    uint16_t nextCharacterId = 10;
    int32_t depth = 1;
    for (const auto& shape : shapes) {
        uint16_t characterId = nextCharacterId++;
        auto shapeBody = swf_fixtures::buildDefineShapeBytes(2, characterId, shape.widthTwips,
                                                                shape.heightTwips, 0xFF, 0x00, 0x00,
                                                                0xFF);
        nestedTags.push_back({static_cast<uint16_t>(TagCode::DefineShape2), shapeBody});
        nestedTags.push_back(
            {26 /* PlaceObject2 */,
             swf_fixtures::buildPlaceObject2Bytes(
                 static_cast<uint16_t>(depth++), false, characterId,
                 swf_fixtures::buildMatrixBytes(shape.offsetXTwips, shape.offsetYTwips))});
    }
    nestedTags.push_back({1 /* ShowFrame */, {}});
    auto spriteBody = swf_fixtures::buildDefineSpriteBytes(/*characterId=*/20, 1, nestedTags);

    std::vector<swf_fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineSprite), spriteBody},
        {26 /* PlaceObject2 */,
         swf_fixtures::buildPlaceObject2Bytes(1, false, 20, std::nullopt, std::string("mc"))},
    };
    if (!rootActionBytes.empty()) {
        tags.push_back({static_cast<uint16_t>(TagCode::DoAction), rootActionBytes});
    }
    tags.push_back({1 /* ShowFrame */, {}});
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    return swf_fixtures::wrapFws(6, body);
}

// A 100x100px, 1-frame movie: root places sprite "outer" (id=20) at stage
// offset (30,30); "outer" places sprite "inner" (id=21) at ITS local offset
// (5,5); "inner" places a 10x10px shape (id=30) at ITS local offset (2,2).
// So the shape's WORLD (stage) position is (30+5+2, 30+5+2) = (37,37) to
// (47,47) — used by the hit-testing regression test that specifically
// exercises TWO levels of MovieClipInstance-child recursion (not just the
// one level buildMovieWithNamedChildContainingShapes's single "mc" already
// covers), confirming offsets compose correctly across nested clips.
std::vector<uint8_t> buildTwoLevelNestedMovieClipMovie() {
    auto shapeBody =
        swf_fixtures::buildDefineShapeBytes(2, /*characterId=*/30, 10 * 20, 10 * 20, 0xFF, 0x00,
                                             0x00, 0xFF);

    std::vector<swf_fixtures::FixtureTag> innerTags = {
        {26 /* PlaceObject2 */,
         swf_fixtures::buildPlaceObject2Bytes(1, false, 30,
                                               swf_fixtures::buildMatrixBytes(2 * 20, 2 * 20))},
        {1 /* ShowFrame */, {}},
    };
    auto innerSpriteBody = swf_fixtures::buildDefineSpriteBytes(/*characterId=*/21, 1, innerTags);

    std::vector<swf_fixtures::FixtureTag> outerTags = {
        {26 /* PlaceObject2 */,
         swf_fixtures::buildPlaceObject2Bytes(1, false, 21,
                                               swf_fixtures::buildMatrixBytes(5 * 20, 5 * 20),
                                               std::string("inner"))},
        {1 /* ShowFrame */, {}},
    };
    auto outerSpriteBody = swf_fixtures::buildDefineSpriteBytes(/*characterId=*/20, 1, outerTags);

    std::vector<swf_fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineShape2), shapeBody},
        {static_cast<uint16_t>(TagCode::DefineSprite), innerSpriteBody},
        {static_cast<uint16_t>(TagCode::DefineSprite), outerSpriteBody},
        {26 /* PlaceObject2 */,
         swf_fixtures::buildPlaceObject2Bytes(1, false, 20,
                                               swf_fixtures::buildMatrixBytes(30 * 20, 30 * 20),
                                               std::string("outer"))},
        {1 /* ShowFrame */, {}},
    };
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    return swf_fixtures::wrapFws(6, body);
}

// A spy IAudioBackend recording every call it receives — Phase 6 tests use
// this to verify StartSound tag dispatch and AVM1 Sound.start()/stop()
// actually reach the backend seam with the right arguments, without
// needing a real (codec-decoding) audio implementation.
class SpyAudioBackend : public flash3ds::audio::IAudioBackend {
public:
    struct PlayCall {
        uint16_t soundId;
        int loopCount;
    };
    std::vector<PlayCall> playCalls;
    std::vector<uint16_t> stopCalls;
    int stopAllCalls = 0;

    void playSound(uint16_t soundId, int loopCount) override {
        playCalls.push_back({soundId, loopCount});
    }
    void stopSound(uint16_t soundId) override { stopCalls.push_back(soundId); }
    void stopAllSounds() override { ++stopAllCalls; }
};

}  // namespace

TEST_CASE(MovieClipInstance_RootDoAction_SetsVariableOnScriptObject) {
    Asm a;
    a.pushString("greeting");
    a.pushString("hi");
    a.op(0x1D);  // ActionSetVariable
    auto bytes = buildRootScriptMovie(a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(root->scriptObject()->getOwnProperty("greeting").toString(), "hi");
}

TEST_CASE(MovieClipInstance_SetProperty_GetProperty_XRoundTrip) {
    Asm a;
    // SetProperty("", 0 /*_x*/, 42)
    a.pushString("");
    a.pushInt(0);
    a.pushInt(42);
    a.op(0x23);  // ActionSetProperty
    // capturedX = GetProperty("", 0 /*_x*/)
    a.pushString("capturedX");
    a.pushString("");
    a.pushInt(0);
    a.op(0x22);  // ActionGetProperty
    a.op(0x1D);  // ActionSetVariable
    auto bytes = buildRootScriptMovie(a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(root->x(), 42.0);
    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedX").toNumber(), 42.0);
}

TEST_CASE(MovieClipInstance_GetMember_SetMember_OnNamedChild) {
    Asm a;
    // mc._x = 99;
    a.pushString("mc");
    a.op(0x1C);  // ActionGetVariable
    a.pushString("_x");
    a.pushInt(99);
    a.op(0x4F);  // ActionSetMember
    // capturedX = mc._x;
    a.pushString("capturedX");
    a.pushString("mc");
    a.op(0x1C);  // ActionGetVariable
    a.pushString("_x");
    a.op(0x4E);  // ActionGetMember
    a.op(0x1D);  // ActionSetVariable
    auto bytes = buildMovieWithNamedChild(1, 1, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto childIt = root->children().find(1);
    CHECK(childIt != root->children().end());
    CHECK_EQ(childIt->second->x(), 99.0);
    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedX").toNumber(), 99.0);
}

TEST_CASE(MovieClipInstance_IndependentPlayhead_ChildAdvancesSeparatelyFromParent) {
    // Root: 2 frames. Child "mc": 3 frames. See buildMovieWithNamedChild.
    auto bytes = buildMovieWithNamedChild(2, 3, {});

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto childIt = root->children().find(1);
    CHECK(childIt != root->children().end());
    auto child = childIt->second;

    CHECK_EQ(root->timeline().currentFrame(), 1u);
    CHECK_EQ(child->timeline().currentFrame(), 1u);

    root->advanceFrame();
    CHECK_EQ(root->timeline().currentFrame(), 2u);
    CHECK_EQ(child->timeline().currentFrame(), 2u);

    root->advanceFrame();
    // Root loops back to frame 1 (2 frames total, playing past the end);
    // the child keeps going to its own frame 3 — independent playheads.
    CHECK_EQ(root->timeline().currentFrame(), 1u);
    CHECK_EQ(child->timeline().currentFrame(), 3u);
}

TEST_CASE(MovieClipInstance_Stop_HaltsOwnTimelineOnly) {
    Asm a;
    a.op(0x07);  // ActionStop
    auto bytes = buildRootScriptMovie(a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK(!root->timeline().isPlaying());
}

TEST_CASE(MovieClipInstance_SetTarget_StopsNamedChildWithoutAffectingRoot) {
    Asm a;
    a.setTargetAction("mc");
    a.op(0x07);  // ActionStop — applies to the "mc" target, not root
    a.setTargetAction("");  // reset
    auto bytes = buildMovieWithNamedChild(1, 3, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto childIt = root->children().find(1);
    CHECK(childIt != root->children().end());

    CHECK(root->timeline().isPlaying());
    CHECK(!childIt->second->timeline().isPlaying());
}

TEST_CASE(MovieClipInstance_CloneSprite_CreatesNewChildAtDepth) {
    Asm a;
    // CloneSprite("mc", "mc2", 5)
    a.pushString("mc");
    a.pushString("mc2");
    a.pushInt(5);
    a.op(0x24);  // ActionCloneSprite
    auto bytes = buildMovieWithNamedChild(1, 1, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(root->children().size(), static_cast<size_t>(2));
    auto cloneIt = root->children().find(5);
    CHECK(cloneIt != root->children().end());
    CHECK_EQ(cloneIt->second->name(), "mc2");
    CHECK_EQ(cloneIt->second->characterId(), root->children().at(1)->characterId());
}

TEST_CASE(MovieClipInstance_RemoveSprite_RemovesNamedChild) {
    Asm a;
    a.pushString("mc");
    a.op(0x25);  // ActionRemoveSprite
    auto bytes = buildMovieWithNamedChild(1, 1, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(root->children().size(), static_cast<size_t>(0));
}

TEST_CASE(MovieClipInstance_RootAndParent_ResolveToExpectedObjects) {
    auto bytes = buildMovieWithNamedChild(1, 1, {});

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto child = root->children().at(1);

    Value rootFromChild = child->scriptObject()->getMember("_root");
    CHECK(rootFromChild.isObject());
    CHECK(rootFromChild.asObject() == root->scriptObject());

    Value parentFromChild = child->scriptObject()->getMember("_parent");
    CHECK(parentFromChild.isObject());
    CHECK(parentFromChild.asObject() == root->scriptObject());

    Value parentFromRoot = root->scriptObject()->getMember("_parent");
    CHECK(parentFromRoot.isUndefined());
}

TEST_CASE(MovieClipInstance_ResolvePath_RelativeAbsoluteAndParent) {
    auto bytes = buildMovieWithNamedChild(1, 1, {});

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto child = root->children().at(1);

    CHECK(root->resolvePath("mc") == child.get());
    CHECK(root->resolvePath("/mc") == child.get());
    CHECK(root->resolvePath("_root.mc") == child.get());
    CHECK(child->resolvePath("..") == root.get());
    CHECK(root->resolvePath("nonexistent") == nullptr);
}

TEST_CASE(MovieClipInstance_XScaleRotation_ApproximateDecompositionRoundTrips) {
    auto bytes = buildRootScriptMovie({});
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    root->setXScale(50.0);
    root->setYScale(200.0);
    CHECK(std::abs(root->xScale() - 50.0) < 0.001);
    CHECK(std::abs(root->yScale() - 200.0) < 0.001);

    root->setRotation(90.0);
    CHECK(std::abs(root->rotation() - 90.0) < 0.001);
    // Scale should survive a subsequent rotation change (decomposition
    // preserves the other axis).
    CHECK(std::abs(root->xScale() - 50.0) < 0.001);
    CHECK(std::abs(root->yScale() - 200.0) < 0.001);
}

// ===========================================================================
// Interactivity-audit phase (2026-08-18): _width/_height bounds computation
// — previously hardcoded to always return 0 (see docs/known-limitations.md
// priority #2). This is the prerequisite bounds machinery hit-testing will
// build on next; hit-testing itself is NOT implemented yet (see
// docs/hit-testing.md).
// ===========================================================================

TEST_CASE(MovieClipInstance_WidthHeight_MatchesSinglePlacedShapeBounds) {
    // A 40x40px shape at local (0,0) inside "mc" — mc's own placement
    // matrix is identity (translate-only, at (0,0)), so mc._width/_height
    // should exactly match the shape's own size.
    auto bytes = buildMovieWithNamedChildContainingShapes({{40 * 20, 40 * 20, 0, 0}});
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto childIt = root->children().find(1);
    CHECK(childIt != root->children().end());
    CHECK_EQ(childIt->second->width(), 40.0);
    CHECK_EQ(childIt->second->height(), 40.0);
}

TEST_CASE(MovieClipInstance_WidthHeight_GetPropertyAndBareMemberAccess_AgreeWithCppApi) {
    auto bytes = buildMovieWithNamedChildContainingShapes({{40 * 20, 40 * 20, 0, 0}});
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    Asm a;
    // capturedWidthGetProperty = GetProperty("mc", 8 /*_width*/)
    a.pushString("capturedWidthGetProperty");
    a.pushString("mc");
    a.pushInt(8);
    a.op(0x22);  // ActionGetProperty
    a.op(0x1D);  // ActionSetVariable
    // capturedHeightGetProperty = GetProperty("mc", 9 /*_height*/)
    a.pushString("capturedHeightGetProperty");
    a.pushString("mc");
    a.pushInt(9);
    a.op(0x22);
    a.op(0x1D);
    // capturedWidthMember = mc._width (bare .member access, NOT GetProperty)
    a.pushString("capturedWidthMember");
    a.pushString("mc");
    a.op(0x1C);  // ActionGetVariable
    a.pushString("_width");
    a.op(0x4E);  // ActionGetMember
    a.op(0x1D);
    // Run directly against root's existing ScriptEnvironment/tree (rather
    // than baking this into the movie's own frame-1 DoAction) so the same
    // already-placed "mc" instance backs both the C++ width()/height() call
    // below and the AVM1-side reads.
    auto script = a.build();
    env.run(*root, script.data(), script.size());

    auto childIt = root->children().find(1);
    CHECK(childIt != root->children().end());
    double expected = childIt->second->width();
    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedWidthGetProperty").toNumber(), expected);
    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedHeightGetProperty").toNumber(),
             childIt->second->height());
    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedWidthMember").toNumber(), expected);
    CHECK_EQ(expected, 40.0);
}

TEST_CASE(MovieClipInstance_Width_ScalesWithXScale) {
    // Real AS2 _width/_height are measured in the PARENT's coordinate space
    // — scaling a clip must change its reported _width, not just its
    // visual size (see MovieClipInstance::width()'s doc comment).
    auto bytes = buildMovieWithNamedChildContainingShapes({{40 * 20, 40 * 20, 0, 0}});
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto childIt = root->children().find(1);
    CHECK(childIt != root->children().end());
    CHECK_EQ(childIt->second->width(), 40.0);

    childIt->second->setXScale(200.0);  // 200% -> double width
    CHECK(std::abs(childIt->second->width() - 80.0) < 0.01);
    // Height must be unaffected by an X-only scale.
    CHECK(std::abs(childIt->second->height() - 40.0) < 0.01);
}

TEST_CASE(MovieClipInstance_Width_UnionsAllPlacedShapes_NotJustFirst) {
    // Two 20x20px shapes: one at local (0,0)-(20,20)px, one at local
    // (60,60)-(80,80)px. The union's bounding box must span the full
    // (0,0)-(80,80)px extent — proving this is a real recursive union
    // across the whole display list, not just the first/last entry.
    auto bytes = buildMovieWithNamedChildContainingShapes({
        {20 * 20, 20 * 20, 0, 0},
        {20 * 20, 20 * 20, 60 * 20, 60 * 20},
    });
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto childIt = root->children().find(1);
    CHECK(childIt != root->children().end());
    CHECK_EQ(childIt->second->width(), 80.0);
    CHECK_EQ(childIt->second->height(), 80.0);
}

TEST_CASE(MovieClipInstance_WidthHeight_EmptyClip_ReturnsZero) {
    // No shapes placed at all inside "mc" — must return 0, not garbage
    // from an uninitialized/degenerate bounds accumulator.
    auto bytes = buildMovieWithNamedChildContainingShapes({});
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto childIt = root->children().find(1);
    CHECK(childIt != root->children().end());
    CHECK_EQ(childIt->second->width(), 0.0);
    CHECK_EQ(childIt->second->height(), 0.0);
}

// ===========================================================================
// Interactivity phase (2026-08-19): hit-testing (design: docs/hit-testing.md)
// ===========================================================================
//
// All fixtures use buildMovieWithNamedChildContainingShapes(): a 100x100px
// root stage containing one named child "mc" (placed at the stage origin,
// identity matrix), which in turn contains the given shapes at the given
// offsets. So a shape placed at ShapePlacement{40*20, 40*20, 0, 0} occupies
// STAGE pixels (0,0)-(40,40) directly (mc's own placement adds no offset).

TEST_CASE(MovieClipInstance_HitTestPoint_PointInsideShape_ReturnsHit) {
    auto bytes = buildMovieWithNamedChildContainingShapes({{40 * 20, 40 * 20, 0, 0}});
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto result = root->hitTestPoint(20.0, 20.0);  // dead center of the 40x40 shape
    CHECK(result.has_value());
    if (result) {
        auto childIt = root->children().find(1);
        CHECK(childIt != root->children().end());
        CHECK(result->clip == childIt->second.get());
        CHECK_EQ(result->characterId, static_cast<uint16_t>(10));  // first shape's id
        CHECK_EQ(result->depth, static_cast<int32_t>(1));
    }
}

TEST_CASE(MovieClipInstance_HitTestPoint_PointOutsideShape_ReturnsNullopt) {
    auto bytes = buildMovieWithNamedChildContainingShapes({{40 * 20, 40 * 20, 0, 0}});
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK(!root->hitTestPoint(90.0, 90.0).has_value());
}

TEST_CASE(MovieClipInstance_HitTestPoint_TopLeftCorner_Inclusive) {
    // rectContainsPoint()'s boundary is closed/inclusive on all 4 edges —
    // confirm that's actually reachable through the full hitTestPoint()
    // pipeline (coordinate conversion + matrix inversion), not just at the
    // rectContainsPoint() unit level (test_swf_records.cpp already covers
    // that in isolation).
    auto bytes = buildMovieWithNamedChildContainingShapes({{40 * 20, 40 * 20, 0, 0}});
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK(root->hitTestPoint(0.0, 0.0).has_value());
    CHECK(root->hitTestPoint(40.0, 40.0).has_value());
}

TEST_CASE(MovieClipInstance_HitTestPoint_OverlappingShapes_TopmostDepthWins) {
    // shapes[0]: (0,0)-(40,40) at depth 1. shapes[1]: (10,10)-(50,50) at
    // depth 2 (placed second -> higher depth -> painted on top -> should
    // win the hit test). Point (30,30) falls inside BOTH.
    auto bytes = buildMovieWithNamedChildContainingShapes({
        {40 * 20, 40 * 20, 0, 0},
        {40 * 20, 40 * 20, 10 * 20, 10 * 20},
    });
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto result = root->hitTestPoint(30.0, 30.0);
    CHECK(result.has_value());
    if (result) {
        CHECK_EQ(result->characterId, static_cast<uint16_t>(11));  // second shape's id
        CHECK_EQ(result->depth, static_cast<int32_t>(2));
    }

    // A point only the FIRST (lower/underneath) shape covers still hits
    // it correctly -- topmost-wins doesn't mean "only the topmost is ever
    // testable."
    auto underOnly = root->hitTestPoint(5.0, 5.0);
    CHECK(underOnly.has_value());
    if (underOnly) CHECK_EQ(underOnly->characterId, static_cast<uint16_t>(10));
}

TEST_CASE(MovieClipInstance_HitTestPoint_InvisibleClip_ReturnsNullopt) {
    // `mc._visible = false;` -- invisible objects never receive input,
    // matching real Flash (and hitTestPoint()'s explicit documented
    // contract, distinct from hitTestBounds()/AS2 hitTest() below).
    Asm a;
    a.pushString("mc");
    a.op(0x1C);  // ActionGetVariable
    a.pushString("_visible");
    a.pushBool(false);
    a.op(0x4F);  // ActionSetMember
    auto bytes = buildMovieWithNamedChildContainingShapes({{40 * 20, 40 * 20, 0, 0}}, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto childIt = root->children().find(1);
    CHECK(childIt != root->children().end());
    CHECK(!childIt->second->visible());  // sanity: the mutation actually took effect

    CHECK(!root->hitTestPoint(20.0, 20.0).has_value());
}

TEST_CASE(MovieClipInstance_HitTestPoint_DegenerateScale_ReturnsNullopt) {
    // `mc._xscale = 0;` -- a zero-size object can't be clicked (matching
    // real Flash); must not crash/divide-by-zero either.
    Asm a;
    a.pushString("mc");
    a.op(0x1C);
    a.pushString("_xscale");
    a.pushInt(0);
    a.op(0x4F);  // ActionSetMember
    auto bytes = buildMovieWithNamedChildContainingShapes({{40 * 20, 40 * 20, 0, 0}}, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto childIt = root->children().find(1);
    CHECK(childIt != root->children().end());
    CHECK_EQ(childIt->second->xScale(), 0.0);  // sanity

    CHECK(!root->hitTestPoint(20.0, 20.0).has_value());
}

TEST_CASE(MovieClipInstance_HitTestPoint_ScaledChild_UsesCurrentNotOriginalTransform) {
    // `mc._xscale = 200; mc._yscale = 200;` -- doubles the child's
    // effective size. A point outside the ORIGINAL 40x40 box but inside
    // the DOUBLED 80x80 box must now hit -- confirms hitTestPoint() reads
    // the clip's CURRENT (possibly script-mutated) matrix_, exactly like
    // rendering does (SceneRenderer uses child.localMatrix(), not the
    // original placement entry.matrix, for the same reason).
    Asm a;
    a.pushString("mc");
    a.op(0x1C);
    a.pushString("_xscale");
    a.pushInt(200);
    a.op(0x4F);
    a.pushString("mc");
    a.op(0x1C);
    a.pushString("_yscale");
    a.pushInt(200);
    a.op(0x4F);
    auto bytes = buildMovieWithNamedChildContainingShapes({{40 * 20, 40 * 20, 0, 0}}, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    // 60px would have missed the original 40x40 box but is inside the
    // doubled 80x80 one.
    CHECK(root->hitTestPoint(60.0, 60.0).has_value());
}

TEST_CASE(MovieClipInstance_HitTestPoint_TwoLevelsNestedMovieClip_ComposesOffsetsAndRecurses) {
    // root -> "outer" (offset 30,30) -> "inner" (offset 5,5) -> shape
    // (offset 2,2, 10x10) — world bounds (37,37)-(47,47). Exercises the
    // MovieClipInstance-child recursion branch TWICE (root->outer,
    // outer->inner), not just the single level every other test here uses.
    auto bytes = buildTwoLevelNestedMovieClipMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto hit = root->hitTestPoint(40.0, 40.0);  // inside (37,37)-(47,47)
    CHECK(hit.has_value());
    if (hit) {
        CHECK_EQ(hit->characterId, static_cast<uint16_t>(30));
        // The clip returned should be "inner" (the immediate owner of the
        // hit shape), not "outer" or root.
        auto outerIt = root->children().find(1);
        CHECK(outerIt != root->children().end());
        auto innerIt = outerIt->second->children().find(1);
        CHECK(innerIt != outerIt->second->children().end());
        CHECK(hit->clip == innerIt->second.get());
    }

    // A point that's inside "outer"'s own local area but outside the
    // doubly-offset shape's actual world bounds must miss.
    CHECK(!root->hitTestPoint(32.0, 32.0).has_value());
}

TEST_CASE(MovieClipInstance_HitTestBounds_AS2HitTest_PointInsideAggregateBounds_ReturnsTrue) {
    // AS2: `capturedHit = mc.hitTest(20, 20);` — real MovieClip.hitTest(x,
    // y), 2-argument form, via CallMethod (the OOP-callable-method
    // dispatch, matching the existing getBytesLoaded()/stop() pattern).
    Asm a;
    a.pushString("capturedHit");
    a.pushInt(20);  // arg1: x
    a.pushInt(20);  // arg2: y
    a.pushInt(2);   // numArgs
    a.pushString("mc");
    a.op(0x1C);  // ActionGetVariable -> resolves "mc"
    a.pushString("hitTest");
    a.op(0x52);  // ActionCallMethod
    a.op(0x1D);  // ActionSetVariable
    auto bytes = buildMovieWithNamedChildContainingShapes({{40 * 20, 40 * 20, 0, 0}}, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK(root->scriptObject()->getOwnProperty("capturedHit").toBoolean());
}

TEST_CASE(MovieClipInstance_HitTestBounds_AS2HitTest_PointOutsideAggregateBounds_ReturnsFalse) {
    Asm a;
    a.pushString("capturedHit");
    a.pushInt(90);
    a.pushInt(90);
    a.pushInt(2);
    a.pushString("mc");
    a.op(0x1C);
    a.pushString("hitTest");
    a.op(0x52);
    a.op(0x1D);
    auto bytes = buildMovieWithNamedChildContainingShapes({{40 * 20, 40 * 20, 0, 0}}, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK(!root->scriptObject()->getOwnProperty("capturedHit").toBoolean());
}

TEST_CASE(MovieClipInstance_HitTestBounds_InvisibleClip_StillReturnsTrue) {
    // The documented, deliberate distinction from hitTestPoint(): real
    // Flash's MovieClip.hitTest() tests GEOMETRY, not rendered visibility.
    Asm a;
    a.pushString("mc");
    a.op(0x1C);
    a.pushString("_visible");
    a.pushBool(false);
    a.op(0x4F);

    a.pushString("capturedHit");
    a.pushInt(20);
    a.pushInt(20);
    a.pushInt(2);
    a.pushString("mc");
    a.op(0x1C);
    a.pushString("hitTest");
    a.op(0x52);
    a.op(0x1D);
    auto bytes = buildMovieWithNamedChildContainingShapes({{40 * 20, 40 * 20, 0, 0}}, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto childIt = root->children().find(1);
    CHECK(childIt != root->children().end());
    CHECK(!childIt->second->visible());  // sanity

    CHECK(root->scriptObject()->getOwnProperty("capturedHit").toBoolean());
}

TEST_CASE(MovieClipInstance_HitTestBounds_OneArgumentTargetForm_NotImplementedReturnsFalse) {
    // `mc.hitTest(someOtherClip)` (1-arg form) is explicitly NOT
    // implemented (see MovieClipInstance.h's hitTestBounds() doc comment)
    // -- must fail safe (false), not crash or misinterpret the argument.
    Asm a;
    a.pushString("capturedHit");
    a.pushString("_root");  // arbitrary single argument
    a.pushInt(1);           // numArgs
    a.pushString("mc");
    a.op(0x1C);
    a.pushString("hitTest");
    a.op(0x52);
    a.op(0x1D);
    auto bytes = buildMovieWithNamedChildContainingShapes({{40 * 20, 40 * 20, 0, 0}}, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK(!root->scriptObject()->getOwnProperty("capturedHit").toBoolean());
}

// ===========================================================================
// Phase 6: Key / Mouse / StartDrag-EndDrag / Sound / ClipActionRecord
// ===========================================================================

TEST_CASE(MovieClipInstance_Key_IsDown_ReadsFromInputState) {
    Asm a;
    // capturedDown = Key.isDown(37 /*LEFT*/)
    a.pushString("capturedDown");
    a.pushInt(37);
    a.pushInt(1);  // numArgs
    a.pushString("Key");
    a.op(0x1C);  // ActionGetVariable
    a.pushString("isDown");
    a.op(0x52);  // ActionCallMethod
    a.op(0x1D);  // ActionSetVariable
    auto bytes = buildRootScriptMovie(a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    env.inputState().setKeyDown(37, true);
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK(root->scriptObject()->getOwnProperty("capturedDown").toBoolean());
}

TEST_CASE(MovieClipInstance_XMouseYMouse_GetProperty_ReadsFromInputState) {
    Asm a;
    a.pushString("capturedX");
    a.pushString("");
    a.pushInt(20);  // _xmouse
    a.op(0x22);     // ActionGetProperty
    a.op(0x1D);
    a.pushString("capturedY");
    a.pushString("");
    a.pushInt(21);  // _ymouse
    a.op(0x22);
    a.op(0x1D);
    auto bytes = buildRootScriptMovie(a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    env.inputState().setMousePosition(55.0, 66.0);
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedX").toNumber(), 55.0);
    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedY").toNumber(), 66.0);
}

TEST_CASE(MovieClipInstance_XMouse_BareMemberAccess_ReadsFromInputState) {
    Asm a;
    a.pushString("capturedX");
    a.pushString("_xmouse");
    a.op(0x1C);  // ActionGetVariable — resolves via handleNativeGet, not GetProperty
    a.op(0x1D);
    auto bytes = buildRootScriptMovie(a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    env.inputState().setMousePosition(12.0, 34.0);
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedX").toNumber(), 12.0);
}

// --- _xmouse/_ymouse coordinate-space conversion (interactivity phase,
// 2026-08-18 fix) -----------------------------------------------------------
//
// The two tests above (MovieClipInstance_XMouseYMouse_GetProperty_
// ReadsFromInputState / MovieClipInstance_XMouse_BareMemberAccess_
// ReadsFromInputState) never call InputState::setViewportSize(), so they
// exercise the "no known viewport -> identity" path — left completely
// unmodified by this fix (still passing, unchanged assertions), which is
// exactly requirement E from docs/known-limitations.md's writeup for this
// fix ("preserve existing behavior for movies whose stage size exactly
// matches the input viewport" — the *default*, no-viewport-set case is
// defined to behave as if they always match). The tests below cover the
// actual scaling math via an EXPLICIT viewport, which nothing before this
// fix ever exercised.
//
// Note on requirement F (viewport offset/letterboxing): SceneRenderer's own
// stage<->device-pixel mapping has no offset/letterboxing/pillarboxing term
// at all (confirmed against SceneRenderer.cpp — a plain, independent-axis
// stretch-to-fill), so stageMouseX()/stageMouseY() intentionally don't add
// one either — a dedicated "offset" test isn't applicable here; the
// top-left/bottom-right corner tests below (C/D) already confirm there's no
// stray offset (0,0 raw must map to exactly (0,0) stage, not (0,0)+something).

TEST_CASE(MovieClipInstance_XMouse_ViewportMatchesStage_CoordinatesUnchanged) {
    // Requirement A2: an EXPLICIT viewport equal to the movie's own stage
    // size must still produce scale == 1.0 (unlike the two tests above,
    // this exercises the actual division/multiplication, not just the
    // "viewport never set" shortcut).
    Asm a;
    a.pushString("capturedX");
    a.pushString("");
    a.pushInt(20);  // _xmouse
    a.op(0x22);     // ActionGetProperty
    a.op(0x1D);
    a.pushString("capturedY");
    a.pushString("");
    a.pushInt(21);  // _ymouse
    a.op(0x22);
    a.op(0x1D);
    auto bytes = buildRootScriptMovie(a.build());  // 100x100px stage

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    env.inputState().setViewportSize(100.0, 100.0);  // matches the 100x100px stage
    env.inputState().setMousePosition(55.0, 66.0);
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedX").toNumber(), 55.0);
    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedY").toNumber(), 66.0);
}

TEST_CASE(MovieClipInstance_XMouse_600x450StageDifferentViewport_ScalesNonUniformly) {
    // Requirements B/E: a 600x450 stage (hobo.swf's real dimensions) with a
    // 400x240 input viewport (the 3DS top screen's logical size, per
    // nintendo3ds_main.cpp) — independent, non-square X/Y scale factors
    // (600/400 = 1.5x, 450/240 = 1.875x), proving X and Y are NOT scaled by
    // a single shared/uniform factor.
    Asm a;
    a.pushString("capturedX");
    a.pushString("");
    a.pushInt(20);  // _xmouse
    a.op(0x22);
    a.op(0x1D);
    a.pushString("capturedY");
    a.pushString("");
    a.pushInt(21);  // _ymouse
    a.op(0x22);
    a.op(0x1D);
    auto bytes = buildRootScriptMovieWithStage(600 * 20, 450 * 20, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    env.inputState().setViewportSize(400.0, 240.0);
    env.inputState().setMousePosition(200.0, 120.0);  // dead center of the 400x240 viewport
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    // 200 * (600/400) = 300; 120 * (450/240) = 225 — dead center of the
    // 600x450 stage too, as expected for a center point under a pure
    // stretch-to-fill (no offset) mapping.
    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedX").toNumber(), 300.0);
    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedY").toNumber(), 225.0);
}

TEST_CASE(MovieClipInstance_XMouse_TopLeftViewportCorner_MapsToStageOrigin) {
    // Requirement C.
    Asm a;
    a.pushString("capturedX");
    a.pushString("");
    a.pushInt(20);
    a.op(0x22);
    a.op(0x1D);
    a.pushString("capturedY");
    a.pushString("");
    a.pushInt(21);
    a.op(0x22);
    a.op(0x1D);
    auto bytes = buildRootScriptMovieWithStage(600 * 20, 450 * 20, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    env.inputState().setViewportSize(400.0, 240.0);
    env.inputState().setMousePosition(0.0, 0.0);
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedX").toNumber(), 0.0);
    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedY").toNumber(), 0.0);
}

TEST_CASE(MovieClipInstance_XMouse_BottomRightViewportCorner_MapsToStageWidthHeight) {
    // Requirement D.
    Asm a;
    a.pushString("capturedX");
    a.pushString("");
    a.pushInt(20);
    a.op(0x22);
    a.op(0x1D);
    a.pushString("capturedY");
    a.pushString("");
    a.pushInt(21);
    a.op(0x22);
    a.op(0x1D);
    auto bytes = buildRootScriptMovieWithStage(600 * 20, 450 * 20, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    env.inputState().setViewportSize(400.0, 240.0);
    env.inputState().setMousePosition(400.0, 240.0);  // bottom-right corner of the viewport
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedX").toNumber(), 600.0);
    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedY").toNumber(), 450.0);
}

TEST_CASE(MovieClipInstance_XMouse_BareMemberAccess_AppliesStageConversionToo) {
    // Confirms the fix applies to BOTH read sites (handleNativeGet's bare
    // "_xmouse" member path — see MovieClipInstance_XMouse_
    // BareMemberAccess_ReadsFromInputState's ActionGetVariable form above —
    // and GetProperty's numeric-index path, already covered by the tests
    // above), not just one of the two.
    Asm a;
    a.pushString("capturedX");
    a.pushString("_xmouse");
    a.op(0x1C);  // ActionGetVariable — resolves via handleNativeGet, not GetProperty
    a.op(0x1D);
    auto bytes = buildRootScriptMovieWithStage(600 * 20, 450 * 20, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    env.inputState().setViewportSize(400.0, 240.0);
    env.inputState().setMousePosition(200.0, 120.0);
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(root->scriptObject()->getOwnProperty("capturedX").toNumber(), 300.0);
}

TEST_CASE(MovieClipInstance_StartDrag_LockCenter_FollowsMousePosition_UntilEndDrag) {
    // ActionStartDrag pops (in order): Target, LockCenter, Constrain — so
    // push order (bottom to top) is Constrain, LockCenter, Target.
    Asm a;
    a.pushBool(false);  // Constrain
    a.pushBool(true);   // LockCenter
    a.pushString("mc");  // Target
    a.op(0x27);          // ActionStartDrag
    // 3 root frames so a single advanceFrame() call doesn't loop back to
    // frame 1 and re-issue StartDrag (see test comment below).
    auto bytes = buildMovieWithNamedChild(3, 1, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    env.inputState().setMousePosition(80.0, 90.0);
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);
    auto mc = root->children().at(1);

    // StartDrag ran at frame 1 creation, but positioning only happens once
    // per tick via ScriptEnvironment::updateDrag() — not yet applied.
    CHECK_EQ(mc->x(), 0.0);

    root->advanceFrame();  // frame 1 -> 2; updateDrag() applies mouse pos
    CHECK_EQ(mc->x(), 80.0);
    CHECK_EQ(mc->y(), 90.0);

    // End the drag, then move the mouse again — since frame 2's script is
    // empty (StartDrag only ran once, on frame 1), the clip should NOT
    // follow the mouse anymore.
    env.endDrag();
    env.inputState().setMousePosition(200.0, 200.0);
    root->advanceFrame();  // frame 2 -> 3
    CHECK_EQ(mc->x(), 80.0);
    CHECK_EQ(mc->y(), 90.0);
}

TEST_CASE(MovieClipInstance_StartDrag_Constrain_ClampsToRectangle) {
    // StartDrag("mc", lockCenter=true, constrain=true, L=10, T=20, R=200, B=250)
    Asm a;
    a.pushInt(250);  // bottom
    a.pushInt(200);  // right
    a.pushInt(20);   // top
    a.pushInt(10);   // left
    a.pushBool(true);   // Constrain
    a.pushBool(true);   // LockCenter
    a.pushString("mc");
    a.op(0x27);  // ActionStartDrag
    auto bytes = buildMovieWithNamedChild(3, 1, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    env.inputState().setMousePosition(500.0, 500.0);
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);
    auto mc = root->children().at(1);

    root->advanceFrame();
    CHECK_EQ(mc->x(), 200.0);  // clamped to the constraint rectangle's right edge
    CHECK_EQ(mc->y(), 250.0);  // clamped to the bottom edge
}

TEST_CASE(MovieClipInstance_ClipActions_LoadOnCreation_EnterFramePerTick_UnloadOnRemove) {
    using flash3ds::swf::ClipEventFlag;

    Asm loadA;
    loadA.pushString("loaded");
    loadA.pushBool(true);
    loadA.op(0x1D);
    loadA.pushString("enterFrameCount");
    loadA.pushInt(0);
    loadA.op(0x1D);

    Asm enterFrameA;
    enterFrameA.pushString("enterFrameCount");
    enterFrameA.pushString("enterFrameCount");
    enterFrameA.op(0x1C);  // ActionGetVariable
    enterFrameA.pushInt(1);
    enterFrameA.op(0x47);  // ActionAdd2
    enterFrameA.op(0x1D);

    Asm unloadA;
    unloadA.pushString("unloaded");
    unloadA.pushBool(true);
    unloadA.op(0x1D);

    std::vector<swf_fixtures::ClipActionFixture> clipActions = {
        {static_cast<uint32_t>(ClipEventFlag::kLoad), std::nullopt, loadA.build()},
        {static_cast<uint32_t>(ClipEventFlag::kEnterFrame), std::nullopt, enterFrameA.build()},
        {static_cast<uint32_t>(ClipEventFlag::kUnload), std::nullopt, unloadA.build()},
    };

    std::vector<swf_fixtures::FixtureTag> nestedTags = {{1 /* ShowFrame */, {}}};
    auto spriteBody = swf_fixtures::buildDefineSpriteBytes(/*characterId=*/20, 1, nestedTags);
    auto placeBytes = swf_fixtures::buildPlaceObject2WithClipActionsBytes(
        /*depth=*/1, /*characterId=*/20, std::nullopt, std::string("mc"), clipActions);

    std::vector<swf_fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineSprite), spriteBody},
        {26 /* PlaceObject2 */, placeBytes},
        {1 /* ShowFrame */, {}},
        {1 /* ShowFrame */, {}},
    };
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 2, tags);
    auto bytes = swf_fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto mc = root->children().at(1);
    CHECK(mc->scriptObject()->getOwnProperty("loaded").toBoolean());
    CHECK_EQ(mc->scriptObject()->getOwnProperty("enterFrameCount").toNumber(), 0.0);

    root->advanceFrame();  // ticks mc's own advanceFrame() too -> EnterFrame fires once
    CHECK_EQ(mc->scriptObject()->getOwnProperty("enterFrameCount").toNumber(), 1.0);

    CHECK(!mc->scriptObject()->getOwnProperty("unloaded").toBoolean());
    mc->removeFromParent();
    CHECK(mc->scriptObject()->getOwnProperty("unloaded").toBoolean());
    CHECK_EQ(root->children().size(), static_cast<size_t>(0));
}

TEST_CASE(MovieClipInstance_StartSoundTag_DispatchesToAudioBackend) {
    auto soundBody =
        swf_fixtures::buildDefineSoundBytes(/*soundId=*/8, 0, 0, false, false, /*sampleCount=*/100);
    auto soundInfo =
        swf_fixtures::buildSoundInfoBytes(false, false, std::nullopt, std::nullopt, uint16_t{4});
    auto startSoundBody = swf_fixtures::buildStartSoundBytes(8, soundInfo);

    std::vector<swf_fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineSound), soundBody},
        {static_cast<uint16_t>(TagCode::StartSound), startSoundBody},
        {1 /* ShowFrame */, {}},
    };
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    auto bytes = swf_fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    SpyAudioBackend spy;
    env.setAudioBackend(&spy);
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(spy.playCalls.size(), static_cast<size_t>(1));
    CHECK_EQ(spy.playCalls[0].soundId, static_cast<uint16_t>(8));
    CHECK_EQ(spy.playCalls[0].loopCount, 4);
}

TEST_CASE(MovieClipInstance_StartSoundTag_SyncStop_DispatchesStop) {
    auto soundBody =
        swf_fixtures::buildDefineSoundBytes(/*soundId=*/9, 0, 0, false, false, /*sampleCount=*/100);
    auto soundInfo = swf_fixtures::buildSoundInfoBytes(/*syncStop=*/true, false, std::nullopt,
                                                        std::nullopt, std::nullopt);
    auto startSoundBody = swf_fixtures::buildStartSoundBytes(9, soundInfo);

    std::vector<swf_fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineSound), soundBody},
        {static_cast<uint16_t>(TagCode::StartSound), startSoundBody},
        {1 /* ShowFrame */, {}},
    };
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    auto bytes = swf_fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    SpyAudioBackend spy;
    env.setAudioBackend(&spy);
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK(spy.playCalls.empty());
    CHECK_EQ(spy.stopCalls.size(), static_cast<size_t>(1));
    CHECK_EQ(spy.stopCalls[0], static_cast<uint16_t>(9));
}

TEST_CASE(MovieClipInstance_AVM1SoundObject_AttachNumericIdAndStart_DispatchesToAudioBackend) {
    Asm a;
    // var s = new Sound();
    a.pushString("s");
    a.pushInt(0);
    a.pushString("Sound");
    a.op(0x40);  // ActionNewObject
    a.op(0x1D);
    // s.attachSound(5);
    a.pushInt(5);
    a.pushInt(1);
    a.pushString("s");
    a.op(0x1C);
    a.pushString("attachSound");
    a.op(0x52);  // ActionCallMethod
    a.op(0x17);  // ActionPop — discard the (unused) return value
    // s.start(0, 2);
    a.pushInt(0);
    a.pushInt(2);
    a.pushInt(2);
    a.pushString("s");
    a.op(0x1C);
    a.pushString("start");
    a.op(0x52);
    a.op(0x17);

    auto soundBody =
        swf_fixtures::buildDefineSoundBytes(/*soundId=*/5, 0, 0, false, false, /*sampleCount=*/100);
    std::vector<swf_fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineSound), soundBody},
        {static_cast<uint16_t>(TagCode::DoAction), a.build()},
        {1 /* ShowFrame */, {}},
    };
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    auto bytes = swf_fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    SpyAudioBackend spy;
    env.setAudioBackend(&spy);
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(spy.playCalls.size(), static_cast<size_t>(1));
    CHECK_EQ(spy.playCalls[0].soundId, static_cast<uint16_t>(5));
    CHECK_EQ(spy.playCalls[0].loopCount, 2);
}

// --- Phase 7: ExternalInterface -----------------------------------------

TEST_CASE(MovieClipInstance_ExternalInterface_Available_IsTrue) {
    Asm a;
    a.pushString("avail");
    a.pushString("ExternalInterface");
    a.op(0x1C);  // GetVariable
    a.pushString("available");
    a.op(0x4E);  // GetMember
    a.op(0x1D);  // SetVariable
    auto bytes = buildRootScriptMovie(a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK(root->scriptObject()->getOwnProperty("avail").toBoolean());
}

TEST_CASE(MovieClipInstance_ExternalInterface_Call_DispatchesToRegisteredHostFunction) {
    // result = ExternalInterface.call("nativeGreet", "noe", 3);
    Asm a;
    a.pushString("result");
    a.pushString("nativeGreet");  // arg0 — the callee name (call()'s own first arg)
    a.pushString("noe");          // arg1
    a.pushInt(3);                 // arg2
    a.pushInt(3);                 // numArgs
    a.pushString("ExternalInterface");
    a.op(0x1C);  // GetVariable
    a.pushString("call");
    a.op(0x52);  // CallMethod
    a.op(0x1D);  // SetVariable
    auto bytes = buildRootScriptMovie(a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    std::vector<Value> received;
    env.registerHostFunction("nativeGreet", [&received](const std::vector<Value>& args) {
        received = args;
        return Value::string("ok");
    });

    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(received.size(), static_cast<size_t>(2));
    CHECK_EQ(received[0].toString(), "noe");
    CHECK_EQ(received[1].toNumber(), 3.0);
    CHECK_EQ(root->scriptObject()->getOwnProperty("result").toString(), "ok");
}

TEST_CASE(MovieClipInstance_ExternalInterface_Call_UnregisteredName_ReturnsUndefined) {
    Asm a;
    a.pushString("result");
    a.pushString("neverRegistered");
    a.pushInt(1);  // numArgs
    a.pushString("ExternalInterface");
    a.op(0x1C);
    a.pushString("call");
    a.op(0x52);
    a.op(0x1D);
    auto bytes = buildRootScriptMovie(a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK(root->scriptObject()->getOwnProperty("result").isUndefined());
}

TEST_CASE(MovieClipInstance_ExternalInterface_AddCallback_NativeInvokeCallback_RunsAs2Function) {
    // function(n) { return n * 2; }
    Asm fnBody;
    fnBody.pushString("n");
    fnBody.op(0x1C);  // GetVariable — resolves the named (non-register) param
    fnBody.pushInt(2);
    fnBody.op(0x0C);  // Multiply
    fnBody.op(0x3E);  // Return

    // ExternalInterface.addCallback("double", "thisMarker", function(n){...});
    Asm a;
    a.pushString("double");                          // arg0: methodName
    a.pushString("thisMarker");                       // arg1: instance
    a.defineFunctionV1("", {"n"}, fnBody.build());     // arg2: function (anonymous — pushed)
    a.pushInt(3);                                      // numArgs
    a.pushString("ExternalInterface");
    a.op(0x1C);  // GetVariable
    a.pushString("addCallback");
    a.op(0x52);  // CallMethod
    a.op(0x17);  // Pop — discard the boolean return value
    auto bytes = buildRootScriptMovie(a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK(env.hasCallback("double"));
    CHECK(!env.hasCallback("notRegistered"));

    Value result = env.invokeCallback("double", {Value::number(21)});
    CHECK_EQ(result.toNumber(), 42.0);
}

TEST_CASE(MovieClipInstance_ExternalInterface_InvokeCallback_UnregisteredName_ReturnsUndefined) {
    ScriptEnvironment env;
    Value result = env.invokeCallback("nope", {});
    CHECK(result.isUndefined());
}

// --- Phase 9: OOP-callable MovieClip methods (found missing via real
// hobo.swf content — see handleNativeGet's doc comment in
// MovieClipInstance.cpp) ------------------------------------------------

TEST_CASE(MovieClipInstance_CallMethod_Stop_HaltsOwnTimeline) {
    // `_root.stop();` via CallMethod bytecode — the OOP form, as opposed to
    // MovieClipInstance_Stop_HaltsOwnTimelineOnly's bare `stop();` action
    // code.
    Asm a;
    a.pushInt(0);  // numArgs
    a.pushString("_root");
    a.op(0x1C);  // ActionGetVariable
    a.pushString("stop");
    a.op(0x52);  // ActionCallMethod
    auto bytes = buildRootScriptMovie(a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK(!root->timeline().isPlaying());
}

TEST_CASE(MovieClipInstance_CallMethod_GotoAndStopNumericFrame_MovesPlayheadAndStops) {
    // `_root.gotoAndStop(3);` — AS2's 1-based numeric frame form.
    Asm a;
    a.pushInt(3);  // arg: frame 3
    a.pushInt(1);  // numArgs
    a.pushString("_root");
    a.op(0x1C);  // ActionGetVariable
    a.pushString("gotoAndStop");
    a.op(0x52);  // ActionCallMethod
    auto bytes = buildMultiFrameRootScriptMovie(3, a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(root->timeline().currentFrame(), static_cast<uint32_t>(3));
    CHECK(!root->timeline().isPlaying());
}

TEST_CASE(MovieClipInstance_CallMethod_GotoAndPlayLabel_MovesPlayheadAndKeepsPlaying) {
    // `_root.gotoAndPlay("end");` — AS2's frame-label form.
    Asm a;
    a.pushString("end");  // arg: label
    a.pushInt(1);          // numArgs
    a.pushString("_root");
    a.op(0x1C);  // ActionGetVariable
    a.pushString("gotoAndPlay");
    a.op(0x52);  // ActionCallMethod
    auto bytes = buildMultiFrameRootScriptMovie(3, a.build(), {"", "", "end"});

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    CHECK_EQ(root->timeline().currentFrame(), static_cast<uint32_t>(3));
    CHECK(root->timeline().isPlaying());
}

TEST_CASE(MovieClipInstance_CallMethod_GetBytesLoadedAndTotal_ReportFullyLoadedFileSize) {
    // `loaded = _root.getBytesLoaded(); total = _root.getBytesTotal();` —
    // this runtime never streams, so both must equal the whole file's
    // declared length from the very first frame.
    Asm a;
    a.pushString("loaded");
    a.pushInt(0);
    a.pushString("_root");
    a.op(0x1C);
    a.pushString("getBytesLoaded");
    a.op(0x52);
    a.op(0x1D);  // ActionSetVariable

    a.pushString("total");
    a.pushInt(0);
    a.pushString("_root");
    a.op(0x1C);
    a.pushString("getBytesTotal");
    a.op(0x52);
    a.op(0x1D);
    auto bytes = buildRootScriptMovie(a.build());

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    double loaded = root->scriptObject()->getOwnProperty("loaded").toNumber();
    double total = root->scriptObject()->getOwnProperty("total").toNumber();
    CHECK(loaded > 0.0);
    CHECK_EQ(loaded, total);
    CHECK_EQ(loaded, static_cast<double>(movie->declaredFileLength));
}
