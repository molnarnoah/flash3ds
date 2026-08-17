# SWF Support Status

Tracks what the runtime actually implements, updated at the end of every
phase (see `docs/architecture.md` for the phase plan).

## Phase 1 (current)

### Container format

| Feature | Status |
|---|---|
| `FWS` (uncompressed) | ✅ Implemented |
| `CWS` (zlib) | ✅ Implemented (via system zlib, capped at 128 MiB decompressed) |
| `ZWS` (LZMA) | ⚠️ Signature recognized, decompression **not implemented** — loader returns a clean error |

### Header fields

| Field | Status |
|---|---|
| Signature / version | ✅ |
| Declared file length | ✅ (read, not currently cross-checked against actual size) |
| FrameSize (stage RECT) | ✅ |
| FrameRate (8.8 fixed point) | ✅ |
| FrameCount | ✅ |

### Tag stream

| Feature | Status |
|---|---|
| Tag header decode (short + long/extended length) | ✅ |
| Tag name lookup table | ✅ (all tag IDs listed in the project spec, plus a handful of common others for forward-compat) |
| Per-tag body parsing (DefineShape, DefineSprite, DoAction bytecode, …) | ❌ Not started — Phase 3 (shapes/sprites) and Phase 4 (AVM1) |
| Unknown-tag logging (id + offset + length) | ✅ |
| ActionScript-presence detection (`DoAction`/`DoInitAction`/`DoABC`/`DoABC2`) | ✅ |
| Graceful handling of truncated/malformed tag streams | ✅ (stops the scan, logs a warning, returns whatever was read) |

### SWF version target

Spec target is SWF 6–8 / AVM1. Phase 1 does not reject other versions (the
version byte is just recorded) since header/tag-stream parsing is
version-independent; version-specific behavior will matter starting with
tag-body parsing in later phases.

## Phase 2 (current)

Builds a main-timeline model on top of Phase 1's flat tag list. Movie now
owns its decompressed tag-stream bytes (`Movie::data`), so any tag's body
can be parsed on demand via `Movie::tagBodyReader()`.

### Records

| Record | Status |
|---|---|
| `MATRIX` (2D affine transform: scale/rotate/translate) | ✅ |
| `CXFORM` / `CXFORMWITHALPHA` (color transform) | ✅ |

### Tag body parsing

| Tag | Status |
|---|---|
| `ShowFrame` (1) | ✅ (frame-boundary marker, drives Timeline frame grouping) |
| `PlaceObject` (4) | ✅ CharacterId/Depth/Matrix/optional ColorTransform |
| `PlaceObject2` (26) | ✅ full flag set (HasCharacter/HasMatrix/HasColorTransform/HasRatio/HasName/HasClipDepth); `HasClipActions` bytes are present in the tag but intentionally left unparsed (AVM1 event handlers are Phase 4+) |
| `RemoveObject` (5) | ✅ |
| `RemoveObject2` (28) | ✅ |
| `FrameLabel` (43) | ✅ name only (used for `Timeline::gotoAndStop("label")`) |
| All other tags | ❌ still name/offset/length only (Phase 3+: shapes, sprites, bitmaps, text, sound, DoAction bytecode, …) |

### Display list / timeline

| Feature | Status |
|---|---|
| Depth-indexed `DisplayList` (add / update-in-place / replace / remove) | ✅ — see `docs/shift-dx-behavior.md` for the add-vs-replace RE cross-check |
| `Timeline` built from a `Movie`'s top-level tags, grouped at `ShowFrame` boundaries | ✅ |
| `gotoAndStop(frame)` / `gotoAndPlay(frame)` (numeric, 1-based, clamped) | ✅ |
| `gotoAndStop(label)` / `gotoAndPlay(label)` (via `FrameLabel`) | ✅ |
| `nextFrame()` / `prevFrame()` (always stops playback, matching AS2 semantics) | ✅ |
| `play()` / `stop()` | ✅ |
| `advanceOneFrame()` (per-tick playback advance; loops to frame 1 past the end) | ✅ |
| Nested timelines (`DefineSprite`/MovieClip bodies) | ❌ Phase 3 — Phase 2 only walks the Movie's own top-level tags |
| `PlaceObject3`, clip-action event handlers | ❌ later phases |

Implementation note: `Timeline` rebuilds the display list by replaying
frames `[1, target]` from scratch on every `gotoAndStop`/`gotoAndPlay` call.
This is intentionally simple (and covered by the backward-jump test in
`tests/test_timeline.cpp`) rather than incremental; revisit if profiling
ever shows it matters for a real target movie's frame count.

## Phase 3 (current)

Adds shape parsing, a character dictionary (shapes + nested-sprite
timelines), matrix composition, tessellation, and a basic software
renderer on top of Phase 2's Timeline/DisplayList. See `docs/renderer.md`
for the renderer architecture and known limitations.

### Records

| Record | Status |
|---|---|
| `concatMatrix(parent, child)` (world-transform composition) | ✅ |

### Shape sub-format (`src/swf/ShapeRecords.h/.cpp`)

| Feature | Status |
|---|---|
| `FILLSTYLEARRAY` — solid fills | ✅ |
| `FILLSTYLEARRAY` — linear/radial/focal-radial gradient fills | ✅ parsed (matrix + gradient stops); rendered as a single averaged flat color (no real gradient rendering yet) |
| `FILLSTYLEARRAY` — bitmap fills (repeating/clipped, smoothed/non-smoothed) | ✅ parsed (character ID + matrix); rendered as a flat gray placeholder (bitmap decoding not implemented) |
| `LINESTYLEARRAY` (LineStyle1) | ✅ |
| `LineStyle2` (DefineShape4) | ❌ not implemented |
| `SHAPERECORD` stream: StyleChangeRecord (MoveTo, style indices, new-styles sub-array) | ✅ |
| `SHAPERECORD` stream: StraightEdgeRecord | ✅ |
| `SHAPERECORD` stream: CurvedEdgeRecord (quadratic bezier) | ✅ parsed; flattened to line segments at render time (`ShapeTessellator`) |

### Tag body parsing

| Tag | Status |
|---|---|
| `DefineShape` (2) | ✅ |
| `DefineShape2` (22) | ✅ |
| `DefineShape3` (32) | ✅ (RGBA solid/gradient/line colors) |
| `DefineShape4` (83) | ❌ not implemented — `parseDefineShape` returns `std::nullopt` for this tag code |
| `DefineSprite` (39) | ✅ header (CharacterId/FrameCount) + nested control-tag stream scan (absolute offsets into `Movie::data` — see `CharacterDictionary.h`) |
| `DefineBits*` / `DefineText*` / `DefineFont*` / `DefineButton*` | ❌ still name/offset/length only (Phase 8+) |
| `SetBackgroundColor` | ❌ not parsed — renderer always clears to white |
| All other unlisted tags | ❌ still name/offset/length only |

### Character resolution

| Feature | Status |
|---|---|
| `CharacterDictionary::build()` — resolves `DefineShape`/`2`/`3` and `DefineSprite` character IDs | ✅ |
| Nested `DefineSprite` tag streams reusable by `Timeline` (shared code path with top-level movie tags, no duplication) | ✅ |
| Bitmap/Text/Button character resolution | ❌ Phase 8+ |

### Rendering

| Feature | Status |
|---|---|
| `IRenderer` abstraction (`beginFrame`/`endFrame`/`fillPolygon`/`strokePolyline`) | ✅ |
| `SoftwareRenderer` (RGBA8 framebuffer, even-odd scanline fill with alpha blending, naive stroke rasterizer, PPM output) | ✅ desktop/testing implementation |
| `ShapeTessellator` (SHAPERECORD stream → flat polygons/polylines) | ✅ **simplified**: one closed polygon per MoveTo run, no edge-boundary merging — see `docs/renderer.md` for exactly what this does and doesn't render correctly |
| `SceneRenderer` (DisplayList walk, character resolution, `concatMatrix` world-transform composition, recursive sprite rendering) | ✅ |
| Independent per-instance sprite playhead | ❌ Phase 4/5 (AVM1 + MovieClip API) — see `docs/renderer.md` |
| `ColorTransform` / clip-depth application at render time | ❌ later phase — parsed and stored, not yet applied |
| Nintendo 3DS backend (`Nintendo3DSRenderer`) | ❌ Phase 10 |
| CLI `--render <frame> <out.ppm>` | ✅ |

### Not yet implemented (by design — later phases)

- Bitmap / Text / Button rendering, `LineStyle2`/`DefineShape4`, real gradient rendering, `ColorTransform` application (Phase 8, or earlier if a target title needs it)
- AVM1 VM (Phase 4)
- MovieClip API / `_root` / properties / `onClipEvent` / independent sprite playheads (Phase 5)
- Sound / Input (Phase 6)
- ExternalInterface (Phase 7)
- Nintendo 3DS backend (Phase 10)
