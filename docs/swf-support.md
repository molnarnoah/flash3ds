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
| `PlaceObject2` (26) | ✅ full flag set (HasCharacter/HasMatrix/HasColorTransform/HasRatio/HasName/HasClipDepth); `HasClipActions`' `ClipActionRecord`s are now parsed too (Phase 6 — see `docs/avm1-support.md`'s Input section) |
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
| `FILLSTYLEARRAY` — linear gradient fills | ✅ parsed AND rendered as a real per-pixel 256-stop gradient (2026-08-28 graphics/gradients task — see `docs/renderer.md`'s "Gradient rendering" section) |
| `FILLSTYLEARRAY` — radial/focal-radial gradient fills | ✅ parsed (matrix + gradient stops); still rendered as a single averaged flat color — real corpus evidence (`/tmp/gradient_scan.cpp`, hobo.swf) found zero radial/focal-radial fills, so real rendering is deliberately not implemented against no evidence (same discipline as `docs/renderer.md`'s other evidence-scoped decisions) |
| `FILLSTYLEARRAY` — bitmap fills (repeating/clipped, smoothed/non-smoothed) | ✅ parsed AND rendered with real decoded bitmap pixel data (Priority Fix List item #2, 2026-08-31 — see `docs/renderer.md`'s "Bitmap rendering" section). Sampling is nearest-neighbor only (`smoothed` is parsed but not distinguished at render time — no established texture-filtering precedent to extend, same reasoning `docs/renderer.md` documents for other deferred filtering). |
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
| `DefineBits*` / `DefineText*` / `DefineFont*` / `DefineButton*` | Text/Font/Button: ✅ (Phase 8). Bitmaps: `DefineBitsLossless`(20)/`DefineBitsLossless2`(36)/`DefineBitsJpeg2`(21)/`DefineBitsJpeg3`(35) ✅ fully decoded to RGBA8 (Priority Fix List item #2, 2026-08-31 — see `src/swf/DefineBitsTag.h`); `DefineBits`(6)/`JPEGTables`(8)/`DefineBitsJpeg4`(90) ❌ not implemented, zero real-corpus evidence |
| `SetBackgroundColor` | ❌ not parsed — renderer always clears to white |
| All other unlisted tags | ❌ still name/offset/length only |

### Character resolution

| Feature | Status |
|---|---|
| `CharacterDictionary::build()` — resolves `DefineShape`/`2`/`3` and `DefineSprite` character IDs | ✅ recursively — a character-defining tag nested inside a `DefineSprite`'s own tag stream (legal per spec; the character ID dictionary is global across the file, not scoped per-sprite) resolves correctly too, not just top-level tags (fixed during Phase 5 — see `tests/test_character_dictionary.cpp`'s `..._ResolvesShapeNestedInsideSprite`) |
| Nested `DefineSprite` tag streams reusable by `Timeline` (shared code path with top-level movie tags, no duplication) | ✅ |
| Bitmap/Text/Button character resolution | ❌ Phase 8+ |

### Rendering

| Feature | Status |
|---|---|
| `IRenderer` abstraction (`beginFrame`/`endFrame`/`fillPolygon`/`strokePolyline`) | ✅ |
| `SoftwareRenderer` (RGBA8 framebuffer, even-odd scanline fill with alpha blending, naive stroke rasterizer, PPM output) | ✅ desktop/testing implementation |
| `ShapeTessellator` (SHAPERECORD stream → flat polygons/polylines) | ✅ **simplified**: one closed polygon per MoveTo run, no edge-boundary merging — see `docs/renderer.md` for exactly what this does and doesn't render correctly |
| `SceneRenderer` (MovieClipInstance-tree walk, character resolution, `concatMatrix` world-transform composition, recursive sprite rendering) | ✅ |
| Independent per-instance sprite playhead | ✅ Phase 5 — each `MovieClipInstance` owns its own `Timeline`; `SceneRenderer` recurses into instances, not a shared per-character cache — see `docs/renderer.md` |
| `ColorTransform` / clip-depth application at render time | ❌ later phase — parsed and stored (as of Phase 5, per-`MovieClipInstance`, script-mutable via `_alpha`), not yet applied to rendered pixels |
| Nintendo 3DS backend (`Nintendo3DSRenderer`) | ❌ Phase 10 |
| CLI `--render <frame> <out.ppm>` | ✅ ticks a real `MovieClipInstance` tree frame-by-frame (running DoAction scripts along the way) rather than jumping straight to the target frame — see `docs/renderer.md` |

### AVM1 / MovieClip API (Phase 4/5 — see `docs/avm1-support.md` for the full opcode matrix)

| Feature | Status |
|---|---|
| AVM1 bytecode interpreter (`src/avm1/Interpreter.cpp`) | ✅ Phase 4 — full opcode set, tested in isolation |
| `DoAction`/`DoInitAction` tag dispatch into a running clip | ✅ Phase 5 — `MovieClipInstance` runs a clip's current-frame `DoAction` bodies and (once, per character) `DoInitAction` |
| `HostBindings` wired to a real `Timeline`/`DisplayList` (GotoFrame/Play/Stop/GetProperty/SetProperty/CloneSprite/RemoveSprite/SetTarget) | ✅ Phase 5 — see `runtime::MovieClipInstance`'s internal `MovieClipHostBindings` |
| `_root` / `_parent` / named child clip access (`this.childName`) | ✅ Phase 5 — via `avm1::Object`'s native property hooks (`src/avm1/Value.h`) |
| `_x`/`_y`/`_xscale`/`_yscale`/`_rotation`/`_alpha`/`_visible`/`_currentframe`/`_totalframes`/`_name`/`_target` | ✅ Phase 5, both via `GetProperty`/`SetProperty` and via `.member` access |
| `_width`/`_height` | ❌ not computed — always returns 0 (would need full recursive subtree bounding-box computation) |
| `StartDrag`/`EndDrag` | ✅ Phase 6 — real drag tracking with `lockCenter`/constraint-rectangle support, driven by `InputState`'s mouse position once per tick |
| `_xmouse`/`_ymouse` | ✅ Phase 6 — via `GetProperty`(20/21) and bare `.member` access, both reading `InputState` |
| `Key.isDown()`/`Key.getCode()`/named constants | ✅ Phase 6 — native `Key` object backed by `InputState` |
| `Mouse.show()`/`Mouse.hide()` | ✅ Phase 6 — recognized no-ops (no cursor rendering model) |
| `Sound` object (`attachSound`/`start`/`stop`/`setVolume`/`getVolume`) | ⚠️ Phase 6 — wired to `IAudioBackend`, but `attachSound()` only resolves a numeric character ID, not the real AS2 `String` linkage-name form (needs `ExportAssets`, not implemented) |
| `onClipEvent`/button `on()` handlers | ⚠️ Phase 6/8 — `ClipActionRecord` parsing done; only `Load`/`Unload`/`EnterFrame` are dispatched. `DefineButton`/`2` are now parsed (Phase 8) and their action bytecode (`actionsV1`/`condActionsV2`) captured, but NOT dispatched — like the mouse-related `onClipEvent`s, it needs hit-testing/bounds infrastructure that doesn't exist yet (see this doc's Phase 8 section and `docs/avm1-support.md`'s Known Phase 8 limitations) |
| ExternalInterface (`ExternalInterface.call`/`addCallback`) | ✅ Phase 7 — AS2 <-> native/host communication in both directions; see `docs/avm1-support.md` |

### Sound (Phase 6 — see `docs/avm1-support.md` for the AVM1-facing side)

| Tag / feature | Status |
|---|---|
| `DefineSound` (14) | ✅ structural header fields only (format/rate/16-bit/stereo/sample count) — no codec decode |
| `StartSound` (15) / `SOUNDINFO` | ✅ fully parsed (in/out points, loop count, sync flags, envelope points) and dispatched per-frame to `IAudioBackend` |
| `SoundStreamHead`/`2` (18/45) / `SoundStreamBlock` (19) | ❌ not implemented — streaming sound is a later-phase concern if a target title needs it |
| `DefineButtonSound` (17) | ❌ not implemented — `DefineButton`/`2` parsing exists now (Phase 8) but nothing attaches per-state sounds to it yet |
| `IAudioBackend` abstraction (`src/audio/IAudioBackend.h`) | ✅ mirrors `IRenderer`'s design; `NullAudioBackend` (logs, plays nothing) is the only implementation so far — a real desktop/3DS backend is a later addition that needs zero changes to `runtime/`/`avm1/` |

## Phase 8 (current) — Text / Font / Button

Adds four new character-defining tags, all resolved into `CharacterDictionary`
(`swf::FontDef`/`swf::TextDef`/`swf::ButtonDef`/`swf::EditTextDef`) exactly
like Phase 3's shapes and Phase 6's sounds, and rendered by `SceneRenderer`
(text/button/edit-text are new leaf character kinds it can draw; buttons
always render their "Up" state, since there's no mouse hit-testing yet).

### Fonts

| Tag | Status |
|---|---|
| `DefineFont` (10, "v1") | ✅ glyph outlines (bare SHAPE records — no fill/line style arrays; a synthetic one-entry fill style is created at render time from the TextRecord/EditText color instead — see `docs/avm1-support.md`). No code table (v1 has none of its own; see below) |
| `DefineFont2` (48, "v2") | ✅ glyph outlines, code table (`FontDef::codeTable`, `glyphIndexForCode()`), bold/italic flags, font name, and (iff `HasLayout`) ascent/descent/leading + per-glyph advance width + per-glyph bounds. Kerning table is parsed-and-discarded (not applied by the renderer) |
| `DefineFont3` (75) | ❌ explicitly rejected (`parseDefineFont2` returns `std::nullopt` for tag 75) — its glyph coordinates use a 20x finer em-square (20480 units/em vs. `DefineFont2`'s 1024) for sub-pixel hinting; silently treating it as `DefineFont2` would mis-scale every glyph by 20x, so it's rejected rather than guessed at |
| `DefineFontInfo`/`DefineFontInfo2` (13/62) | ❌ not implemented — these attach a code table to an existing `DefineFont` v1 (which has none of its own). A v1 font's `FontDef::codeTable` is always empty; this only matters for `DefineEditText` initial-text rendering (`DefineText`/`DefineText2` reference glyphs by index directly and never need one) |
| `DefineFontAlignZones`/`DefineFontName` (73/88) | ❌ not implemented — text-rendering-quality/licensing metadata, not needed for basic rendering |

### Text

| Tag | Status |
|---|---|
| `DefineText` (11) | ✅ full `TEXTRECORD` parsing (font/color/x-offset/y-offset changes, glyph-index+advance runs), RGB colors |
| `DefineText2` (33) | ✅ same as `DefineText`, RGBA colors |
| `SceneRenderer` glyph-run rendering | ✅ walks each `TextRecord`, carrying font/color/position forward across records that don't set them (matching the SWF spec's incremental-state model), looks up each glyph's outline in its `FontDef`, and reuses `ShapeTessellator` (via a synthesized single-fill-style `Shape`, scaled by `textHeight/1024`) to draw it — see `docs/avm1-support.md`'s "Text/Font" section for the scale-factor convention |
| `CsmTextSettings` (74) | ❌ not implemented — advanced/CSM anti-aliasing hints, not needed for basic rendering |

### Buttons

| Tag | Status |
|---|---|
| `DefineButton` (7, "v1") | ✅ `BUTTONRECORD` list (Up/Over/Down/HitTest state flags, character/depth/matrix) + trailing action bytecode block (`actionsV1`) — parsed but not dispatched (see the AVM1/MovieClip API table above) |
| `DefineButton2` (34, "v2") | ✅ `BUTTONRECORD2` list (adds `CXFORMWITHALPHA` per record) + `BUTTONCONDACTION` list (`condActionsV2` — per-transition conditions, optional key-press trigger, action bytecode), parsed but not dispatched. A record with `HasFilterList` set aborts parsing the REMAINDER of that button's records (unknown-length `FILTERLIST` isn't implemented — see `swf/DefineButtonTag.h`); `HasBlendMode` alone is supported (fixed 1-byte field, safely skipped) |
| `DefineButtonCxform` (23) | ❌ not implemented (v1-only per-state color transform, rare) |
| `DefineButtonSound` (17) | ❌ not implemented (see the Sound table above) |
| `SceneRenderer` button rendering | ⚠️ always draws the "Up" state's records only — no hit-testing/mouse-state model exists yet (see this doc's `onClipEvent`/button table entry above) |

### Dynamic/input text (`DefineEditText`)

| Tag / feature | Status |
|---|---|
| `DefineEditText` (37) | ✅ full structural parsing: every documented flag bit, optional font/height/color/max-length/layout fields, variable name, initial text |
| Variable binding (`_root.myField` <-> displayed text) | ❌ not implemented |
| User input/editing, word-wrap, scrolling, alignment | ❌ not implemented |
| `SceneRenderer` initial-text rendering | ⚠️ narrow: only renders when the field embeds a `DefineFont2` font (one WITH a code table) and has literal `initialText` — reuses the same glyph-drawing path as `DefineText`, with a naive fixed-line-height `\n`/`\r` handling and no real baseline/font-metric placement (see `swf/DefineEditTextTag.h` and `renderer/SceneRenderer.cpp`'s `renderEditTextCharacter()`) |

### Not yet implemented (by design — later phases)

- Bitmap rendering, `LineStyle2`/`DefineShape4`, real radial/focal-radial gradient rendering (linear gradient rendering is done — see the `FILLSTYLEARRAY` row above and `docs/renderer.md`) (later phase, or earlier if a target title needs it)
- `_width`/`_height`, all mouse/keyboard-related `onClipEvent`s and button `on()` handlers (blocked on hit-testing/bounds — see the AVM1/MovieClip API table above)
- Audio codec decode, streaming sound, `ExportAssets`-based `Sound.attachSound(name)`, `DefineButtonSound` (see the Sound table above)
- `DefineFont3`, `DefineFontInfo`/`2`, EditText variable binding/word-wrap/scrolling (see the Phase 8 tables above)
- Nintendo 3DS backend (Phase 10)

## Phase 9 (current) — Hobo compatibility testing

Ran the runtime against a real copy of `hobo.swf` end-to-end for the first
time (see `docs/compatibility.md` for the full report). Two real bugs
found and fixed, both from genuine content, neither reproducible from any
prior synthetic test fixture:

- **`readShapeRecordStream()` byte-alignment bug.** A mid-stream
  `StyleChangeRecord` with `StateNewStyles` set now correctly byte-aligns
  before reading its new `FILLSTYLEARRAY`/`LINESTYLEARRAY` (a spec
  requirement — these are byte-level structures inside the otherwise
  bit-packed shape record stream). Was silently corrupting every
  multi-style-region shape's fill styles beyond the first change. See
  `src/swf/ShapeRecords.cpp` and the regression test
  `ShapeWithStyle_MidStreamNewStyles_ByteAlignsBeforeNewStyleArrays`.
- **Missing OOP-callable `MovieClip` methods.** `MovieClipInstance` now
  exposes `stop()`/`play()`/`nextFrame()`/`prevFrame()`/`gotoAndStop()`/
  `gotoAndPlay()` (numeric-frame and frame-label forms)/`getBytesLoaded()`/
  `getBytesTotal()` as real callable methods (via
  `handleNativeGet()` returning a native `FunctionDef`), not just the
  bare unqualified action-code forms. `getBytesLoaded`/`getBytesTotal`
  both return `Movie::declaredFileLength` (this runtime never streams, so
  "loaded" is trivially "total"). See `src/runtime/MovieClipInstance.cpp`.

Also confirmed (not fixed — see `docs/compatibility.md` for the full
prioritized list): `DefineMorphShape`/`DefineMorphShape2` are recognized
but not resolved into `CharacterDictionary` (19 occurrences in `hobo.swf`,
real-content-confirmed gap, natural next-phase scope); a handful of
`DefineSprite` tag streams end without a trailing `ShowFrame` (handled
gracefully, likely a benign encoder quirk); the file uses `DefineFont2`,
not `DefineFont3` as the old spec-derived table in `compatibility.md`
claimed before this phase corrected it against the real file.

## Phase 10 (Nintendo 3DS backend)

No SWF tag/record parsing changed — `src/swf/` is entirely part of
`flash3ds_core`, cross-compiled unchanged for the 3DS. This phase's SWF-
format-adjacent work was all in `tools/gen_3ds_demo_swf.py`, a from-scratch
clean-room generator (hand-authored `DefineShape3`/`PlaceObject2`/
`SetBackgroundColor`/`ShowFrame`/`End` byte construction against the public
spec) used to embed a small demo movie into the 3DS build. Two encoding
bugs were found and fixed in that generator while independently verifying
its output against this project's own desktop rendering pipeline (not
against the parser it's testing — see `docs/3ds-toolchain.md` for the full
story): a bogus `HasTranslate` bit in the hand-rolled `MATRIX` encoder
(the real SWF spec's `MATRIX` record has no such flag — translate fields
are unconditional, only scale/rotate are flag-gated) and a wrong
`PlaceObject2` flags-byte bit assignment for `HasMatrix` (`0x08` used
instead of the correct `0x04`, cross-checked against this project's own
`src/swf/PlaceObjectTag.cpp` parser to fix). Both are generator-side bugs,
not parser bugs — no `src/swf/` code changed.

## Roadmap Phase 9 (`docs/implementation-roadmap-2026-08-21-part2.md`) — `DefineMorphShape` parsing + rendering, 2026-08-25

**Not the same "Phase 9" as the "Hobo compatibility testing" section
above** — that section is this file's own original (2026-08-18-era)
10-phase numbering; this is the differently-numbered roadmap-part2 Phase
9, same collision pattern already noted for "Phase 8" elsewhere in this
project (see `CLAUDE.md`'s "Roadmap Part 2 progress" and `docs/avm1-
support.md`'s disambiguated Phase-8 section).

Implemented `DefineMorphShape` (tag 46) parsing (`src/swf/
DefineMorphShapeTag.h/.cpp`), `CharacterDictionary` integration, and
rendering (`SceneRenderer::renderMorphShapeCharacter`). Scope: **v1 only
(tag 46)** — `DefineMorphShape2` (tag 84) is deliberately not
implemented, since re-running `swf_diagnostic`'s tag histogram against all
8 corpus games found zero occurrences of tag 84 anywhere.

Rendering uses **START-side geometry and fill/line colors only
(ratio=0)** — the roadmap's own pre-approved "simplest correct-enough
first implementation" (mirroring this project's existing gradient-as-
flat-average simplification precedent), not full morph-tween
interpolation. This was verified as exactly correct for the real corpus,
not merely convenient: a new evidence tool,
`tools/real_game_harness/morph_ratio_scan.cpp`, walks every
`DefineMorphShape` character and every `PlaceObject2` record targeting one
(including inside nested `DefineSprite` tag streams) and reports every
`Ratio` value found. Run against all 7 Hobo files: **100% of morph
placements use ratio=0** (explicit or absent) — zero non-zero ratios
anywhere in the corpus. `EndEdges`/end-side styles are still parsed and
stored in `MorphShapeDef` (cheap, and available for a future true-
interpolation pass) even though the renderer only consumes the start
side.

7 new tests: 5 parser tests (`tests/test_define_morph_shape_tag.cpp`,
against a new `buildDefineMorphShapeBytes` fixture independently bit-
packed from the public MORPHFILLSTYLE/MORPHLINESTYLE/MORPHGRADIENT spec —
same clean-room convention as every other `SwfTestFixtures.h` builder), 1
`CharacterDictionary` integration test, and 1 `SceneRenderer` rendering
test (a synthetic morph with a small green START rectangle and a much
larger red END rectangle, confirming only the START side's geometry/color
actually paints — the pixel inside the END rectangle's larger footprint
but outside the START rectangle must stay background white). 368/368
tests passing (up from 361).

Real-game validation: `tools/real_game_harness/run_harness.sh` (frames
1-5, all 8 corpus games) produces **byte-identical** MD5s before and
after (verified via `git stash` on every Phase 9 file and re-running both
configurations) — expected, since the real corpus's `DefineMorphShape`
placements sit in gameplay content this harness's frame 1-5 render never
reaches, the same pattern Roadmap Phase 8 found for `Math.random`/
`Math.ceil`.

Docs: `docs/known-limitations.md` L7 updated (`DefineMorphShape` done/
evidenced, `DefineMorphShape2`/`DefineShape4`/`PlaceObject3`/
`CsmTextSettings` still open), `docs/compatibility-matrix.md` §2's
`DefineMorphShape`/`DefineMorphShape2` rows updated,
`docs/implementation-roadmap-2026-08-21-part2.md` and `CLAUDE.md` updated
with completion summaries.

Verified: clean full rebuild (zero warnings), 368/368 tests passing.
