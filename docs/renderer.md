# Renderer

**Status: Phase 3 basic renderer implemented; Phase 5 wired it to a real
MovieClipInstance tree (independent per-instance playheads); Phase 8 added
static text, non-interactive button, and edit-text glyph rendering; Phase 9
fixed a shape-parsing bug (see below) found by rendering real content;
Phase 10 added a real Nintendo 3DS `IRenderer` (top screen only — see
below).**

**Phase 10 — Nintendo3DSRenderer.** `src/renderer/Nintendo3DSRenderer.h/.cpp`
implements `IRenderer` by composition: it owns a `SoftwareRenderer` sized to
the logical (non-rotated) stage dimensions and forwards
`beginFrame`/`fillPolygon`/`strokePolyline` to it completely unchanged
(reusing the exact same, already-tested CPU rasterizer the desktop build
uses), then in `endFrame()` reads every pixel back out via
`SoftwareRenderer::pixelAt()` and blits it into the real 3DS LCD
framebuffer obtained via libctru's `gfxGetFramebuffer(...)`.

The rotated/column-major framebuffer indexing formula
(`(x * fbWidth + (fbWidth - 1 - y)) * bytesPerPixel`) was cross-checked
against libctru's own `source/gfx.c` (not assumed from general homebrew
folklore) — see `Nintendo3DSRenderer.h`'s header comment for the exact
reasoning and the `GSP_SCREEN_WIDTH`/`GSP_SCREEN_HEIGHT_TOP`/
`GSP_SCREEN_HEIGHT_BOTTOM` constants involved (the formula is identical for
both screens — only the logical width/height and `gfxScreen_t` argument
differ). Default pixel format assumed is `GSP_BGR8_OES` (3 bytes/pixel,
B-G-R order), per `gfxInitDefault()`'s own documented default. The user
confirmed the Phase 10 `.3dsx` boots and runs in Azahar; pixel-exact
visual confirmation of the framebuffer orientation specifically (right
side up, no off-by-one row/column) has not been separately reported.

**Dual-screen support and the `presentFrame()` fix.** The test app
(`nintendo3ds_main.cpp`) now drives both screens every frame — one
`Nintendo3DSRenderer` instance per screen (`GFX_TOP` at 400x240, `GFX_BOTTOM`
at 320x240). This surfaced a real finding while integrating it: libctru's
`gfxFlushBuffers()`/`gfxSwapBuffers()` are **global, both-screens**
operations (confirmed directly in `source/gfx.c` — `gfxSwapBuffers()`
unconditionally calls `gfxScreenSwapBuffers()` for both `GFX_TOP` and
`GFX_BOTTOM`), not per-screen. `endFrame()` originally called both once per
invocation, which was correct by coincidence when only one screen (one
`endFrame()` call per real frame) existed, but double-swaps each screen's
buffer index once a second screen is added — showing stale/flickering
content instead of the frame just drawn. Fixed by moving both calls out of
`endFrame()` (which now only blits pixels) into a new static
`Nintendo3DSRenderer::presentFrame()`, called exactly once per real frame
after every active screen's `endFrame()` has run — see that method's own
comment for the full mechanism.

**Bottom-screen button/circle-pad/touch test picture.** `nintendo3ds_main.cpp`'s
`drawButtonTestScreen()` draws a live diagnostic picture on the bottom
screen every frame using `IRenderer::fillPolygon`/`strokePolyline` directly
(no SWF content involved) — a labeled-by-position (no text rendering
available; see below) box per D-Pad direction/face button/shoulder
button/Start/Select that fills bright green while held, a bounding box +
offset dot for the Circle Pad (raw `hidCircleRead()`), and a dot at the
current touch position (raw `hidTouchRead()`, drawn whenever it reads
non-`(0,0)` — see `docs/input.md`'s `KEY_TOUCH` reliability note for why
that heuristic is used instead of the `KEY_TOUCH` bit). This exercises
`Nintendo3DSInput`'s underlying `hid` calls plus `IRenderer`'s primitives on
a second, differently-sized screen — a genuinely more thorough test than
the top-screen SWF demo alone, though still not a substitute for actually
watching it run (no font/text rendering is wired into this diagnostic
picture, so button identity is conveyed only by screen position, not a
label).

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
  interface: `beginFrame`/`endFrame`, `fillPolygon`, `strokePolyline`, and
  (added 2026-08-28, see "Gradient rendering" below) `fillPolygonGradient`.
  Not coupled to OpenGL, OpenGL ES, or citro3d — the same `SceneRenderer`
  walk drives any implementation.
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
  fill region. Linear gradient fills render as real per-pixel gradients
  (see "Gradient rendering" below); radial/focal-radial gradient fills are
  still reduced to the average of their stop colors, and bitmap fills are
  still reduced to a flat gray placeholder (bitmap decoding is
  unimplemented). Revisit with real edge-merging if/when target content
  (see `docs/compatibility.md`) needs it.

### Gradient rendering (2026-08-28)

Real per-pixel linear gradient rendering, replacing the flat-averaged-color
approximation for `FillStyleType::kLinearGradient` fills — the fix behind
Hobo1's title screen "HOBO"/"CONTROLS"/"PLAY!" text now showing a real
color ramp instead of a single muddy flat color. Scoped to linear gradients
only, by real-corpus evidence (`/tmp/gradient_scan.cpp`, a throwaway
diagnostic that scanned every `DefineShape`/`2`/`3` fill style — including
inside nested `DefineSprite` tag streams — across real `hobo.swf`): 2990
shapes, 13,254 solid fills, 161 linear gradient fills, and **zero**
radial/focal-radial/bitmap fills. Following this project's established
evidence-driven-scope discipline (see the top-level `CLAUDE.md`'s Roadmap
Phase 8/9 entries — `Math`-only, `DefineMorphShape`-v1-only — for the same
pattern applied elsewhere), radial/focal-radial gradients and bitmap fills
deliberately stay on the pre-existing flat-color-average path rather than
being implemented against no corpus evidence.

The pipeline, end to end:

1. **`ShapeTessellator`** (`buildGradientRamp()`) resolves a `Gradient`'s
   `GradientRecord` stops into a 256-entry color ramp (linear interpolation
   between bracketing stops, matching the SWF spec's 0-255 ratio space) at
   tessellation time, stored in a `TessellatedPolygon`'s new
   `PaintKind::kLinearGradient`/`GradientPaint` fields alongside the
   existing flat `color` (which stays populated as a degenerate-matrix
   fallback — see step 2). `GradientPaint::matrix` is the FillStyle's own
   `gradientMatrix`, unchanged, still in the shape's own local twips space.
2. **`SceneRenderer`** (`resolveGradientFill()`, file-local in
   `SceneRenderer.cpp`) composes `gradientMatrix` with the shape's world
   matrix and the stage's twips-per-pixel scale into one forward affine
   transform (SWF gradient-square space -> device pixels), then inverts it
   (device pixels -> gradient-square space) so `IRenderer::
   fillPolygonGradient()` can map each pixel it visits directly back to a
   ramp index with no further matrix work. A near-singular (degenerate)
   transform falls back to the polygon's flat `color` via the ordinary
   `fillPolygon()` call, same as a gradient scaled to nothing would look in
   a real player. The 256-entry ramp is also `ColorTransform`-applied here
   (256 `applyColorTransform()` calls per gradient-filled polygon — a cold
   path, not the tuned raster hot path, so this cost is deliberately not
   optimized).
3. **`SoftwareRenderer`/`Nintendo3DSRenderer`** (`fillPolygonGradient()`/
   `fillSpanGradient()`) rasterize the polygon with the SAME active-edge-
   table scanline algorithm `fillPolygon()`/`fillSpan()` already use for
   flat fills — deliberately a full, independent copy rather than a shared
   refactor, so this addition can never risk the flat-fill path's own
   measured, tuned performance (see this file's "Performance"/pacing
   history in `SoftwareRenderer.cpp`). `GradientSpreadMode` (`kPad`/
   `kReflect`/`kRepeat`) is applied per-pixel before the ramp lookup.

See `IRenderer.h`'s `DeviceGradientFill` doc comment for the full
coordinate-space contract, and `docs/swf-support.md`'s `FILLSTYLEARRAY` row
for the current support-matrix status.
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
