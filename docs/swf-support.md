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

### Not yet implemented (by design — later phases)

- Shape / Sprite / Bitmap / Text rendering, nested MovieClip timelines (Phase 3, 8)
- AVM1 VM (Phase 4)
- MovieClip API / `_root` / properties / `onClipEvent` (Phase 5)
- Sound / Input (Phase 6)
- ExternalInterface (Phase 7)
- Nintendo 3DS backend (Phase 10)
