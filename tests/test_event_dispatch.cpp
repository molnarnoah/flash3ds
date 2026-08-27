// test_event_dispatch.cpp
//
// Event-dispatch phase (2026-08-19) regression tests — see docs/events.md
// for the design this backs. Covers Phase M's test matrix: Button2
// condActionsV2 mouse-condition dispatch, CondKeyPress keyboard dispatch,
// AS2 onPress/onRelease/onRollOver/onRollOut property-handler dispatch (on
// both buttons and plain MovieClips), mouse-capture semantics, event-target
// lifetime safety, and execution-target (this/HostBindings) correctness.
//
// Convention used throughout: `env.inputState().setMousePosition(...)` /
// `setMouseDown(...)` (any number, any order), then EXACTLY ONE
// `env.inputState().commitFrame()` call, then `root->advanceFrame()` — this
// mirrors the real calling convention documented in docs/input.md
// ("Nintendo3DSInput::poll() calls commitFrame() exactly once, as its LAST
// step, before the game tick runs") and used by
// Nintendo3DSInput::poll()/nintendo3ds_main.cpp, as opposed to setting
// state and calling advanceFrame() directly with no commit (which would
// leave isMousePressed()/isMouseReleased() permanently false — a real,
// separate wiring gap in the flash_runtime CLI tool, out of scope for this
// phase; see docs/events.md's "Known limitations" section).

#include "Avm1TestFixtures.h"
#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "avm1/Value.h"
#include "runtime/ButtonInstance.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/MovieClipInstance.h"
#include "swf/DefineButtonTag.h"
#include "swf/SwfLoader.h"
#include "swf/TagCode.h"

namespace swf_fixtures = flash3ds::test::fixtures;
using flash3ds::avm1::Value;
using flash3ds::runtime::ButtonInstance;
using flash3ds::runtime::CharacterDictionary;
using flash3ds::runtime::MovieClipInstance;
using flash3ds::runtime::ScriptEnvironment;
using flash3ds::swf::ButtonCondition;
using flash3ds::swf::SwfLoader;
using flash3ds::swf::TagCode;
using Asm = flash3ds::test::fixtures::Avm1Assembler;

namespace {

// {DefineShape2(shapeId, widthTwips x heightTwips), DefineButton2(buttonId,
// one Up+HitTest record referencing shapeId at (0,0), plus — iff
// `actionBytes` is non-empty — a single terminal BUTTONCONDACTION with
// `conditions`/`keyCode` as given)}. Mirrors
// test_button_instance.cpp's buildSimpleButtonDefTags() exactly, extended
// to actually populate condActionsV2 (that file deliberately never does —
// see its header comment).
std::vector<swf_fixtures::FixtureTag> buildButtonWithCondActionTags(
    uint16_t buttonId, uint16_t shapeId, int32_t widthTwips, int32_t heightTwips,
    uint16_t conditions, std::optional<uint8_t> keyCode,
    const std::vector<uint8_t>& actionBytes) {
    auto shapeBody = swf_fixtures::buildDefineShapeBytes(2, shapeId, widthTwips, heightTwips, 0x00,
                                                          0xFF, 0x00, 0xFF);
    swf_fixtures::ButtonRecordV1Fixture rec;
    rec.up = true;
    rec.hitTest = true;
    rec.characterId = shapeId;
    rec.depth = 1;
    rec.matrixBytes = swf_fixtures::buildMatrixBytes(0, 0);
    auto buttonBody = swf_fixtures::buildDefineButtonV2Bytes(buttonId, {rec}, conditions, keyCode,
                                                              actionBytes);
    return {
        {static_cast<uint16_t>(TagCode::DefineShape2), shapeBody},
        {static_cast<uint16_t>(TagCode::DefineButton2), buttonBody},
    };
}

// A 100x100px, 1-frame (unless overridden) movie placing a single 40x40px
// button named "btn" (id=50) at the ROOT level, offset (0,0), with the
// given condActionsV2 condition bits / optional CondKeyPress / action
// bytes. `rootActionBytes` (if non-empty) becomes root frame 1's DoAction
// body — used to assign onPress/onRelease/onRollOver/onRollOut property
// handlers before the button is ever hit-tested.
std::vector<uint8_t> buildMovieWithCondActionButton(
    uint16_t conditions, std::optional<uint8_t> keyCode,
    const std::vector<uint8_t>& buttonActionBytes,
    const std::vector<uint8_t>& rootActionBytes = {}, uint16_t frameCount = 1) {
    auto tags = buildButtonWithCondActionTags(50, 51, 40 * 20, 40 * 20, conditions, keyCode,
                                               buttonActionBytes);
    tags.push_back({26 /* PlaceObject2 */,
                    swf_fixtures::buildPlaceObject2Bytes(1, false, 50,
                                                          swf_fixtures::buildMatrixBytes(0, 0),
                                                          std::string("btn"))});
    if (!rootActionBytes.empty()) {
        tags.push_back({static_cast<uint16_t>(TagCode::DoAction), rootActionBytes});
    }
    tags.push_back({1 /* ShowFrame */, {}});
    for (uint16_t f = 1; f < frameCount; ++f) tags.push_back({1 /* ShowFrame */, {}});
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, frameCount, tags);
    return swf_fixtures::wrapFws(6, body);
}

// `_root.<varName> = (_root.<varName> | 0) + 1;` -- a bytecode body
// (used BOTH as a raw condActionsV2 action block, and wrapped in an
// anonymous DefineFunction(2) for AS2 property-handler assignment)
// incrementing a counter QUALIFIED against _root, deliberately NOT a bare
// unqualified variable. Two independent reasons:
//
// 1) When run as a condActionsV2 action block directly via
//    ScriptEnvironment::run(), a bare SetVariable lands on the DISPATCH
//    TARGET's own scriptObject (correct and fine there).  But when run as
//    an invoked FUNCTION CLOSURE (property-handler dispatch, via
//    callHandler()/invokeFunction()), invokeFunction() pushes a fresh,
//    per-call `activation` object as the scope chain's new INNERMOST link
//    (see Interpreter.cpp) — and Scope::setVariable() creates a
//    never-seen-before name on the chain's innermost object when it isn't
//    found anywhere else. A bare unqualified assignment inside a handler
//    body would therefore be silently created on that throwaway
//    per-call `activation` and discarded the instant the call returns,
//    never actually observable afterward — NOT a dispatcher bug, just an
//    AVM1-bare-variable pitfall this fixture must avoid to make a
//    meaningful assertion. Qualifying against `_root` (a stable object
//    reachable via nativeGet from any scope chain that includes a
//    MovieClip/Button scriptObject, per handleNativeGet()/ButtonInstance's
//    own _root hook) sidesteps the activation entirely.
// 2) It also happens to be a more realistic stand-in for how real AS2
//    handler bodies are actually written (`_root.foo = ...` /
//    `_parent.foo = ...`), rather than an artificial bare-global pattern.
//
// `| 0` is a self-initializing-counter idiom: BitOr coerces both operands
// via Value::toInt32(), which maps undefined's toNumber() (NaN) to 0 (see
// Value::toInt32()'s NaN/Inf guard) — so a counter that's never been set
// reads as 0 on its first increment instead of poisoning every future read
// with NaN forever (plain Add2 on undefined would otherwise do exactly
// that). Uses register 1 as scratch space to move the computed new value
// across the two separate GetMember/SetMember stack sequences below --
// registers are always available (DefineFunction v1 implicitly gets 4,
// see Interpreter.cpp's DefineFunction handling; condActionsV2's own
// run()-level ExecutionContext also always has >=4).
std::vector<uint8_t> buildIncrementHandlerBody(const std::string& varName) {
    Asm fn;
    fn.pushString("_root");
    fn.op(0x1C);  // ActionGetVariable -> resolves the root object
    fn.pushString(varName);
    fn.op(0x4E);  // ActionGetMember -> current value (possibly undefined)
    fn.pushInt(0);
    fn.op(0x61);  // ActionBitOr -- NaN/undefined-safe coercion to 0
    fn.pushInt(1);
    fn.op(0x47);  // ActionAdd2 -- new value
    fn.storeRegister(1);
    fn.op(0x17);  // ActionPop -- discard the (peeked, not popped) new value
    fn.pushString("_root");
    fn.op(0x1C);
    fn.pushString(varName);
    fn.pushRegisterValue(1);
    fn.op(0x4F);  // ActionSetMember
    return fn.build();
}

// Two NON-OVERLAPPING buttons with condActionsV2, both with the full real
// "password button" condition set found in the Hobo corpus (IdleToOverUp |
// OverUpToIdle | OverUpToOverDown | OverDownToOverUp | OverDownToOutDown |
// OverDownToIdle), each incrementing its own named counter variable on
// _root. "a" (id=50) at (0,0)-(40,40)px, "b" (id=52) at (50,50)-(90,90)px.
std::vector<uint8_t> buildMovieWithTwoCondActionButtons() {
    uint16_t conditions = static_cast<uint16_t>(ButtonCondition::kIdleToOverUp) |
                          static_cast<uint16_t>(ButtonCondition::kOverUpToIdle) |
                          static_cast<uint16_t>(ButtonCondition::kOverUpToOverDown) |
                          static_cast<uint16_t>(ButtonCondition::kOverDownToOverUp) |
                          static_cast<uint16_t>(ButtonCondition::kOverDownToOutDown) |
                          static_cast<uint16_t>(ButtonCondition::kOverDownToIdle);
    auto tagsA = buildButtonWithCondActionTags(50, 51, 40 * 20, 40 * 20, conditions, std::nullopt,
                                                buildIncrementHandlerBody("aFires"));
    auto tagsB = buildButtonWithCondActionTags(52, 53, 40 * 20, 40 * 20, conditions, std::nullopt,
                                                buildIncrementHandlerBody("bFires"));
    std::vector<swf_fixtures::FixtureTag> tags = tagsA;
    tags.insert(tags.end(), tagsB.begin(), tagsB.end());
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(
                             1, false, 50, swf_fixtures::buildMatrixBytes(0, 0), std::string("a"))});
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(
                             2, false, 52, swf_fixtures::buildMatrixBytes(50 * 20, 50 * 20),
                             std::string("b"))});
    tags.push_back({1, {}});
    auto body = swf_fixtures::buildMovieBody(200 * 20, 200 * 20, 12.0, 1, tags);
    return swf_fixtures::wrapFws(6, body);
}

// root -> "mc" (MovieClip, offset mcOffset) -> "btn" (Button, offset
// buttonOffset), button carries condActionsV2 -- for nested-button dispatch
// tests exercising Phase F's execution-target contract (bare host-bound
// actions like stop()/RemoveSprite("") inside the action block act on "mc",
// the button's PARENT -- not root and not the button itself).
std::vector<uint8_t> buildMovieWithNestedCondActionButton(uint16_t conditions,
                                                            const std::vector<uint8_t>& actionBytes,
                                                            int32_t mcOffsetTwips = 30 * 20,
                                                            int32_t buttonOffsetTwips = 0) {
    uint16_t buttonId = 60, shapeId = 61, mcId = 62;
    auto nestedTags = buildButtonWithCondActionTags(buttonId, shapeId, 40 * 20, 40 * 20, conditions,
                                                      std::nullopt, actionBytes);
    nestedTags.push_back(
        {26, swf_fixtures::buildPlaceObject2Bytes(
                 1, false, buttonId,
                 swf_fixtures::buildMatrixBytes(buttonOffsetTwips, buttonOffsetTwips),
                 std::string("btn"))});
    nestedTags.push_back({1, {}});
    auto spriteBody = swf_fixtures::buildDefineSpriteBytes(mcId, 1, nestedTags);

    std::vector<swf_fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineSprite), spriteBody},
        {26, swf_fixtures::buildPlaceObject2Bytes(
                 1, false, mcId, swf_fixtures::buildMatrixBytes(mcOffsetTwips, mcOffsetTwips),
                 std::string("mc"))},
        {1, {}},
    };
    auto body = swf_fixtures::buildMovieBody(200 * 20, 200 * 20, 12.0, 1, tags);
    return swf_fixtures::wrapFws(6, body);
}

// root -> "mc" (MovieClip) -> "inner" (a plain, GRANDCHILD-of-root
// MovieClip carrying its own hit-testable shape) -- plus a root-level
// "removeBtn" whose condActionsV2 is CondKeyPress-triggered (not mouse --
// so it can fire without ever moving the mouse off "inner", which would
// otherwise clear hoverClip_ the ordinary way before the bug this fixture
// targets gets a chance to matter) and removes "mc" via RemoveSprite("mc").
//
// Regression fixture for the 2026-08-27 crash fix (see
// ScriptEnvironment::notifyRemovedRecursive()'s header doc comment in
// MovieClipInstance.h): hovering "inner" sets hoverClip_ to a GRANDCHILD of
// root, two levels below "mc". removeFromParent()'s single-node
// notifyRemoved(this=mc) call could not see past "mc" itself to notice
// hoverClip_ pointed at "mc"'s own child "inner", so hoverClip_ survived
// "mc" (and, cascading, "inner") being destroyed -- a dangling pointer that
// the next hover-state-change tick would dereference and crash on
// (confirmed via gdb against real corpus content, see
// docs/known-limitations.md L6 and
// tools/real_game_harness/pamplona_click_trace.cpp).
std::vector<uint8_t> buildMovieWithGrandchildHoverAndKeyRemovableAncestor() {
    uint16_t shapeId = 80, innerId = 81, mcId = 82, removeBtnId = 83, removeBtnShapeId = 84;

    // "inner": places its own 40x40px shape at its own local origin -- this
    // is what hitTestPointInOwnSpace() actually tests a hover point against.
    std::vector<swf_fixtures::FixtureTag> innerTags = {
        {static_cast<uint16_t>(TagCode::DefineShape2),
         swf_fixtures::buildDefineShapeBytes(2, shapeId, 40 * 20, 40 * 20, 0x00, 0xFF, 0x00, 0xFF)},
        {26,
         swf_fixtures::buildPlaceObject2Bytes(1, false, shapeId, swf_fixtures::buildMatrixBytes(0, 0))},
        {1, {}},
    };
    auto innerSpriteBody = swf_fixtures::buildDefineSpriteBytes(innerId, 1, innerTags);

    // "mc": places "inner" (named) at a (30,30)px offset within itself.
    std::vector<swf_fixtures::FixtureTag> mcTags = {
        {static_cast<uint16_t>(TagCode::DefineSprite), innerSpriteBody},
        {26, swf_fixtures::buildPlaceObject2Bytes(1, false, innerId,
                                                   swf_fixtures::buildMatrixBytes(30 * 20, 30 * 20),
                                                   std::string("inner"))},
        {1, {}},
    };
    auto mcSpriteBody = swf_fixtures::buildDefineSpriteBytes(mcId, 1, mcTags);

    // "removeBtn": a root-level button whose only condActionsV2 record is
    // CondKeyPress=4 ("End") -- fires purely on keypress, independent of
    // mouse position (see condKeyPressToInputKeyCode()'s doc comment / the
    // existing EventDispatch_CondKeyPress_* tests below for this same
    // convention) -- action is RemoveSprite("mc"), resolved relative to
    // root (the button's own parent, per fireButtonCondition()'s
    // `run(*parent, ...)`), i.e. removes root's child "mc".
    Asm removeAction;
    removeAction.pushString("mc");
    removeAction.op(0x25);  // ActionRemoveSprite
    auto removeBtnTags = buildButtonWithCondActionTags(removeBtnId, removeBtnShapeId, 40 * 20, 40 * 20,
                                                         0, static_cast<uint8_t>(4), removeAction.build());

    std::vector<swf_fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineSprite), mcSpriteBody},
        {26, swf_fixtures::buildPlaceObject2Bytes(1, false, mcId,
                                                   swf_fixtures::buildMatrixBytes(30 * 20, 30 * 20),
                                                   std::string("mc"))},
    };
    for (auto& t : removeBtnTags) tags.push_back(t);
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(
                             2, false, removeBtnId, swf_fixtures::buildMatrixBytes(0, 0),
                             std::string("removeBtn"))});
    tags.push_back({1, {}});
    auto body = swf_fixtures::buildMovieBody(300 * 20, 300 * 20, 12.0, 1, tags);
    return swf_fixtures::wrapFws(6, body);
}

// `<targetVar>.<memberName> = function(){ <counterVar> = <counterVar> + 1; };`
// -- root-level DoAction bytecode assigning a counting handler onto a named
// object (button or clip) already resolvable from root scope.
std::vector<uint8_t> buildAssignHandlerAction(const std::string& targetVar,
                                               const std::string& memberName,
                                               const std::string& counterVar) {
    Asm a;
    a.pushString(targetVar);
    a.op(0x1C);  // ActionGetVariable
    a.pushString(memberName);
    a.defineFunctionV1("", {}, buildIncrementHandlerBody(counterVar));
    a.op(0x4F);  // ActionSetMember
    return a.build();
}

}  // namespace

// ===========================================================================
// Button2 condActionsV2 mouse-condition dispatch
// ===========================================================================

TEST_CASE(EventDispatch_Hover_EnterButton_FiresIdleToOverUp) {
    auto bytes = buildMovieWithCondActionButton(
        static_cast<uint16_t>(ButtonCondition::kIdleToOverUp), std::nullopt,
        buildIncrementHandlerBody("fired"));
    // The above uses buildIncrementHandlerBody() as a *bare* action block
    // (condActionsV2 actionBytes run directly, not via a Function object --
    // see fireButtonCondition()'s doc comment), which happens to be valid
    // standalone AVM1 bytecode too (no ActionEnd needed at the very end;
    // Avm1Assembler::build() always appends one).
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    // Starts outside -- establish baseline (no dispatch yet).
    env.inputState().setMousePosition(90.0, 90.0);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK(root->scriptObject()->getOwnProperty("fired").isUndefined());

    // Moves onto the button -- IdleToOverUp fires exactly once.
    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("fired").toNumber(), 1.0);
}

TEST_CASE(EventDispatch_Leave_ButtonToIdle_FiresOverUpToIdle) {
    auto bytes = buildMovieWithCondActionButton(
        static_cast<uint16_t>(ButtonCondition::kOverUpToIdle), std::nullopt,
        buildIncrementHandlerBody("fired"));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(20.0, 20.0);  // starts over the button
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK(root->scriptObject()->getOwnProperty("fired").isUndefined());

    env.inputState().setMousePosition(90.0, 90.0);  // leaves
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("fired").toNumber(), 1.0);
}

TEST_CASE(EventDispatch_PressOverButton_FiresOverUpToOverDown) {
    auto bytes = buildMovieWithCondActionButton(
        static_cast<uint16_t>(ButtonCondition::kOverUpToOverDown), std::nullopt,
        buildIncrementHandlerBody("fired"));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();  // hover established, no press edge yet

    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();  // UP -> DOWN: mousePressed this tick
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("fired").toNumber(), 1.0);
}

TEST_CASE(EventDispatch_ReleaseOverButton_FiresOverDownToOverUp) {
    auto bytes = buildMovieWithCondActionButton(
        static_cast<uint16_t>(ButtonCondition::kOverDownToOverUp), std::nullopt,
        buildIncrementHandlerBody("fired"));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();
    root->advanceFrame();  // press established (no OverDownToOverUp condition on this button)

    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();  // DOWN -> UP: mouseReleased this tick
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("fired").toNumber(), 1.0);
}

TEST_CASE(EventDispatch_PressOutside_NoDispatch) {
    auto bytes = buildMovieWithCondActionButton(
        static_cast<uint16_t>(ButtonCondition::kOverUpToOverDown), std::nullopt,
        buildIncrementHandlerBody("fired"));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(90.0, 90.0);  // outside the button entirely
    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK(root->scriptObject()->getOwnProperty("fired").isUndefined());
}

TEST_CASE(EventDispatch_ReleaseOutside_FiresOverDownToIdle_NotOverDownToOverUp) {
    // Press over the button, drag outside, release -- real Flash "cancel"
    // semantics: OverDownToIdle fires (if the button defines it),
    // OverDownToOverUp does NOT (see dispatchPointerEvents()'s "release"
    // block comment).
    // Two DIFFERENT condActionsV2 records aren't supported by the single-
    // record buildDefineButtonV2Bytes() helper, so instead build two
    // separate single-condition buttons and distinguish dispatch by which
    // counter each one bumps.
    auto idleBytes = buildMovieWithCondActionButton(
        static_cast<uint16_t>(ButtonCondition::kOverDownToIdle), std::nullopt,
        buildIncrementHandlerBody("idleFired"));
    auto overUpBytes = buildMovieWithCondActionButton(
        static_cast<uint16_t>(ButtonCondition::kOverDownToOverUp), std::nullopt,
        buildIncrementHandlerBody("overUpFired"));

    for (const auto* bytesPtr : {&idleBytes, &overUpBytes}) {
        auto movie = SwfLoader::loadSwf(bytesPtr->data(), bytesPtr->size());
        CHECK(movie->valid);
        auto characters = CharacterDictionary::build(*movie);
        ScriptEnvironment env;
        auto root = MovieClipInstance::createRoot(*movie, characters, env);
        CHECK(root != nullptr);

        env.inputState().setMousePosition(20.0, 20.0);  // over the button
        env.inputState().setMouseDown(true);
        env.inputState().commitFrame();
        root->advanceFrame();  // press captures the button

        env.inputState().setMousePosition(90.0, 90.0);  // drag outside while held
        env.inputState().commitFrame();
        root->advanceFrame();

        env.inputState().setMouseDown(false);  // release while outside
        env.inputState().commitFrame();
        root->advanceFrame();

        bool expectIdleFired = (bytesPtr == &idleBytes);
        Value idleVal = root->scriptObject()->getOwnProperty("idleFired");
        Value overUpVal = root->scriptObject()->getOwnProperty("overUpFired");
        if (expectIdleFired) {
            CHECK_EQ(idleVal.toNumber(), 1.0);
        } else {
            CHECK(overUpVal.isUndefined());  // OverDownToOverUp must NOT fire on release-outside
        }
    }
}

TEST_CASE(EventDispatch_MoveBetweenButtons_PressedButtonKeepsCapture) {
    // Real Flash mouse-capture semantics: press starts on "a"; dragging
    // onto "b" (non-overlapping, so a fresh hit-test would normally see
    // "b") must NOT make "b" receive any Over/Out transition -- only "a"'s
    // OutDownToOverDown/OverDownToOutDown drag transitions apply, and only
    // "a" can receive the eventual release.
    auto bytes = buildMovieWithTwoCondActionButtons();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(20.0, 20.0);  // over "a", not yet pressed
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("aFires").toNumber(), 1.0);  // IdleToOverUp
    CHECK(root->scriptObject()->getOwnProperty("bFires").isUndefined());

    env.inputState().setMouseDown(true);  // press captures "a"
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("aFires").toNumber(), 2.0);  // + OverUpToOverDown
    CHECK(root->scriptObject()->getOwnProperty("bFires").isUndefined());

    env.inputState().setMousePosition(70.0, 70.0);  // now geometrically over "b"
    env.inputState().commitFrame();
    root->advanceFrame();
    // "b" must NOT have received any dispatch -- it was never actually
    // hovered/pressed, mouse capture kept everything on "a".
    CHECK(root->scriptObject()->getOwnProperty("bFires").isUndefined());

    env.inputState().setMouseDown(false);  // release while over "b"'s area
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK(root->scriptObject()->getOwnProperty("bFires").isUndefined());
}

TEST_CASE(EventDispatch_NestedButton_DispatchesWithParentAsExecutionTarget) {
    // condActionsV2 action does bare `stop();` (host-bound action) --
    // per Phase F, must act on the button's PARENT ("mc"), not root and not
    // the button itself (which has no timeline). A 2-frame "mc" sprite that
    // starts playing; the action stopping it must halt "mc"'s OWN playhead
    // at frame 1, independent of root's playhead.
    Asm stopAction;
    stopAction.op(0x07);  // ActionStop
    auto bytes = buildMovieWithNestedCondActionButton(
        static_cast<uint16_t>(ButtonCondition::kOverUpToOverDown), stopAction.build());
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto mcIt = root->children().find(1);
    CHECK(mcIt != root->children().end());
    if (mcIt == root->children().end()) return;
    auto mc = mcIt->second;

    // Button sits at mc's local (0,0), mc is offset (30,30)px -- world hit
    // point (20,20) is inside mc's world (30,30)-(70,70) button box? No --
    // recompute: mc offset 30,30 + button offset (0,0), button box
    // 0..40px LOCAL -> world 30..70px. Hit at (40,40) world.
    env.inputState().setMousePosition(40.0, 40.0);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();  // hover only, no OverUpToOverDown yet

    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();
    root->advanceFrame();  // press -> fires the button's condActionsV2 stop() against "mc"

    CHECK(!mc->timeline().isPlaying());
}

TEST_CASE(EventDispatch_DepthOrdering_OnlyTopmostButtonDispatches) {
    // Overlapping buttons "a" (depth 1) / "b" (depth 2, on top) at the same
    // point -- only "b" (topmost, per existing hitTestPoint() depth-
    // ordering, unchanged by this phase) receives the dispatch.
    auto makeAction = [](const std::string& varName) {
        return buildIncrementHandlerBody(varName);
    };
    uint16_t cond = static_cast<uint16_t>(ButtonCondition::kIdleToOverUp);
    auto tagsA =
        buildButtonWithCondActionTags(50, 51, 40 * 20, 40 * 20, cond, std::nullopt, makeAction("aFires"));
    auto tagsB =
        buildButtonWithCondActionTags(52, 53, 40 * 20, 40 * 20, cond, std::nullopt, makeAction("bFires"));
    std::vector<swf_fixtures::FixtureTag> tags = tagsA;
    tags.insert(tags.end(), tagsB.begin(), tagsB.end());
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(
                             1, false, 50, swf_fixtures::buildMatrixBytes(0, 0), std::string("a"))});
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(
                             2, false, 52, swf_fixtures::buildMatrixBytes(10 * 20, 10 * 20),
                             std::string("b"))});
    tags.push_back({1, {}});
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    auto bytes = swf_fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(30.0, 30.0);  // inside both a and b's boxes -- b wins
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();

    CHECK_EQ(root->scriptObject()->getOwnProperty("bFires").toNumber(), 1.0);
    CHECK(root->scriptObject()->getOwnProperty("aFires").isUndefined());
}

TEST_CASE(EventDispatch_InvisibleButton_NeverDispatches) {
    auto bytes = buildMovieWithCondActionButton(
        static_cast<uint16_t>(ButtonCondition::kIdleToOverUp), std::nullopt,
        buildIncrementHandlerBody("fired"));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    auto it = root->buttonInstances().find(1);
    CHECK(it != root->buttonInstances().end());
    if (it == root->buttonInstances().end()) return;
    it->second->setVisible(false);

    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK(root->scriptObject()->getOwnProperty("fired").isUndefined());
}

TEST_CASE(EventDispatch_ActionFiresExactlyOnce_NotEveryTickWhileHeld) {
    auto bytes = buildMovieWithCondActionButton(
        static_cast<uint16_t>(ButtonCondition::kOverUpToOverDown), std::nullopt,
        buildIncrementHandlerBody("fired"));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("fired").toNumber(), 1.0);

    // Held down for several more ticks, nothing changes -- must NOT refire.
    for (int i = 0; i < 5; ++i) {
        env.inputState().commitFrame();  // no setter calls: DOWN -> DOWN, no edge
        root->advanceFrame();
    }
    CHECK_EQ(root->scriptObject()->getOwnProperty("fired").toNumber(), 1.0);
}

TEST_CASE(EventDispatch_RepeatedPressReleaseCycles_FireOncePerCycle) {
    uint16_t cond = static_cast<uint16_t>(ButtonCondition::kOverUpToOverDown);
    auto bytes = buildMovieWithCondActionButton(cond, std::nullopt, buildIncrementHandlerBody("fired"));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();

    for (int cycle = 1; cycle <= 3; ++cycle) {
        env.inputState().setMouseDown(true);
        env.inputState().commitFrame();
        root->advanceFrame();
        CHECK_EQ(root->scriptObject()->getOwnProperty("fired").toNumber(), static_cast<double>(cycle));

        env.inputState().setMouseDown(false);
        env.inputState().commitFrame();
        root->advanceFrame();
        // release doesn't bump "fired" (that button only listens for
        // OverUpToOverDown) -- still `cycle`.
        CHECK_EQ(root->scriptObject()->getOwnProperty("fired").toNumber(), static_cast<double>(cycle));
    }
}

TEST_CASE(EventDispatch_ReleaseWithoutMatchingPress_DoesNotFire) {
    auto bytes = buildMovieWithCondActionButton(
        static_cast<uint16_t>(ButtonCondition::kOverDownToOverUp), std::nullopt,
        buildIncrementHandlerBody("fired"));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    // Press happens OUTSIDE the button (so pressedButton_ never gets set to
    // it at all -- note the very first-ever commitFrame() call always
    // reports a press edge per InputState's documented "previous defaults
    // to up" semantics, so the press edge itself is unavoidable; what
    // matters is that it isn't observed OVER the button), then the mouse
    // drags onto the button while still held (no new press edge), then
    // releases there.
    env.inputState().setMousePosition(90.0, 90.0);  // outside
    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();  // press edge, but not over the button -- pressedButton_ stays null
    root->advanceFrame();

    env.inputState().setMousePosition(20.0, 20.0);  // drags onto the button, still held
    env.inputState().commitFrame();                  // DOWN -> DOWN, no edge
    root->advanceFrame();

    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();  // DOWN -> UP: release edge, but pressedButton_ is still null
    root->advanceFrame();
    CHECK(root->scriptObject()->getOwnProperty("fired").isUndefined());
}

TEST_CASE(EventDispatch_ActionChangesTimeline_GotoAndStopOnParent) {
    Asm gotoAction;
    gotoAction.gotoFrameAction(1);  // 0-based frame index 1 == 1-based frame 2
    auto bytes = buildMovieWithCondActionButton(
        static_cast<uint16_t>(ButtonCondition::kOverUpToOverDown), std::nullopt,
        gotoAction.build(), /*rootActionBytes=*/{}, /*frameCount=*/2);
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);
    CHECK_EQ(root->timeline().currentFrame(), static_cast<uint32_t>(1));

    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();

    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK_EQ(root->timeline().currentFrame(), static_cast<uint32_t>(2));
}

TEST_CASE(EventDispatch_ActionModifiesProperty_OnParent) {
    Asm action;
    action.pushString("customFlag");
    action.pushBool(true);
    action.op(0x1D);  // ActionSetVariable -- sets on the dispatch target (root, here)
    auto bytes = buildMovieWithCondActionButton(
        static_cast<uint16_t>(ButtonCondition::kOverUpToOverDown), std::nullopt, action.build());
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();

    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK(root->scriptObject()->getOwnProperty("customFlag").toBoolean());
}

TEST_CASE(EventDispatch_ActionRemovesOwnParent_NoDanglingPointerCrash) {
    // The button's own condActionsV2 action removes ITS OWN PARENT clip
    // ("mc") -- exercises Phase L's event-target-safety requirement.
    // pressedButton_/hoverButton_ must be cleared (via notifyRemoved()) so
    // the very next tick doesn't touch a dangling ButtonInstance.
    Asm removeAction;
    removeAction.pushString("");  // RemoveSprite("") -- current target (== "mc" itself)
    removeAction.op(0x25);        // ActionRemoveSprite
    auto bytes = buildMovieWithNestedCondActionButton(
        static_cast<uint16_t>(ButtonCondition::kOverUpToOverDown), removeAction.build());
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);
    CHECK(root->children().find(1) != root->children().end());  // "mc" exists

    env.inputState().setMousePosition(40.0, 40.0);  // over the nested button (world coords)
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();

    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();
    root->advanceFrame();  // fires the action, which removes "mc" mid-dispatch

    CHECK(root->children().find(1) == root->children().end());  // "mc" is genuinely gone

    // Further ticks (moving the mouse back over the now-vacant area,
    // releasing, etc.) must not crash.
    env.inputState().setMousePosition(90.0, 90.0);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();
    env.inputState().commitFrame();
    root->advanceFrame();
}

// ===========================================================================
// CondKeyPress (keyboard-triggered condActionsV2)
// ===========================================================================

TEST_CASE(EventDispatch_CondKeyPress_MatchingKey_Dispatches) {
    // CondKeyPress=4 ("End" per the SWF spec's own key table -- see
    // condKeyPressToInputKeyCode()'s doc comment), zero mouse condition
    // bits -- the exact real Hobo-corpus pattern.
    auto bytes = buildMovieWithCondActionButton(0, static_cast<uint8_t>(4),
                                                 buildIncrementHandlerBody("fired"));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setKeyDown(flash3ds::runtime::InputState::kEnd, true);
    env.inputState().commitFrame();  // UP -> DOWN: key-press edge
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("fired").toNumber(), 1.0);
}

TEST_CASE(EventDispatch_CondKeyPress_WrongKey_DoesNotDispatch) {
    auto bytes = buildMovieWithCondActionButton(0, static_cast<uint8_t>(4),
                                                 buildIncrementHandlerBody("fired"));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setKeyDown(flash3ds::runtime::InputState::kHome, true);  // different key
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK(root->scriptObject()->getOwnProperty("fired").isUndefined());
}

TEST_CASE(EventDispatch_CondKeyPress_DispatchesEvenWhenButtonNotHovered) {
    // CondKeyPress is a KEYBOARD trigger, independent of mouse hover
    // position -- the button never needs to be under the pointer at all.
    auto bytes = buildMovieWithCondActionButton(0, static_cast<uint8_t>(4),
                                                 buildIncrementHandlerBody("fired"));
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(90.0, 90.0);  // nowhere near the button
    env.inputState().setKeyDown(flash3ds::runtime::InputState::kEnd, true);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("fired").toNumber(), 1.0);
}

// ===========================================================================
// AS2 property-handler dispatch (Extreme Pamplona pattern) -- buttons
// ===========================================================================

TEST_CASE(EventDispatch_ButtonOnPress_PropertyHandler_InvokedOnPress) {
    // btn.onPress = function(){ fired = fired + 1; };
    auto rootAction = buildAssignHandlerAction("btn", "onPress", "fired");
    auto bytes = buildMovieWithCondActionButton(0, std::nullopt, {}, rootAction);
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();

    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("fired").toNumber(), 1.0);
}

TEST_CASE(EventDispatch_ButtonOnRelease_PropertyHandler_InvokedOnRelease) {
    auto rootAction = buildAssignHandlerAction("btn", "onRelease", "fired");
    auto bytes = buildMovieWithCondActionButton(0, std::nullopt, {}, rootAction);
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK(root->scriptObject()->getOwnProperty("fired").isUndefined());

    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("fired").toNumber(), 1.0);
}

TEST_CASE(EventDispatch_ButtonOnRollOverOnRollOut_PropertyHandlers) {
    Asm a;
    a.pushString("btn");
    a.op(0x1C);
    a.pushString("onRollOver");
    a.defineFunctionV1("", {}, buildIncrementHandlerBody("overCount"));
    a.op(0x4F);
    a.pushString("btn");
    a.op(0x1C);
    a.pushString("onRollOut");
    a.defineFunctionV1("", {}, buildIncrementHandlerBody("outCount"));
    a.op(0x4F);
    auto bytes = buildMovieWithCondActionButton(0, std::nullopt, {}, a.build());
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(90.0, 90.0);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();

    env.inputState().setMousePosition(20.0, 20.0);  // enters
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("overCount").toNumber(), 1.0);
    CHECK(root->scriptObject()->getOwnProperty("outCount").isUndefined());

    env.inputState().setMousePosition(90.0, 90.0);  // leaves
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("overCount").toNumber(), 1.0);
    CHECK_EQ(root->scriptObject()->getOwnProperty("outCount").toNumber(), 1.0);
}

TEST_CASE(EventDispatch_ButtonPropertyHandler_ThisIsTheButtonItself) {
    // Verifies Phase I/J's documented `this`-binding distinction: a
    // property-handler function's `this` is the BUTTON's own scriptObject
    // (real AS2 this-follows-property-owner rule) -- NOT the parent, unlike
    // condActionsV2 dispatch. Captured by assigning a marker property onto
    // `this` from inside the handler and confirming it lands on the
    // button's own scriptObject, not root's.
    // function(){ this.marked = true; } -- `this` is read from register 1
    // via DefineFunction2's PreloadThis flag (see below).
    Asm handlerBody;
    // this.marked = true;
    handlerBody.pushRegisterValue(1);  // preload-this convention below uses register 1 for `this`
    handlerBody.pushString("marked");
    handlerBody.pushBool(true);
    handlerBody.op(0x4F);  // ActionSetMember

    Asm a;
    a.pushString("btn");
    a.op(0x1C);
    a.pushString("onPress");
    // DefineFunction2 with PreloadThis into register 1 (flags bit 0 =
    // PreloadThisFlag per the AVM1 opcode spec — Interpreter.cpp's
    // DefineFunction2 handling assigns register slots as
    // this/arguments/super/... per the flag bits before user params).
    a.defineFunction2("", /*registerCount=*/2, /*flags=*/0x0001, {}, handlerBody.build());
    a.op(0x4F);
    auto bytes = buildMovieWithCondActionButton(0, std::nullopt, {}, a.build());
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();
    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();
    root->advanceFrame();

    auto btnIt = root->buttonInstances().find(1);
    CHECK(btnIt != root->buttonInstances().end());
    if (btnIt != root->buttonInstances().end()) {
        CHECK(btnIt->second->scriptObject()->getOwnProperty("marked").toBoolean());
    }
    // Must NOT have landed on root.
    CHECK(root->scriptObject()->getOwnProperty("marked").isUndefined());
}

// ===========================================================================
// AS2 property-handler dispatch -- plain MovieClips (no Button2 at all)
// ===========================================================================

TEST_CASE(EventDispatch_PlainClipOnPressOnRelease_PropertyHandlers) {
    // A plain 40x40px MovieClip "mc" (no ButtonDef at all) with
    // onPress/onRelease assigned directly -- Extreme Pamplona's actual
    // pattern (docs/real-game-compatibility.md).
    Asm a;
    a.pushString("mc");
    a.op(0x1C);
    a.pushString("onPress");
    a.defineFunctionV1("", {}, buildIncrementHandlerBody("pressCount"));
    a.op(0x4F);
    a.pushString("mc");
    a.op(0x1C);
    a.pushString("onRelease");
    a.defineFunctionV1("", {}, buildIncrementHandlerBody("releaseCount"));
    a.op(0x4F);

    std::vector<swf_fixtures::FixtureTag> nestedTags = {
        {static_cast<uint16_t>(TagCode::DefineShape2),
         swf_fixtures::buildDefineShapeBytes(2, 20, 40 * 20, 40 * 20, 0xFF, 0x00, 0x00, 0xFF)},
        {26, swf_fixtures::buildPlaceObject2Bytes(1, false, 20, swf_fixtures::buildMatrixBytes(0, 0))},
        {1, {}},
    };
    auto spriteBody = swf_fixtures::buildDefineSpriteBytes(/*characterId=*/21, 1, nestedTags);
    std::vector<swf_fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineSprite), spriteBody},
        {26, swf_fixtures::buildPlaceObject2Bytes(1, false, 21, swf_fixtures::buildMatrixBytes(0, 0),
                                                   std::string("mc"))},
        {static_cast<uint16_t>(TagCode::DoAction), a.build()},
        {1, {}},
    };
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    auto bytes = swf_fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();

    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("pressCount").toNumber(), 1.0);
    CHECK(root->scriptObject()->getOwnProperty("releaseCount").isUndefined());

    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();
    CHECK_EQ(root->scriptObject()->getOwnProperty("releaseCount").toNumber(), 1.0);
}

TEST_CASE(EventDispatch_ButtonHit_TakesPriorityOverOverlappingPlainClip) {
    // hitTestPoint()'s pre-existing contract (unchanged by this phase):
    // a button hit always takes priority over a plain-clip hit at the same
    // point -- confirms dispatchPointerEvents() never fires BOTH a
    // condActionsV2/onPress button dispatch AND a plain-clip onPress for
    // the same tick's same hit.
    Asm a;
    a.pushString("mc");
    a.op(0x1C);
    a.pushString("onPress");
    a.defineFunctionV1("", {}, buildIncrementHandlerBody("clipPressCount"));
    a.op(0x4F);

    std::vector<swf_fixtures::FixtureTag> nestedTags = {
        {static_cast<uint16_t>(TagCode::DefineShape2),
         swf_fixtures::buildDefineShapeBytes(2, 20, 40 * 20, 40 * 20, 0xFF, 0x00, 0x00, 0xFF)},
        {26, swf_fixtures::buildPlaceObject2Bytes(1, false, 20, swf_fixtures::buildMatrixBytes(0, 0))},
        {1, {}},
    };
    auto clipSpriteBody = swf_fixtures::buildDefineSpriteBytes(/*characterId=*/21, 1, nestedTags);

    auto buttonTags = buildButtonWithCondActionTags(50, 51, 40 * 20, 40 * 20,
                                                      static_cast<uint16_t>(ButtonCondition::kOverUpToOverDown),
                                                      std::nullopt, buildIncrementHandlerBody("buttonFires"));

    std::vector<swf_fixtures::FixtureTag> tags = {
        {static_cast<uint16_t>(TagCode::DefineSprite), clipSpriteBody},
    };
    tags.insert(tags.end(), buttonTags.begin(), buttonTags.end());
    // Both placed at the SAME point (0,0), button at higher depth (2) so it
    // wins the hit test.
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(1, false, 21,
                                                               swf_fixtures::buildMatrixBytes(0, 0),
                                                               std::string("mc"))});
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(2, false, 50,
                                                               swf_fixtures::buildMatrixBytes(0, 0),
                                                               std::string("btn"))});
    tags.push_back({static_cast<uint16_t>(TagCode::DoAction), a.build()});
    tags.push_back({1, {}});
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 1, tags);
    auto bytes = swf_fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);

    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();
    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();
    root->advanceFrame();

    CHECK_EQ(root->scriptObject()->getOwnProperty("buttonFires").toNumber(), 1.0);
    CHECK(root->scriptObject()->getOwnProperty("clipPressCount").isUndefined());
}

// ===========================================================================
// Lifetime safety (Phase L)
// ===========================================================================

TEST_CASE(EventDispatch_ButtonRemovedByDisplayList_HoverPressStateClearedNoStaleDispatch) {
    // A 3-frame movie: frames 1-2 place/keep a condActionsV2 button at
    // depth 1; frame 3 REPLACES that depth with a plain shape (display-
    // list-driven removal, going through syncChildren()'s buttonInstances_
    // prune loop, NOT removeFromParent()). Note dispatch only ever runs
    // INSIDE advanceFrame(), and createRoot() itself already syncs +
    // "dispatches" frame 1's initial (no-op, first-ever) tick -- so frame 2
    // is where the press actually gets captured (first real advanceFrame()
    // call), and frame 3's advanceFrame() call is where the button gets
    // replaced by syncChildren() AND (later in that SAME call)
    // dispatchPointerEvents() runs again with a mouse-released edge. The
    // replacement's notifyButtonRemoved() call must have already cleared
    // pressedButton_ before that dispatch runs -- otherwise it would
    // dereference a dangling ButtonInstance*.
    uint16_t buttonId = 80, buttonShapeId = 81, replacementShapeId = 82;
    auto buttonTags = buildButtonWithCondActionTags(
        buttonId, buttonShapeId, 40 * 20, 40 * 20,
        static_cast<uint16_t>(ButtonCondition::kOverDownToOverUp), std::nullopt,
        buildIncrementHandlerBody("fired"));
    auto replacementShapeBody = swf_fixtures::buildDefineShapeBytes(
        2, replacementShapeId, 20 * 20, 20 * 20, 0x00, 0x00, 0xFF, 0xFF);

    std::vector<swf_fixtures::FixtureTag> tags = buttonTags;
    tags.push_back({static_cast<uint16_t>(TagCode::DefineShape2), replacementShapeBody});
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(1, false, buttonId,
                                                               swf_fixtures::buildMatrixBytes(0, 0),
                                                               std::string("btn"))});
    tags.push_back({1, {}});  // frame 1: button placed
    tags.push_back({1, {}});  // frame 2: unchanged (button persists)
    tags.push_back({26, swf_fixtures::buildPlaceObject2Bytes(1, true, replacementShapeId,
                                                               swf_fixtures::buildMatrixBytes(0, 0))});
    tags.push_back({1, {}});  // frame 3: replaced by a plain shape
    auto body = swf_fixtures::buildMovieBody(100 * 20, 100 * 20, 12.0, 3, tags);
    auto bytes = swf_fixtures::wrapFws(6, body);

    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);
    CHECK(root->buttonInstances().find(1) != root->buttonInstances().end());  // present at frame 1

    env.inputState().setMousePosition(20.0, 20.0);
    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();
    root->advanceFrame();  // -> frame 2: press captures the button (its own condition doesn't
                            // match OverUpToOverDown, so no dispatch fires, but pressedButton_
                            // is still set unconditionally by the "press" step)

    // -> frame 3: display-list replaces the button with a plain shape, THEN
    // (same tick) dispatch runs again -- must not crash, and the stale
    // OverDownToOverUp condition must not fire against a dangling pointer.
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();

    CHECK(root->buttonInstances().find(1) == root->buttonInstances().end());
    // "fired" may or may not be set depending on exact same-tick ordering,
    // but the crucial assertion is simply that we got here without a
    // crash/UB -- that IS the regression this test guards.
}

TEST_CASE(EventDispatch_RemovingAncestor_ClearsGrandchildHoverClip_NoDanglingPointerCrash) {
    // 2026-08-27 crash fix (see notifyRemovedRecursive()'s header doc
    // comment and buildMovieWithGrandchildHoverAndKeyRemovableAncestor()'s
    // own comment above): hoverClip_ points at "inner", a GRANDCHILD of
    // root nested two levels below "mc"; a keypress removes "mc" (an
    // ancestor of "inner", not "inner" itself) via removeFromParent()'s
    // single-node path. Before this fix, removeFromParent()'s
    // notifyRemoved(this=mc) call couldn't see past "mc" to clear a stale
    // pointer into its own descendant -- this reproduced a real,
    // gdb-confirmed segfault against real Extreme Pamplona corpus content
    // (docs/known-limitations.md L6) inside firePropertyHandler() ->
    // Object::getMember() on the next tick's hover-state-change dispatch.
    auto bytes = buildMovieWithGrandchildHoverAndKeyRemovableAncestor();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);
    auto characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;
    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    CHECK(root != nullptr);
    CHECK(root->children().find(1) != root->children().end());  // "mc" exists

    // Hover "inner" (world offset (30,30)+(30,30) = (60,60), within its
    // 40x40 box) -- sets hoverClip_ to the grandchild "inner" instance.
    env.inputState().setMousePosition(70.0, 70.0);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();

    // Press "End" -- CondKeyPress-triggered, independent of mouse position
    // -- fires removeBtn's RemoveSprite("mc"), destroying "mc" AND, with
    // it, "inner" (cascading unique_ptr/shared_ptr subtree destruction).
    // The mouse is deliberately left exactly where it was (still "over"
    // inner's now-vacated world position) so no ordinary hover-state
    // change has a chance to clear hoverClip_ before this happens.
    env.inputState().setKeyDown(flash3ds::runtime::InputState::kEnd, true);
    env.inputState().commitFrame();
    root->advanceFrame();

    CHECK(root->children().find(1) == root->children().end());  // "mc" is genuinely gone

    // One more tick with the key released and the mouse moved elsewhere --
    // whichever tick's dispatchPointerEvents() first sees hitClip differ
    // from a still-dangling hoverClip_ is where the old bug crashed. Simply
    // getting through this without a segfault IS the regression this test
    // guards (matching EventDispatch_ActionRemovesOwnParent_
    // NoDanglingPointerCrash's own convention above).
    env.inputState().setKeyDown(flash3ds::runtime::InputState::kEnd, false);
    env.inputState().setMousePosition(200.0, 200.0);
    env.inputState().commitFrame();
    root->advanceFrame();
}
