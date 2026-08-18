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
Timeline             AVM1 VM         <-- Timeline: Phase 2 done; AVM1: Phase 4 done, Phase 5 wired in
   |                    |
   +---------+----------+
             |
             v
    MovieClipInstance tree            <-- Phase 5 done (per-instance Timeline + scripting Object;
             |                            wraps Display List, runs DoAction/DoInitAction)
    +--------+--------+
    |        |         |
    v        v         v
  Shape    Sprite    Text             <-- Shape/Sprite: Phase 3+5 done; Text: Phase 8 not started
    |
    v
 Renderer                             <-- Phase 3 done (SoftwareRenderer, desktop/testing);
                                          Phase 5 rewired to walk MovieClipInstance
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

## Current status: Phase 5

Phase 1 built **SWF Loader → SWF Parser** (flat tag list). Phase 2 added
**Timeline → Display List**: `PlaceObject`/`PlaceObject2`/`RemoveObject`/
`RemoveObject2`/`FrameLabel` tag-body parsing, a depth-indexed
`DisplayList`, and a `Timeline` with playhead control
(`gotoAndStop`/`gotoAndPlay`/`nextFrame`/`prevFrame`/`play`/`stop`). Phase 3
added **Shape/Sprite → Renderer**: `DefineShape`/`2`/`3` parsing (fill/line
styles + SHAPERECORD stream), a `CharacterDictionary` resolving character
IDs to shapes and nested `DefineSprite` timelines, `concatMatrix` world-
transform composition, a (deliberately simplified) `ShapeTessellator`, and
a `SoftwareRenderer`/`SceneRenderer` pair that walks the display list and
recurses into sprites, plus a CLI `--render` flag. Phase 4 built the **AVM1
VM**: a `Value`/`Object` dynamic-type model with prototype-chain member
lookup and Array semantics, `Stack`/`Scope`/`GlobalObject`/
`ExecutionContext`, and a tree-walking `Interpreter` covering the full
AVM1 opcode set — arithmetic/comparison/bitwise/string ops, variables,
objects/arrays, function definition and calling (including
`DefineFunction2` register parameters/preload flags, closures, and
recursion with a bounded call depth), and control flow (`Jump`/`If`) —
tested entirely **in isolation** against raw bytecode buffers, not yet
wired into anything. Phase 5 did the wiring: **`MovieClipInstance`**
(`src/runtime/MovieClipInstance.h/.cpp`) is the new integration point
between `runtime/` and `avm1/` — every placed sprite gets its own
`Timeline` (a genuinely independent playhead, replacing Phase 3's shared
per-character cache), runs its `DoAction`/`DoInitAction` scripts through
the Phase 4 interpreter with a real `HostBindings` implementation wired to
that `Timeline`/`DisplayList`, and exposes `_x`/`_y`/`_currentframe`/
`_root`/`_parent`/named-child-clip access etc. through `avm1::Object`'s new
native-property hooks. `SceneRenderer` now walks the `MovieClipInstance`
tree instead of a raw `Timeline`, so script-driven transform/visibility
changes actually render. Along the way, Phase 5 also fixed a real Phase 3
bug: `CharacterDictionary` only scanned the movie's top-level tags, so a
character defined nested inside a `DefineSprite`'s own tag stream (legal
per spec) silently never resolved — it's now a recursive scan. Text/bitmap/
button rendering, `_width`/`_height`, `onClipEvent`/button `on()` handlers,
and color-transform application at render time still don't exist — see
[swf-support.md](swf-support.md) for the exact feature matrix,
[renderer.md](renderer.md) for the renderer's specific limitations, and
[avm1-support.md](avm1-support.md) for the AVM1 opcode support matrix,
documented confidence levels, and known Phase 5 limitations.

## Module layout

```
src/
  platform/   Log.h/.cpp                  — logging (no platform deps yet)
  swf/        SwfReader.h/.cpp            — byte/bit stream reader, RECT
              SwfRecords.h/.cpp           — MATRIX, CXFORM(WITHALPHA) readers,
                                             concatMatrix (world-transform compose)
              TagCode.h/.cpp              — SWF tag ID <-> name table
              TagDispatcher.h/.cpp        — generic tag-header reader
              PlaceObjectTag.h/.cpp       — PlaceObject/2, RemoveObject/2,
                                             FrameLabel body parsers
              ShapeRecords.h/.cpp         — FILLSTYLEARRAY/LINESTYLEARRAY/
                                             SHAPERECORD stream reader
              DefineShapeTag.h/.cpp       — DefineShape/2/3 tag body parser
              SwfLoader.h/.cpp            — FWS/CWS signature, zlib inflate,
                                             header parse, tag scan
  runtime/    Movie.h/.cpp                — loaded-movie model; owns the
                                             decompressed tag-stream bytes
              DisplayList.h/.cpp          — depth-indexed display list
              Timeline.h/.cpp             — per-frame tag grouping + playhead
                                             (generalized: main movie OR any
                                             nested DefineSprite tag stream)
              CharacterDictionary.h/.cpp  — characterId -> Shape/Sprite lookup
                                             (recursive: scans nested
                                             DefineSprite streams too)
              MovieClipInstance.h/.cpp    — Phase 5: ScriptEnvironment +
                                             MovieClipInstance — the AVM1/
                                             scene-graph integration point;
                                             the only module allowed to
                                             depend on both runtime/ and avm1/
  renderer/   IRenderer.h                 — abstract pixel-output interface
              SoftwareRenderer.h/.cpp     — RGBA8 framebuffer, scanline fill,
                                             PPM output (desktop/testing)
              ShapeTessellator.h/.cpp     — Shape -> flat polygons/polylines
              SceneRenderer.h/.cpp        — MovieClipInstance-tree walk ->
                                             IRenderer, recursive sprite
                                             rendering with per-instance
                                             transform/visibility (Phase 5)
  avm1/       Value.h/.cpp                — dynamic Value/Object type model,
                                             prototype chain, Array semantics,
                                             native property hooks (Phase 5)
              Stack.h                     — AVM1 operand stack
              Scope.h/.cpp                — scope chain (variable lookup)
              GlobalObject.h/.cpp         — global object construction
              ActionCode.h/.cpp           — AVM1 opcode enum + name table
              HostBindings.h              — abstract seam to MovieClip/
                                             Timeline actions (implemented by
                                             runtime/MovieClipInstance.cpp's
                                             MovieClipHostBindings, Phase 5)
              ExecutionContext.h/.cpp     — stack+scope+registers+constant
                                             pool+globals for one call frame
              Interpreter.h/.cpp          — tree-walking bytecode dispatch
                                             loop over DoAction bodies
tools/
  flash_runtime/main.cpp                  — CLI SWF inspector (+ --timeline, --render)
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
  Renderer/audio/input backends sit behind abstract interfaces (`IRenderer`
  done in Phase 3; `AudioManager`/`InputManager` still Phase 6), so a
  `SoftwareRenderer`/`Nintendo3DSRenderer` pair (etc.) can share the same
  core without the core depending on either.
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
