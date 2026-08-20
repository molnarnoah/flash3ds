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
    CHECK_EQ(config.input.selectKeyCode, InputState::kEscape);
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

TEST_CASE(GameConfig_SharedKeyCode_BothFieldsResolveToSameCode) {
    // The documented default config.ini deliberately maps A and START to
    // the SAME target code (Key.ENTER) -- see romfs/config.ini. This is
    // what Nintendo3DSInput's OR-merge logic (see that file's poll(),
    // 3DS-only and not unit-testable on desktop) depends on to preserve
    // "either physical button presses the same logical key" instead of
    // one silently overriding the other -- see that file's own comment
    // for the merge logic itself. This test only verifies the CONFIG side
    // of that: both fields really do resolve to the identical code.
    GameConfig config = GameConfig::defaults();
    CHECK_EQ(config.input.aKeyCode, config.input.startKeyCode);
    CHECK_EQ(config.input.bKeyCode, config.input.selectKeyCode);
}

TEST_CASE(GameConfig_InvalidBoolean_FallsBackToDefault) {
    GameConfig config = GameConfig::fromIniText("[touch]\nenabled=maybe\n");
    CHECK(config.input.touchEnabled);  // default is true, invalid value ignored
}

TEST_CASE(GameConfig_InvalidTouchScreen_FallsBackToDefault) {
    GameConfig config = GameConfig::fromIniText("[touch]\nscreen=sideways\n");
    CHECK(config.input.touchUsesBottomScreen);  // default is "bottom"
}
