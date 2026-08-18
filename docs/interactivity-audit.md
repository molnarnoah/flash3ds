# Interactivity Audit — Mouse/Touch, Hit Testing, Buttons, MovieClip Events

**Interactivity phase (2026-08-18), STEP 1 deliverable.** Traced against
the actual current source (not assumed from prior docs) before any code was
changed. The eight required parts follow, in order; each cites file/line.
Priority #2's "select the smallest root-cause fix" decision, and the one
fix actually implemented this turn, are at the bottom.

## 1. Current input pipeline (as it exists today, traced end to end)

```
Nintendo 3DS hid service (hidScanInput/hidKeysHeld/hidTouchRead)
    -> Nintendo3DSInput::poll()            (src/platform/Nintendo3DSInput.cpp:12-59)
    -> runtime::InputState                  (src/runtime/InputState.h/.cpp)
    -> ScriptEnvironment::inputState()      (src/runtime/MovieClipInstance.h:117-122)
    -> AVM1 `Key`/`Mouse`/`_xmouse`/`_ymouse` native reads
       (src/runtime/MovieClipInstance.cpp:40-52 Key.isDown/getCode,
        78-90 Mouse.show/hide, 357-358/511-512 _xmouse/_ymouse)
    -> [ END — nothing downstream of this ]
```

**Confirmed: the pipeline stops at raw AS2-readable mouse position/button
state.** There is no hit-testing stage, no `DisplayObject`/Button/MovieClip
event dispatch stage, and no `RuntimeEventDispatcher`-shaped mechanism of
any kind — `InputState` is purely a passive data bag any AS2 script can
poll (`Key.isDown()`, `_xmouse`), never something that actively drives
events INTO scripts. The only thing in this codebase that actively reacts
to `InputState` on its own initiative (rather than being polled) is
`ScriptEnvironment::updateDrag()` (`MovieClipInstance.cpp:282-300`, called
once per tick from the root's own `advanceFrame()`) — StartDrag/EndDrag's
real implementation, unrelated to hit-testing/button dispatch.

**Input structures:** `InputState` (`src/runtime/InputState.h`) —
`keysDown_` (`unordered_set<int>`), `lastKeyCode_`, `mouseX_`/`mouseY_`
(`double`, already in STAGE PIXELS — see §4), `mouseDown_` (`bool`). No
history of the PREVIOUS tick's state is kept anywhere — `mouseDown_` is
simply "is it down right now," with no way to detect a press-this-tick vs.
was-already-down-last-tick transition. **This is a concrete, confirmed gap
for button/mouse EVENT semantics** (press/release/rollOver/rollOut are all
inherently edge-triggered — "state changed since last tick" — not
level-triggered), separate from hit-testing itself.

**Screen selection:** none exists. `Nintendo3DSInput` is instantiated once
per screen-owning renderer context by the caller (`nintendo3ds_main.cpp`),
but there's no concept anywhere of "this input event targets the top movie
vs. the bottom movie" — see §13 of the task spec, not addressed by this
audit turn (out of scope — no top/bottom dual-SWF wrapper exists yet
either, per `docs/architecture.md`).

**Touch coordinates:** `Nintendo3DSInput.cpp:19-29` reads `hidTouchRead()`
(`touch.px`/`touch.py`, native 3DS bottom-screen pixel space, 320x240)
rescaled to `screenWidth_`/`screenHeight_` (constructor-provided, matching
`InputState`'s target coordinate space) if they differ from the raw
320x240 — see §4 for what that target space actually is.

**Button state transitions:** none tracked. `setKeyDown(code, down)` just
inserts/erases from a set every poll — no edge detection (see above).

## 2. Current coordinate system

Traced by reading `SceneRenderer.cpp`, `Nintendo3DSInput.cpp`, and
`InputState.h`/`.cpp` directly:

- **SWF/stage/local/world coordinates**: twips (1/20 px), per
  `swf::Matrix`/`swf::Rect` — this is the ONLY coordinate space any SWF
  parsing/AVM1/`MovieClipInstance` code ever works in internally. World
  transform composition is `swf::concatMatrix` (parent-then-child), exactly
  as already documented in `docs/renderer.md`.
- **Renderer/device coordinates**: pixels. `SceneRenderer::render()`
  computes `pixelsPerTwipX`/`pixelsPerTwipY` from `movie_->frameSize`
  (stage twips) and the caller-supplied `outputWidthPixels`/
  `outputHeightPixels` (`SceneRenderer.cpp:48-55`), then every leaf-render
  call does `worldTwips -> applyMatrix -> twipsToDevice` (device px) — a
  ONE-WAY conversion (stage twips -> device px). **There is no inverse
  (device px -> stage twips) conversion function anywhere in the codebase**
  — a real, confirmed gap for hit-testing, which fundamentally needs "given
  a screen-space touch point, what stage-space point does it correspond
  to."
- **3DS touch/screen coordinates**: `Nintendo3DSInput` is constructed with
  `screenWidth_`/`screenHeight_` (whatever the caller passes —
  `nintendo3ds_main.cpp` passes each screen's actual pixel dimensions, 400x240
  top / 320x240 bottom) and rescales raw touch px into THAT space
  (`Nintendo3DSInput.cpp:24-27`). Critically: **`InputState::mouseX()`/
  `mouseY()` end up in DEVICE/SCREEN pixel space, not stage/SWF pixel
  space** — and `_xmouse`/`_ymouse` (AS2-visible) read `InputState` directly
  with NO stage-scaling applied (`MovieClipInstance.cpp:357-358,511-512`).
  **This is a real, confirmed bug-in-waiting, not yet exercised**: if the
  3DS screen's pixel dimensions ever differ from the movie's own stage
  pixel dimensions (`movie_->frameSize`, which for `hobo.swf` is
  600x450 — LARGER than either 3DS screen), `_xmouse`/`_ymouse` (and any
  future hit-testing built directly on `InputState`'s raw values) would be
  wrong by exactly that scale factor. Not discovered by any prior test
  because no test/desktop harness in this project has ever set
  `InputState`'s mouse position while also rendering a non-1:1-scaled
  stage.

**FIXED in a later turn this same phase** (2026-08-18): see
`docs/known-limitations.md`'s "Sub-fix 2/N" and `docs/input.md`'s
corrected-flow section for the full writeup.
`InputState::setViewportSize()`/`viewportWidth()`/`viewportHeight()` now
record what pixel space `mouseX()`/`mouseY()` are actually in;
`MovieClipInstance::stageMouseX()`/`stageMouseY()` (the new `_xmouse`/
`_ymouse` implementation) convert into the loaded movie's own
`frameSize`-derived stage-pixel space using the same ratio
`SceneRenderer::render()` already uses for the forward (stage-twips ->
device-pixel) direction. Top-left/bottom-right mapping accuracy (flagged
below as "not verified this phase") is now covered by dedicated regression
tests (`MovieClipInstance_XMouse_TopLeftViewportCorner_MapsToStageOrigin`/
`_BottomRightViewportCorner_MapsToStageWidthHeight`,
`tests/test_movieclip_instance.cpp`).

## 3. Current `DefineButton2` implementation

Already ground-truth-audited in the prior compatibility-audit phase (see
`docs/compatibility-matrix.md` §2, §5) and re-confirmed unchanged this
phase:

- **Parsing**: `swf::parseDefineButton()` (`src/swf/DefineButtonTag.cpp`)
  parses `BUTTONRECORD`/`BUTTONRECORD2` lists (state flags Up/Over/Down/
  HitTest + characterId/depth/matrix, plus a per-record `CXFORMWITHALPHA`
  for v2) and, for v2, a `BUTTONCONDACTION` list (`condActionsV2` — 9-bit
  condition bitmask covering every state transition, optional key-press
  trigger, raw AVM1 action bytes). `DefineButtonTag.h:52-62`'s
  `ButtonCondition` enum already documents all 9 transition bits
  (`kIdleToOverUp`, `kOverUpToIdle`, `kOverUpToOverDown`,
  `kOverDownToOverUp`, `kOverDownToOutDown`, `kOutDownToOverDown`,
  `kOutDownToIdle`, `kIdleToOverDown`, `kOverDownToIdle`) — this is a
  COMPLETE, ready-to-use transition table; nothing needs to be added to
  parse a condition mask, only something to DRIVE it from real state
  transitions.
- **Hit-state geometry**: `stateHitTest` records exist and are parsed
  (`ButtonRecordDef::stateHitTest`), but — until this turn's fix — nothing
  ever computed usable BOUNDS from them (see §5/§6). As of this turn,
  `characterOwnBoundsRect()` (`MovieClipInstance.cpp`, new) DOES resolve
  hit-test-state bounds for `_width`/`_height` purposes when a button is a
  placed leaf character — but this is bounds computation, not hit testing
  itself (see §5).
- **ActionScript handlers**: `actionsV1`/`condActionsV2` are fully parsed
  and stored (see prior audit) but confirmed by exhaustive grep to never be
  read by anything outside `swf/`'s own parser and `SceneRenderer`'s
  render-only (Up-state-only) consumer.
- **Button instance creation**: **buttons have NO per-placement instance
  object at all.** Unlike sprites (which become a real, stateful
  `MovieClipInstance` child with its own scripting object), a placed button
  is just a `DisplayListEntry::characterId` resolved straight to a
  `ButtonDef` at render time (`SceneRenderer::renderCharacter`,
  `SceneRenderer.cpp:122-131`) — there is no object to hold "this button's
  CURRENT state (Up/Over/Down)," no AS2-visible scripting object for
  `button.onRelease = function() {...}` to be set ON, and no lifecycle
  (created/destroyed) tracking. **This is the single largest missing piece**
  for button interactivity — even with hit-testing and event dispatch,
  there is currently no object in this codebase a button event could be
  dispatched AGAINST, nor state (Up/Over/Down) it could be dispatched FROM.

## 4. Current hit-testing implementation

**None exists.** Confirmed by codebase-wide grep for "hitTest"/"hit test"/
"pointInRect"/"containsPoint" — zero matches anywhere in `src/` before this
turn. `docs/hit-testing.md` (new this turn) documents the planned design,
reusing the bounds machinery this turn's fix introduced
(`swf::transformRect`, `MovieClipInstance::computeBoundsInOwnSpace()`) —
see that doc. Not implemented this turn; see the priority decision below.

## 5. Current `_width`/`_height` implementation

**Fixed this turn.** Before: hardcoded to `0` in two places
(`MovieClipInstance.cpp`'s `handleNativeGet()` and `MovieClipHostBindings::
getProperty()`'s index 8/9 cases). Now: `MovieClipInstance::width()`/
`height()`, backed by a new recursive `computeBoundsInOwnSpace()` that
unions every currently-placed leaf character's own bounds (`ShapeDef::
bounds`/`TextDef::bounds`/`EditTextDef::bounds` — all already parsed,
untouched by this fix — and, for buttons, a union of `stateHitTest`
records' referenced-character bounds, or `stateUp` records as a fallback)
and every nested `MovieClipInstance` child's own recursively-computed
bounds, each transformed through the relevant placement/local matrix via a
new `swf::transformRect()` helper (properly AABB-of-4-transformed-corners,
not a naive per-field remap — so rotation/skew correctly grows the
reported box). See `docs/known-limitations.md`'s STEP 1-10 writeup for
this fix's full detail.

## 6. Current event representation

**No event type/object exists anywhere in this codebase.** There is no
`InputEvent`, `MouseEvent`, or similar struct/class. The closest existing
analog is `swf::ClipActionRecord` (`src/swf/PlaceObjectTag.h`) — a purely
DATA record (event-flag bitmask + raw AVM1 action bytes), not a runtime
event object; and `runClipEvent(swf::ClipEventFlag flag)`
(`MovieClipInstance.cpp:655-661`) — a direct "look up matching
`ClipActionRecord`s and run their bytecode" function, not a generic
dispatcher. There is no equivalent representation at all for a button
state-transition (`ButtonCondAction`'s `conditions` bitmask is likewise
pure data, never read outside the `swf::`/`SceneRenderer` files).

## 7. Current `onClipEvent()` implementation

See `docs/onclipevent-compatibility.md` (new this turn) for the full
19-flag table. Summary: `swf::ClipEventFlag` defines all 19 documented
bits; `PlaceObject2`'s `ClipActionRecord` parsing captures all 19 correctly
into `clipActions_`; but `runClipEvent()` is only ever CALLED with 3 of the
19 (`kLoad`, `kUnload`, `kEnterFrame`) from exactly 4 call sites
(`initializeNewlyCreated()`, `syncChildren()`, `advanceFrame()`,
`removeFromParent()`) — confirmed by exhaustive grep, unchanged from the
prior phase's audit.

## 8. Exact list of missing pieces (for mouse/touch interactivity to work at all)

In dependency order (each depends on the ones above it):

1. ~~Bounding-box geometry for any placed object (`_width`/`_height`)~~ —
   **done this turn.**
2. **Device-px -> stage-twips inverse coordinate mapping.** Does not exist
   at all (see §2). Needed before ANY hit test can run, since `InputState`'s
   mouse position is in screen/device px, not stage twips.
3. **Previous-tick input state (edge detection).** `InputState` is
   level-only; press/release/rollOver/rollOut are inherently edge-triggered.
   Needs either a "previous InputState snapshot" or a small dedicated
   press/release-tracking layer.
4. **A hit-testing routine** (screen/stage point -> which display-list
   entry, walking depth-order, respecting `_visible`, nested transforms —
   see `docs/hit-testing.md`'s design). Bounding-box-only for a first pass,
   per the task's own instruction.
5. **A per-placement Button "instance" object** — buttons currently have NO
   runtime object of their own at all (see §3) — needed to hold current
   state (Up/Over/Down), an AS2-visible scripting object
   (`onRelease`/`onPress`/etc. get SET on something), and identity across
   frames (so "was this button being pressed last tick" is answerable).
   This is architecturally the same shape of problem `MovieClipInstance`
   solved for sprites in Phase 5 — buttons currently have no Phase-5
   equivalent at all.
6. **A generic event-dispatch mechanism** (§11 of the task spec) connecting
   1-5 above to actually invoking AVM1 callbacks
   (`button.onRelease`/`onClipEvent(press)`/etc.) — see `docs/events.md`
   (new this turn, design only).
7. **`onClipEvent`'s remaining 15 mouse/key flags actually dispatched**
   (depends on 2-4 and, for the mouse ones specifically, `Press`/`Release`/
   `RollOver`/`RollOut`/`DragOver`/`DragOut` also depend on 5's button-
   equivalent concept existing for `MovieClip`s too — real Flash MovieClips
   ARE independently mouse-interactive, not just buttons).
8. **Button `on()` handler dispatch** (depends on 4, 5, 6).

## Priority decision — smallest root-cause fix, largest unlock

Per the task's explicit instruction ("select the smallest root-cause fix
that unlocks the largest amount of Flash interactivity... do not proceed
blindly"): item 1 above (`_width`/`_height`) is the correct, and only
correct, first move — literally every other item in the dependency chain
(hit-testing, button state, event dispatch, `onClipEvent` mouse flags,
button `on()` handlers) requires real bounding-box geometry to exist first,
and before this turn it flatly did not (hardcoded `0`). **This is the fix
implemented this turn** — see `docs/known-limitations.md` for the full
STEP 1-10 writeup, `tests/test_movieclip_instance.cpp`'s 5 new
`MovieClipInstance_Width*`/`*Height*` tests for regression coverage, and
this doc's §5 above for the implementation summary.

**Explicitly NOT done this turn** (each needs its own repro-fix-test-
regression cycle per the audit charter, not batched together): the
device-px -> stage-twips inverse mapping, edge-detected input state,
bounding-box hit-testing itself, the button-instance object, the generic
event dispatcher, and all `onClipEvent`/button-`on()` dispatch. These are
the next items in the dependency chain, roughly in the order listed in §8
above — item 2 (coordinate mapping) is the natural next pick, being small,
isolated, and itself a hard blocker for item 4.
