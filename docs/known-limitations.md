# Known Limitations — Prioritized

**Compatibility-audit phase (2026-08-18).** Built from the ground-truth
audit behind `docs/compatibility-matrix.md`/`docs/avm1-compatibility.md`.
Five highest-impact limitations, ranked by how much they block real content
(primarily `hobo.swf`, the project's primary compatibility target — see
`docs/compatibility.md`). Each entry below states the failure-classification
taxonomy category (per the audit-phase charter: PARSER, TAG SUPPORT,
TIMELINE, DISPLAY LIST, RENDERER, AVM1, OBJECT MODEL, INPUT, AUDIO, TEXT,
FONT, EXTERNALINTERFACE, MEMORY, PERFORMANCE, 3DS PLATFORM, UNKNOWN).

## Priority #1 — ColorTransform/`_alpha` never applied to rendered pixels — **FIXED THIS PHASE**

**Classification: RENDERER.**

### STEP 1 — Audit

`MovieClipInstance` and `DisplayListEntry` both store a real, script-mutable
`swf::ColorTransform` (`_alpha` maps directly to `colorTransform_.alphaMult`
— `MovieClipInstance.h:295-296`), populated from `PlaceObject2`'s
`CXFORMWITHALPHA` field. But `SceneRenderer` — audited line by line — never
once read `colorTransform()`/`.alpha()` anywhere in its render call chain.
Confirmed by grep: zero references to color-transform/alpha in
`src/renderer/`. A prior doc (`docs/compatibility.md`, Phase 9) claimed
`hobo.swf`'s "PLAY!" button visibly "fades in by frame 5" — this looked, at
first read, like a contradiction worth resolving (either the claim was
wrong, or there's a second, undiscovered code path applying alpha).

### STEP 2-3 — Reproduction, isolation

Minimal repro: place a `DefineSprite` character, set its `_alpha` (or, at
the AVM1 layer, have a script assign `_alpha = 50`), render, and check
whether the rendered pixel color is blended toward the background or comes
out as the shape's raw, fully-opaque color. Before the fix: always fully
opaque, regardless of `_alpha`. Isolated entirely to `src/renderer/
SceneRenderer.cpp` — no other subsystem involved (the *storage* and
*script-mutability* of `_alpha` were already correct, per
`tests/test_movieclip_instance.cpp`'s existing property tests).

### STEP 4-5 — Fix

Added `swf::concatColorTransform(parent, child)` (`src/swf/SwfRecords.h/
.cpp`) — composes color transforms down the display tree, mirroring the
existing `concatMatrix`. Added `swf::applyColorTransform(color, ct)`
(`src/swf/ShapeRecords.h/.cpp`, alongside `RgbaColor`) — applies `output =
input * mult + add` per channel, clamped to `[0,255]`, with an identity
fast-path. Threaded an effective, world-composed `ColorTransform` alongside
the existing `worldMatrix` through every `SceneRenderer` render entry point
(`renderClip`/`renderCharacter`/`renderShapeCharacter`/`renderTextCharacter`/
`renderEditTextCharacter`/`renderGlyph`), applied at the exact point each
tessellated polygon/stroke/glyph color is resolved, immediately before
`IRenderer::fillPolygon`/`strokePolyline`. Composition happens once per
MovieClip child (parent cxform ∘ child's own `colorTransform()`) and once
per leaf character (parent cxform ∘ the `DisplayListEntry`'s own
`colorTransform`, populated from that placement's `CXFORMWITHALPHA` if
present) — matching exactly how `worldMatrix` composition already works.

### STEP 6 — Desktop test

Added two regression tests, `SceneRenderer_MovieClipInstanceAlpha_
BlendsRenderedColorWithBackground` and `SceneRenderer_
MovieClipInstanceAlphaZero_RendersFullyTransparent`
(`tests/test_scene_renderer.cpp`): a nested-sprite fixture's child clip has
its `_alpha` set (50% and 0% respectively) via the same C++ setter real AS2
`_alpha = x;` compiles down to, then the rendered pixel is checked against
the expected alpha-blended-with-white-background color. Both pass. Full
suite: **189/189 passing** (up from 187 before these two additions), zero
regressions.

**Independent real-content verification:** rendered `hobo.swf` frames 1-5
with the desktop CLI both before and after the fix and diffed byte-for-byte
— **100% identical output**, meaning `hobo.swf`'s frame-1-5 content never
actually exercises a non-identity `ColorTransform`/`_alpha` change on any
placed character. This means the Phase 9 doc's "PLAY! fading in by frame 5"
observation, whatever its cause, **is not attributable to ColorTransform** —
there IS a real visual difference between frame 1 and frame 5 in that
region (device px bbox ~(167,308)-(301,355), 4189 pixels changed), but it
must come from a different mechanism (most likely a different/additional
shape placed on a later frame). This is flagged as an open, not-fully-
resolved correction to the record in `docs/compatibility-matrix.md`, not
asserted as solved.

### STEP 7-8 — 3DS build

`cmake --build build_3ds` succeeds cleanly with the fix (only pre-existing,
unrelated newlib ABI-note/`_close`-not-implemented warnings, identical to
before this change). New `.3dsx` built successfully. **Not yet tested on
real hardware or Azahar this phase** — delivered to the user for that
confirmation, matching the same "code-complete, toolchain-verified, not yet
hardware-confirmed" caveat every prior 3DS-side change in this project has
carried honestly.

### STEP 9 — Regression test

Done — see STEP 6. Both new tests are permanent, run every `ctest` cycle.

### STEP 10 — What now works / what remains

**Now works:** any content using `_alpha` (script-driven fades, tweens,
damage-flash effects) or a `PlaceObject2` `CXFORMWITHALPHA` (author-placed
partial transparency/tint) renders correctly for the first time. This is a
broadly-applicable fix — alpha fades are one of the most common visual
techniques in Flash content generally, independent of any one target title.

**Remains:** the specific mechanism behind `hobo.swf`'s observed frame-1-5
visual change in the button region is still uncharacterized (see above) —
candidate follow-up, not chased further this phase per the "don't fix
multiple things at once" rule. `ColorTransform` composition itself has not
been independently cross-checked against a real Flash-authored nested-CXFORM
case beyond this project's own regression tests (same caveat class as the
AVM1 "unverified assumption" list in `docs/avm1-compatibility.md`).

---

## Priority #2 — No mouse/button interactivity (blocks all real gameplay progression)

**Interactivity phase (2026-08-18): this priority is now the SOLE focus**
per explicit user instruction — Priorities #3-5 below are deliberately on
hold until this one reaches a stable state (see `CLAUDE.md`). Full audit:
`docs/interactivity-audit.md`. First sub-fix, done this turn (STEP 1-10
below); remaining sub-items tracked in `docs/interactivity-audit.md` §8 and
NOT yet started.

### Sub-fix 1/N — `_width`/`_height` hardcoded to 0 — **FIXED THIS TURN**

**Classification: DISPLAY LIST (bounding-box geometry).**

**STEP 1 — Audit.** Confirmed (again, independently, this turn) that both
`MovieClipInstance::handleNativeGet()`'s `_width`/`_height` branch and
`MovieClipHostBindings::getProperty()`'s index 8/9 cases returned a
hardcoded `0`, unconditionally. Everything else in the interactivity
dependency chain (hit-testing, button state, event dispatch) needs real
bounding-box geometry to exist first — see `docs/interactivity-audit.md`
§8's dependency ordering — making this the correct, and only correct,
first move ("smallest root-cause fix that unlocks the largest amount of
interactivity").

**STEP 2-3 — Reproduction/isolation.** Minimal repro: place any shape
inside a named MovieClip, read `mc._width`/`mc._height` from AS2 — always
`0` regardless of the shape's actual size. Isolated entirely to
`runtime::MovieClipInstance` — no dependency on hit-testing, input, or
rendering.

**STEP 4-5 — Fix.** Added `swf::transformRect(const Matrix&, const Rect&)
-> Rect` (`src/swf/SwfRecords.h/.cpp`) — transforms a Rect's 4 corners by a
matrix and returns the true axis-aligned bounding box of the result (not a
naive per-field remap — correctly grows the box under rotation/skew). Added
`MovieClipInstance::computeBoundsInOwnSpace()` (private, recursive) —
unions every currently-placed leaf character's bounds (`ShapeDef::bounds`/
`TextDef::bounds`/`EditTextDef::bounds`, all pre-existing/untouched; for
`ButtonDef`, a new `characterOwnBoundsRect()` helper unions `stateHitTest`-
flagged records' referenced-character bounds, falling back to `stateUp`
records if no explicit hit-test state exists) and every nested
`MovieClipInstance` child's own recursively-computed bounds, each
transformed through the relevant placement/local matrix. `width()`/
`height()` (public) then transform that own-space union through the
clip's own `matrix_` and convert to pixels — matching real AS2 semantics
where `_width`/`_height` are measured in the PARENT's coordinate space
(rotating/scaling a clip changes its reported size).

**STEP 6 — Desktop tests.** Five new regression tests
(`tests/test_movieclip_instance.cpp`): a single placed shape's bounds match
exactly; `GetProperty`(8/9) and bare `.member` access both agree with the
direct C++ `width()`/`height()` call; `_xscale=200` doubles `_width` while
leaving `_height` unchanged; two shapes at different offsets union into the
full combined bounding box (proving real recursion/union, not a first-
entry-only shortcut); an empty clip returns exactly `0`, not garbage from
an uninitialized accumulator. **194/194 tests passing** (up from 189),
zero regressions.

**Independent real-content check:** rendered `hobo.swf` frame 1 before and
after this fix and diffed byte-for-byte — **100% identical** (expected:
this is a pure property-computation addition with no rendering code
touched). Confirms no rendering regression.

**STEP 7-8 — 3DS build.** `cmake --build build_3ds` succeeds cleanly (only
the same pre-existing, unrelated newlib warnings as every prior 3DS build
in this project). New `.3dsx` built (387556 bytes, up from 385528 —
confirms the new code linked in). **Not yet tested on Azahar/hardware.**

**STEP 9 — Regression tests.** Done — see STEP 6, all 5 permanent.

**STEP 10 — What now works / what remains.** `_width`/`_height` now return
real, correct-per-the-implemented-algorithm values for shapes, text,
edit-text, buttons (bounding-box approximation for the button hit-area
case), and recursively-nested MovieClips, matching real AS2 measurement
semantics (parent-space, transform-inclusive). **Remains, and is now
unblocked to start:** hit-testing itself, the device-px -> stage-twips
coordinate conversion (a separate, now-surfaced gap — see
`docs/input.md`), edge-detected input state, the per-placement Button
instance object, the generic event dispatcher, and all `onClipEvent`/
button-`on()` dispatch — see `docs/interactivity-audit.md` §8 for the full
remaining dependency chain, in order. Not independently verified against a
real Flash-authored file's exact `_width`/`_height` output this phase (no
such reference was available) — the algorithm is implemented per the
publicly-understood AS2 semantics, same confidence tier as this project's
other "believed correct, not independently cross-checked" items (see
`docs/avm1-compatibility.md`'s unverified-assumption inventory).

### Sub-fix 2/N — `_xmouse`/`_ymouse` device-pixel/stage-pixel coordinate mismatch — **FIXED THIS TURN**

**Classification: INPUT + AVM1 (coordinate-space conversion).**

**STEP 1 — Audit.** Traced the full pipeline (see `docs/input.md`'s
"Interactivity phase" section for the complete before/after diagram):
`Nintendo3DSInput::poll()` rescales raw `touchPosition` into whatever pixel
space its constructor was given (`nintendo3ds_main.cpp` passes the TOP
screen's logical 400x240, NOT the loaded movie's stage size), writes it
into `InputState` via `setMousePosition()`, and `_xmouse`/`_ymouse`
(`MovieClipInstance.cpp`'s `handleNativeGet()` bare-member path and
`MovieClipHostBindings::getProperty()` case 20/21) read that value back
with **zero conversion**. Confirmed `InputState` is deliberately
stage-agnostic (own header comment: "No AVM1/runtime dependency in either
direction") and that `SceneRenderer::render()` already has the canonical
stage-twips <-> output-viewport-pixels ratio this fix needed to mirror
(`pixelsPerTwipX`/`pixelsPerTwipY`, confirmed non-uniform, no offset/
letterboxing anywhere in `SceneRenderer.cpp`).

**STEP 2-3 — Reproduction/isolation.** Minimal repro: a 600x450-stage
movie (matching `hobo.swf`'s real dimensions), `InputState` mouse position
set to a raw viewport-pixel value, `_xmouse` read back — pre-fix, returns
the raw value unscaled regardless of stage size (verifiable with the new
`MovieClipInstance_XMouse_600x450StageDifferentViewport_ScalesNonUniformly`
test, which would fail against the pre-fix code). Isolated to
`InputState`/`MovieClipInstance` — no dependency on hit-testing, buttons,
or rendering.

**STEP 4-5 — Fix.** Added `InputState::setViewportSize(width, height)` /
`viewportWidth()`/`viewportHeight()` (`src/runtime/InputState.h/.cpp`) — a
plain size fact ("what pixel space does `mouseX()`/`mouseY()` mean"),
still no AVM1/Movie dependency, keeping `InputState`'s "dumb bag" design
intact. Added `MovieClipInstance::stageMouseX()`/`stageMouseY()`
(`src/runtime/MovieClipInstance.h/.cpp`) — the one place that already
knows both `InputState` and the movie's own `frameSize` — which scale
`InputState`'s raw value by `movie_->frameSize.widthPixels() /
viewportWidth()` (and the Y equivalent), falling back to the identity
(no scaling) whenever no viewport was ever set (every pre-existing test,
the desktop CLI). Both `_xmouse`/`_ymouse` read sites now call these
instead of `InputState::mouseX()`/`mouseY()` directly.
`Nintendo3DSInput::poll()` now also calls `state.setViewportSize
(screenWidth_, screenHeight_)` every poll, so the 3DS input path is wired
end-to-end with no further caller changes needed.

**STEP 6 — Desktop tests.** Five new regression tests
(`tests/test_movieclip_instance.cpp`): an EXPLICIT viewport matching the
stage size still yields unscaled coordinates (proving the scaling math
itself, not just the "viewport never set" shortcut the two pre-existing
`_xmouse`/`_ymouse` tests already covered and which remain untouched); a
600x450 stage against a 400x240 viewport scales X and Y by independent,
non-square factors (1.5x/1.875x); the viewport's top-left corner maps to
exact stage-space (0,0); the viewport's bottom-right corner maps to exact
stage width/height; the bare-member (`handleNativeGet`) read path gets the
same conversion as the `GetProperty` numeric-index path. **199/199 tests
passing** (up from 194), zero regressions — both pre-existing `_xmouse`/
`_ymouse` tests pass completely unchanged.

**Independent real-content check:** rendered `hobo.swf` (real stage:
600x450) frames 1-5 before and after this fix and diffed byte-for-byte —
**100% identical** (expected: this is a pure property-read change: no
rendering code was touched, and no test/CLI path calls
`setViewportSize()`, so every existing render path stays on the
identity/unscaled branch).

**STEP 7-8 — 3DS build.** `cmake --build build_3ds` succeeds cleanly (only
the same pre-existing, unrelated newlib/ABI warnings as every prior 3DS
build in this project). New `.3dsx` built (387780 bytes, up from 387556 —
confirms the new code linked in). **Not yet tested on Azahar/hardware.**

**STEP 9 — Regression tests.** Done — see STEP 6, all 5 permanent.

**STEP 10 — What now works / what remains.** `_xmouse`/`_ymouse` now
report coordinates in the loaded movie's own stage-pixel space, correctly
scaled from whatever pixel space the host's input backend reports raw
touch/mouse coordinates in — matching real Flash Player's `_xmouse`/
`_ymouse` contract and the same coordinate space `_x`/`_y`/`_width`/
`_height` already use. This is an **input-coordinate correctness fix
only** — hit-testing, `ButtonDef` instances, button state transitions,
mouse event dispatch (press/release/rollOver/rollOut), and `onClipEvent`
changes were explicitly out of scope and remain unbuilt. **Recommended
next step: edge-detected input state** (tracking press/release EDGES —
"just went down this tick" / "just went up this tick" — rather than only
the current held/not-held level `InputState` tracks today; hit-testing and
button state machines both need edge detection to fire `press`/`release`
exactly once per transition rather than every tick the mouse happens to be
down). See `docs/interactivity-audit.md` §8 for the full remaining
dependency chain.

### Sub-fix 3/N — Edge-detected input state (`isPressed`/`isReleased`) — **FIXED THIS TURN**

**Classification: INPUT (platform-independent primitive, no AVM1/Flash-layer
changes).**

**STEP 1 — Audit.** Traced the actual pipeline (`Nintendo3DSInput::poll()`
-> `InputState` -> runtime consumers) before writing any code — full
writeup in `docs/input.md`'s "Input-transitions phase" section. Key
findings, all confirmed by reading the real code rather than assumed:
`InputState` had no previous-frame concept anywhere (`keysDown_`/
`mouseDown_`/`mouseX_`/`mouseY_` were all "current only"); `poll()` runs
exactly once per real hardware frame (`hidScanInput()`, ~60Hz), which is
**decoupled from and more frequent than** the SWF's own timeline advance
(`root->advanceFrame()`, throttled to the movie's authored frame rate) —
so "once per poll()" is the correct edge-computation granularity, not
"once per SWF frame"; touch was never a separate field from mouse (already
routed into `setMousePosition()`/`setMouseDown()` directly, a pre-existing
Phase 10 design decision, not new); libctru's own `hidKeysDown()`/
`hidKeysUp()` already exist as a native edge source but aren't reusable
for `InputState`'s own edge detection because the desktop test suite has
no `hid` layer at all and needs identical semantics.

**STEP 2-3 — Reproduction/isolation.** N/A in the usual "found a bug"
sense — this phase adds a missing CAPABILITY (there was no edge detection
to reproduce a failure of), not a broken existing behavior. Isolated
design decision: an explicit `InputState::commitFrame()` method, called
once by `Nintendo3DSInput::poll()` as its last step, diffs current vs.
previous state and caches the results — see `docs/input.md` for the full
model and why "once per `commitFrame()` call" is the correct granularity
given the confirmed `poll()` call pattern.

**STEP 4-5 — Fix/implementation.** Added
`InputState::commitFrame()`/`isKeyPressed()`/`isKeyReleased()`/
`isMousePressed()`/`isMouseReleased()`/`isTouchDown()`/`isTouchPressed()`/
`isTouchReleased()` (`src/runtime/InputState.h/.cpp`) — `isKeyDown()`/
`isMouseDown()`/`mouseX()`/`mouseY()` are completely UNCHANGED (still
live/current reads at all times), preserving every existing caller's
behavior exactly, per the task's explicit requirement. Touch's edge
methods are thin, documented ALIASES over the mouse-down edge state (not
a second parallel tracking mechanism), matching how touch was already
represented before this phase. `Nintendo3DSInput::poll()` now calls
`state.commitFrame()` as its last step and also maps L/R shoulder buttons
(`'L'`/`'R'` ASCII codes, same reasonable-effort convention as the
existing X/Y mapping) — previously unmapped, needed so all of
A/B/X/Y/L/R/START/SELECT/D-Pad have some testable `InputState` code.

**STEP 6 — Desktop tests.** 18 new regression tests
(`tests/test_input_state.cpp`): the full UP/DOWN/HELD/RELEASE/RELEASED
matrix from the task spec (6 tests), 8 edge-case tests (held for many
frames, sub-tick press invisibility, release-without-another-press,
repeated press/release cycles, simultaneous different buttons,
simultaneous touch+button, no-setter-calls-between-commits, aliased key
codes), and 4 touch-specific tests (up, press-with-coordinates, held-
while-moving, release-after-movement). **217/217 tests passing** (up from
199), zero regressions — every pre-existing `_xmouse`/`_ymouse`/viewport-
conversion/`InputState_*` test passes completely unchanged.

**Independent real-content check:** rendered `hobo.swf` frames 1-5 before
and after this phase and diffed byte-for-byte — **100% identical**
(expected: this phase touches only `InputState`/`Nintendo3DSInput`,
neither of which is in the rendering or `MovieClipInstance` code path).

**STEP 7-8 — 3DS build.** `cmake --build build_3ds` succeeds cleanly (only
the same pre-existing, unrelated newlib/ABI warnings as every prior 3DS
build). New `.3dsx` built (390048 bytes, up from 387780 — confirms the new
code linked in). **Not yet tested on Azahar/hardware** — no
emulator/device access from this environment; see `docs/3ds-limitations.md`'s
new entry for the honest confidence level (desktop-verified + reasoned
about the confirmed `poll()` cadence, but a real physical press producing
exactly one edge has never actually been observed on real polling timing).

**STEP 9 — Regression tests.** Done — see STEP 6, all 18 permanent.

**STEP 10 — What now works / what remains.** `InputState` can now
distinguish UP/DOWN/PRESSED/RELEASED for both keys/buttons and
mouse/touch, computed once per input tick with no risk of "poll() poll()
poll()" producing duplicate events, and with documented, intentional
(not-a-bug) behavior for sub-tick presses, missed polls, and aliased key
codes. **This is explicitly an input-layer-only primitive** — no AVM1/
Flash-visible behavior changed at all this phase (no new AS2 API, no
`Button` dispatch, no `onClipEvent` changes — none of `_xmouse`/`_ymouse`/
`Key.isDown()`/existing button state were touched beyond what STEP 6
regression-tested as unchanged). **Recommended next step: bounding-box
hit-testing** (design already written, `docs/hit-testing.md`) — it can now
correctly consume `isMousePressed()`/`isMouseReleased()` (via `_xmouse`/
`_ymouse`'s already-correct stage coordinates from Sub-fix 2/N) to
determine exactly which tick a click began/ended on, rather than only
"is the mouse currently down," which was the last missing low-level
primitive hit-testing and button dispatch both needed.

### Sub-fix 4/N — Bounding-box hit-testing (`hitTestPoint()` primitive + AS2 `MovieClip.hitTest()`) — **FIXED THIS TURN**

**Classification: DISPLAY LIST + AVM1 (geometry query, no event dispatch).**

**STEP 1 — Audit.** `docs/hit-testing.md`'s design (written 2026-08-18,
before either blocker existed) was re-read first — its two stated
blockers (device-px -> stage-pixel coordinate conversion, edge-detected
input state) were both already resolved by Sub-fix 2/N and Sub-fix 3/N.
Confirmed the design's reusable building blocks still matched the current
code exactly: `swf::transformRect()`, `MovieClipInstance::
computeBoundsInOwnSpace()` (private), `characterOwnBoundsRect()` (leaf
bounds resolution, including its Button hit-state fallback logic) — all
present and unchanged since the `_width`/`_height` fix. Confirmed
`SceneRenderer::renderClip()`'s exact world-transform composition
(`concatMatrix(parentWorld, child.localMatrix())` per level, root's own
`matrix_` as the base, ascending/back-to-front depth order, gated on
`visible()`) to keep hit-testing's own transform composition in lockstep
with what's actually rendered.

**STEP 2-3 — Reproduction/isolation.** N/A (adding a missing capability,
not fixing a broken one — same as Sub-fix 3/N). Isolated design decision:
implement the design doc's pseudocode essentially verbatim as
`MovieClipInstance::hitTestPoint()`/`hitTestPointInOwnSpace()`, plus — a
deliberate, scoped ADDITION beyond the original design — real AS2
`MovieClip.hitTest(x, y)` (`hitTestBounds()`), since it reuses the exact
same new primitives at near-zero extra cost and is the only way any of
this becomes AS2-visible/usable this turn (see `docs/hit-testing.md`'s
"AS2 `hitTest()` vs the internal primitive" table for why these are two
genuinely different queries, not one subsuming the other).

**STEP 4-5 — Fix/implementation.** Added `swf::Point`/`transformPoint()`/
`invertMatrix()`/`rectContainsPoint()` (`src/swf/SwfRecords.h/.cpp`) —
standard 2x3 affine matrix inverse, returns `false` (not garbage) for a
degenerate/zero-determinant matrix. Added `MovieClipInstance::
worldMatrix()` (this instance's `matrix_` composed with every ancestor's,
mirroring `SceneRenderer`'s own composition exactly). Added
`MovieClipInstance::hitTestPoint(stageXPixels, stageYPixels) ->
std::optional<HitTestResult>` — the topmost-hit-under-a-point primitive:
walks a clip's display list in REVERSE (topmost-first) depth order,
recurses into `MovieClipInstance` children via their own content (not a
shortcut aggregate-bounds test), respects `visible()`, and correctly
treats a degenerate matrix anywhere in the chain as un-hit-testable. Added
`MovieClipInstance::hitTestBounds()` + AS2-visible `hitTest(x, y)`
(2-argument form; the 1-argument `hitTest(target)` and 3-argument
exact-shape forms are explicitly NOT implemented, flagged via `LOG_WARN`
rather than guessed at) via the existing OOP-callable-method dispatch
pattern (`handleNativeGet()`, same mechanism `stop()`/`getBytesLoaded()`/
`gotoAndStop()` already use).

**STEP 6 — Desktop tests.** 12 new tests in `tests/test_movieclip_instance.cpp`
(point inside/outside a shape, inclusive-boundary corners, overlapping-
shapes topmost-wins — both the topmost AND an under-only point, invisible-
clip exclusion, degenerate-`_xscale` exclusion, script-mutated-transform
awareness — a doubled `_xscale`/`_yscale` correctly changes what's
hit-testable, two-level nested-`MovieClip` recursion with composed
offsets, and 5 `hitTest()`-specific cases including the deliberate
visibility-ignoring behavior and the not-implemented 1-argument form) + 8
new tests in `tests/test_swf_records.cpp` (`invertMatrix()`/
`transformPoint()`/`rectContainsPoint()` in isolation: identity,
translate-only, scale+translate, rotation+skew round-trips, two flavors of
degenerate-matrix rejection, inclusive/exclusive boundary checks).
**237/237 tests passing** (up from 217), zero regressions.

**Independent real-content check:** rendered `hobo.swf` frames 1-5 before
and after this phase and diffed byte-for-byte — **100% identical**
(expected: this phase adds pure query APIs, touches zero rendering code).

**STEP 7-8 — 3DS build.** `cmake --build build_3ds` succeeds cleanly (only
the same pre-existing, unrelated newlib/ABI warnings as every prior 3DS
build). New `.3dsx` built (392032 bytes, up from 390048 — confirms the new
code linked in). **Not yet tested on Azahar/hardware.**

**STEP 9 — Regression tests.** Done — see STEP 6, all 20 permanent.

**STEP 10 — What now works / what remains.** Two independent, real hit-
testing capabilities now exist: an internal "what's the frontmost thing
under this point" primitive (`hitTestPoint()`, not yet consumed by
anything — ready for a future button/mouse-event-dispatch phase) and a
real, AS2-visible `MovieClip.hitTest(x, y)` that game scripts can call
directly today. **Both are pure geometry queries — no event dispatch,
`ButtonDef` state machine, `onClipEvent` changes, or press/release/
rollOver/rollOut wiring exist yet**, exactly as scoped. Bounding-box only
(exact vector-shape hit-testing remains a documented future upgrade — the
architecture change needed is isolated to one leaf-level check, per
`docs/hit-testing.md`'s "Why bounding-box, not exact shape" section, and
requires nothing else to be redesigned). `hitTest(target)` (1-arg,
compare-two-objects' -bounds form) is NOT implemented — flagged, not
guessed at.

### Sub-fix 5/N — `ButtonInstance`: a real runtime object for placed buttons — **FIXED THIS TURN**

**STEP 1 — Audit.** Traced the actual source (not inferred) end-to-end:
`swf::ButtonDef`/`ButtonRecordDef` parsing (`swf/DefineButtonTag.h`),
`CharacterDictionary`'s generic (already-working) character-variant
storage, `DisplayList`/`PlaceObjectTag`'s already-generic, character-
type-agnostic placement pipeline — all already fully correct and
untouched by this phase. The actual, confirmed gap:
`MovieClipInstance::syncChildren()` created a runtime instance ONLY for
`SpriteDef` characters (`std::holds_alternative<SpriteDef>(*def)`) —
every other type, buttons included, was skipped with no runtime object
created at all. A second confirmed finding: `characterOwnBoundsRect()`
(built for the `_width`/`_height` fix) already correctly resolved a
button's HitTest-state-preferred hit-area geometry — meaning the
requirement 5/12 worry ("what if the parser doesn't retain hit-state
geometry") turned out to already be a non-issue; only the RUNTIME
INSTANCE WRAPPER and its hit-testing/lifetime/AS2-identity integration
were actually missing. Full writeup: `docs/buttons.md`'s "Architecture
audit" section.

**STEP 4-5 — Fix/implementation.** Added `ButtonInstance`
(`src/runtime/ButtonInstance.{h,cpp}`) — the button-phase counterpart of
`MovieClipInstance` for `SpriteDef`, following the exact same established
`Def` (shared immutable) / `Instance` (per-placement mutable) split.
Extracted `emptyBoundsRect()`/`isEmptyBoundsRect()`/`unionBoundsRect()`/
`characterOwnBoundsRect()` out of `MovieClipInstance.cpp`'s anonymous
namespace into a new shared file (`src/runtime/CharacterBounds.{h,cpp}`)
so both files can reuse them without duplication — pure mechanical
extraction, verified zero behavior change. `MovieClipInstance` gained a
new `buttonInstances_` map (deliberately separate from `children_`, NOT
visible to `SceneRenderer` — guarantees pixel-identical rendering by
construction), synced via the SAME two-phase pattern/entry points
`syncChildren()`'s existing Sprite-child logic already uses.
`hitTestPointInOwnSpace()` gained one more branch (a thin wrapper around
the SAME existing primitives, not a second implementation) so
`HitTestResult` can now carry a `ButtonInstance*`. Added a per-tick
`updateButtonStatesRecursive()` driver, called root-only from
`advanceFrame()` (mirroring `updateDrag()`'s own precedent), computing the
UP/OVER/DOWN state machine (`!isOver -> UP`; `isOver && !mouseDown ->
OVER`; `isOver && mouseDown -> DOWN`) from one root-level
`hitTestPoint()` call. Added a bare AS2 identity object
(`ButtonInstance::scriptObject_`, no properties/methods wired) plus a
`childNameToDepth_`/`handleNativeGet()` fallback so a named button
placement resolves to a real, distinct object. Full architecture writeup,
including the documented 3-vs-5-state simplification and the exact AS2
identity gap left open: `docs/buttons.md`.

**STEP 6 — Desktop tests.** 16 new tests in the new
`tests/test_button_instance.cpp`: creation (+ confirming NO
`MovieClipInstance` is also created), default state, two-independent-
placements-of-the-same-def, transforms (origin/translated/scaled/nested-
in-MovieClip), overlapping-buttons depth ordering, HitTest-vs-visual-state
geometry, invisible-button exclusion, the full UP/OVER/DOWN transition
table driven through real `advanceFrame()` ticks, multiple-buttons
only-topmost-gets-OVER, display-list-driven removal, same-depth
replacement-with-a-different-button (verified via a `std::weak_ptr` that
the OLD instance is genuinely destroyed, not just relabeled — a raw
pointer comparison would be unreliable since the allocator can legally
reuse a just-freed address), and duplicate-placement AS2 name resolution.
**253/253 tests passing** (up from 237), zero regressions.

**Independent real-content check:** rendered `hobo.swf` frames 1-5 before
and after this phase and diffed byte-for-byte — **100% identical**
(expected: `SceneRenderer.cpp` was not touched at all this phase).

**STEP 7-8 — 3DS build.** `cmake --build build_3ds` succeeds cleanly (only
the same pre-existing, unrelated newlib/ABI warnings as every prior 3DS
build). New `.3dsx` built (397216 bytes, up from 392032 — confirms the new
code linked in). **Not yet tested on Azahar/hardware** — no emulator/
device access from this environment.

**STEP 9 — Regression tests.** Done — see STEP 6, all 16 permanent.

**STEP 10 — What now works / what remains.** A placed SWF Button2 now has
a real runtime instance with its own transform, depth, visibility, hit
area, and UP/OVER/DOWN state — verified against both synthetic fixtures
and, via a diagnostic-only tool, real `hobo.swf` content (3 real buttons
placed on frame 1, including one correctly composed through a level of
MovieClip nesting — see `docs/buttons.md`'s "Real Hobo `DefineButton2`
findings"). **No ActionScript event dispatch of any kind exists yet** —
`onPress`/`onRelease`/`onRollOver`/`onRollOut`/`onClipEvent(mouse*)`/
`Mouse.onMouseDown`/`onMouseUp`, and the button's own parsed
`condActionsV1`/`condActionsV2` bytecode, are all still completely
undispatched, exactly as scoped. Hobo interaction does **not** work yet.
**Recommended next step: a generic event dispatcher** (design:
`docs/events.md`, not yet written) — every primitive it needs now exists
(`hitTestPoint()`'s `HitTestResult::button`, `ButtonInstance::
updateState()`'s "did it change" return value, `InputState::
isMousePressed()`/`isMouseReleased()`), it just needs to actually call
them and fire the right AS2 handlers on the right transitions.

### Remaining sub-fixes for this priority (not started, in dependency order)

See `docs/interactivity-audit.md` §8 for the complete list and reasoning: a
generic event dispatcher (design: `docs/events.md`), then finally
`onClipEvent`'s remaining 15 mouse/key flags (status:
`docs/onclipevent-compatibility.md`) and button `on()`/`condActionsV2`
handler dispatch. The device-px -> stage-pixel coordinate mapping,
edge-detected input state, bounding-box hit-testing, and a real
per-placement Button runtime instance that used to head this list are all
**DONE** — see Sub-fix 2/N, Sub-fix 3/N, Sub-fix 4/N, and Sub-fix 5/N
above.

**Refinement from the real-game-corpus phase (2026-08-18, analysis only —
nothing below was implemented this phase):** `docs/real-game-compatibility.md`'s
cross-game matrix (Hobo 1–7 + Extreme Pamplona, `tests/games/`) shows this
priority actually needs **two independent dispatch mechanisms**, not one:

1. **Native `DefineButton2` `condActionsV2` dispatch** — button
   press/release/rollOver/etc. action lists compiled directly into the
   tag's binary condition-flag records, no AS2 source-level handler
   property involved. Confirmed via string-scan evidence: **all 7 Hobo
   games** (`hobo.swf` plus all 6 sequels) use `DefineButton2` but show
   **zero** occurrences of the literal strings `onPress`/`onRelease`/
   `onRollOver`/`onRollOut`/`onClipEvent` anywhere in the decompressed
   body — their button interactivity can only be this mechanism.
   Implementing this alone unblocks **all 7 Hobo games identically** (the
   family is structurally uniform on this specific feature — see the
   Hobo-family comparison table) and has no bearing on Extreme Pamplona.
2. **`object.onPress`/`onRelease = function(){}`-style property-handler
   dispatch** — the generic event-handler-property mechanism `docs/events.md`
   already scopes. Confirmed via the same string-scan evidence: **Extreme
   Pamplona's main loader** (and only that file in this corpus) shows
   `onPress`/`onRelease` **found** as literal strings — meaning at least
   part of its interactivity is authored the AS2-property-handler way,
   not (or not only) via `condActionsV2`. Implementing only mechanism 1
   above would **not** unblock Extreme Pamplona's interactivity.

Neither mechanism was implemented or even prototyped this phase — this is
a scoping refinement to the existing "generic event dispatcher" plan
(`docs/events.md`), based on new, real-content evidence that didn't exist
before this phase's corpus was built. See `docs/real-game-compatibility.md`'s
"If we implement feature X, which games does it help?" section for the
full reasoning and every other corpus-derived, feature-to-game mapping
(MP3 decode, `DefineMorphShape`, `DefineShape4`, `PlaceObject3`,
`GlobalObject` built-ins, `CsmTextSettings`).


**Classification: AVM1 + DISPLAY LIST (hit-testing needs `_width`/`_height`, itself blocked on bounding-box computation) + OBJECT MODEL.**

Confirmed, not hypothesized: `_width`/`_height` are hardcoded to `0`
everywhere (`MovieClipInstance.cpp:510,349-350`); no hit-testing code exists
anywhere in the codebase; `onClipEvent`'s 16 mouse/keyboard-related flags
(`Press`, `Release`, `RollOver`, `RollOut`, `MouseDown`, `MouseUp`, ...) are
parsed into `clipActions_` but never dispatched (confirmed by exhaustive
grep of every `runClipEvent()` call site — only `Load`/`Unload`/
`EnterFrame` fire); `DefineButton`/`DefineButton2`'s `actionsV1`/
`condActionsV2` bytecode is parsed but literally never executed by any code
path (confirmed by codebase-wide grep for those field names — the only
consumers are the `swf/` parser itself and `SceneRenderer`'s render-only Up-
state drawing).

**Impact:** clicking/pressing any button, or any `onClipEvent(press)`-driven
interaction, does nothing. `hobo.swf`'s title screen has a "PLAY!" button
(`DefineButton2`, 16 occurrences in the file) — this is very likely why
Phase 9's manual walk never progressed past the title screen despite full,
correct rendering of it.

**Scope note:** this is a genuinely large feature (bounding-box computation
for `_width`/`_height`, a hit-test routine, a button state machine
[Up/Over/Down transitions driven by mouse position + button-down state],
wiring `onClipEvent`'s mouse flags to that same hit-test result, and button
`on()` handler dispatch) — larger than a single-limitation "minimal repro →
fix → test" cycle can responsibly cover in one pass. Recommended as the
**next** limitation to pick up, likely itself split into 2-3 sub-phases
(bounding box → hit test → button/onClipEvent dispatch) rather than one
atomic change.

## Priority #3 — `GlobalObject` has zero named built-ins (`Math`, `Date`, `Number`, `String`, `Boolean`)

**Classification: OBJECT MODEL.**

Confirmed: `GlobalObject::create()` is 7 lines, returns one bare `Object`
with no properties at all (`src/avm1/GlobalObject.cpp`). Any content calling
`Math.random()`, `Math.floor()`, `Math.abs()`, `new Date()`, `Number(x)`,
`String(x)`, `Boolean(x)` fails outright ("not a function"/"undefined is
not an object"). `Math.*` in particular is extremely common in any game
with randomness, easing, or numeric formatting — plausibly a real,
un-diagnosed blocker for `hobo.swf`'s gameplay frames (not yet reached — see
Priority #2) or its sequels. Not previously documented as a gap anywhere in
this project.

**Scope:** moderate — implementing `Math` alone (a plain object with native
functions for `random`/`floor`/`ceil`/`round`/`abs`/`min`/`max`/`sqrt`/
`pow`, etc.) would likely cover the overwhelming majority of real content's
needs and is well-isolated (same `nativeImpl` mechanism `Key`/`Mouse`/
`Sound` already use, in `ScriptEnvironment`'s constructor or a new
`GlobalObject` extension point).

## Priority #4 — `DefineMorphShape`/`DefineMorphShape2` not resolved (confirmed real-content gap)

**Classification: TAG SUPPORT + RENDERER.**

Confirmed present in real `hobo.swf` content: 19 occurrences (per
`docs/compatibility.md`'s Phase 9 tag histogram). `TagCode` recognizes the
tag codes but no parser exists, and `CharacterDictionary::
scanTagsForCharacters()` has no branch for them at all — any `PlaceObject`
referencing one silently places nothing (a real, silent visual gap, not a
crash). Scope: needs `MORPHFILLSTYLE`/`MORPHLINESTYLE` parsing (a variant of
the existing `FILLSTYLEARRAY`/`LINESTYLEARRAY` readers, but with two
color/matrix endpoints instead of one) plus start/end shape parsing and a
ratio-interpolated render — a genuine chunk of new work, not a quick fix,
consistent with the Phase 9 assessment that first found this gap.

## Priority #5 — Audio codec decode (no SWF sound is actually audible)

**Classification: AUDIO.**

Confirmed: zero codec decode exists anywhere in the codebase for any format
(uncompressed PCM framing, ADPCM, or MP3) — `DefineSound`'s compressed
sample data is parsed only at the header level and never touched again.
`Nintendo3DSAudioBackend::playSound()`, read line-by-line, reserves an ndsp
channel and unpauses it but never calls `ndspChnWaveBufAdd()` — there's
simply no decoded buffer to queue. `hobo.swf` has 35 `DefineSound`
occurrences — every one of them is currently silent on both desktop and
3DS. Scope: large — even "uncompressed PCM" framing needs correct
sample-rate/bit-depth/channel-count handling per the SWF spec's `DefineSound`
header fields (already parsed), and ADPCM/MP3 (likely what `hobo.swf`
actually uses, per `docs/compatibility.md`'s "unverified" note on its sound
format) would need a real decoder — not a "smallest missing layer" fix.

---

## Not prioritized into the top 5, but confirmed real (tracked for later)

- `SetBackgroundColor` not parsed — small, isolated, zero-risk fix; low
  visual impact (only matters for movies with a non-white/non-default stage
  color) — good candidate for a quick follow-up between larger items.
- Gradient fills rendered as a flat averaged color (no real gradient
  rendering) — cosmetic-only impact for vector-heavy content.
- Shape tessellation topology gaps (shapes with holes, multi-run fill
  regions) — real but likely rare in typical game-asset shapes; needs a
  concrete failing real-content example to prioritize confidently (UNKNOWN
  whether `hobo.swf` itself is affected — not checked this phase).
- `Try`/`Catch`/`Finally` entirely unimplemented — uncommon in simple AS2
  game logic, per `docs/avm1-support.md`'s existing assessment; no evidence
  yet that any target title needs it.
- Bitmap tag family (`DefineBits*`) entirely unimplemented — **note**:
  `hobo.swf` itself has zero bitmap tags (confirmed, Phase 9 tag histogram)
  — the task spec's instruction to not assume this is a real blocker is
  honored: it is NOT currently blocking the primary target file. Whether
  the Hobo *sequels* (`hobo 2`-`7`, not yet tested) use bitmaps is unknown.
