// InputState.h
//
// Phase 6: host-settable keyboard/mouse state that AVM1's Key/Mouse built-
// ins and _xmouse/_ymouse read from. Deliberately dumb: it's just a bag of
// "what's true right now", set by whatever's driving the runtime (a desktop
// test harness's keyboard/mouse events now; Nintendo 3DS touch/button
// polling later — Phase 10) and read by ScriptEnvironment's native Key/
// Mouse objects and MovieClipHostBindings::getProperty. No AVM1/runtime
// dependency in either direction — this is a leaf class other modules
// depend ON, not one that depends on them, so it's reusable by the future
// 3DS input backend without touching this file.
//
// Key codes follow AS2's Key class conventions (ASCII-range for printable
// keys, plus named constants for the common non-printable ones Flash
// exposes as Key.BACKSPACE/Key.LEFT/etc.) — these are well-documented,
// widely cross-referenced public constants (not derived from any
// Shift-DX/gameswf internals), but have NOT been cross-checked against a
// real Flash Player's Key.getCode() output for every code, so treat exact
// non-printable-key values as reasonable-confidence, not verified.

#pragma once

#include <cstdint>
#include <unordered_set>

namespace flash3ds::runtime {

class InputState {
public:
    // --- keyboard ---------------------------------------------------------
    void setKeyDown(int keyCode, bool down);
    bool isKeyDown(int keyCode) const;

    // Key.getCode(): the code of the most recent key to change state to
    // "down" (0 if no key has ever gone down). Real Flash also has a
    // separate Key.getAscii(); not modeled here — see
    // docs/avm1-support.md's Phase 6 limitations.
    int lastKeyCode() const { return lastKeyCode_; }

    // --- mouse --------------------------------------------------------------
    void setMousePosition(double xPixels, double yPixels);
    double mouseX() const { return mouseX_; }
    double mouseY() const { return mouseY_; }

    void setMouseDown(bool down) { mouseDown_ = down; }
    bool isMouseDown() const { return mouseDown_; }

    // --- input viewport (interactivity phase, 2026-08-18) -----------------
    //
    // The pixel-space dimensions that setMousePosition()'s raw x/y values
    // are expressed in — i.e. whatever coordinate space the host's touch/
    // mouse polling reports in, BEFORE any SWF-stage-aware conversion. On
    // the 3DS, Nintendo3DSInput rescales the raw 320x240 touch panel into
    // its own constructor's screenWidthPixels/screenHeightPixels and calls
    // setViewportSize() with those same dimensions — but that space has NO
    // relationship to any loaded movie's stage size, since InputState (by
    // design, see the file header) has no Movie/AVM1 dependency at all.
    //
    // This still keeps InputState "dumb": it stores a SIZE (a fact about
    // what raw x/y already means), not a coordinate-space TRANSFORM. The
    // actual "convert from this viewport into the loaded movie's own stage-
    // pixel space" math lives in MovieClipInstance (see its
    // stageMouseX()/stageMouseY()), the one place that already knows both
    // the movie's frameSize AND is where _xmouse/_ymouse are read from —
    // mirroring SceneRenderer's own stage<->device-pixel ratio approach
    // rather than adding a second, incompatible coordinate system here.
    //
    // Unset (0,0, the default) means "no known viewport" — every existing
    // caller that never calls setViewportSize() (every test predating this,
    // the desktop CLI) gets IDENTITY behavior: raw x/y is treated as
    // already being in stage-pixel space, exactly matching this class's
    // pre-existing (pre-2026-08-18) behavior.
    void setViewportSize(double widthPixels, double heightPixels);
    double viewportWidth() const { return viewportWidth_; }
    double viewportHeight() const { return viewportHeight_; }

    // --- well-known AS2 Key.* constants (documented confidence — see file
    // header) — exposed here so both InputState's own callers and
    // ScriptEnvironment's native Key object (which reads them to populate
    // Key.BACKSPACE etc.) share one definition. -------------------------
    enum KeyCode : int {
        kBackspace = 8,
        kTab = 9,
        kEnter = 13,
        kShift = 16,
        kControl = 17,
        kAlt = 18,
        kCapsLock = 20,
        kEscape = 27,
        kSpace = 32,
        kPageUp = 33,
        kPageDown = 34,
        kEnd = 35,
        kHome = 36,
        kLeft = 37,
        kUp = 38,
        kRight = 39,
        kDown = 40,
        kInsert = 45,
        kDelete = 46,
    };

private:
    std::unordered_set<int> keysDown_;
    int lastKeyCode_ = 0;

    double mouseX_ = 0.0;
    double mouseY_ = 0.0;
    bool mouseDown_ = false;

    double viewportWidth_ = 0.0;
    double viewportHeight_ = 0.0;
};

}  // namespace flash3ds::runtime
