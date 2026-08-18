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

## Interactivity phase (2026-08-18) — coordinate-space finding

Tracing the full input pipeline for the interactivity phase
(`docs/interactivity-audit.md` §1-2 has the complete trace) surfaced a
real, previously-undocumented, **not yet exercised** gap:
`Nintendo3DSInput`'s `screenWidth_`/`screenHeight_` (the space
`InputState::mouseX()`/`mouseY()` end up in) is the DEVICE/SCREEN's own
pixel dimensions (e.g. 400x240 top / 320x240 bottom, per
`nintendo3ds_main.cpp`'s construction), **not** the loaded movie's own
stage pixel dimensions (`Movie::frameSize`, e.g. 600x450 for `hobo.swf`).
`_xmouse`/`_ymouse` (`MovieClipInstance.cpp:357-358,511-512`) read
`InputState` directly with **no stage-scaling applied at all**. Whenever a
movie's stage size doesn't exactly match the rendering screen's pixel
size, `_xmouse`/`_ymouse` — and any future hit-testing built directly on
`InputState`'s raw values — would be wrong by exactly that scale factor.
Not caught by any existing test (no test in this project has ever set
`InputState`'s mouse position while also rendering a non-1:1-scaled
stage). Flagged, not fixed this turn — the fix belongs in a new
device-px -> stage-twips conversion layer, tracked as the next item in
`docs/interactivity-audit.md` §8 and designed for in `docs/hit-testing.md`.
