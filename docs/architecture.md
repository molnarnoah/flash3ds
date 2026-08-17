# Architecture

`flash3ds-runtime` is a clean-room, independently implemented SWF/AVM1
runtime. It is a **separate project** from the Shift-DX Ghidra
reverse-engineering work — no Shift-DX/gameswf binary code is copied here.
The RE results are used only as a *behavioral reference* (see
[shift-dx-behavior.md](shift-dx-behavior.md)) to sanity-check that our
independent implementation matches real-world Flash-port behavior.

## Target pipeline

```
SWF file
   |
   v
SWF Loader        (src/swf/SwfLoader.*)
   |
   v
SWF Parser         (src/swf/SwfReader.*, TagDispatcher.*, TagCode.*)
   |
   +-------------------+
   |                    |
   v                    v
Timeline             AVM1 VM         <-- not yet implemented (Phase 2 / Phase 4)
   |                    |
   +---------+----------+
             |
             v
      Display List                    <-- not yet implemented (Phase 2)
             |
    +--------+--------+
    |        |         |
    v        v         v
  Shape    Sprite    Text             <-- not yet implemented (Phase 3 / Phase 8)
    |
    v
 Renderer                             <-- not yet implemented (Phase 3)
    |
    v
Top / Bottom Screen                   <-- not yet implemented (Phase 10)
    |
    v
Nintendo 3DS
```

The runtime is deliberately modular so the renderer, audio, input, and
platform layers can be swapped for Nintendo 3DS-specific implementations
later without touching the SWF/AVM1 core.

## Current status: Phase 1

Phase 1 implements the bottom of the pipeline only: **SWF Loader → SWF
Parser**, stopping at a flat list of tags (no Timeline/DisplayList/AVM1
yet — those begin in Phase 2+). See [swf-support.md](swf-support.md) for
exactly what is and isn't implemented.

## Module layout

```
src/
  platform/   Log.h/.cpp                  — logging (no platform deps yet)
  swf/        SwfReader.h/.cpp            — byte/bit stream reader, RECT
              TagCode.h/.cpp              — SWF tag ID <-> name table
              TagDispatcher.h/.cpp        — generic tag-header reader
              SwfLoader.h/.cpp            — FWS/CWS signature, zlib inflate,
                                             header parse, tag scan
  runtime/    Movie.h/.cpp                — Phase 1 result model
tools/
  flash_runtime/main.cpp                  — CLI SWF inspector
tests/
  TestFramework.h, TestMain.cpp           — tiny dependency-free test harness
  SwfTestFixtures.h/.cpp                  — programmatic SWF fixture builder
  test_*.cpp                              — unit tests
docs/                                     — this directory
```

## Design principles

- **Never crash on untrusted input.** `SwfReader` never reads out of
  bounds; a read past the end sets a sticky `failed()` flag and returns a
  zeroed value instead of touching invalid memory. `SwfLoader` checks
  `failed()` after every structural read and returns a `Movie` with
  `valid == false` and a human-readable `errorMessage` rather than
  asserting or crashing.
- **No global state.** Every loader/reader/dispatcher call is explicit and
  reentrant; this matters once Top/Bottom dual-screen movies need to run
  side by side (Phase 10).
- **Platform-independent core.** `flash3ds_core` links only against zlib.
  Renderer/audio/input backends (Phase 3/6/10) will be separate targets
  behind abstract interfaces (`IRenderer`, `AudioManager`, `InputManager`),
  so a `DesktopRenderer`/`Nintendo3DSRenderer` pair (etc.) can share the
  same core.
- **Defensive resource limits.** CWS decompression is capped (128 MiB) to
  avoid unbounded memory growth on a corrupt/malicious "zip-bomb" SWF.

## Build

CMake, C++17, zlib (system). See the repo root `CMakeLists.txt`.

```
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/flash_runtime path/to/file.swf [--debug|--quiet]
```
