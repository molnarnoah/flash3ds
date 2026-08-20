// Nintendo3DSInput.h
//
// Phase 10 — Nintendo 3DS backend. Polls libctru's hid service once per
// frame and feeds the result into a platform-independent InputState
// (src/runtime/InputState.h), the same seam AVM1's Key/Mouse built-ins and
// _xmouse/_ymouse already read from (Phase 6) — so runtime/ and avm1/ need
// no 3DS-specific code at all.
//
// Mapping decisions (documented here since they're judgment calls, not
// spec-mandated):
//   - Touch screen (bottom screen digitizer) drives the AS2 mouse: touch
//     position -> _xmouse/_ymouse, touch-held -> Mouse "down". The 3DS has
//     no cursor-based pointer, so this is the closest analog available —
//     it means mouse coordinates are only meaningful/updated while the
//     bottom screen is actually being touched, which matches how most
//     homebrew ports of pointer-driven content behave.
//   - D-Pad and the face buttons (A/B/X/Y) map to InputState::KeyCode's
//     arrow keys and a few printable-key stand-ins (documented per-button
//     below) so AS2 content using Key.isDown() against common codes has
//     something to bind to. This mapping is a reasonable-effort convention
//     for a control scheme AS2/Flash was never designed for, not a
//     spec-derived fact — content-specific remapping will likely be needed
//     per game once real 3DS content is tested (not done in this session;
//     see docs/3ds-toolchain.md and docs/input.md).
//   - START maps to Key.ENTER (13), SELECT to Key.ESCAPE (27) as
//     reasonable-effort conventions.
//   - L/R shoulder buttons (input-transitions phase, 2026-08-19) map to
//     printable ASCII codes 'L'/'R' — same reasonable-effort convention as
//     X/Y above (no natural AS2 Key.* equivalent exists for a shoulder
//     button either). Added specifically so all of A/B/X/Y/L/R/START/
//     SELECT/D-Pad have SOME InputState key code to test edge detection
//     against — previously L/R weren't fed into InputState at all.
//
// Virtual Console resource layer (2026-08-19): every mapping above is now
// CONFIGURABLE via vc::InputMapping (src/vc/GameConfig.h), sourced from a
// packaged game's config.ini [input]/[touch]/[mouse] sections — see
// docs/virtual-console.md. This class still owns 100% of the actual
// libctru hid polling/rescaling logic unchanged; the only thing that
// became a constructor parameter is WHICH InputState key code each
// physical button maps to, and whether touch/mouse are fed into
// InputState at all. The mapping struct's own default values exactly
// match this file's ORIGINAL hardcoded behavior for D-Pad/A/B/START/
// SELECT (Key.LEFT/RIGHT/UP/DOWN, Key.ENTER, Key.ESCAPE), touch (enabled),
// and mouse (enabled) — a caller that never touches config.ini gets
// unchanged behavior for those. The one deliberate exception: X/Y's
// documented default changed from literal ASCII 'X'/'Y' to Key.SPACE/
// Key.SHIFT, to match vc::GameConfig's own documented default config.ini
// (see GameConfig.h) — AS2 content is far more likely to test
// Key.SPACE/Key.SHIFT than literal 'X'/'Y' character codes, and having
// ONE canonical default (shared by "no config.ini present" and "config.ini
// present with the documented example values") was worth this small,
// clearly-documented behavior change over preserving the old accidental
// default. L/R's default (ASCII 'L'/'R') is unchanged.
//
// Edge detection (input-transitions phase, 2026-08-19): poll() now calls
// InputState::commitFrame() as its LAST step, once per call — see that
// method's doc comment (runtime/InputState.h) for the full model. This is
// safe/correct here specifically because poll() is called exactly once per
// real hardware frame (once per hidScanInput(), from
// nintendo3ds_main.cpp's main loop) — never more, never less — so "once
// per poll()" and "once per real input sample" are the same thing on this
// platform.
//
// This file is only compiled for the 3DS target (guarded by __3DS__).

#pragma once

#ifndef __3DS__
#error "Nintendo3DSInput.h is only valid in a 3DS cross-compile (__3DS__ not defined)"
#endif

#include <3ds.h>

#include "runtime/InputState.h"
#include "vc/GameConfig.h"

namespace flash3ds::platform {

class Nintendo3DSInput {
public:
    // screenWidthPixels/screenHeightPixels: the LOGICAL (non-rotated)
    // dimensions of whichever screen is being used as the touch input
    // surface (320x240 for the bottom screen on real hardware; the current
    // nintendo3ds_main.cpp actually passes the TOP screen's 400x240, since
    // that's the screen the embedded demo renders to — see that file).
    // Used to rescale libctru's raw touch::px/py (320x240 touch-panel-
    // logical space, per libctru's own documentation) into this same
    // caller-chosen pixel space.
    //
    // Interactivity-phase fix (2026-08-18): poll() now also records these
    // dimensions on InputState via setViewportSize(), so callers no longer
    // need to pass the SWF stage's own width/height here to get correct
    // _xmouse/_ymouse values — MovieClipInstance::stageMouseX()/
    // stageMouseY() converts from THIS constructor's pixel space into
    // whatever movie is actually loaded (which may not even be known yet
    // at construction time — see docs/input.md). Just pass whatever screen
    // pixel dimensions are actually being touched/rendered to.
    // `mapping` supplies the physical-button->InputState-keycode table and
    // the touch/mouse enabled flags (see vc::InputMapping, GameConfig.h) —
    // defaults to InputMapping{}'s own default values, which reproduce
    // this class's original hardcoded behavior except for X/Y (see this
    // file's header comment for why that one default changed).
    Nintendo3DSInput(int screenWidthPixels, int screenHeightPixels,
                      const vc::InputMapping& mapping = vc::InputMapping{});

    // Call once per frame, after hidScanInput(), before reading
    // InputState. Updates `state` in place.
    void poll(runtime::InputState& state);

private:
    int screenWidth_;
    int screenHeight_;
    vc::InputMapping mapping_;
    // Raw (untransformed) touch-panel logical resolution, per libctru
    // convention (320x240) — used only if the caller's screen dimensions
    // differ, to rescale touch::px/py into the caller's space.
    static constexpr int kRawTouchWidth = 320;
    static constexpr int kRawTouchHeight = 240;
};

}  // namespace flash3ds::platform
