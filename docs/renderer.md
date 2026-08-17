# Renderer

**Status: Phase 3 basic renderer implemented.** Phase 10 (Nintendo 3DS
backend, dual-screen) is still not started.

## Architecture

```
Timeline::displayList()  ──┐
CharacterDictionary       ─┼──▶  SceneRenderer  ──▶  IRenderer
Movie::frameSize (stage)  ─┘                          │
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
- **`SceneRenderer`** (`src/renderer/SceneRenderer.h/.cpp`) — walks a
  `Timeline`'s current `DisplayList` in depth order (back-to-front, per the
  SWF display model), resolves each entry's `characterId` via
  `CharacterDictionary`, composes world transforms with `concatMatrix`
  (parent-then-child), tessellates shape characters on the fly, and
  recurses into `DefineSprite` characters at their placement transform.
  Converts local shape points → world twips (via the composed matrix) →
  device pixels (via a stage-size-to-viewport pixel scale) before handing
  geometry to `IRenderer`.

## Known Phase 3 limitations

- **No independent sprite playhead.** Every placed instance of a given
  `DefineSprite` character currently shares one lazily-built `Timeline` and
  therefore renders at whatever frame that Timeline is on (frame 1
  initially) — there's no AVM1/frame-advance model per *instance* yet.
  Proper per-instance `MovieClip` state (independent play position, its own
  `gotoAndStop`/`nextFrame`) arrives with the AVM1 VM and MovieClip API
  (Phase 4/5).
- **No color transform application.** `PlaceObject`/`PlaceObject2`'s
  `ColorTransform` (tint/alpha) is parsed and stored on `DisplayListEntry`
  but not yet applied when rendering — shapes render at their raw fill
  color regardless of any color transform on their placement.
  `DisplayListEntry::clipDepth` (clip layers) is similarly parsed but not
  yet honored by the renderer. Kept out of scope to keep Phase 3's
  rendering surface small and testable; both are natural Phase 8+ (or
  earlier, if a target title needs it) follow-ups.
- **No background color tag support.** `SetBackgroundColor` isn't parsed
  yet, so `SceneRenderer::render` always clears to white.
- **Bitmap and text characters are not resolved at all.**
  `CharacterDictionary` only knows `DefineShape`/`2`/`3` and `DefineSprite`;
  `DefineBits*`/`DefineText*`/`DefineFont*`/`DefineButton*` references
  resolve to nothing and are silently skipped by `SceneRenderer` (Phase 8+).
- **Recursion guard.** `SceneRenderer` caps sprite-in-sprite recursion at 64
  levels to guard against a malformed/cyclic sprite reference; exceeding it
  logs a warning and stops that branch rather than crashing.

## CLI usage

```sh
flash_runtime movie.swf --render <frame> out.ppm
```

Renders the given 1-based frame of the movie's top-level timeline to a
binary PPM file sized to the movie's stage dimensions in pixels (`viewer`,
`convert`, or any PPM-capable image tool can open it).

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
  is right, not just identity-transform rendering).
