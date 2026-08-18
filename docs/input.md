# Input

**Status: Phase 6 built `InputState` (host-settable keyboard/mouse bag)
backing `Key.isDown()`/`_xmouse`/`_ymouse`/`StartDrag`/`EndDrag`. Phase 10
added `Nintendo3DSInput`, a real libctru `hid`-polling implementation that
feeds it.**

`InputState` (`src/runtime/InputState.h/.cpp`) is deliberately a plain
host-settable data struct, not an abstract interface like `IRenderer`/
`IAudioBackend` — see that file's header comment for why that's the right
shape specifically for input (there's no meaningful "input backend"
polymorphism the way there is for pixel output or sound playback; every
platform just needs to set the same handful of fields once per tick).
`docs/shift-dx-behavior.md`'s "Input mapping" section confirms Shift-DX
routed 3DS touch through the same code path as Flash "mouse" events — the
same design this project uses.

## Phase 10 — Nintendo3DSInput

`src/platform/Nintendo3DSInput.h/.cpp` polls libctru's `hid` service once
per frame (`hidKeysHeld()`/`hidTouchRead()`) and writes the result into an
`InputState`. Mapping decisions (all documented, judgment-call
conventions, not spec-derived facts, since AS2/Flash's Key/Mouse model was
never designed for a handheld's button layout):

- **Touch screen -> mouse.** Touch position feeds `_xmouse`/`_ymouse`;
  touch-held feeds `Mouse` "down" state. This is the closest 3DS analog to
  a pointer device, but means mouse coordinates only update while the
  bottom screen is actually being touched (there is no cursor-based
  pointer on real hardware). One specific detail is flagged as an open
  question rather than resolved with certainty: libctru's own `hid.h`
  documents `KEY_TOUCH` as "Not actually provided by HID" — this
  implementation currently gates touch reads on that bit anyway, since no
  hardware/emulator was available in this session to observe the actual
  runtime behavior and confirm or replace that approach.
- **D-Pad / Circle Pad -> arrow keys** (`Key.LEFT`/`RIGHT`/`UP`/`DOWN`),
  merging both input methods per libctru's own `KEY_LEFT`/etc. convenience
  masks (each already ORs the D-Pad and Circle Pad bits together).
- **A/START -> `Key.ENTER`, B/SELECT -> `Key.ESCAPE`** (a common
  confirm/cancel convention), **X/Y -> printable ASCII `'X'`/`'Y'`** (no
  natural AS2 equivalent exists for these buttons at all).

The user confirmed the Phase 10 `.3dsx` boots and runs in Azahar (a
Citra-based 3DS emulator); this specific mapping's real-input behavior has
not been separately reported.

## Bottom-screen button/circle-pad/touch test picture

`nintendo3ds_main.cpp`'s `drawButtonTestScreen()` (see `docs/renderer.md`'s
matching section for the drawing-side details) draws a live diagnostic
picture on the bottom screen every frame: a box per D-Pad direction (the
raw `KEY_DUP`/`KEY_DDOWN`/`KEY_DLEFT`/`KEY_DRIGHT` bits, deliberately NOT
the merged `KEY_LEFT`/etc. aliases `InputState`'s own mapping uses, so the
Circle Pad and D-Pad can be told apart on screen), face button, shoulder
button, and Start/Select, each filling bright green while held; a bounding
box + offset dot for the Circle Pad driven by raw `hidCircleRead()`
(independent of `InputState`'s D-Pad-merged `Key.LEFT`/etc — this is a
genuinely separate, more precise analog readout); and a dot at the current
touch position via raw `hidTouchRead()`.

This is a real, exercised hardware-input smoke test — distinct from (and
running alongside) `Nintendo3DSInput`'s own `InputState`-feeding poll, which
continues to drive the embedded SWF's AS2 `Key`/`Mouse` side exactly as
before. Both read from the same underlying `hidScanInput()` snapshot each
frame without interfering with each other.

**Quit control changed:** START alone used to quit the app; it now takes
**START+SELECT held together** (the standard homebrew convention) instead,
specifically so START's own indicator box is visible/testable on the
button-test screen rather than exiting the instant it's pressed.

None of this has been exercised against real 3DS button/touch/circle-pad
input on a physical console in this session (no hardware was available,
only Azahar) — it's real code that compiles and links against libctru (see
[3ds-toolchain.md](3ds-toolchain.md)), with a documented, reasonable-effort
mapping a target title can override if its own control needs differ.

## Interactivity phase (2026-08-18) — coordinate-space finding, then fix

Tracing the full input pipeline for the interactivity phase
(`docs/interactivity-audit.md` §1-2 has the complete trace) surfaced a
real, previously-undocumented gap: `Nintendo3DSInput`'s
`screenWidth_`/`screenHeight_` (the space `InputState::mouseX()`/
`mouseY()` end up in) is the DEVICE/SCREEN's own pixel dimensions (e.g.
400x240 top / 320x240 bottom, per `nintendo3ds_main.cpp`'s construction),
**not** the loaded movie's own stage pixel dimensions (`Movie::frameSize`,
e.g. 600x450 for `hobo.swf`). `_xmouse`/`_ymouse` read `InputState`
directly with **no stage-scaling applied at all**. Flagged first, then
fixed in the same phase — see below.

### Old (incorrect) flow

```
raw touch::px/py (320x240 libctru panel space)
  -> Nintendo3DSInput::poll() rescales by screenWidth_/screenHeight_
     (e.g. 400x240, the TOP screen's logical size)
  -> InputState::setMousePosition(x, y)   [now in 400x240 "screen" space]
  -> AS2 _xmouse/_ymouse read InputState::mouseX()/mouseY() DIRECTLY
     [WRONG whenever the movie's stage size != 400x240 — e.g. hobo.swf's
      600x450 stage would report coordinates squeezed into a 400x240
      range, off by a factor of 1.5x/1.875x]
```

### Corrected flow

```
raw touch::px/py (320x240 libctru panel space)
  -> Nintendo3DSInput::poll() rescales by screenWidth_/screenHeight_
     (unchanged — still whatever pixel space the caller constructed it
     with, e.g. 400x240)
  -> InputState::setMousePosition(x, y)              [raw viewport-pixel space]
  -> InputState::setViewportSize(screenWidth_, screenHeight_)  [NEW — records
     what pixel space the value above is actually in]
  -> AS2 _xmouse/_ymouse now call MovieClipInstance::stageMouseX()/
     stageMouseY(), which read BOTH InputState::mouseX()/mouseY() AND
     InputState::viewportWidth()/viewportHeight(), then scale into the
     ACTUAL LOADED MOVIE's own stage-pixel space using movie_->frameSize:

         stageX = rawX * (movie_->frameSize.widthPixels()  / viewportWidth)
         stageY = rawY * (movie_->frameSize.heightPixels() / viewportHeight)

     [CORRECT — reports coordinates in the same stage-pixel space _x/_y/
      _width/_height already use, matching real Flash Player's _xmouse/
      _ymouse contract]
```

This mirrors `SceneRenderer::render()`'s own `pixelsPerTwipX`/
`pixelsPerTwipY` stage<->device-pixel ratio (see `docs/renderer.md`) —
same non-uniform (independent X/Y), no-offset stretch-to-fill mapping,
just inverted and expressed in stage pixels rather than device pixels.
Deliberately reused rather than reinvented, per this fix's own scoping
task.

### Exact coordinate-space assumptions

- **No Y-axis flip.** Both SWF stage Y and 3DS device/touch pixel Y
  increase downward — confirmed against `SceneRenderer::twipsToDevice()`,
  which applies no flip either.
- **No offset/letterboxing/pillarboxing.** `SceneRenderer.cpp` has no such
  logic anywhere (confirmed) — a plain independent-axis stretch-to-fill —
  so the inverse conversion here adds none either. A raw (0,0) always maps
  to stage (0,0) exactly.
- **Non-uniform (independent X/Y) scaling.** X and Y each use their own
  ratio; a 600x450 stage against a 400x240 viewport scales X by 1.5x and Y
  by 1.875x independently, not a single shared aspect-locked factor.
- **Backward-compatible default.** If `InputState::setViewportSize()` is
  never called (every test predating this fix, and the desktop CLI, which
  has no input backend at all), `viewportWidth()`/`viewportHeight()` stay
  at their `0.0` default and `stageMouseX()`/`stageMouseY()` fall back to
  returning the raw value unscaled — i.e. "stage size == input viewport"
  is the implicit assumption whenever no viewport is known, exactly
  preserving pre-fix behavior for every existing caller.
- **Units are pixels throughout**, matching `_x`/`_y`/`_width`/`_height`'s
  existing pixel-valued AS2 contract (not twips) — `movie_->frameSize`'s
  `widthPixels()`/`heightPixels()` helpers (twips/20.0) are used, the same
  helpers `width()`/`height()` already use.
- **`movie_` is the one true top-level Movie for every instance in the
  tree** (root and every descendant share the same pointer — see
  `createRoot()`/`syncChildren()`/`cloneSprite()`), so `stageMouseX()`/
  `stageMouseY()` give the same answer no matter which `MovieClipInstance`
  they're called on.

### Remaining limitations

- **Single global "input viewport."** Only ONE `InputState` (and thus one
  viewport size) is tracked at a time. `nintendo3ds_main.cpp` only wires
  the TOP screen's `Nintendo3DSInput` into `env.inputState()` — the bottom
  screen's touch reading (`drawButtonTestScreen()`) is separate/diagnostic
  and never feeds AS2. If dual-screen touch-to-stage mapping is ever
  needed (e.g. a movie meant to be touched on the bottom screen), this
  single-viewport model would need extending — not attempted here, out of
  scope for this fix.
- **Still no hit-testing, button dispatch, or mouse-event dispatch** — this
  fix only corrects the raw coordinate VALUE `_xmouse`/`_ymouse` report;
  everything that would consume a correct coordinate to determine "is the
  pointer over this clip" remains unbuilt (see `docs/hit-testing.md`).
- **Never exercised against real 3DS touch hardware** — no
  hardware/emulator input access from this environment; only compile/link
  verification and desktop-side unit tests were possible (see
  `docs/test-results.md`).
- **Not yet re-derived: what the "correct" viewport for the embedded 3DS
  demo actually is.** `nintendo3ds_main.cpp` still passes `kTopWidth`/
  `kTopHeight` (400x240) to `Nintendo3DSInput`'s constructor — this fix
  makes that choice CORRECT REGARDLESS of what the embedded demo's own
  stage size happens to be (no longer needs to match), but nobody has
  verified what the embedded demo's actual authored stage dimensions are.
