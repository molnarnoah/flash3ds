// test_vc_config.cpp — Virtual Console resource layer: GameConfig
// (config.ini -> structured config, reusing runtime::InputState::KeyCode
// for every key mapping -- see src/vc/GameConfig.h).

#include "TestFramework.h"
#include "runtime/InputState.h"
#include "vc/GameConfig.h"

using flash3ds::runtime::InputState;
using flash3ds::vc::GameConfig;
using flash3ds::vc::parseKeyToken;

// --- parseKeyToken -----------------------------------------------------

TEST_CASE(ParseKeyToken_NamedKeyword_ResolvesToInputStateKeyCode) {
    CHECK_EQ(parseKeyToken("ENTER").value(), InputState::kEnter);
    CHECK_EQ(parseKeyToken("ESCAPE").value(), InputState::kEscape);
    CHECK_EQ(parseKeyToken("SPACE").value(), InputState::kSpace);
    CHECK_EQ(parseKeyToken("SHIFT").value(), InputState::kShift);
}

TEST_CASE(ParseKeyToken_SinglePrintableChar_ResolvesToItsOwnCharCode) {
    CHECK_EQ(parseKeyToken("L").value(), static_cast<int>('L'));
    CHECK_EQ(parseKeyToken("R").value(), static_cast<int>('R'));
    CHECK_EQ(parseKeyToken("A").value(), static_cast<int>('A'));
}

TEST_CASE(ParseKeyToken_Invalid_ReturnsNullopt) {
    CHECK(!parseKeyToken("").has_value());
    CHECK(!parseKeyToken("NOT_A_REAL_KEY").has_value());
    CHECK(!parseKeyToken("TOOLONG").has_value());
}

// --- GameConfig::fromIniText / defaults ---------------------------------

TEST_CASE(GameConfig_Defaults_MatchDocumentedConfigIniExample) {
    // See docs/virtual-console.md / romfs/config.ini -- these two must
    // never drift apart (GameConfig::defaults() IS what "missing
    // config.ini" falls back to).
    GameConfig config = GameConfig::defaults();

    CHECK_EQ(config.swfFilename, std::string("game.swf"));
    CHECK_EQ(config.input.aKeyCode, InputState::kEnter);
    CHECK_EQ(config.input.bKeyCode, InputState::kEscape);
    CHECK_EQ(config.input.xKeyCode, InputState::kSpace);
    CHECK_EQ(config.input.yKeyCode, InputState::kShift);
    CHECK_EQ(config.input.lKeyCode, static_cast<int>('L'));
    CHECK_EQ(config.input.rKeyCode, static_cast<int>('R'));
    CHECK_EQ(config.input.startKeyCode, InputState::kEnter);
    // SELECT's default is kEnd, not kEscape (2026-08-30 fix) -- see
    // GameConfig.h's selectKeyCode doc comment: kEnd matches the
    // CondKeyPress=4 ("End") every real Hobo game's frame-1 buttons gate
    // on, confirmed (docs/known-limitations.md L11) to drive real
    // root-timeline navigation. The old kEscape default meant SELECT had
    // no effect on real corpus content at all.
    CHECK_EQ(config.input.selectKeyCode, InputState::kEnd);
    CHECK_EQ(config.input.zlKeyCode, static_cast<int>('Z'));
    CHECK_EQ(config.input.zrKeyCode, static_cast<int>('V'));
    CHECK_EQ(config.input.cStickUpKeyCode, static_cast<int>('I'));
    CHECK_EQ(config.input.cStickDownKeyCode, static_cast<int>('K'));
    CHECK_EQ(config.input.cStickLeftKeyCode, static_cast<int>('J'));
    CHECK_EQ(config.input.cStickRightKeyCode, static_cast<int>('M'));
    CHECK(config.input.touchEnabled);
    CHECK(config.input.touchUsesBottomScreen);
    CHECK(config.input.mouseEnabled);
}

TEST_CASE(GameConfig_MissingFile_UsesDocumentedDefaults) {
    // "" is exactly what a missing-file fetch collapses to (see
    // vc::buildGamePackage) -- this test documents that equivalence
    // explicitly, distinct from GameConfig_EmptyFile_UsesDefaults below.
    CHECK(GameConfig::fromIniText("") == GameConfig::defaults());
}

TEST_CASE(GameConfig_EmptyFile_UsesDefaults) {
    GameConfig config = GameConfig::fromIniText("");
    CHECK(config == GameConfig::defaults());
}

TEST_CASE(GameConfig_Valid_OverridesEveryField) {
    GameConfig config = GameConfig::fromIniText(
        "[game]\n"
        "swf=mygame.swf\n"
        "[input]\n"
        "A=X\n"
        "B=Y\n"
        "START=SPACE\n"
        "SELECT=SHIFT\n"
        "[touch]\n"
        "enabled=false\n"
        "screen=top\n"
        "[mouse]\n"
        "enabled=false\n");

    CHECK_EQ(config.swfFilename, std::string("mygame.swf"));
    CHECK_EQ(config.input.aKeyCode, static_cast<int>('X'));
    CHECK_EQ(config.input.bKeyCode, static_cast<int>('Y'));
    CHECK_EQ(config.input.startKeyCode, InputState::kSpace);
    CHECK_EQ(config.input.selectKeyCode, InputState::kShift);
    CHECK(!config.input.touchEnabled);
    CHECK(!config.input.touchUsesBottomScreen);
    CHECK(!config.input.mouseEnabled);
}

TEST_CASE(GameConfig_CustomSwfFilename_IsUsedVerbatim) {
    GameConfig config = GameConfig::fromIniText("[game]\nswf=hobo3.swf\n");
    CHECK_EQ(config.swfFilename, std::string("hobo3.swf"));
}

TEST_CASE(GameConfig_DefaultSwfFilename_WhenGameSectionOmitted) {
    GameConfig config = GameConfig::fromIniText("[input]\nA=SPACE\n");
    CHECK_EQ(config.swfFilename, std::string("game.swf"));
}

TEST_CASE(GameConfig_Comment_DoesNotOverrideDefault) {
    GameConfig config = GameConfig::fromIniText("[game]\n; swf=commented_out.swf\n");
    CHECK_EQ(config.swfFilename, std::string("game.swf"));
}

TEST_CASE(GameConfig_UnknownSection_IsIgnored_RestStillParses) {
    GameConfig config = GameConfig::fromIniText(
        "[some_future_section]\n"
        "future_key=1\n"
        "[game]\n"
        "swf=real.swf\n");
    CHECK_EQ(config.swfFilename, std::string("real.swf"));
}

TEST_CASE(GameConfig_UnknownKey_IsIgnored_RestStillParses) {
    GameConfig config = GameConfig::fromIniText(
        "[game]\n"
        "swf=real.swf\n"
        "future_key=123\n");
    CHECK_EQ(config.swfFilename, std::string("real.swf"));
}

TEST_CASE(GameConfig_InvalidKeyToken_FallsBackToFieldDefault_OthersUnaffected) {
    GameConfig config = GameConfig::fromIniText(
        "[input]\n"
        "A=NOT_A_REAL_KEY\n"
        "B=Y\n");

    // A's invalid token is ignored -- A keeps its OWN default (Key.ENTER),
    // not some corrupted/zeroed value -- while B (a different field, valid
    // token) still parses normally.
    CHECK_EQ(config.input.aKeyCode, InputState::kEnter);
    CHECK_EQ(config.input.bKeyCode, static_cast<int>('Y'));
}

TEST_CASE(GameConfig_DuplicateSetting_LastOneWins) {
    GameConfig config = GameConfig::fromIniText("[game]\nswf=first.swf\nswf=second.swf\n");
    CHECK_EQ(config.swfFilename, std::string("second.swf"));
}

TEST_CASE(GameConfig_AMapping_ParsesEveryNamedAndCharToken) {
    CHECK_EQ(GameConfig::fromIniText("[input]\nA=ENTER\n").input.aKeyCode, InputState::kEnter);
    CHECK_EQ(GameConfig::fromIniText("[input]\nA=SPACE\n").input.aKeyCode, InputState::kSpace);
    CHECK_EQ(GameConfig::fromIniText("[input]\nA=Z\n").input.aKeyCode, static_cast<int>('Z'));
}

TEST_CASE(GameConfig_BMapping_ParsesEveryNamedAndCharToken) {
    CHECK_EQ(GameConfig::fromIniText("[input]\nB=ESCAPE\n").input.bKeyCode, InputState::kEscape);
    CHECK_EQ(GameConfig::fromIniText("[input]\nB=SHIFT\n").input.bKeyCode, InputState::kShift);
}

TEST_CASE(GameConfig_StartMapping_Parses) {
    CHECK_EQ(GameConfig::fromIniText("[input]\nSTART=SPACE\n").input.startKeyCode,
             InputState::kSpace);
}

TEST_CASE(GameConfig_LRMapping_Parses) {
    GameConfig config = GameConfig::fromIniText("[input]\nL=UP\nR=DOWN\n");
    CHECK_EQ(config.input.lKeyCode, InputState::kUp);
    CHECK_EQ(config.input.rKeyCode, InputState::kDown);
}

TEST_CASE(GameConfig_ZLZRMapping_Parses) {
    GameConfig config = GameConfig::fromIniText("[input]\nZL=UP\nZR=DOWN\n");
    CHECK_EQ(config.input.zlKeyCode, InputState::kUp);
    CHECK_EQ(config.input.zrKeyCode, InputState::kDown);
}

TEST_CASE(GameConfig_CStickMapping_ParsesAllFourDirections) {
    GameConfig config = GameConfig::fromIniText(
        "[input]\n"
        "CSTICK_UP=W\n"
        "CSTICK_DOWN=S\n"
        "CSTICK_LEFT=A\n"
        "CSTICK_RIGHT=D\n");
    CHECK_EQ(config.input.cStickUpKeyCode, static_cast<int>('W'));
    CHECK_EQ(config.input.cStickDownKeyCode, static_cast<int>('S'));
    CHECK_EQ(config.input.cStickLeftKeyCode, static_cast<int>('A'));
    CHECK_EQ(config.input.cStickRightKeyCode, static_cast<int>('D'));
}

TEST_CASE(GameConfig_InvalidZLToken_FallsBackToFieldDefault_OthersUnaffected) {
    GameConfig config = GameConfig::fromIniText(
        "[input]\n"
        "ZL=NOT_A_REAL_KEY\n"
        "ZR=UP\n");
    CHECK_EQ(config.input.zlKeyCode, static_cast<int>('Z'));  // invalid -- kept default
    CHECK_EQ(config.input.zrKeyCode, InputState::kUp);        // valid -- parsed normally
}

TEST_CASE(GameConfig_InvalidCStickToken_FallsBackToFieldDefault) {
    GameConfig config = GameConfig::fromIniText("[input]\nCSTICK_UP=TOOLONG\n");
    CHECK_EQ(config.input.cStickUpKeyCode, static_cast<int>('I'));  // invalid -- kept default
}

TEST_CASE(GameConfig_SharedKeyCode_BothFieldsResolveToSameCode) {
    // The documented default config.ini deliberately maps A and START to
    // the SAME target code (Key.ENTER) -- see romfs/config.ini. This is
    // what Nintendo3DSInput's OR-merge logic (see that file's poll(),
    // 3DS-only and not unit-testable on desktop) depends on to preserve
    // "either physical button presses the same logical key" instead of
    // one silently overriding the other -- see that file's own comment
    // for the merge logic itself. This test only verifies the CONFIG side
    // of that: both fields really do resolve to the identical code.
    //
    // B/SELECT deliberately do NOT share a code any more (2026-08-30 fix,
    // see GameConfig.h's selectKeyCode doc comment): SELECT's default
    // moved to kEnd (matching real Hobo content's CondKeyPress=4 trigger),
    // independent of B's own kEscape default. Asserted explicitly here so
    // a future edit that "helpfully" re-syncs them gets caught by this
    // test, not silently reintroduced.
    GameConfig config = GameConfig::defaults();
    CHECK_EQ(config.input.aKeyCode, config.input.startKeyCode);
    CHECK(config.input.bKeyCode != config.input.selectKeyCode);
}

TEST_CASE(GameConfig_InvalidBoolean_FallsBackToDefault) {
    GameConfig config = GameConfig::fromIniText("[touch]\nenabled=maybe\n");
    CHECK(config.input.touchEnabled);  // default is true, invalid value ignored
}

TEST_CASE(GameConfig_InvalidTouchScreen_FallsBackToDefault) {
    GameConfig config = GameConfig::fromIniText("[touch]\nscreen=sideways\n");
    CHECK(config.input.touchUsesBottomScreen);  // default is "bottom"
}

// Track A A3 (2026-08-27, task #58): confirms this ALREADY-GENERIC config
// mechanism can express exactly what real hobo.swf's own AVM1 bytecode
// checks (found via static disassembly, docs/hobo-playability-
// verification.md's Finding 5/6, and dynamic tracing,
// docs/hobo-title-progression.md) -- proving "wire the 3DS entry point to
// hobo.swf" needed zero code changes to GameConfig/GamePackage/
// nintendo3ds_main.cpp, only a title-specific config.ini a packager would
// ship alongside hobo.swf.
//
// Update (2026-08-30): SELECT=END below used to be this test's own worked
// EXAMPLE, deliberately DIFFERENT from the checked-in default (which was
// SELECT=ESCAPE at the time). That default has since been promoted to
// kEnd itself -- see GameConfig.h's selectKeyCode doc comment: with the
// old ESCAPE default, physically pressing SELECT sent a key code no real
// Hobo content's buttons listen for at all, a real reported "SELECT does
// nothing" symptom. This test's explicit `SELECT=END` line is now
// redundant with the default (harmless to leave -- fromIniText() applying
// a value identical to the field's own default is indistinguishable from
// omitting it) but kept for this test's own documentation value: it's
// still proving the mechanism can express what real content needs, now
// simply already true out of the box too.
//
// Hobo1's frame-1 preview icon (characterId=80) and its real frame-10
// player character (characterId=1913) poll Key.isDown(37/38/39/40) --
// InputState::kLeft/kUp/kRight/kDown, matching this project's own
// KeyCode enum values exactly (see runtime/InputState.h) -- for movement,
// which the D-Pad/Circle Pad already feed unconditionally
// (docs/virtual-console.md section 5: "D-Pad -> arrow keys is NOT
// configurable"), needing no config.ini entry at all. It also polls
// Key.isDown(65)/Key.isDown(83) ("A"/"S", ASCII, not the 3DS A button) for
// punch/kick-allow gating, and every frame-1 DefineButton2's
// condActionsV2 gates on CondKeyPress=4 ("End" per the SWF spec's legacy
// key table, InputState::kEnd) -- confirmed dynamically to drive real
// root-timeline navigation (docs/known-limitations.md L11). This example
// maps X/Y (free after the documented default's SPACE/SHIFT) to literal
// 'A'/'S', and SELECT (now the documented default itself, and distinct
// from the START+SELECT-held quit gesture, which needs BOTH held) to End.
TEST_CASE(GameConfig_Hobo1ExampleMapping_ParsesToExpectedKeyCodes) {
    GameConfig config = GameConfig::fromIniText(
        "[game]\n"
        "swf=hobo.swf\n"
        "[input]\n"
        "X=A\n"
        "Y=S\n"
        "SELECT=END\n");

    CHECK_EQ(config.swfFilename, std::string("hobo.swf"));
    CHECK_EQ(config.input.xKeyCode, static_cast<int>('A'));
    CHECK_EQ(config.input.yKeyCode, static_cast<int>('S'));
    CHECK_EQ(config.input.selectKeyCode, InputState::kEnd);

    // Movement keys are NOT part of config.ini at all -- confirming they
    // resolve to the exact codes hobo.swf's own Key.isDown() calls check,
    // independent of anything parsed above (see this test's own header
    // comment).
    CHECK_EQ(static_cast<int>(InputState::kLeft), 37);
    CHECK_EQ(static_cast<int>(InputState::kUp), 38);
    CHECK_EQ(static_cast<int>(InputState::kRight), 39);
    CHECK_EQ(static_cast<int>(InputState::kDown), 40);
}
