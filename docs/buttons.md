# Buttons: ButtonDef → ButtonInstance

**Status: ButtonInstance phase (2026-08-19).** A placed SWF Button2 now has
a real runtime instance with its own transform, depth, visibility, hit
area, and UP/OVER/DOWN state. **Event dispatch (onPress/onRelease/
onRollOver/onRollOut/onClipEvent(mouse*)/`Mouse.onMouseDown`/`onMouseUp`/
the button's own `condActionsV1`/`condActionsV2` bytecode) is NOT
IMPLEMENTED YET** — see "Not implemented yet" below. This doc covers only
what this phase actually built.

## Architecture audit (what existed before this phase, confirmed by tracing
the actual source, not inferred)

- **`swf::ButtonDef`/`ButtonRecordDef`/`ButtonCondAction`**
  (`src/swf/DefineButtonTag.h`) — already fully parsed for both
  `DefineButton` (v1, tag 7) and `DefineButton2` (v2, tag 34) via
  `parseDefineButton()`. `ButtonRecordDef` has `stateUp`/`stateOver`/
  `stateDown`/`stateHitTest` bools, `characterId`, `depth`, `matrix`, and an
  optional per-record `ColorTransform` (v2 only). `ButtonDef` has
  `characterId`, `trackAsMenu` (v2 only), `records`, and exactly one of
  `actionsV1` (raw AS2 bytecode, v1) or `condActionsV2` (a list of
  condition-bitmask-keyed bytecode blocks, v2) populated. Neither is
  dispatched anywhere — confirmed unchanged by this phase.
- **`CharacterDictionary`** (`src/runtime/CharacterDictionary.{h,cpp}`) —
  `CharacterDef` is a `std::variant` of 7 alternatives including
  `swf::ButtonDef`. `CharacterDictionary::build()` already recognizes
  `TagCode::DefineButton`/`DefineButton2` (both top-level and, recursively,
  nested inside a `DefineSprite`'s own tag stream) and calls
  `parseDefineButton()`, storing the result exactly like every other
  character type. **Nothing needed to change here** — button DEFINITIONS
  were already fully generic/working before this phase.
- **`DisplayList`/`DisplayListEntry`/`PlaceObjectTag`**
  (`src/runtime/DisplayList.h`, `src/swf/PlaceObjectTag.{h,cpp}`) —
  `DisplayListEntry` is entirely character-type-agnostic
  (`{depth, characterId, matrix, colorTransform, ratio, name, clipDepth,
  clipActions}`) and `DisplayList::applyPlaceObject()` is fully generic
  (add/replace/update-in-place semantics driven only by depth + `Move`/
  `HasCharacter` flags, never by which `CharacterDef` alternative the
  characterId resolves to). **Nothing needed to change here either** —
  button PLACEMENT was already fully generic/working.
- **The actual gap, confirmed by tracing `MovieClipInstance::
  syncChildren()`** (`src/runtime/MovieClipInstance.cpp`): before this
  phase, a runtime instance was created ONLY when
  `std::holds_alternative<SpriteDef>(*def)` was true for a display-list
  depth's resolved character — every other type, buttons included, was
  simply `continue`d past with **no runtime object created at all**. A
  placed button was nothing more than a raw `DisplayListEntry` — no
  transform of its own beyond the entry's, no independent visibility, no
  state, no AS2 identity, nothing to hit-test against except by falling
  through to the generic "leaf character" bounds check.
- **`SceneRenderer::renderCharacter()`** (`src/renderer/SceneRenderer.cpp`)
  — for a `ButtonDef` character, always rendered only the
  `stateUp`-flagged records (hardcoded), with an explicit existing comment
  confirming "No mouse hit-testing/state machine yet ... always draw the
  'Up' state." This phase deliberately does **not** touch this function —
  see "Rendering" below.
- **`characterOwnBoundsRect()`** (moved this phase from
  `MovieClipInstance.cpp`'s anonymous namespace into the new
  `src/runtime/CharacterBounds.{h,cpp}` — see "Refactor" below) — already,
  BEFORE this phase, correctly resolved a button's hit-area geometry: it
  unions the `stateHitTest`-flagged records' nested-character bounds,
  falling back to `stateUp`-flagged records if the button defines no
  explicit HitTest state at all (built during the `_width`/`_height` fix,
  reused unchanged here — see "Hit-test integration" below). **This means
  requirement 5/12's central worry — "what if the parser doesn't retain
  hit-state geometry" — turned out to already be a non-issue**: the
  geometry was already there, fully correct, just not yet wired to
  anything that could use it for a placed button specifically.

## New architecture

### `ButtonInstance` (`src/runtime/ButtonInstance.{h,cpp}`)

The runtime placement of a `swf::ButtonDef` — the button-phase counterpart
of `MovieClipInstance` for `SpriteDef`, following the exact same
established split:

- `ButtonDef` (parsed SWF definition) stays SHARED IMMUTABLE data — a
  non-owning `const swf::ButtonDef*` reference, never copied, never
  mutated.
- `ButtonInstance` holds all per-PLACEMENT mutable state: `matrix_`
  (local transform), `colorTransform_`, `visible_`, `state_`/
  `previousState_` (UP/OVER/DOWN), and its own bare AS2 `scriptObject_`.
  Two placements of the same `ButtonDef` get two fully independent
  `ButtonInstance` objects — verified by
  `ButtonInstance_TwoPlacementsOfSameDef_AreIndependentInstances`
  (mutating one's state/visibility never touches the other's).

Public surface: `characterId()`/`parent()`/`depthInParent()`/`name()`,
`localMatrix()`/`setLocalMatrix()`, `colorTransform()`/
`setColorTransform()`, `visible()`/`setVisible()`, `worldMatrix()`
(composes with the owning `MovieClipInstance` chain, mirroring
`MovieClipInstance::worldMatrix()`'s own `concatMatrix(parentWorld,
ownMatrix)` composition — so a button nested at any depth, root → clip →
button, gets a correct world transform for free — see
`ButtonInstance_Transform_NestedInMovieClip_ComposesWithParent`),
`state()`/`previousState()`/`updateState(isOver, mouseDown)`, and
`hitTestLocal(localPoint, characters)`.

### Refactor: `src/runtime/CharacterBounds.{h,cpp}`

`emptyBoundsRect()`/`isEmptyBoundsRect()`/`unionBoundsRect()`/
`characterOwnBoundsRect()` were extracted out of `MovieClipInstance.cpp`'s
anonymous namespace (internal linkage — uncallable from anywhere else) into
this new shared file, as plain `flash3ds::runtime` free functions, so BOTH
`MovieClipInstance.cpp` and the new `ButtonInstance.cpp` can call
`characterOwnBoundsRect()` without duplicating its logic — satisfying "do
not create a second hit-testing implementation." Pure mechanical
extraction, zero behavior change (verified: all 237 pre-existing tests
still pass unmodified, plus `hobo.swf` frames 1-5 byte-identical before/
after).

### Display-list integration

`MovieClipInstance` gained a new member, `std::map<int32_t,
std::shared_ptr<ButtonInstance>> buttonInstances_` — deliberately a
**separate map from `children_`**, not a `MovieClipInstance` subtype, and
**not visible to `SceneRenderer` at all**. `SceneRenderer` keeps walking
the raw `DisplayList` exactly as before — this is what guarantees
pixel-identical rendering by construction, not by convention (see
"Rendering", below).

`MovieClipInstance::syncChildren()` — the same method that creates/removes
Sprite children — now also creates/removes `ButtonInstance`s, using the
exact same two-phase (remove-stale-then-create-new) pattern, keyed by the
same depth, driven by the same `DisplayList` re-sync every tick:

- **Removal**: a `buttonInstances_` entry is erased when its depth no
  longer has a display-list entry, or the entry's `characterId` changed
  (mirrors `children_`'s own removal rule verbatim) — covers tag-driven
  removal (`RemoveObject`/`RemoveObject2`), replacement at the same depth,
  and timeline frame changes uniformly, all via the SAME mechanism
  Sprites already use. A `MovieClip` that owns buttons being itself
  removed cascades their destruction for free (they live inside its own
  `buttonInstances_` map, destroyed along with it via `shared_ptr`) — no
  extra code needed.
- **Creation**: for every depth not already in `children_` or
  `buttonInstances_`, if the resolved character is a `swf::ButtonDef`, a
  new `ButtonInstance` is constructed with the placing `DisplayListEntry`'s
  matrix/colorTransform/characterId/name, and registered.

Called from the exact same two entry points `syncChildren()`'s Sprite path
already uses (`MovieClipInstance::createRoot()` and every
`MovieClipInstance::advanceFrame()`), so buttons anywhere in the tree —
including newly-created nested `MovieClip`s — get synced automatically,
with no separate lifetime system.

### Rendering — deliberately unchanged

`SceneRenderer.cpp` was **not modified**. It continues to render a button
placement directly from the raw `DisplayListEntry` (always the Up-state
records, exactly as before this phase). Verified: `hobo.swf` frames 1-5
rendered before/after this phase are **byte-for-byte identical**
(`md5sum` match). The only new behavior this phase adds is a *parallel*
runtime-identity/state/hit-testing layer that SceneRenderer is entirely
unaware of.

### Hit-test integration

Reuses the existing hit-testing system (`MovieClipInstance::
hitTestPoint()`/`hitTestPointInOwnSpace()`, sub-fix 4/N) — no second
hit-testing implementation was created. `hitTestPointInOwnSpace()` gained
one more branch, checked between the existing `children_` (MovieClip)
branch and the generic leaf-character fallback: if the current depth has a
`buttonInstances_` entry, call `ButtonInstance::hitTestLocal()` — a thin
wrapper that inverts the button's own `matrix_`, transforms the query
point into the button's local space, and tests it against
`characterOwnBoundsRect()`'s result (the SAME hit-area geometry the
pre-existing `_width`/`_height` code already computed for buttons) via the
SAME `rectContainsPoint()` primitive the generic leaf branch uses. If it
hits, `HitTestResult` now also carries a `ButtonInstance*` (new field,
`nullptr` for every non-button hit) so calling code can identify "this was
a button" and get its runtime instance.

Because this all happens inside the SAME recursive per-depth walk
`hitTestPointInOwnSpace()` already performs (topmost-first depth order,
recursing into nested `MovieClip` children exactly as before), depth
ordering among multiple overlapping buttons (and among buttons and
non-button content) falls out correctly for free — verified by
`ButtonInstance_HitTest_OverlappingButtons_HigherDepthWins`. Nested buttons
(root → MovieClip → Button) are reached correctly for the same reason — no
special-casing needed.

**HitTest-state vs. visual-state geometry**: `characterOwnBoundsRect()`
already (from the `_width`/`_height` fix) prioritizes `stateHitTest`-
flagged records over `stateUp`-flagged ones, falling back to Up-state
geometry only if no explicit HitTest state exists — exactly matching real
Flash's own documented behavior (an author-supplied hit area is preferred;
its absence is tolerated, not an error). Verified against a synthetic
button whose Up-state visual (20x20px) and HitTest-state geometry (60x60px,
a different shape entirely) deliberately differ:
`ButtonInstance_HitTest_UsesHitTestGeometry_NotSmallerVisualUpState` proves
hit-testing uses the 60x60 HitTest box, not the smaller visual box.

### UP/OVER/DOWN state machine

`ButtonInstance::updateState(isOver, mouseDown)`:

```
!isOver                -> kUp
isOver && !mouseDown   -> kOver
isOver &&  mouseDown   -> kDown
```

Verified against the task's exact example transition table end-to-end
(`ButtonInstance_StateTransitions_FullMatrix_MatchesDocumentedSemantics`):
outside → UP; enters → OVER; presses while over → DOWN; remains pressed →
DOWN (no spurious re-transition); releases over → OVER; leaves → UP.

**Known, documented simplification vs. real Flash**: real Flash Player
models a richer 5-state button (Up/Over/Down/OutDown/IdleDown-ish, via the
`ButtonCondition` bitmask's `kOverDownToOutDown`/`kOutDownToOverDown`/
`kOutDownToIdle` transitions), distinguishing "pressed, then dragged off
the button while still held" from a plain "not over." This 3-state model
does not distinguish that case — dragging off simply reports `kUp`,
matching the *visual* convention of showing Up-state artwork once the
pointer leaves, which is the only externally observable effect anyway
without event dispatch (no `on(dragOut)`/`on(dragOver)` handler exists to
fire differently). Flagged here rather than silently deviating.

**Driver**: a new per-tick method, `MovieClipInstance::
updateButtonStatesRecursive(hitButton, mouseDown)`, called ONLY from the
ROOT's `advanceFrame()` (mirroring `ScriptEnvironment::updateDrag()`'s own
"root-only, once per full-tree tick" precedent exactly) — but implemented
as a tree-recursive `MovieClipInstance` method rather than centralized in
`ScriptEnvironment`, since (unlike drag, which tracks at most one target)
buttons are distributed across the whole tree. The root computes exactly
ONE `hitTestPoint(stageMouseX(), stageMouseY())` call — which, because
hit-testing now already covers buttons at any depth (see above), already
finds the single topmost button anywhere in the tree the pointer currently
hits (or none) in one pass — then recursively propagates `isOver`
(`button == hitButton`) and the current `env_->inputState().isMouseDown()`
to every `ButtonInstance` in the tree. Verified with two non-overlapping
buttons that only the one under the pointer gets `OVER`
(`ButtonInstance_StateTransitions_MultipleButtons_OnlyTopmostGetsOver`).

**Explicitly does NOT dispatch any ActionScript event** — see "Not
implemented yet" below.

### AS2 object identity

Before this phase, buttons had **no AS2 object identity bridge at all** —
confirmed by the same audit that found the missing runtime instance in the
first place. This phase establishes a minimal one:

- `ButtonInstance::wireScriptObject()` creates a bare `avm1::Object` with
  **no `nativeGet`/`nativeSet`/`nativeEnumerate` hooks wired** — it exists
  purely so a named placement has a real, distinct object identity.
- `MovieClipInstance::syncChildren()` registers a named button's
  `entry.name` in the SAME `childNameToDepth_` map named Sprite children
  already use.
- `MovieClipInstance::handleNativeGet()`'s named-child resolution gained a
  fallback: if `childNameToDepth_` resolves to a depth with no
  `children_` entry, it now also checks `buttonInstances_` before giving
  up. Verified: `ButtonInstance_DuplicatePlacements_BothResolveByName`
  confirms `_root.a`/`_root.b` (two named button placements) each resolve
  to a real, distinct object via `nativeGet()`.

**Exact missing bridge, documented rather than worked around**: the
returned object has no properties or methods — `_root.someButtonName._x`
resolves to `undefined` (no `nativeGet` hook), not a real coordinate. And
`MovieClipInstance::resolvePath()` (used by `SetTarget`/`tellTarget`/
`CallMethod`-on-a-path) still only walks `children_` — it cannot resolve a
path segment that names a button at all, so `_root.someButtonName.
somethingElse` (multi-segment paths through a button) and any AVM1 action
that targets a button by path (rather than by direct member access) don't
work. Wiring `_x`/`_y`/`_visible`/etc. onto `ButtonInstance::
scriptObject_`, and/or extending `resolvePath()`, is left to a future
phase — this phase only establishes that a real, distinct object exists to
extend.

### Real Hobo `DefineButton2` findings (requirement 13 — diagnostic only, NOT wired to interaction)

Ran a standalone, throwaway diagnostic (not part of the permanent
build/tests) against the real `/home/claude/hobo-testing/hobo.swf`. Full
raw output delivered alongside this doc
(`hobo_button_diagnostic.txt`). Summary:

- **16 top-level `DefineButton2` character definitions** exist in the file
  (all v2 — tag code 34; zero v1 `DefineButton` tags). Record counts range
  from 1 (a trivial "same graphic in all 4 states" button, e.g.
  characterId 88/90) up to 18 records (characterId 1324/1325 — complex
  multi-part buttons whose Up/Down states each composite several nested
  characters at several depths). Every button with `condActionsV2` uses
  `conditions=0x000` except one (characterId 1320, `conditions=0x01a` —
  `kOverUpToOverDown | kOverDownToOverUp | kOverDownToOutDown`); nearly
  every `condActionsV2[0]` also carries a non-empty `keyCode` field (`4`)
  — present but genuinely unusual (worth independent confirmation later;
  not investigated further here, out of scope for this phase).
- **On frame 1 of the root timeline, 3 buttons are actually PLACED**:
  - Depth 25, characterId 12, unnamed, at world position
    (372.75px, 327.35px) on the 600x450px stage.
  - Depth 44, characterId 85, unnamed, at world position
    (267.50px, 370.55px).
  - Nested one level inside a named MovieClip (`/mutebutton`), depth 1,
    characterId 88 (the trivial 1-record button), unnamed at its own
    level, with a correctly-composed world position of
    (581.45px, 430.90px) — confirms nested button → correct world
    transform works against REAL content, not just synthetic test fixtures.
  - **All three are UNNAMED placements** — per the "AS2 object identity"
    section above, none of them are reachable via `_root.someName` today;
    this is the real-content instance of the exact documented gap.
- This diagnostic proves the full pipeline end-to-end against real
  content: real `DefineButton2` tag → `CharacterDictionary` → placed
  `ButtonInstance` → correct world transform (including through one level
  of MovieClip nesting) → correct UP default state. **Hit-area geometry
  and hit-testing were exercised only against synthetic test fixtures this
  phase, not against these specific real Hobo buttons** — doing so would
  require simulating actual mouse input against the running movie, which
  edges toward "making Hobo's event system work," explicitly out of scope
  per requirement 13 ("DO NOT attempt to make Hobo's full event system
  work yet").

## Not implemented yet (explicit stop condition for this phase)

- `onPress`/`onRelease`/`onReleaseOutside`/`onRollOver`/`onRollOut`/
  `onDragOver`/`onDragOut` (button event-handler syntax).
- `onClipEvent(press)`/`onClipEvent(release)`/`onClipEvent(rollOver)`/
  `onClipEvent(rollOut)`/`onClipEvent(mouseDown)`/`onClipEvent(mouseUp)`
  and every other mouse-related `ClipEventFlag` — still parsed into
  `clipActions_` (Phase 6) but never dispatched (unchanged by this phase).
- `Mouse.onMouseDown`/`Mouse.onMouseMove`/`Mouse.onMouseUp` (global mouse
  event callbacks).
- The button's own parsed `condActionsV1`/`condActionsV2` bytecode — still
  parsed, still never executed by any code path.
- A generic event-dispatch mechanism of any kind (design doc:
  `docs/events.md`, not yet written).
- `ButtonInstance::scriptObject_` properties/methods (`_x`/`_y`/`_visible`/
  etc.) and `resolvePath()` support for button path segments — see "AS2
  object identity" above.
- Exact vector-shape hit testing (bounding-box only, matching the existing
  `hitTestPoint()`/`hitTest()` scope — see `docs/hit-testing.md`'s "Why
  bounding-box, not exact shape").
- `DefineButtonCxform`/`DefineButtonSound` (unrelated legacy tags, not
  parsed anywhere in this project — unchanged).

**Do not claim Hobo interaction works yet — it does not.** This phase
proves the runtime REPRESENTATION of a placed button is now correct and
real; nothing in this phase makes clicking a button in `hobo.swf` (or any
other content) actually do anything.

## Recommended next step

A generic event dispatcher (design: `docs/events.md`, not yet written)
that consumes `hitTestPoint()`'s `HitTestResult::button` field +
`ButtonInstance::updateState()`'s returned "did it change" bool +
`InputState::isMousePressed()`/`isMouseReleased()` (all of which now exist
and are exercised end-to-end by this phase's tests) to actually fire
`onPress`/`onRelease`/`onRollOver`/`onRollOut` and the button's own
`condActionsV2` bytecode on the correct `ButtonCondition` transitions.
