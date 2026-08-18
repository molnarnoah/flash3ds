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
Timeline             AVM1 VM         <-- Timeline: Phase 2 done; AVM1: Phase 4 done, Phase 5 wired in,
   |                    |                Phase 6 added native Key/Mouse/Sound + real StartDrag/EndDrag,
   |                    |                Phase 7 added native ExternalInterface + Interpreter::callFunction
   +---------+----------+
             |
             v
    MovieClipInstance tree            <-- Phase 5 done (per-instance Timeline + scripting Object;
             |                            wraps Display List, runs DoAction/DoInitAction); Phase 6
             |                            added ClipActionRecord (onClipEvent Load/Unload/EnterFrame)
    +--------+--------+
    |        |         |
    v        v         v
  Shape   Sprite   Text/Font   Button   <-- Shape/Sprite: Phase 3+5 done; Text/Font/Button: Phase 8 done
    |
    v
 Renderer                             <-- Phase 3 done (SoftwareRenderer, desktop/testing);
                                          Phase 5 rewired to walk MovieClipInstance; Phase 8 added
                                          glyph-run (Text/EditText) and non-interactive Button drawing

 Audio                                <-- Phase 6: IAudioBackend seam (src/audio/), StartSound tag +
                                          AVM1 Sound object dispatch; NullAudioBackend only so far

 Input                                <-- Phase 6: InputState (src/runtime/InputState.*) backs
                                          Key.isDown()/_xmouse/_ymouse/StartDrag

 Native bridge                        <-- Phase 7: ExternalInterface.call/addCallback (AS2 <-> native/
                                          host C++, both directions — src/runtime/MovieClipInstance.*'s
                                          ScriptEnvironment::registerHostFunction/callHostFunction/
                                          hasCallback/invokeCallback)
    |
    v
Top / Bottom Screen                   <-- Phase 10: Nintendo3DSRenderer targets the top
    |                                      screen only so far (GFX_TOP); bottom-screen
    v                                      use is an explicit follow-up, not implemented
Nintendo 3DS                          <-- Phase 10: real IRenderer/input-feed/IAudioBackend
                                          implementations (src/renderer/Nintendo3DSRenderer.*,
                                          src/platform/Nintendo3DSInput.*,
                                          src/audio/Nintendo3DSAudioBackend.*) + entry point
                                          (src/platform/nintendo3ds_main.cpp), cross-compiled
                                          and linked via a from-source toolchain bootstrap
                                          (docs/3ds-toolchain.md) -- NOT hardware/emulator-run
```

The runtime is deliberately modular so the renderer, audio, input, and
platform layers can be swapped for Nintendo 3DS-specific implementations
later without touching the SWF/AVM1 core.

## Current status: Phase 8 complete; Phase 9 (Hobo compatibility testing) and Phase 10 (Nintendo 3DS backend) underway

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
per spec) silently never resolved — it's now a recursive scan.

Phase 6 added **Sound / Input** without changing the AVM1 interpreter's
dispatch loop at all: `Object::FunctionDef` gained an optional `nativeImpl`
slot (a `std::function`) that `invokeFunction()` calls directly when set,
letting `ScriptEnvironment` populate `_global` with C++-backed `Key`/
`Mouse`/`Sound` built-ins through the exact same `NewObject`/`CallMethod`/
`CallFunction` paths a scripted function already uses. `InputState`
(`src/runtime/InputState.h/.cpp`) is a small host-settable keyboard/mouse
bag with no dependency on `avm1`/`runtime` in either direction, backing
`Key.isDown()`/`_xmouse`/`_ymouse`/real `StartDrag`-`EndDrag` (with
`lockCenter` and constraint-rectangle support). `DefineSound`/`StartSound`
tag parsing is structural (header fields only, no codec decode) and
dispatches to a new `audio::IAudioBackend` seam (`src/audio/`, mirroring
`IRenderer`'s design — `NullAudioBackend` is the only implementation so
far). `PlaceObject2`'s `ClipActionRecord` section is now parsed, and
`onClipEvent`'s `Load`/`Unload`/`EnterFrame` handlers dispatch for real;
the mouse/keyboard-related clip events and button `on()` handlers are
still not dispatched (need hit-testing/bounds and `DefineButton`
respectively — see `docs/avm1-support.md`'s Known Phase 6 limitations).

Phase 7 added **ExternalInterface** — AS2 <-> native/host communication in
both directions — reusing Phase 6's native-function (`nativeImpl`) seam
plus one new interpreter-level building block, a public
`Interpreter::callFunction()` that lets native/host code invoke an
already-constructed AS2 `Function` value directly (needed for the native ->
AS2 direction). `ExternalInterface.call(name, ...args)` (AS2 -> native)
dispatches to a C++ function registered ahead of time via
`ScriptEnvironment::registerHostFunction()`; `ExternalInterface.
addCallback(name, instance, function)` (native -> AS2) registers an AS2
function that native/host code can later find and run via
`ScriptEnvironment::hasCallback()`/`invokeCallback()`. Since this is a
same-process C++ embedding rather than a browser+JS bridge like real
Flash, `avm1::Value` crosses the boundary directly in both directions — a
deliberate documented simplification/improvement over real
`ExternalInterface` semantics (see `docs/avm1-support.md`'s
"ExternalInterface" section).

Phase 8 added **Text / Font / Button**: `DefineFont`/`DefineFont2` (glyph
outlines + code table + optional layout metrics — `src/swf/
DefineFontTag.h/.cpp`), `DefineText`/`DefineText2` (`TEXTRECORD`/
`GLYPHENTRY` runs — `src/swf/DefineTextTag.h/.cpp`), `DefineButton`/
`DefineButton2` (per-state character placements + action bytecode —
`src/swf/DefineButtonTag.h/.cpp`), and `DefineEditText` (structural parsing
of a dynamic/input text field — `src/swf/DefineEditTextTag.h/.cpp`) are all
now resolved into `CharacterDictionary` alongside shapes/sprites/sounds.
`ShapeRecords.h/.cpp` gained a small refactor (`readShapeRecordStream()`,
factored out of `readShapeWithStyle()`) so font glyphs — which have a bare
SHAPE record stream with no `FillStyleArray`/`LineStyleArray` of their own
— can reuse the exact same SHAPERECORD-decoding logic without duplicating
it. `SceneRenderer` gained matching leaf-rendering support: static text and
edit-text initial-text are drawn glyph-by-glyph (`renderGlyph()` — looks up
each glyph's outline in its font, scales by `textHeight/1024`, and reuses
`ShapeTessellator`), and buttons draw their "Up" state only (no mouse hit-
testing/state machine exists yet). Button `on()` handlers and the mouse-
related `onClipEvent`s deferred since Phase 6 are still not dispatched —
see `docs/avm1-support.md`'s Known Phase 8 limitations.

Bitmap rendering, `_width`/`_height`, and color-transform application at
render time still don't exist — see [swf-support.md](swf-support.md) for
the exact feature matrix, [renderer.md](renderer.md) for the renderer's
specific limitations, and [avm1-support.md](avm1-support.md) for the AVM1
opcode support matrix, documented confidence levels, and known Phase 6/7/8
limitations.

Phase 9 (Hobo compatibility testing) ran the whole pipeline above against
a real `hobo.swf` for the first time and found two real bugs neither
Phase 1-8's synthetic test fixtures had exercised: a shape-record
byte-alignment bug (`readShapeRecordStream()` wasn't byte-aligning before
a mid-stream `StyleChangeRecord`'s new style arrays — see
`src/swf/ShapeRecords.cpp`) that was silently corrupting every shape with
more than one style region, and a missing OOP-callable `MovieClip` method
surface (`stop()`/`play()`/`gotoAndStop()`/`gotoAndPlay()`/`nextFrame()`/
`prevFrame()`/`getBytesLoaded()`/`getBytesTotal()`, added to
`MovieClipInstance::handleNativeGet()` — see `src/runtime/
MovieClipInstance.cpp`). Both are fixed and regression-tested. See
`docs/compatibility.md` for the full report, including a screenshot-
verified render of the real title screen and the prioritized list of
still-open findings (`DefineMorphShape` not resolved into
`CharacterDictionary`, a handful of `DefineSprite`s missing a trailing
`ShowFrame`, and more of the Hobo series/Extreme Pamplona not yet tested).

Phase 10 (Nintendo 3DS backend, the final phase) built real
`IRenderer`/`InputState`-feeding/`IAudioBackend` implementations over
libctru/citro3d (`src/renderer/Nintendo3DSRenderer.*`,
`src/platform/Nintendo3DSInput.*`, `src/audio/Nintendo3DSAudioBackend.*`)
and a real entry point (`src/platform/nintendo3ds_main.cpp`), and — since
devkitPro's own servers aren't reachable from this project's build
environment — bootstrapped a devkitARM-equivalent toolchain entirely from
source (Ubuntu's generic `gcc-arm-none-eabi` cross-compiler + libctru/
citro3d/devkitarm-crtls built straight from their public GitHub repos). See
[3ds-toolchain.md](3ds-toolchain.md) for the full bootstrap writeup,
including every build issue hit and how each was resolved. The result was
validated end-to-end in this session: `flash3ds_core` (the same
platform-independent library the desktop build uses, completely
unmodified) plus the new 3DS backend and entry point cross-compile and
link with zero undefined non-weak symbols, package into a structurally
valid `.3dsx` via a from-source-built `3dsxtool`, and the desktop build
(187 tests) keeps passing throughout — but this was **not** run on real
3DS hardware or an emulator in this session (no such device was available
here), so "boots and renders correctly on an actual 3DS" remains
unverified; see 3ds-toolchain.md's "What's verified vs. not" section for
the precise boundary. Two small, genuine portability bugs in the
platform-independent core were found and fixed along the way (`uint32_t`
is not the same type as `unsigned int`/`int` on this ARM target, unlike on
x86_64 desktop, which broke two `std::clamp`/`std::min`/`std::max` calls in
`Timeline.cpp`/`SoftwareRenderer.cpp` — both now explicit about their
types on every platform).

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
                                             SHAPERECORD stream reader;
                                             Phase 8: readShapeRecordStream()
                                             factored out for font glyphs
                                             (bare SHAPE, no style arrays)
              DefineShapeTag.h/.cpp       — DefineShape/2/3 tag body parser
              DefineFontTag.h/.cpp        — Phase 8: DefineFont/DefineFont2
                                             (glyph outlines, code table,
                                             optional layout metrics)
              DefineTextTag.h/.cpp        — Phase 8: DefineText/DefineText2
                                             (TEXTRECORD/GLYPHENTRY runs)
              DefineButtonTag.h/.cpp      — Phase 8: DefineButton/
                                             DefineButton2 (per-state
                                             placements + action bytecode)
              DefineEditTextTag.h/.cpp    — Phase 8: DefineEditText
                                             (structural parsing only)
              SwfLoader.h/.cpp            — FWS/CWS signature, zlib inflate,
                                             header parse, tag scan
  runtime/    Movie.h/.cpp                — loaded-movie model; owns the
                                             decompressed tag-stream bytes
              DisplayList.h/.cpp          — depth-indexed display list
              Timeline.h/.cpp             — per-frame tag grouping + playhead
                                             (generalized: main movie OR any
                                             nested DefineSprite tag stream)
              CharacterDictionary.h/.cpp  — characterId -> character lookup
                                             (Shape/Sprite/Sound, + Phase 8:
                                             Font/Text/Button/EditText;
                                             recursive: scans nested
                                             DefineSprite streams too)
              MovieClipInstance.h/.cpp    — Phase 5: ScriptEnvironment +
                                             MovieClipInstance — the AVM1/
                                             scene-graph integration point;
                                             the only module allowed to
                                             depend on both runtime/ and avm1/
                                             (Phase 6: + InputState/
                                             IAudioBackend/drag state,
                                             native Key/Mouse/Sound globals,
                                             ClipActionRecord dispatch;
                                             Phase 7: + registerHostFunction/
                                             callHostFunction/hasCallback/
                                             invokeCallback, native
                                             ExternalInterface global)
              InputState.h/.cpp           — Phase 6: host-settable keyboard/
                                             mouse state; no avm1/runtime
                                             dependency either way
  platform/   Nintendo3DSInput.h/.cpp     — Phase 10: polls libctru hid,
                                             feeds InputState (touch->mouse,
                                             D-Pad/Circle Pad->arrow keys,
                                             face buttons->reasonable-effort
                                             stand-ins — see file header for
                                             the mapping rationale). 3DS-only
                                             (__3DS__), not linked into
                                             flash3ds_core
              nintendo3ds_main.cpp        — Phase 10: 3DS entry point; plays
                                             an EMBEDDED demo movie (real SD-
                                             card loading is an explicit
                                             follow-up — see file header and
                                             docs/3ds-toolchain.md)
              EmbeddedDemoSwf.h           — GENERATED (tools/gen_3ds_demo_swf.py)
                                             clean-room demo SWF byte array
  renderer/   IRenderer.h                 — abstract pixel-output interface
              SoftwareRenderer.h/.cpp     — RGBA8 framebuffer, scanline fill,
                                             PPM output (desktop/testing)
              ShapeTessellator.h/.cpp     — Shape -> flat polygons/polylines
              SceneRenderer.h/.cpp        — MovieClipInstance-tree walk ->
                                             IRenderer, recursive sprite
                                             rendering with per-instance
                                             transform/visibility (Phase 5);
                                             Phase 8: renderCharacter()
                                             dispatches Text/EditText glyph
                                             runs (renderGlyph()) and
                                             Button "Up"-state rendering
              Nintendo3DSRenderer.h/.cpp  — Phase 10: IRenderer wrapping a
                                             SoftwareRenderer, blitting to the
                                             real LCD framebuffer via
                                             gfxGetFramebuffer(); see
                                             docs/renderer.md's Phase 10 note
                                             for the rotated-framebuffer
                                             indexing confidence level.
                                             3DS-only (__3DS__), not linked
                                             into flash3ds_core
  audio/      IAudioBackend.h             — Phase 6: abstract sound-output
                                             interface, mirrors IRenderer.h
              NullAudioBackend.h/.cpp     — Phase 6: logs, plays nothing —
                                             ScriptEnvironment's default
              Nintendo3DSAudioBackend.h/  — Phase 10: real ndsp channel
              .cpp                          reservation/pause/stop; codec
                                             decode still doesn't exist (Phase
                                             6 limitation), so nothing is
                                             actually audible yet — see file
                                             header. 3DS-only (__3DS__), not
                                             linked into flash3ds_core
  avm1/       Value.h/.cpp                — dynamic Value/Object type model,
                                             prototype chain, Array semantics,
                                             native property hooks (Phase 5),
                                             native (C++-backed) FunctionDef::
                                             nativeImpl + makeNativeFunction()
                                             (Phase 6)
              Stack.h                     — AVM1 operand stack
              Scope.h/.cpp                — scope chain (variable lookup)
              GlobalObject.h/.cpp         — global object construction
              ActionCode.h/.cpp           — AVM1 opcode enum + name table
              HostBindings.h              — abstract seam to MovieClip/
                                             Timeline actions (implemented by
                                             runtime/MovieClipInstance.cpp's
                                             MovieClipHostBindings, Phase 5;
                                             Phase 6 added StartDrag's
                                             DragOptions struct)
              ExecutionContext.h/.cpp     — stack+scope+registers+constant
                                             pool+globals for one call frame
              Interpreter.h/.cpp          — tree-walking bytecode dispatch
                                             loop over DoAction bodies;
                                             Phase 7 added the public
                                             callFunction() entry point for
                                             native/host code to invoke an
                                             AS2 Function value directly
tools/
  flash_runtime/main.cpp                  — CLI SWF inspector (+ --timeline, --render)
  gen_3ds_demo_swf.py                     — Phase 10: generates src/platform/EmbeddedDemoSwf.h
                                             (not part of the CMake build; regenerate manually)
tests/
  TestFramework.h, TestMain.cpp           — tiny dependency-free test harness
  SwfTestFixtures.h/.cpp                  — programmatic SWF fixture builder
  test_*.cpp                              — unit tests
cmake/
  Toolchain-3DS.cmake                     — Phase 10: 3DS cross-compile toolchain file
third_party/
  3ds-support/                            — Phase 10: vendored devkitarm-crtls crt0/linker-
                                             script/specs (MPL-2.0, verbatim) — see its own
                                             README.md for provenance
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
  done in Phase 3; `IAudioBackend` done in Phase 6; input is a plain
  host-settable data struct, `InputState`, rather than an interface — see
  its own file header for why that's the right shape for input
  specifically), so a `SoftwareRenderer`/`Nintendo3DSRenderer` pair (etc.)
  can share the same core without the core depending on either.
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

For the Nintendo 3DS cross-compile (Phase 10), see
[3ds-toolchain.md](3ds-toolchain.md) for the full toolchain bootstrap; once
built:

```
cmake -S . -B build_3ds \
    -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-3DS.cmake \
    -DFLASH3DS_3DS_TOOLCHAIN_ROOT=/path/to/3ds-toolchain
cmake --build build_3ds -j
# -> build_3ds/flash3ds_3ds.3dsx
```
