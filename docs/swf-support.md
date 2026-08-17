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

### Not yet implemented (by design — later phases)

- Timeline / DisplayList (Phase 2)
- Shape / Sprite / Bitmap / Text rendering (Phase 3, 8)
- AVM1 VM (Phase 4)
- MovieClip API / `_root` / properties (Phase 5)
- Sound / Input (Phase 6)
- ExternalInterface (Phase 7)
- Nintendo 3DS backend (Phase 10)
