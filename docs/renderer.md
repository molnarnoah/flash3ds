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
  into flat polygons/polylines in local twip space. As of the
  run-scoped fill fix (2026-08-30, see below), it splits each pen-drawn
  run (the edges between one MoveTo and the next) into one polygon PER
  FILL STYLE actually used within that run, in authored edge order — so a
  style-only change with no MoveTo now correctly starts a new polygon
  instead of silently continuing under the stale style. This renders
  simple single-contour shapes AND multi-region shapes authored as one
  continuous pen run correctly, but still does **not** correctly render
  shapes with holes (e.g. the letter "O": an outer boundary and an inner
  counter loop authored as two separate MoveTo'd contours under the same
  fill style still render as two solid same-color patches instead of an
  even-odd cutout — see "Run-scoped fill reconstruction" below for why
  this is a deliberately separate, still-open problem). Linear gradient
  fills render as real per-pixel gradients (see "Gradient rendering"
  below); radial/focal-radial gradient fills are still reduced to the
  average of their stop colors, and bitmap fills are still reduced to a
  flat gray placeholder (bitmap decoding is unimplemented).

### Run-scoped fill reconstruction (2026-08-30)

Fixes a real user-reported bug: hobo.swf's title/menu screen was rendering
"only base colors... not everything" — every fill region past the first
in a shape was missing whenever the shape's SHAPERECORD stream switched
fill style mid-run via a style-only `StyleChangeRecord` (no `hasMoveTo`).
The pre-fix tessellator treated each MoveTo-to-MoveTo pen run as ONE
polygon using whichever fill style was active when the run *started* —
correct for simple content, but silently wrong for any run whose style
changed partway through with no new MoveTo, which is exactly how a fair
amount of hobo.swf's vector art (adjacent same-run color regions) is
authored.

**The fix.** `tessellateShape()` now tracks a `fillSubpath`/
`fillSubpathStyle` accumulator alongside the pre-existing stroke-subpath
one, flushing it into a closed `TessellatedPolygon` whenever the resolved
fill style (fillStyle1, falling back to fillStyle0) changes mid-run — in
addition to the existing MoveTo and end-of-shape flush points inherited
from the stroke path. Edges stay in their authored sequential order within
one run, so this needs no endpoint search or matching at all.

**Two more "textbook-correct" designs were tried first and rejected**,
after each caused a real regression confirmed by directly comparing
before/after renders of hobo.swf (not just unit tests, which didn't catch
either regression):

1. *Whole-shape edge grouping with dual (fillStyle0 + fillStyle1)
   contribution, chained by matching endpoints across the entire shape.*
   The spec's own every-edge-borders-two-regions model, and the most
   "correct" of the three attempts on paper. Broke hobo.swf's mute-button
   icon (characterId=89): its black speaker-cutout contour, which the
   original content already authors as several independently-closed
   MoveTo'd contours, got welded across unrelated contours by the
   endpoint-matching step whenever two different contours happened to
   share a coordinate — producing a garbled ~65-point polygon — and the
   white circle behind it lost its cutout entirely (rendered solid white)
   from the spurious reversed fillStyle0 contribution.
2. *The same whole-shape chaining, with contours additionally filtered by
   winding sign* (shoelace-formula sign, keeping only contours matching
   the dominant contour's sign) as a hole-vs-region heuristic. Did not fix
   the mute icon (the scrambling happens during endpoint chaining, before
   any sign check runs) and introduced a second regression: hobo.swf's
   "Armor Games" caption, whose separate letters legitimately share one
   fill style and wind in mixed directions, had its non-dominant-sign
   letters discarded, garbling the text into "Amrae".

Both were dropped in favor of the final run-scoped design specifically
because scoping fill-chain construction to within one MoveTo-bounded run
makes both failure modes structurally impossible: there's no endpoint
search to weld unrelated contours together, and there's no cross-contour
winding-sign judgment call to get wrong.

**Verification.** All 431 unit tests pass, including a new regression
test (`TessellateShape_TwoAdjacentSquaresNoMoveToBetween_
ProducesTwoSeparatePolygons`) reproducing the exact bug pattern. Re-ran
`/tmp/dump_tess.cpp` (a throwaway diagnostic linking directly against
`libflash3ds_core.a`) against hobo.swf's characterId=89 (the mute icon)
and confirmed it now tessellates into exactly 6 polygons — matching its 6
MoveTo-bounded subpaths 1:1 — with clean, monotonic point sequences and no
cross-contour scrambling. Rendered hobo.swf frame 1 before and after (via
`git stash` on just the tessellator files) and compared crops directly:
after the fix, the HOBO logo, CONTROLS text, gradient character panel, and
"Armor Games" caption all show their full multi-region detail that was
previously missing or muddy, and the mute-icon crop shows the correct
white circle with its black speaker cutout intact — neither of the two
rejected iterations' regressions reappear.

**Was open, now closed** — see "Hole/counter rendering" below
(2026-08-31): a single fill style whose edges form multiple disjoint
contours where one is genuinely meant to be a hole in the other (the
letter "O" case) used to render as two solid same-color patches rather
than a true even-odd cutout. This no longer happens.

### Hole/counter rendering (2026-08-31, Priority Fix List item #1)

Closes the gap left open above. `ShapeTessellator` now assigns every
`TessellatedPolygon` a `fillGroupId` — the identity of the resolved
`swf::FillStyle` it came from, in first-seen order per `tessellateShape()`
call (see `ShapeTessellator.h`'s own "Hole/counter rendering" comment for
the full field-level writeup). `IRenderer` gained `fillPolygonGroup()`/
`fillPolygonGradientGroup()`, which fill several closed contours together
in ONE combined even-odd scanline pass (reusing `SoftwareRenderer`'s
existing AET sweep unchanged, just built from multiple contours' edges
instead of one) — a point covered by an odd number of the group's
contours is filled, an even number (inside both an outer boundary and an
inner counter) is left as background, producing a real cutout.
`SceneRenderer.cpp`'s new `fillTessellatedPolygons()` groups a shape's
polygons by `fillGroupId` and issues one combined call per group instead
of one `fillPolygon()` call per contour.

This deliberately requires no edge welding, endpoint matching, or
winding-sign filtering — the two properties that sank the two rejected
run-scoped-predecessor designs above — because it never changes what a
contour IS, only which already-correct contours get painted together
vs. independently.

**A real regression was found and fixed during this same change**,
against the same characterId=89 mute-button icon called out in both
rejected designs above — worth recording since it's a genuinely different
failure mode from either of those. The icon emits polygons in the order
{white outer circle, black detail x3, white highlight, black detail x1};
the two white contours share a `fillGroupId` (same `FillStyle`) but are
NOT a boundary+hole pair — the second white contour is a deliberate
painter's-algorithm overpaint, meant to be drawn AFTER the three black
contours to restore a white highlight on top of them. An initial version
of `fillTessellatedPolygons()` scanned the *entire* remaining polygon list
for same-`fillGroupId` matches regardless of what sat between them,
pulling that second white contour forward and combining it with the
first — which both discarded the "repaint on top of black" step entirely
(rendering a plain solid white circle, confirmed via a real hobo.swf
render) and applied a spurious even-odd hole-punch between two contours
that were never meant to be combined. The fix: only combine same-
`fillGroupId` contours when they're **contiguous** in the polygon list —
i.e. no differently-styled contour sits between them in the tessellator's
authored-order emission. A genuine boundary+hole pair (the actual target
of this whole fix) is unaffected, since nothing of a different fill style
is ever tessellated between two contours of the very same fill run in
that case; only spurious combinations across an intervening different-
style overpaint are prevented.

**Verification.** All 434 unit tests pass (431 pre-existing + 3 new:
`fillGroupId` assertions in `test_shape_tessellator.cpp`,
`SoftwareRenderer_FillPolygonGroup_TwoNestedContours_InnerBecomesHole`
and `SoftwareRenderer_FillPolygonGroup_SingleContour_MatchesPlainFillPolygon`
in `test_software_renderer.cpp`). Rendered hobo.swf frame 1 before and
after (via `git stash` on just the affected files) and compared crops
directly: the character-portrait panel AND the CONTROLS keybox panel now
render as proper hollow-bordered frames instead of solid filled
rectangles, the "A"/"S" control-key letters now show their counters
correctly, the mute-button icon renders pixel-identical to its original
(already-correct) appearance, and the HOBO logo/CONTROLS text/"Armor
Games" caption — the previously-fragile regions from the two rejected
designs above — are unchanged. No other differences were found within the
full before/after diff bounding box (20,452 pixels, x:[176,422],
y:[107,378]).

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

### Bitmap rendering (2026-08-31, Priority Fix List item #2)

Real per-pixel bitmap-fill rendering, replacing the flat gray
(160,160,160,255) placeholder every bitmap fill previously rendered as (a
hard visual ceiling for any title, like Extreme Pamplona, that leans on
bitmap art rather than vector shapes). Scope confirmed by real-corpus
tag histograms (`tools/swf_diagnostic`, all 8 Hobo titles + Extreme
Pamplona's loader and every reachable content sub-SWF): `DefineBitsLossless`
(20), `DefineBitsLossless2` (36), `DefineBitsJpeg2` (21), and
`DefineBitsJpeg3` (35) are the only variants present anywhere in the
corpus — `DefineBits`(6)/`JPEGTables`(8) (needs an external shared-tables
tag this parser doesn't thread through) and `DefineBitsJpeg4`(90) (adds a
deblocking-filter field on top of JPEG3) have zero occurrences and stay
unimplemented, same evidence-driven-scope discipline as the Gradient
rendering section above.

`src/swf/DefineBitsTag.h/.cpp` decodes all four supported tags EAGERLY
(unlike every other `Define*` parser in this codebase, which only reads
structural fields — see that file's header comment for why bitmaps are the
one deliberate exception) into one normalized representation,
`swf::BitmapDef` (straight, non-premultiplied RGBA8, row-major): lossless
tags are zlib-inflated per BitmapFormat (3 = 8-bit colormapped through a
color table, 4 = 15-bit RGB, 5 = 24-bit RGB / 32-bit premultiplied ARGB,
un-premultiplied at parse time so downstream code never needs to know
which format a bitmap came from), and JPEG tags are decoded via a newly
vendored decoder, `third_party/jpgd` (Rich Geldreich's Public-Domain/
Apache-2.0 `jpeg-compressor`, chosen over LGPL `libjpeg` for the same
no-dynamic-linking-on-3DS-homebrew reason `third_party/minimp3` documents
its own license choice).

**Real-corpus finding: the "erroneous EOI/SOI" JPEG quirk.** Every
`DefineBitsJpeg2`/`3` tag across the corpus except one initially failed to
decode. Investigation (`tools/real_game_harness/bitmap_ram_probe.cpp` plus
ad hoc byte-level extraction) found the raw tag bytes are not always one
self-contained JPEG stream as the spec describes: several encoders instead
emit a shared quantization/Huffman-TABLES segment (SOI+DQT+DHT), then a
spurious 4-byte `0xFF 0xD9 0xFF 0xD8` ("EOI SOI") marker pair, then the
actual per-image segment (its own APP0/SOF/SOS/entropy-coded scan/EOI)
which references the FIRST segment's tables by ID without redefining
them — a real, well-known Flash-authoring-tool encoding quirk, not a
corrupt file. `swf::stripErroneousEoiSoiMarkers()` (file-local in
`DefineBitsTag.cpp`) splices out every occurrence of that exact 4-byte
sequence before handing the bytes to `jpgd`, producing one continuous,
valid JPEG stream — verified via a standalone probe against every failing
corpus sample: 100% decode success, zero regression on the one sample that
never had the quirk. See `DefineBitsTag.cpp`'s own comment on
`stripErroneousEoiSoiMarkers()` for the byte-offset evidence and the
(disproven) simpler "trim to last SOI" theory this project tried first.

The fill-rendering pipeline mirrors the Gradient rendering pipeline above
exactly, end to end:

1. **`ShapeTessellator`** resolves a bitmap `FillStyle` into
   `PaintKind::kBitmap`/`BitmapPaint` (matrix + `bitmapCharacterId` +
   `repeat`/`smoothed` flags) — deliberately NOT resolving actual pixel
   data at tessellation time, since `ShapeTessellator` has no
   `CharacterDictionary` access by design (same separation of concerns
   `docs/architecture.md` documents elsewhere).
2. **`SceneRenderer`** (`resolveBitmapFill()`, file-local in
   `SceneRenderer.cpp`) looks the bitmap character up via
   `CharacterDictionary::find()`, then composes/inverts the shape's own
   `BitmapMatrix` with its world matrix and the stage's twips-per-pixel
   scale — same matrix-inversion shape as `resolveGradientFill()` — with
   one extra step: per the SWF spec's "20 twips per bitmap pixel at 100%
   zoom" convention, every one of the six resulting affine coefficients is
   additionally divided by 20 to fold shape-local twips directly down to
   bitmap PIXEL indices (an identity `BitmapMatrix` then means exactly one
   bitmap pixel spans 20 twips of shape-local space — see
   `tests/test_scene_renderer.cpp`'s
   `SceneRenderer_BitmapFill_SamplesRealBitmapPixelsNotGrayPlaceholder`
   for a hand-verifiable 1:1 device-pixel-to-bitmap-pixel test built on
   exactly this fact). A near-singular transform or an unresolved/failed
   bitmap character falls back to the polygon's flat `color`
   (`toFlatColor()`'s pre-existing gray placeholder), same degenerate-case
   handling the gradient path uses.
3. **`SoftwareRenderer`/`Nintendo3DSRenderer`** (`fillPolygonBitmap()`/
   `fillPolygonBitmapGroup()`) rasterize with the same independent
   active-edge-table scanline copy pattern as `fillPolygonGradient()`,
   sampling via **nearest-neighbor only** — `smoothed` is parsed and
   carried through but bilinear filtering is deliberately deferred, same
   "no established texture-filtering precedent to extend" reasoning as
   other deferred-quality work in this file. `repeat` wraps via double-
   modulo; clip mode (the default, non-repeating case) clamps to the
   nearest edge pixel. `ColorTransform` is applied PER SAMPLED PIXEL at
   render time (not precomputed into a ramp the way the 256-entry gradient
   ramp is) — a bitmap can have far more distinct colors than a gradient's
   256 stops, so precomputing a full color-transformed copy isn't the same
   easy win there.

**RAM cost, measured (not assumed) across the real corpus** via
`tools/real_game_harness/bitmap_ram_probe.cpp` — see
`docs/memory-audit.md`'s Priority Fix List item #2 addendum for the full
per-file/per-character breakdown and 3DS RAM-budget discussion. Headline
figures: ~1.5 MB decoded across Hobo2's 10 bitmap characters, ~7.3 MB
across Extreme Pamplona's 10, and two standalone 1024x768 `DefineBitsJpeg2`
characters (Hobo6/Hobo7, one each) that alone cost ~3 MB decoded RGBA8
each — real, resident, EAGERLY-decoded memory per `DefineBitsTag.h`'s own
design (see that file's header comment for why lazy/on-demand decode,
Roadmap Phase 5's pattern for other character kinds, wasn't used here).

See `IRenderer.h`'s `DeviceBitmapFill` doc comment for the full
coordinate-space contract, and `docs/swf-support.md`'s `FILLSTYLEARRAY`/
`DefineBits*` rows for the current support-matrix status.

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
- **Anti-aliasing is edge-pixel-coverage only, X-direction, flat fills only
  (2026-08-30, Fidelity-audit TASK 3 divergence #1 — see
  `docs/flash-fidelity-audit.md`).** `SoftwareRenderer::fillPolygon()`
  blends the fractional-coverage boundary pixel column(s) of each scanline
  span instead of hard-rounding them, but this is deliberately scoped
  narrow, not a full AA implementation: (1) it only smooths near-vertical/
  diagonal silhouette edges via X-direction sub-pixel coverage at each
  scanline's left/right crossings — a shape's purely horizontal top/bottom
  edges are NOT anti-aliased, since rendering still samples exactly one
  scanline row per integer Y with no Y-direction coverage/supersampling;
  (2) `fillPolygonGradient()` (gradient fills) is untouched — its own
  independent copy of the scanline loop still hard-rounds both edges,
  same as before this fix; (3) `strokePolyline()` (line strokes) is
  untouched — still the naive Bresenham + square-stamp path, no AA. See
  `fillPolygon()`'s own `.cpp` comment for the coverage-formula
  derivation and verification method.

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
