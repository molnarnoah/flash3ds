// Nintendo3DSInput.cpp
//
// See Nintendo3DSInput.h for the mapping-convention notes.

#include "platform/Nintendo3DSInput.h"

namespace flash3ds::platform {

Nintendo3DSInput::Nintendo3DSInput(int screenWidthPixels, int screenHeightPixels)
    : screenWidth_(screenWidthPixels), screenHeight_(screenHeightPixels) {}

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
    if (held & KEY_TOUCH) {
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
    state.setKeyDown(runtime::InputState::kLeft, (held & KEY_LEFT) != 0);
    state.setKeyDown(runtime::InputState::kRight, (held & KEY_RIGHT) != 0);
    state.setKeyDown(runtime::InputState::kUp, (held & KEY_UP) != 0);
    state.setKeyDown(runtime::InputState::kDown, (held & KEY_DOWN) != 0);

    // --- face buttons / START / SELECT -> reasonable-effort key-code
    // stand-ins ------------------------------------------------------------
    // A/B/X/Y/START/SELECT have no natural AS2 Key.* equivalent. A and
    // START both map to Enter, B and SELECT both map to Escape (a common
    // "confirm"/"cancel" convention), X/Y map to printable ASCII codes
    // 'X'/'Y' — all as a documented, arbitrary convention content can
    // rebind against if needed (see header comment).
    state.setKeyDown(runtime::InputState::kEnter,
                      (held & (KEY_A | KEY_START)) != 0);
    state.setKeyDown(runtime::InputState::kEscape,
                      (held & (KEY_B | KEY_SELECT)) != 0);
    state.setKeyDown('X', (held & KEY_X) != 0);
    state.setKeyDown('Y', (held & KEY_Y) != 0);
}

}  // namespace flash3ds::platform
