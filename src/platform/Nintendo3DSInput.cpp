// Nintendo3DSInput.cpp
//
// See Nintendo3DSInput.h for the mapping-convention notes.

#include "platform/Nintendo3DSInput.h"

namespace flash3ds::platform {

Nintendo3DSInput::Nintendo3DSInput(int screenWidthPixels, int screenHeightPixels,
                                     const vc::InputMapping& mapping)
    : screenWidth_(screenWidthPixels), screenHeight_(screenHeightPixels), mapping_(mapping) {}

void Nintendo3DSInput::poll(runtime::InputState& state) {
    // hidScanInput() itself is the caller's responsibility (once per frame,
    // before this call) — done that way so a caller polling multiple
    // subsystems from the same hid snapshot only pays for one scan.
    const u32 held = hidKeysHeld();

    // Tell InputState what pixel space setMousePosition()'s x/y below are
    // actually expressed in (interactivity phase, 2026-08-18 fix) — this is
    // what lets MovieClipInstance::stageMouseX()/stageMouseY() (the actual
    // _xmouse/_ymouse implementation) convert into the loaded movie's own
    // stage-pixel space, regardless of whether screenWidth_/screenHeight_
    // happen to match that movie's stage size. Set every poll (cheap, and
    // correct even if a future caller reconstructs/reconfigures screen
    // dimensions between polls).
    state.setViewportSize(screenWidth_, screenHeight_);

    // --- touch screen -> mouse ------------------------------------------
    // Virtual Console layer (2026-08-19): both touchEnabled AND
    // mouseEnabled must be true to feed InputState's pointer state at all
    // — the 3DS has exactly one pointer-like input source (the touch
    // digitizer), so these two independently-configurable flags (see
    // vc::InputMapping, GameConfig.h) both gate the SAME underlying path
    // here rather than two separate mechanisms. If disabled, InputState's
    // mouse-down edge is explicitly cleared (same as the "not touched"
    // branch below) rather than left stale.
    const bool pointerEnabled = mapping_.touchEnabled && mapping_.mouseEnabled;
    if (pointerEnabled && (held & KEY_TOUCH)) {
        touchPosition touch;
        hidTouchRead(&touch);
        double x = touch.px;
        double y = touch.py;
        if (screenWidth_ != kRawTouchWidth || screenHeight_ != kRawTouchHeight) {
            x = x * (static_cast<double>(screenWidth_) / kRawTouchWidth);
            y = y * (static_cast<double>(screenHeight_) / kRawTouchHeight);
        }
        state.setMousePosition(x, y);
        state.setMouseDown(true);
    } else {
        // KEY_TOUCH is documented as "Not actually provided by HID" in
        // libctru's own hid.h — held touch state may need to be inferred
        // from touchPosition being non-(0,0) on some SDK versions instead.
        // Kept as a documented open question (see docs/input.md's Phase 10
        // section) rather than guessed at further in this session, since
        // no hardware/emulator was available to observe actual behavior.
        state.setMouseDown(false);
    }

    // --- D-Pad / Circle Pad -> arrow keys --------------------------------
    // Unlike the face/shoulder/start/select buttons below, D-Pad->arrow-key
    // is NOT configurable — there's no natural alternative binding for a
    // directional pad, and the Virtual Console spec only asks for A/B/X/Y/
    // L/R/START/SELECT to be remappable (see config.ini's [input] section,
    // docs/virtual-console.md).
    state.setKeyDown(runtime::InputState::kLeft, (held & KEY_LEFT) != 0);
    state.setKeyDown(runtime::InputState::kRight, (held & KEY_RIGHT) != 0);
    state.setKeyDown(runtime::InputState::kUp, (held & KEY_UP) != 0);
    state.setKeyDown(runtime::InputState::kDown, (held & KEY_DOWN) != 0);

    // --- face buttons / START / SELECT / L / R -> CONFIGURED InputState
    // key codes (Virtual Console layer, 2026-08-19) --------------------
    // Each physical button's target InputState key code now comes from
    // `mapping_` (see vc::InputMapping, GameConfig.h) instead of being
    // hardcoded — this class still owns 100% of the hid-polling/edge logic
    // itself, only the TARGET key code became configurable. mapping_'s own
    // default values reproduce this class's original hardcoded behavior
    // (see this file's header comment for the one deliberate exception,
    // X/Y's default).
    //
    // config.ini can legally map two DIFFERENT physical buttons to the
    // SAME InputState key code (the documented default already does this:
    // A and START both map to Key.ENTER — matching this class's original
    // hardcoded behavior, which combined both with a single OR'd
    // setKeyDown call. B and SELECT used to share Key.ESCAPE the same
    // way; SELECT's default moved to Key.END on 2026-08-30 — see
    // GameConfig.h's selectKeyCode doc comment — so they no longer share
    // a code by default, though a config.ini is still free to remap them
    // back together). Calling
    // setKeyDown() once per physical button independently would let a
    // later call silently stomp an earlier one for the same code (e.g.
    // "A held, START not held" would incorrectly clear Key.ENTER if the
    // START entry were processed after the A entry) — so every physical
    // button's flag is first OR-merged per DISTINCT target code below,
    // and setKeyDown() is called at most once per distinct code, the
    // first time that code is encountered (fixed A/B/X/Y/L/R/START/SELECT
    // order, so behavior is deterministic even when multiple buttons that
    // share a code are held in the same tick).
    // ZL/ZR/C-Stick (2026-08-24) join this SAME OR-merge table rather than
    // getting their own separate setKeyDown() calls — for exactly the
    // reason the comment above already explains for A/START etc.: any
    // future config.ini could map one of these six to a code an existing
    // button already targets (or to each other), and only routing every
    // configurable button through one shared per-code OR-merge pass keeps
    // that safe. They are NOT mixed with the D-Pad's arrow-key setKeyDown()
    // calls above, which is deliberate — see GameConfig.h's InputMapping
    // C-Stick fields for why that would be a correctness hazard, not just
    // a style choice.
    constexpr int kPhysicalButtonCount = 14;
    const int targetCodes[kPhysicalButtonCount] = {
        mapping_.aKeyCode,      mapping_.bKeyCode,          mapping_.xKeyCode,
        mapping_.yKeyCode,      mapping_.lKeyCode,          mapping_.rKeyCode,
        mapping_.startKeyCode,  mapping_.selectKeyCode,     mapping_.zlKeyCode,
        mapping_.zrKeyCode,     mapping_.cStickUpKeyCode,   mapping_.cStickDownKeyCode,
        mapping_.cStickLeftKeyCode, mapping_.cStickRightKeyCode,
    };
    const bool physicalDown[kPhysicalButtonCount] = {
        (held & KEY_A) != 0,           (held & KEY_B) != 0,
        (held & KEY_X) != 0,           (held & KEY_Y) != 0,
        (held & KEY_L) != 0,           (held & KEY_R) != 0,
        (held & KEY_START) != 0,       (held & KEY_SELECT) != 0,
        (held & KEY_ZL) != 0,          (held & KEY_ZR) != 0,
        (held & KEY_CSTICK_UP) != 0,   (held & KEY_CSTICK_DOWN) != 0,
        (held & KEY_CSTICK_LEFT) != 0, (held & KEY_CSTICK_RIGHT) != 0,
    };
    for (int i = 0; i < kPhysicalButtonCount; ++i) {
        bool alreadyHandled = false;
        for (int j = 0; j < i; ++j) {
            if (targetCodes[j] == targetCodes[i]) {
                alreadyHandled = true;
                break;
            }
        }
        if (alreadyHandled) continue;

        bool down = physicalDown[i];
        for (int j = i + 1; j < kPhysicalButtonCount; ++j) {
            if (targetCodes[j] == targetCodes[i]) down = down || physicalDown[j];
        }
        state.setKeyDown(targetCodes[i], down);
    }

    // Edge detection: exactly one commit per poll() call (see header's
    // doc comment) — MUST be last, after every setKeyDown()/
    // setMousePosition()/setMouseDown() call above has already run for
    // this tick, so this tick's full "current" state is in place before
    // the previous/current diff happens.
    state.commitFrame();
}

}  // namespace flash3ds::platform
