# Hit Testing

**Status: IMPLEMENTED (interactivity phase, 2026-08-19, sub-fix 4/N — see
`docs/known-limitations.md` for the full STEP 1-10 writeup).** This doc was
originally written (2026-08-18) as a design/architecture record BEFORE any
code existed — the "Planned algorithm" section below is exactly what was
implemented, unchanged, confirming the design held up. The sections below
are left as originally written (design record), with implementation notes
added inline where the actual code differs from or extends the plan (e.g.
the AS2-visible `MovieClip.hitTest(x, y)` method, which was an intentional
ADDITION beyond the original "internal primitive only" scope, made because
it reuses the exact same machinery at near-zero extra cost and is the only
way any of this becomes AS2-visible/usable yet). See
`docs/interactivity-audit.md` §4/§8 for the original audit finding (zero
hit-testing code existed anywhere in this codebase before this phase) and
the dependency-chain reasoning for why bounds computation (`_width`/
`_height`, sub-fix 1/N) and coordinate conversion (`_xmouse`/`_ymouse`,
sub-fix 2/N) had to come first.

## Implementation summary (2026-08-19)

- `swf::Point`, `swf::transformPoint()`, `swf::invertMatrix()`,
  `swf::rectContainsPoint()` (`src/swf/SwfRecords.h/.cpp`) — the new
  geometric primitives this needed; standard 2x3 affine matrix inverse,
  returning `false` (not a garbage/degenerate result) for a zero-determinant
  matrix, exactly as this doc's original design called for.
- `MovieClipInstance::worldMatrix()` — this instance's `matrix_` composed
  with every ancestor's, kept in lockstep with how `SceneRenderer` computes
  world transforms for rendering (same `concatMatrix` composition, same
  "root's own matrix_ is the base" convention).
- `MovieClipInstance::hitTestPoint(stageXPixels, stageYPixels) ->
  std::optional<HitTestResult>` (`HitTestResult{clip, characterId, depth}`)
  — the internal "topmost hit-testable thing under a point" primitive from
  the "Planned algorithm" section below, implemented essentially verbatim.
  Public, not yet consumed by anything else (no event dispatch exists yet)
  — a pure query, ready for a future button/mouse-event-dispatch phase to
  call.
- `MovieClipInstance::hitTestBounds(stageXPixels, stageYPixels) -> bool` +
  AS2-visible `MovieClip.hitTest(x, y)` (2-argument form) — a deliberate,
  scoped ADDITION beyond the original design: real Flash's `hitTest(x, y)`
  is NOT the same query as the "topmost object" primitive above (it tests
  ONE clip's own full aggregate bounding box, ignores `_visible`, doesn't
  care about z-order) — see "AS2 `hitTest()` vs the internal primitive"
  below for the full distinction. The 1-argument `hitTest(target)` and the
  3-argument `hitTest(x, y, shapeFlag=true)` (exact-shape) forms are
  explicitly NOT implemented — see "Why bounding-box, not exact shape"
  below, unchanged from the original plan.
- 12 new regression tests (`tests/test_movieclip_instance.cpp`) + 8 new
  geometry-primitive tests (`tests/test_swf_records.cpp`): point-inside/
  outside a shape, inclusive boundary, overlapping-shapes topmost-wins,
  invisible-clip exclusion (both directions: `hitTestPoint()` respects it,
  `hitTest()` deliberately doesn't), degenerate-scale un-hit-testability,
  script-mutated-transform awareness, two-level nested-MovieClip recursion,
  and the AS2 `hitTest()` method's argument-count edge cases. **237/237
  tests passing** (up from 217).

## AS2 `hitTest()` vs the internal `hitTestPoint()` primitive

These compute genuinely different things, both real/intentional, not one
subsuming the other:

| | `hitTestPoint()` (internal) | `hitTestBounds()`/AS2 `hitTest(x,y)` |
|---|---|---|
| Question answered | "What's the FRONTMOST thing under this point, anywhere in my subtree?" | "Does this point fall in THIS clip's own box?" |
| Respects `_visible`? | Yes — invisible subtrees never match | No — matches real Flash's own (surprising but real) behavior: geometry, not rendered visibility |
| Recurses into children? | Yes, into each child's own CONTENT (so an empty/transparent area of a nested clip correctly passes through) | No — tests the aggregate bounds directly, a single box |
| Z-order / topmost-wins? | Yes (this is its whole purpose) | N/A — one clip, one box |
| AS2-visible today? | No — pure C++ query, for a future event-dispatch phase | Yes — `myClip.hitTest(x, y)` |

## What now exists to build on

- `swf::transformRect(const Matrix&, const Rect&) -> Rect`
  (`src/swf/SwfRecords.h/.cpp`) — transforms a local-space Rect's 4 corners
  by a matrix and returns the resulting axis-aligned bounding box. Written
  generically; hit-testing should use this AS-IS, not reimplement it.
- `MovieClipInstance::computeBoundsInOwnSpace()`
  (`src/runtime/MovieClipInstance.cpp`, private) — recursively unions every
  placed leaf character's and child clip's bounds, in a clip's own local
  space. Currently private/used only by `width()`/`height()`; hit-testing
  will need either to make this accessible (a `friend` or a public
  variant) or, more likely, to add sibling logic that walks the SAME
  `timeline_->displayList()`/`children_` structure but tests a POINT
  against each entry instead of unioning bounds — see "Planned algorithm"
  below, which reuses the same tree-walk shape deliberately.
- `characterOwnBoundsRect()` (anonymous-namespace helper,
  `MovieClipInstance.cpp`) — leaf character (Shape/Text/EditText/Button)
  bounds resolution, including the Button `stateHitTest`-preferred-
  fallback-to-`stateUp` logic already built for `_width`/`_height`. Directly
  reusable for hit-testing's per-leaf bounding-box test.

## What was blocking this (both resolved before this turn started)

1. ~~Device-px -> stage-twips inverse coordinate mapping~~ — **done,
   sub-fix 2/N (2026-08-18)**: `_xmouse`/`_ymouse` (and, as of this turn,
   `hitTestPoint()`/`hitTest()`'s own stage-pixel inputs) already receive
   correctly-converted stage-pixel coordinates — see `docs/input.md`.
2. ~~Previous-tick input state (edge detection)~~ — **done, sub-fix 3/N
   (2026-08-19)**, though NOT actually needed by hit-testing itself (a
   single "is point P inside object O RIGHT NOW" query is inherently
   level/instantaneous, as originally noted below) — it's needed for
   whatever's built ON TOP of hit-testing next (press/release/rollOver/
   rollOut are edge-triggered). Hit-testing
   itself does not need this.

## Planned algorithm (bounding-box first pass, per the task charter) — IMPLEMENTED AS WRITTEN

The pseudocode below matches `MovieClipInstance::hitTestPoint()`/
`hitTestPointInOwnSpace()` (`src/runtime/MovieClipInstance.cpp`)
essentially line-for-line — kept here unedited as a record that the
original design held up with no surprises during implementation.

```
hitTestPoint(clip, pointInClipsOwnLocalSpaceTwips) -> topmost hit MovieClipInstance/leaf, or none:
    if !clip.visible(): return none        // invisible objects never receive input
    for (depth, entry) in clip.timeline().displayList().entries() in REVERSE depth order:
        // reverse (topmost-first) so an overlapping higher-depth object
        // wins, matching real Flash's front-to-back hit-test order —
        // SceneRenderer walks ASCENDING (back-to-front, for painting);
        // hit-testing deliberately walks the opposite direction.
        if entry has a MovieClipInstance child at this depth:
            childLocalPoint = inverse(child.localMatrix()) applied to pointInClipsOwnLocalSpaceTwips
            result = hitTestPoint(child, childLocalPoint)   // recurse
            if result: return result
        else:
            leafBounds = characterOwnBoundsRect(resolved character)
            leafLocalPoint = inverse(entry.matrix) applied to pointInClipsOwnLocalSpaceTwips
            if leafBounds contains leafLocalPoint: return (clip, characterId, depth)
    return none
```

Entry point: `MovieClipInstance::hitTestPoint(stageXPixels, stageYPixels)`
— callable on ANY instance (not just root), generalized slightly beyond the
original pseudocode: it first inverts `worldMatrix()` (this instance's own
matrix_ composed with every ancestor's) to convert the given STAGE-pixel
point into `this` instance's own local space, THEN runs exactly the
recursive walk above scoped to `this`'s own subtree. Calling it on the root
instance (the typical/intended usage) is exactly the pseudocode's
`hitTestPoint(root, ...)` entry point.

**Matrix inversion**: implemented as `swf::invertMatrix(const Matrix&,
Matrix* out) -> bool` (`src/swf/SwfRecords.h/.cpp`) — needed for the
"transform a point from parent space into local space" step above (the
reverse of `swf::concatMatrix`/`transformPoint`, which only go
local-to-parent). Standard 2x3 affine inverse; returns `false` (leaving
`*out` untouched) for a degenerate matrix (`scaleX*scaleY -
rotateSkew0*rotateSkew1` within `1e-9` of zero — e.g. `_xscale = 0`),
exactly as this section originally called for — confirmed via a dedicated
regression test (`MovieClipInstance_HitTestPoint_DegenerateScale_
ReturnsNullopt`) that a zero-`_xscale` clip correctly becomes un-hit-
testable rather than crashing or producing garbage.

**Why bounding-box, not exact shape**: per the task charter — "acceptable
to use bounding-box hit testing if exact vector shape hit testing does not
yet exist... clearly mark it as intermediate... architecture must allow
exact shape hit testing later." The design above satisfies this: exact
shape hit-testing would only need to replace the leaf `leafBounds contains
leafLocalPoint` check with a real point-in-polygon test against the
character's tessellated shape (reusing `ShapeTessellator`'s output) — the
recursive tree-walk, coordinate transforms, visibility check, and
topmost-wins ordering all stay identical. Nothing about the bounding-box
version needs to be thrown away later.

## Explicitly deferred design questions (not yet decided, no code depends on an answer yet)

- Whether `HitTest`-state records should be REQUIRED for a button to be
  hit-testable at all (some real content omits them, relying on Up-state
  geometry as the de facto hit area — real Flash does this too, and
  `characterOwnBoundsRect()`'s existing fallback-to-Up-state logic already
  matches this) vs. treated as an error/warning.
- Whether a `MovieClip` (not just a `Button`) should be hit-testable by
  default for its OWN mouse events (real Flash: yes, any clip with an
  `onClipEvent(press)`/`onRelease`-style handler, or `useHandCursor`/
  `_droptarget`-adjacent APIs) even without a `Button` character at all —
  the algorithm above already treats this uniformly (any leaf/child with
  resolvable bounds is hit-testable), so no special-casing should be
  needed, but this hasn't been exercised against real content yet.
