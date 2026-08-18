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
//
// This file is only compiled for the 3DS target (guarded by __3DS__).

#pragma once

#ifndef __3DS__
#error "Nintendo3DSInput.h is only valid in a 3DS cross-compile (__3DS__ not defined)"
#endif

#include <3ds.h>

#include "runtime/InputState.h"

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
    Nintendo3DSInput(int screenWidthPixels, int screenHeightPixels);

    // Call once per frame, after hidScanInput(), before reading
    // InputState. Updates `state` in place.
    void poll(runtime::InputState& state);

private:
    int screenWidth_;
    int screenHeight_;
    // Raw (untransformed) touch-panel logical resolution, per libctru
    // convention (320x240) — used only if the caller's screen dimensions
    // differ, to rescale touch::px/py into the caller's space.
    static constexpr int kRawTouchWidth = 320;
    static constexpr int kRawTouchHeight = 240;
};

}  // namespace flash3ds::platform
