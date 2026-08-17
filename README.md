# flash3ds-runtime

A clean-room, independently implemented SWF/AVM1 (Flash) runtime, aimed
eventually at a standalone Nintendo 3DS "Flash Virtual Console" capable of
loading and running external SWF files (top/bottom dual-screen) without the
original Shift-DX executable.

This is a **separate project** from any Shift-DX reverse-engineering work.
No proprietary/binary code from Shift-DX or the gameswf library it embeds is
used here — Ghidra RE results are referenced only as a behavioral spec; see
[`docs/shift-dx-behavior.md`](docs/shift-dx-behavior.md).

## Status

**Phase 1 and Phase 2 complete**: SWF loading (FWS/CWS), header parsing, a
generic tag scan with logging, and a Timeline/DisplayList layer
(`PlaceObject`/`PlaceObject2`/`RemoveObject`/`RemoveObject2`/`FrameLabel`
parsing, depth-indexed display list, full playhead control). See
[`docs/architecture.md`](docs/architecture.md) for the full phase plan and
[`docs/swf-support.md`](docs/swf-support.md) for exactly what's implemented
so far.

## Build & test

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Try it

```sh
./build/flash_runtime tests/fixtures/test_avm1.swf --debug
```

```
==== flash3ds SWF Inspector ====
File:            tests/fixtures/test_avm1.swf
Status:          OK
SWF version:     6
Compression:     FWS (uncompressed)
Declared length: 27 bytes
Stage size:      200.0 x 200.0 px (twips: 4000 x 4000)
Frame rate:      25.00 fps
Frame count:     1
ActionScript:    present
Tag count:       3

-- Tags --
  [  14] id=12   DoAction                     len=1
  [  17] id=1    ShowFrame                    len=0
  [  19] id=0    End                          len=0
```

Add `--timeline` to also print the per-frame display list (Phase 2):

```sh
./build/flash_runtime tests/fixtures/test_timeline.swf --quiet --timeline
```

```
-- Timeline -- (3 frames)
  Frame  1: 1 object(s) on display list  [depth=1 char=100 name="hero" x=0.0 y=0.0]
  Frame  2: 1 object(s) on display list  [depth=1 char=100 name="hero" x=2.5 y=0.0]
  Frame  3: 0 object(s) on display list

-- Frame labels --
  "start" -> frame 1
```

## Layout

See [`docs/architecture.md`](docs/architecture.md#module-layout).

## Docs

- [architecture.md](docs/architecture.md) — pipeline, module layout, phase plan
- [swf-support.md](docs/swf-support.md) — SWF feature support matrix
- [avm1-support.md](docs/avm1-support.md) — AVM1 opcode/API status (Phase 4+)
- [externalinterface.md](docs/externalinterface.md) — host-callback design (Phase 7)
- [renderer.md](docs/renderer.md) — rendering design (Phase 3, 10)
- [audio.md](docs/audio.md) — audio design (Phase 6, 10)
- [input.md](docs/input.md) — input design (Phase 6, 10)
- [shift-dx-behavior.md](docs/shift-dx-behavior.md) — Shift-DX RE behavioral cross-checks
- [compatibility.md](docs/compatibility.md) — Hobo/target-game compatibility tracking
