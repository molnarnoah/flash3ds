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
- **A/START -> `Key.ENTER`, B -> `Key.ESCAPE`** (a common confirm/cancel
  convention), **X/Y -> printable ASCII `'X'`/`'Y'`** (no natural AS2
  equivalent exists for these buttons at all). **SELECT's default changed
  (2026-08-30) from `Key.ESCAPE` to `Key.END`** — not a convention this
  time but real-content evidence: every Hobo game's frame-1
  `DefineButton2` characters gate their `condActionsV2` on
  `CondKeyPress=4` ("End"), confirmed to drive real root-timeline
  navigation (`docs/known-limitations.md` L11). With the old `Key.ESCAPE`
  default, physically pressing SELECT had no effect on real corpus content
  at all — see `src/vc/GameConfig.h`'s `selectKeyCode` doc comment for the
  full evidence trail.

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

## Input-transitions phase (2026-08-19) — edge-detected input state

### Audit: the actual current path, traced before any code was touched

```
Nintendo3DSInput::poll()  (once per real hardware frame -- see below)
    -> hidKeysHeld() / hidTouchRead()
    -> InputState::setKeyDown()/setMousePosition()/setMouseDown()
        -> stores "what's true RIGHT NOW" only -- no previous-state
           concept existed anywhere before this phase
    -> runtime consumers (AVM1 Key.isDown(), _xmouse/_ymouse, Mouse) read
       InputState directly, always getting the CURRENT/live value
```

Findings, confirmed by reading the actual code (not assumed):

- **Button state storage (before this phase):** a single
  `std::unordered_set<int> keysDown_` of AS2 `Key.*`-style codes — "is this
  code currently in the set," nothing else. No previous-frame snapshot
  existed anywhere in `InputState`.
- **When `poll()` occurs:** exactly once per iteration of
  `nintendo3ds_main.cpp`'s `while (aptMainLoop())` loop, immediately after
  `hidScanInput()` — i.e. once per REAL hardware vblank (~60Hz).
- **When the runtime advances a (SWF) frame:** `root->advanceFrame()` is
  throttled to the movie's own authored frame rate (`vblanksPerSwfFrame`,
  e.g. 5 for a 12fps SWF at 60Hz) — **decoupled from and typically much
  less frequent than `poll()`**. This means several `poll()` calls
  normally happen between two `root->advanceFrame()` calls, not the
  reverse — a critical fact for choosing where edge detection lives (see
  below).
- **Can multiple polls occur per runtime tick?** Yes, routinely (the
  reverse of what might be assumed) — `poll()` runs every real frame
  regardless of whether the SWF's own timeline ticks that iteration.
- **Touch representation:** NOT a separate field from mouse. Touch was
  already, by Phase 10's own original design, wired directly into
  `setMousePosition()`/`setMouseDown()` — there has never been a second
  "touch" state anywhere in `InputState`. Confirmed by reading
  `Nintendo3DSInput::poll()`'s touch-handling branch directly.
- **Mouse representation:** `mouseX_`/`mouseY_`/`mouseDown_`, all plain
  fields, same "current only" limitation as keys.
- **Existing native edge source:** libctru itself already computes
  `hidKeysDown()`/`hidKeysUp()` once per `hidScanInput()` call —
  `nintendo3ds_main.cpp` already uses `hidKeysDown()` directly (bypassing
  `InputState` entirely) for its A/B/X/Y test-tone triggers. This is NOT
  reused for the fix below, deliberately: it only exists on the 3DS, and
  the desktop test suite (which has no `hid` layer at all) needs the exact
  same press/release semantics to be unit-testable — so `InputState` needed
  its own, fully platform-independent diffing mechanism regardless.

### Model chosen: explicit `commitFrame()`, diffed once per call

`InputState::commitFrame()` is a new, explicit method — separate from
every `setKeyDown()`/`setMousePosition()`/`setMouseDown()` call, which
still only ever update "current" state and never compute anything.
`Nintendo3DSInput::poll()` calls it exactly once, as its LAST step, after
every other setter for that tick has already run.

```
poll() call N:
    setKeyDown()/setMousePosition()/setMouseDown() (any number of calls,
        any order -- these never touch edges)
    commitFrame()   <-- exactly once, last
        diffs current keysDown_/mouseDown_ against the snapshot saved by
        commit N-1, computes this tick's pressed/released sets, caches
        them (stable until commit N+1), then saves current as the new
        snapshot for commit N+1's diff
```

Because `poll()` is called exactly once per real hardware frame (confirmed
above — never more, never less), and `commitFrame()` is called exactly
once per `poll()` call, "once per commit" and "once per real input sample"
are the same thing — this is what prevents "poll() poll() poll()" from
ever producing more than one pressed/released event per actual physical
transition. `isKeyDown()`/`isMouseDown()` were left completely untouched
(still always live/current reads) so every pre-existing caller and test
keeps working exactly as before.

### New APIs (existing `isKeyDown()`/`isMouseDown()`/`mouseX()`/`mouseY()`
unchanged)

| Method | Meaning |
|---|---|
| `commitFrame()` | Computes this tick's edges from current-vs-previous; call once per input tick |
| `isKeyPressed(keyCode)` | True only on the commit where `keyCode` transitioned UP->DOWN |
| `isKeyReleased(keyCode)` | True only on the commit where `keyCode` transitioned DOWN->UP |
| `isMousePressed()` / `isMouseReleased()` | Same, for `mouseDown_` |
| `isTouchDown()` / `isTouchPressed()` / `isTouchReleased()` | Thin, documented ALIASES over `isMouseDown()`/`isMousePressed()`/`isMouseReleased()` — touch and mouse remain the same underlying state (see "Touch representation" above); not a second parallel tracking mechanism |

`Nintendo3DSInput` also gained L/R shoulder-button mapping (`'L'`/`'R'`
ASCII codes, same reasonable-effort convention as the existing X/Y ->
`'X'`/`'Y'` mapping) — previously L/R weren't fed into `InputState` at
all, so all of A/B/X/Y/L/R/START/SELECT/D-Pad now have some testable
`InputState` key code.

### Exact semantics / documented behavior for edge cases

- **Before the first `commitFrame()` ever, or on a commit where nothing
  changed:** `isKeyPressed()`/`isKeyReleased()`/`isMousePressed()`/
  `isMouseReleased()` simply report `false` — "no transition observed,"
  not an error state.
- **A press-then-release (or release-then-press) entirely BETWEEN two
  `commitFrame()` calls is invisible.** Only the state as of the last
  setter call before a commit is what gets diffed. This is a deliberate,
  standard polled-input limitation — the exact same one libctru's own
  `hidKeysDown()`/`hidKeysUp()` have (both computed once per
  `hidScanInput()`) — not a bug introduced by this design.
  Test: `InputState_KeyEdge_VeryShortPress_WithinOneTick_IsInvisible`.
- **A commit with no setter calls at all before it** (simulating a
  "missed"/unavailable poll) produces no spurious edges — state simply
  stays whatever it was last committed as.
  Test: `InputState_KeyEdge_NoSetterCallsBetweenCommits_NoSpuriousEdges`.
- **Aliased key codes** (a real, pre-existing Phase 10 design decision, not
  something new to this phase): `Nintendo3DSInput` maps BOTH the A button
  and the START button onto the same `InputState::kEnter` code (and B/
  SELECT onto `kEscape`). If A is already held and START also goes down,
  `kEnter` was already `true`, so no new press edge fires for the second
  physical button — this is CORRECT behavior for key-CODE-level edge
  detection; the lossy many-to-one physical-button -> AS2-key mapping
  upstream is what collapses the two, and redesigning that mapping is out
  of scope for this phase (flagged, not fixed).
  Test: `InputState_KeyEdge_AliasedKeyCodes_SecondButtonProducesNoNewEdge`.
- **Simultaneous different buttons/touch** both get correctly independent
  edges in the same commit (keys and mouse/touch are entirely separate
  storage, diffed independently).
- **Touch coordinate updates are independent of the edge transition** —
  `isTouchPressed()`/`isTouchDown()`/`isTouchReleased()` never gate or
  affect what `mouseX()`/`mouseY()` report, and vice versa; a touch can
  move every tick while held with the edge state correctly staying
  "held, not pressed" throughout.

### What this phase deliberately does NOT do

Per explicit task scope: no hit-testing, no `Button2`/`ButtonDef` state
machine, no mouse/keyboard event dispatch (`Button.onPress`, `onClipEvent
(mouseDown)`, rollover/rollout, etc.) were implemented. This phase only
provides the low-level `isPressed()`/`isReleased()` primitive those
systems will need — see `docs/known-limitations.md`'s Sub-fix 3/N writeup
for the full STEP 1-10 record and `docs/interactivity-audit.md` §8 for the
remaining dependency chain.
