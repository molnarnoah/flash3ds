# CLAUDE.md — flash3ds-runtime

Context for any Claude session (or Claude Code) picking up work in this
directory.

## What this project is

A **clean-room, independently implemented** SWF/AVM1 (Flash) runtime,
targeting an eventual standalone Nintendo 3DS "Flash Virtual Console". This
is a separate project from the Shift-DX Ghidra reverse-engineering work
that inspired it.

## Hard rules — do not violate these

- **Never copy code from the Shift-DX executable, `code.bin`, or the
  gameswf library it embeds.** Ghidra RE results are a *behavioral
  reference only* — see `docs/shift-dx-behavior.md`. If a design choice
  needs justification from the RE work, cite it in that doc; don't paste
  decompiled logic into `src/`.
- **Do not modify the original Shift-DX source tree, `code.bin`, or
  executable.** This project never touches those files at all — it lives
  entirely in this directory.
- Implement against the **public SWF File Format Specification**, not
  against decompiled Shift-DX internals.
- Prioritize **AVM1/AS2** (SWF 6–8). Do not prioritize AVM2/AS3.

## Current status

**Phase 1 through Phase 6 complete and committed** (see `git log`). Phase 1:
SWF loading (FWS/CWS), header parsing, generic tag scan with logging, CLI
inspector. Phase 2: MATRIX/CXFORM record readers, PlaceObject/PlaceObject2/
RemoveObject/RemoveObject2/FrameLabel tag-body parsing, depth-indexed
`DisplayList` (add/replace/update-in-place/remove), and `Timeline` with
full playhead control (`gotoAndStop`/`gotoAndPlay` by frame or label,
`nextFrame`/`prevFrame`/`play`/`stop`/`advanceOneFrame`). Phase 3:
`DefineShape`/`2`/`3` parsing (fill/line styles + SHAPERECORD stream),
`CharacterDictionary` (characterId -> Shape/Sprite, including nested
`DefineSprite` tag streams reusing `Timeline`), `concatMatrix` world-
transform composition, a deliberately simplified `ShapeTessellator`, an
`IRenderer`/`SoftwareRenderer` (scanline fill, PPM output), and a
`SceneRenderer` that walks the display list and recurses into sprites, plus
a CLI `--render <frame> <out.ppm>` flag. Phase 4: the AVM1 VM — a
`Value`/`Object` dynamic-type model (prototype chain, Array semantics),
`Stack`/`Scope`/`GlobalObject`/`ExecutionContext`, and a tree-walking
`Interpreter` covering the full AVM1 opcode set (arithmetic/comparison/
bitwise/string ops, variables, objects/arrays, function definition/calling
including `DefineFunction2` register params + preload flags, closures,
bounded-depth recursion, and `Jump`/`If` control flow) — tested purely in
isolation against raw bytecode buffers, not yet wired into anything. Phase
5: `MovieClipInstance` (`src/runtime/MovieClipInstance.h/.cpp`) — the AVM1/
scene-graph integration point. Every placed sprite gets its own `Timeline`
(a real independent playhead, replacing Phase 3's shared per-character
cache), runs `DoAction`/`DoInitAction` through the Phase 4 interpreter with
a real `HostBindings` wired to that `Timeline`/`DisplayList`
(`GotoFrame`/`Play`/`Stop`/`GetProperty`/`SetProperty`/`CloneSprite`/
`RemoveSprite`/`SetTarget`, all working), and exposes `_x`/`_y`/
`_currentframe`/`_root`/`_parent`/named-child-clip access through
`avm1::Object`'s new native-property hooks (`nativeGet`/`nativeSet`/
`nativeEnumerate`). `SceneRenderer` now walks the `MovieClipInstance` tree,
so script-driven changes actually render; the CLI's `--render` ticks the
tree frame-by-frame (running scripts along the way) instead of jumping
straight to the target frame. Phase 5 also fixed a real Phase 3 bug found
while building its manual smoke test: `CharacterDictionary` only scanned
top-level tags, so a character defined nested inside a `DefineSprite`'s own
tag stream (legal per spec) silently never resolved — it's now a recursive
scan.

Phase 6: Sound / Input. `Object::FunctionDef` gained an optional
`nativeImpl` (C++-backed function) short-circuit in `invokeFunction()` —
zero changes needed to `NewObject`/`CallMethod`/`CallFunction` dispatch —
which `ScriptEnvironment` uses to populate `_global` with native `Key`
(`isDown`/`getCode`/named constants), `Mouse` (`show`/`hide`, no-ops — no
cursor model), and `Sound` (`attachSound`/`start`/`stop`/`setVolume`/
`getVolume`) built-ins. `InputState` (`src/runtime/InputState.h/.cpp`) is a
small host-settable keyboard/mouse bag backing those plus real
`_xmouse`/`_ymouse` and real `StartDrag`/`EndDrag` (`lockCenter` +
constraint-rectangle support). `DefineSound`(14)/`StartSound`(15) tags are
parsed structurally (header fields only, no codec decode) and dispatched
through a new `audio::IAudioBackend` seam (`src/audio/`, mirrors
`IRenderer`; only `NullAudioBackend` exists so far). `PlaceObject2`'s
`ClipActionRecord` section is now parsed (`swf::ClipEventFlag`,
spec-derived-but-unverified bit layout), and `onClipEvent`'s
`Load`/`Unload`/`EnterFrame` handlers dispatch for real; mouse/keyboard-
related clip events and button `on()` handlers still don't (need
hit-testing/bounds and `DefineButton` respectively). `_width`/`_height`
and color-transform application at render time are still explicitly NOT
done (see `docs/avm1-support.md`'s "Known Phase 6 limitations"). 151
passing unit tests, zero compiler warnings (`-Wall -Wextra`) on a full
clean rebuild.

Read `docs/architecture.md` for the full 10-phase plan, `docs/swf-support.md`
for the current SWF feature matrix, `docs/renderer.md` for the renderer's
specific (documented, deliberate) limitations, and `docs/avm1-support.md`
for the AVM1 opcode support matrix, documented confidence levels, and known
Phase 6 limitations before starting new work. **Do not jump ahead of the
current phase** — the project spec is explicit about working phase-by-
phase, building + testing + documenting at the end of each one.

## Workflow for every phase

1. Implement only the current phase's scope.
2. `cmake --build build -j && ctest --test-dir build --output-on-failure`
3. Fix everything — never leave a failing test or a build warning.
4. Update the relevant `docs/*.md` file(s) with what now works and what
   still doesn't (see `swf-support.md` for the expected format).
5. `git add -A && git commit` with a clear summary of what shipped in that
   phase.

## Next phase (Phase 7)

ExternalInterface. Per the original 10-phase plan: AS2 <-> host/native
communication (`ExternalInterface.call`/`addCallback`), which on a real
Flash Virtual Console likely means AS2 <-> native-3DS-code calls rather
than AS2 <-> JavaScript (there's no browser). Concrete scope to work out at
the start of that phase (read `docs/avm1-support.md`'s Phase 6 section
first — `Sound`/`Key`/`Mouse`'s native-function mechanism,
`avm1::makeNativeFunction`, is almost certainly the right building block to
reuse for whatever `ExternalInterface.addCallback` ends up needing).

Carry-overs / explicitly deferred from Phase 6, in case a target title
needs one of these sooner than its "natural" later phase:

- `Sound.attachSound(name: String)` — the real AS2 linkage-name form —
  needs `ExportAssets` tag parsing (currently unimplemented; only numeric
  `attachSound(id)` resolves).
- Audio codec decode (ADPCM/MP3/etc.) — needed by any real
  `IAudioBackend` implementation; `NullAudioBackend` doesn't need it since
  it never actually plays anything.
- Mouse/keyboard `onClipEvent`s (`press`, `release`, `rollOver`, `mouseDown`,
  `keyDown`, ...) and button `on()` handlers — need hit-testing/bounds
  (itself blocked on `_width`/`_height`, which needs recursive subtree
  bounding-box computation) and, for buttons, `DefineButton`/`DefineButton2`
  parsing (Phase 8+).

Do NOT implement AVM2/ActionScript 3 — out of scope per the project spec.
Keep following the TDD pattern: small test SWFs / programmatic fixtures,
regression test for every bug, build phase-by-phase.

## Build

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```
