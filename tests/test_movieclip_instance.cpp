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
#include "avm1/Value.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/MovieClipInstance.h"
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
