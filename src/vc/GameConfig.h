// GameConfig.h
//
// Virtual Console resource layer: structured, validated configuration for
// ONE packaged game -- which SWF to load and how physical input maps onto
// the runtime's EXISTING logical input model (runtime::InputState). This
// is a pure data/parsing layer: it never touches SWF/AVM1/renderer code,
// and it never talks to a filesystem or RomFS directly (see GamePackage.h
// for that) -- GameConfig::fromIniText() takes already-read text, which is
// what makes it testable on desktop with zero 3DS/filesystem dependency.
//
// Reuses runtime::InputState::KeyCode (see runtime/InputState.h) for every
// named key mapping -- this file introduces NO parallel key-code system.

#pragma once

#include <optional>
#include <string>

#include "runtime/InputState.h"

namespace flash3ds::vc {

// Physical-3DS-button -> logical-InputState-keycode mapping, plus whether
// touch/mouse pointer input is fed into InputState at all, and which
// screen's dimensions the touch digitizer's raw coordinates should be
// interpreted against. Consumed by platform::Nintendo3DSInput (see that
// class's setMapping()/constructor) -- InputState/AVM1/EventDispatcher
// themselves are completely unaware this type exists; they only ever see
// the SAME setKeyDown()/setMousePosition() calls Nintendo3DSInput already
// made before this layer existed.
struct InputMapping {
    // Defaults match this project's documented default config.ini (see
    // docs/virtual-console.md) exactly -- so "no config.ini present" and
    // "config.ini present with these exact values" are indistinguishable
    // in behavior, by construction (GameConfig::defaults() is literally
    // fromIniText("")).
    int aKeyCode = runtime::InputState::kEnter;
    int bKeyCode = runtime::InputState::kEscape;
    int xKeyCode = runtime::InputState::kSpace;
    int yKeyCode = runtime::InputState::kShift;
    int lKeyCode = 'L';
    int rKeyCode = 'R';
    int startKeyCode = runtime::InputState::kEnter;
    int selectKeyCode = runtime::InputState::kEscape;

    bool touchEnabled = true;
    // "bottom" (default, the real touch-digitizer screen on hardware) or
    // "top" -- accepted for generality/testability, but not physically
    // wired to any digitizer on real 3DS hardware (see docs/input.md's
    // existing Nintendo3DSInput notes and docs/virtual-console.md).
    bool touchUsesBottomScreen = true;

    bool mouseEnabled = true;

    // Hand-written (not `= default`) -- this project targets C++17, which
    // has no defaulted comparison operators (a C++20 feature).
    bool operator==(const InputMapping& other) const {
        return aKeyCode == other.aKeyCode && bKeyCode == other.bKeyCode &&
               xKeyCode == other.xKeyCode && yKeyCode == other.yKeyCode &&
               lKeyCode == other.lKeyCode && rKeyCode == other.rKeyCode &&
               startKeyCode == other.startKeyCode && selectKeyCode == other.selectKeyCode &&
               touchEnabled == other.touchEnabled &&
               touchUsesBottomScreen == other.touchUsesBottomScreen &&
               mouseEnabled == other.mouseEnabled;
    }
};

// Parses a config.ini [input]/[touch]/[mouse] section value token
// (e.g. "ENTER", "SPACE", "L") into an InputState key code. Reuses
// InputState::KeyCode for every named constant; a single printable ASCII
// character (e.g. "L", "R", "X") maps directly to its own character code,
// matching the convention platform::Nintendo3DSInput already used before
// this layer existed (see that file's own mapping-convention comment).
// Returns std::nullopt for anything else (empty string, multi-character
// non-keyword token, unrecognized keyword) -- callers apply the
// "invalid entry: ignore it, keep the existing default" rule using this.
std::optional<int> parseKeyToken(const std::string& token);

struct GameConfig {
    // Defaults match docs/virtual-console.md's documented config.ini
    // example exactly.
    std::string swfFilename = "game.swf";
    InputMapping input;

    // Builds a GameConfig from already-read INI text. Never fails: a
    // missing file is represented by an EMPTY string (fromIniText("") ==
    // defaults(), by construction -- see IniDocument::parse's own doc
    // comment), and any malformed/unknown section, key, or value is
    // silently ignored, falling back to that ONE field's own default --
    // never resets the whole config. See docs/virtual-console.md's error
    // handling table.
    static GameConfig fromIniText(const std::string& iniText);

    // Equivalent to fromIniText("") -- kept as its own named entry point
    // both for readability at call sites ("missing config.ini" reads more
    // clearly than "empty string") and so a future default source (e.g. a
    // constant baked in some other way) could diverge from "parse an
    // empty string" without changing every call site.
    static GameConfig defaults();

    bool operator==(const GameConfig& other) const {
        return swfFilename == other.swfFilename && input == other.input;
    }
};

}  // namespace flash3ds::vc
