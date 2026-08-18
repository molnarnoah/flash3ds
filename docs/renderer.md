# Renderer

**Status: Phase 3 basic renderer implemented; Phase 5 wired it to a real
MovieClipInstance tree (independent per-instance playheads); Phase 8 added
static text, non-interactive button, and edit-text glyph rendering; Phase 9
fixed a shape-parsing bug (see below) found by rendering real content.**
Phase 10 (Nintendo 3DS backend, dual-screen) is still not started.

**Phase 9 validation.** Rendered a real `hobo.swf`'s title screen
end-to-end for the first time and found the output recognizably correct —
character illustration, wordmark, button, and panel art all in the right
places and colors — which surfaced a real `ShapeRecords.cpp` bug (see
`docs/compatibility.md`): shapes with more than one style region were
desyncing the shape record bitstream and rendering garbage fill colors for
everything past the first style change. Fixed; one shape's parsed
bounds/fill-color were also independently cross-checked by hand against
the raw SWF bytes in Python, matching exactly.

## Architecture

```
MovieClipInstance tree    ──┐
CharacterDictionary        ─┼──▶  SceneRenderer  ──▶  IRenderer
Movie::frameSize (stage)   ─┘                          │
                                                        ├── SoftwareRenderer (desktop/testing, this phase)
                                                        └── Nintendo3DSRenderer (Phase 10, not started)
```

- **`IRenderer`** (`src/renderer/IRenderer.h`) — the abstract pixel-output
  interface: `beginFrame`/`endFrame`, `fillPolygon`, `strokePolyline`. Not
  coupled to OpenGL, OpenGL ES, or citro3d — the same `SceneRenderer` walk
  drives any implementation.
- **`SoftwareRenderer`** (`src/renderer/SoftwareRenderer.h/.cpp`) — the
  desktop/testing implementation: an RGBA8 framebuffer, even-odd scanline
  polygon fill with alpha blending, a naive Bresenham stroke rasterizer, and
  a binary PPM (P6) file writer for inspecting output without a display.
- **`ShapeTessellator`** (`src/renderer/ShapeTessellator.h/.cpp`) —
  converts a parsed `swf::Shape` (fill/line styles + SHAPERECORD stream)
  into flat polygons/polylines in local twip space. **Deliberately
  simplified**: treats each contiguous run of edges starting at a
  StyleChangeRecord's MoveTo as one closed polygon (fillStyle1-preferred),
  rather than doing full edge-pair boundary merging — this renders simple
  single-contour shapes (rectangles, stars, most simple vector art)
  correctly, but does **not** correctly render shapes with holes (e.g. the
  letter "O") or shapes built from multiple StyleChangeRecords sharing one
  fill region. Gradient fills are reduced to the average of their stop
  colors; bitmap fills are reduced to a flat gray placeholder (bitmap
  decoding is unimplemented). Revisit with real edge-merging if/when target
  content (see `docs/compatibility.md`) needs it.
- **`SceneRenderer`** (`src/renderer/SceneRenderer.h/.cpp`) — takes a
  `runtime::MovieClipInstance&` root (Phase 5) and walks its `Timeline`'s
  current `DisplayList` in depth order (back-to-front, per the SWF display
  model). For an entry that resolves to a Shape character, resolves it via
  `CharacterDictionary`, tessellates it on the fly, and composes its world
  transform with `concatMatrix` (parent-then-child). For an entry that
  resolves to a sprite/MovieClip character, recurses into that depth's own
  `MovieClipInstance` CHILD instead — using its own (possibly AVM1-script-
  mutated) `localMatrix()`/`visible()` and its own independently-advancing
  `Timeline`, rather than Phase 3's shared per-*character* Timeline cache.
  Converts local shape points → world twips (via the composed matrix) →
  device pixels (via a stage-size-to-viewport pixel scale) before handing
  geometry to `IRenderer`.
- **Text/button/edit-text leaf rendering (Phase 8).** `SceneRenderer`'s
  entry point for a non-sprite display-list entry, `renderCharacter()`,
  now dispatches on the resolved `CharacterDef`'s actual kind instead of
  assuming Shape: `swf::TextDef`/`swf::EditTextDef` glyph runs are drawn by
  `renderGlyph()` (looks up each glyph's outline in its `swf::FontDef`,
  scales it by `textHeight/1024` — see `docs/avm1-support.md`'s "Text/Font"
  section — and reuses `ShapeTessellator` via a synthesized one-fill-style
  `Shape`, so every `ShapeTessellator` limitation above applies to glyph
  outlines too, e.g. no hole support); `swf::ButtonDef` draws only its
  "Up"-state records (no mouse hit-testing/state machine exists yet — see
  `docs/avm1-support.md`'s Known Phase 8 limitations).

## Known limitations

- **No color transform application.** `PlaceObject`/`PlaceObject2`'s
  `ColorTransform` (tint/alpha) is parsed and — as of Phase 5 — tracked
  per-instance on `MovieClipInstance::colorTransform()` (and AVM1's `_alpha`
  reads/writes it), but it is still not actually APPLIED when rendering —
  shapes render at their raw fill color regardless of any color transform
  on their placement or any script-set `_alpha`. `DisplayListEntry::
  clipDepth` (clip layers) is similarly parsed but not yet honored by the
  renderer. Kept out of scope to keep the rendering surface small and
  testable; both are natural Phase 8+ (or earlier, if a target title needs
  it) follow-ups.
- **No background color tag support.** `SetBackgroundColor` isn't parsed
  yet, so `SceneRenderer::render` always clears to white.
- **Bitmap characters are still not resolved at all.**
  `CharacterDictionary` knows `DefineShape`/`2`/`3`, `DefineSprite`,
  `DefineSound`, and (as of Phase 8) `DefineFont`/`2`/`DefineText`/`2`/
  `DefineButton`/`2`/`DefineEditText`; `DefineBits*` (bitmap) references
  still resolve to nothing and are silently skipped by `SceneRenderer`
  (later phase, or earlier if a target title needs it).
- **Button rendering has no interactivity.** `SceneRenderer` always draws a
  button's "Up" state — there's no mouse position, no hit-testing against a
  record's bounds, and no Over/Down state switching (see the Phase 8 bullet
  above and `docs/avm1-support.md`'s Known Phase 8 limitations).
- **`DefineEditText` rendering is narrow.** Only fields with BOTH an
  embedded font that has a code table (`DefineFont2`, not a legacy
  `DefineFont` v1 — see `docs/swf-support.md`) AND literal `initialText`
  render at all; there's no word-wrap, scrolling, alignment, or variable-
  binding (`_root.myField` <-> displayed text) — see
  `renderEditTextCharacter()`'s doc comment in `SceneRenderer.cpp`.
- **Recursion guard.** `SceneRenderer` caps sprite-in-sprite recursion at 64
  levels to guard against a malformed/cyclic sprite reference; exceeding it
  logs a warning and stops that branch rather than crashing.

## CLI usage

```sh
flash_runtime movie.swf --render <frame> out.ppm
```

Renders the given 1-based frame of the movie's root timeline to a binary PPM
file sized to the movie's stage dimensions in pixels (`viewer`, `convert`,
or any PPM-capable image tool can open it).

As of Phase 5, this builds a full `MovieClipInstance` tree and TICKS it
forward frame-by-frame (running every intermediate frame's `DoAction`
scripts, per-instance) to reach the requested frame, rather than jumping
straight there — matching how a real player gets to that frame. If a script
calls `stop()` along the way, later frames simply aren't reached (the tool
reports the frame it actually landed on), which is correct behavior, not a
bug.

## Testing

- `tests/test_shape_tessellator.cpp` — tessellation algorithm correctness
  (rectangle → closed polygon, line-only shape → stroke with no fill,
  curved-edge subdivision point count and endpoint accuracy) against
  directly-constructed `swf::Shape` structs (not byte fixtures — this tests
  the algorithm, not the parser).
- `tests/test_software_renderer.cpp` — pixel-level checks: background
  clear, polygon fill (inside vs. outside), out-of-bounds polygon points
  don't crash (they clip), stroke plots pixels along the expected line, PPM
  output has a valid P6 header.
- `tests/test_scene_renderer.cpp` — end-to-end: loads a fixture SWF with a
  shape character placed via `PlaceObject2`, renders it, and checks the
  expected device pixels are filled/unfilled; a second test nests a shape
  inside a `DefineSprite` placed at an offset and checks the *composed*
  world position is correct (proving `concatMatrix` parent/child ordering
  is right, not just identity-transform rendering). Both now build a
  `MovieClipInstance` tree via `ScriptEnvironment`/`createRoot()` rather
  than a bare `Timeline`. Phase 8 added three more: a `DefineText` glyph
  lands at its expected scaled/translated device pixel; a `DefineButton`
  draws only its Up-state record (a Down-state record elsewhere must NOT
  appear); a `DefineEditText` field with an embedded `DefineFont2` font
  renders its `initialText`'s glyph.
- `tests/test_movieclip_instance.cpp` (Phase 5) — AVM1 bytecode actually
  driving a `MovieClipInstance` tree (as opposed to `test_avm1_*.cpp`, which
  tests the interpreter in isolation): `GetProperty`/`SetProperty` and
  `GetMember`/`SetMember` round-tripping `_x`, independent per-instance
  playheads under repeated `advanceFrame()` ticks, `Stop`/`SetTarget`
  affecting only the intended clip, `CloneSprite`/`RemoveSprite`,
  `_root`/`_parent` identity, and `resolvePath()`'s relative/absolute/`..`
  path resolution.
