# Hit Testing

**Interactivity phase (2026-08-18). Status: NOT YET IMPLEMENTED — this doc
is a design/architecture record for the next fix in the interactivity
dependency chain, written per the audit charter's "document the chosen
behavior, don't guess" instruction, so a future session doesn't have to
re-derive this from scratch.** See `docs/interactivity-audit.md` §4/§8 for
the audit finding (zero hit-testing code exists anywhere in this codebase
today) and the dependency-chain reasoning for why bounds computation
(`_width`/`_height`, done this turn — see `docs/known-limitations.md`) had
to come first.

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

## What's still missing (blockers, in dependency order — see `docs/interactivity-audit.md` §8)

1. **Device-px -> stage-twips inverse coordinate mapping.** `InputState`'s
   mouse position is in DEVICE/SCREEN pixel space (see
   `docs/interactivity-audit.md` §2's confirmed finding that this doesn't
   currently match stage pixel space in general). Hit-testing needs a
   stage-twips point as its starting input — this conversion doesn't exist
   yet and must be built first.
2. **Previous-tick input state (edge detection)** — needed for anything
   built ON TOP of hit-testing (press/release/rollOver/rollOut are
   edge-triggered), not for hit-testing itself (a single "is point P inside
   object O RIGHT NOW" query is inherently level/instantaneous). Hit-testing
   itself does not need this.

## Planned algorithm (bounding-box first pass, per the task charter)

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

Entry point: `hitTestPoint(root, screenPointConvertedToStageTwips)` — needs
blocker #1 above resolved first to produce that starting stage-twips point.

**Matrix inversion**: `swf::Matrix` has no `invert()` yet — needed for the
"transform a point from parent space into local space" step above (the
reverse of `swf::concatMatrix`/`applyMatrix`, which only go
local-to-parent). Standard 2x3 affine inverse (assuming the matrix is
non-degenerate, i.e. `scaleX*scaleY - rotateSkew0*rotateSkew1 != 0` — a
degenerate/zero-determinant matrix, e.g. `_xscale = 0`, should make the
object correctly un-hit-testable, matching real Flash: a zero-size object
can't be clicked). This is a new, small, well-scoped piece of work distinct
from bounds computation — natural candidate to bundle with hit-testing's
own implementation turn rather than doing speculatively now.

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
