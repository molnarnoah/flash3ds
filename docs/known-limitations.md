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

### Remaining sub-fixes for this priority (not started, in dependency order)

See `docs/interactivity-audit.md` §8 for the complete list and reasoning:
device-px -> stage-twips coordinate mapping (next pick — small, isolated,
hard-blocks hit-testing), edge-detected input state, bounding-box hit-
testing (design: `docs/hit-testing.md`), a per-placement Button instance
object, a generic event dispatcher (design: `docs/events.md`), then finally
`onClipEvent`'s remaining 15 mouse/key flags (status:
`docs/onclipevent-compatibility.md`) and button `on()` handler dispatch.


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
