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

    // --- edge detection (input-transitions phase, 2026-08-19) -------------
    //
    // AUDIT FINDING this phase confirmed (see docs/input.md): the only
    // thing InputState previously tracked was "what's true RIGHT NOW" — no
    // notion of "what changed since last time" existed anywhere. On the
    // 3DS, `Nintendo3DSInput::poll()` is called exactly once per real
    // hardware frame (once per `hidScanInput()`, inside
    // `nintendo3ds_main.cpp`'s `while (aptMainLoop())` loop) — critically,
    // this is DECOUPLED from and typically more frequent than the SWF's
    // own timeline advancing (`root->advanceFrame()` is throttled to the
    // SWF's authored frame rate, e.g. 12fps against a 60Hz poll loop — see
    // that file). So "once per SWF frame" is NOT a safe definition of
    // "once per input tick"; "once per poll() call" is the only frequency
    // that's actually guaranteed to fire exactly once per real input
    // sample, on every platform (including a future desktop/test harness
    // that has no SWF-timeline concept driving it at all).
    //
    // Model: commitFrame() is an explicit, EXTRA call — separate from the
    // individual setKeyDown()/setMousePosition()/setMouseDown() calls,
    // which only ever update "current" state and never touch edges — that
    // the caller makes exactly ONCE per input tick, after every setter
    // call for that tick has run. It snapshots "current" against
    // whatever was current as of the LAST commitFrame() call ("previous"),
    // computes this tick's pressed/released transitions from that diff,
    // caches them (stable/re-readable until the NEXT commitFrame() call),
    // then updates its "previous" snapshot to match "current" for the
    // following commit. This is what keeps "poll() poll() poll()" from
    // ever producing more than one pressed/released event per actual
    // physical transition: each commitFrame() call computes edges exactly
    // once, from exactly one comparison, no matter how many individual
    // setKeyDown()/etc. calls preceded it.
    //
    // isKeyDown()/isMouseDown() are UNCHANGED and remain live/current reads
    // at all times, independent of commitFrame() — existing callers (and
    // existing tests) keep working exactly as before. isKeyPressed()/
    // isKeyReleased()/isMousePressed()/isMouseReleased() only become
    // meaningful once commitFrame() has been called at least once; before
    // that, or on a commit where nothing changed, they simply report no
    // transition (false) — there is no error state, this is deliberate and
    // matches "no edge observed yet."
    //
    // A press-then-release (or release-then-press) that both happen
    // BETWEEN two commitFrame() calls (i.e. within a single input tick, no
    // commit in between) is invisible to this model — only the LAST
    // setter call before a commit is what's diffed. This is a deliberate,
    // standard polled-input limitation (the exact same one libctru's own
    // hidKeysDown()/hidKeysUp() have, since they're likewise computed once
    // per hidScanInput() call), not a bug — see docs/input.md's edge-case
    // writeup and the matching regression test
    // (InputState_KeyPressed_VeryShortPress_WithinOneTick_IsInvisible).
    void commitFrame();

    bool isKeyPressed(int keyCode) const;
    bool isKeyReleased(int keyCode) const;

    bool isMousePressed() const { return mousePressed_; }
    bool isMouseReleased() const { return mouseReleased_; }

    // --- touch (input-transitions phase, 2026-08-19) ----------------------
    //
    // Touch is NOT a separate underlying state from mouse — by PRE-EXISTING
    // design (Phase 10 — see Nintendo3DSInput.h's own header comment:
    // "Touch screen ... drives the AS2 mouse"), Nintendo3DSInput's touch
    // handling calls setMousePosition()/setMouseDown() directly; there has
    // never been a second, parallel "touch" field in this class. Per this
    // phase's own task scoping ("if touch is currently represented
    // separately, do not unnecessarily merge the two layers yet") — since
    // it was NOT separate before this phase, these are intentionally thin,
    // documented ALIASES over the exact same mouse-down edge state, not a
    // second parallel tracking mechanism (which would risk the two
    // diverging under some future input path and silently disagreeing with
    // each other). Named distinctly anyway, matching this task's own
    // vocabulary, so call sites that conceptually mean "touch" can say so.
    bool isTouchDown() const { return isMouseDown(); }
    bool isTouchPressed() const { return isMousePressed(); }
    bool isTouchReleased() const { return isMouseReleased(); }

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

    // --- edge-detection state (input-transitions phase, 2026-08-19) -------
    // "previous" snapshots, updated only by commitFrame(); "pressed"/
    // "released" are this tick's computed transitions, cached until the
    // next commitFrame() call. See commitFrame()'s doc comment above.
    std::unordered_set<int> previousKeysDown_;
    std::unordered_set<int> pressedKeys_;
    std::unordered_set<int> releasedKeys_;

    bool previousMouseDown_ = false;
    bool mousePressed_ = false;
    bool mouseReleased_ = false;
};

}  // namespace flash3ds::runtime
