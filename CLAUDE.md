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

**Phase 1 through Phase 5 complete and committed** (see `git log`). Phase 1:
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
scan. `onClipEvent`/button `on()` handlers, `_width`/`_height`, and
color-transform application at render time are explicitly NOT done (see
`docs/avm1-support.md`'s "Known Phase 5 limitations"). 126 passing unit
tests, zero compiler warnings (`-Wall -Wextra`) on a full clean rebuild.

Read `docs/architecture.md` for the full 10-phase plan, `docs/swf-support.md`
for the current SWF feature matrix, `docs/renderer.md` for the renderer's
specific (documented, deliberate) limitations, and `docs/avm1-support.md`
for the AVM1 opcode support matrix, documented confidence levels, and known
Phase 5 limitations before starting new work. **Do not jump ahead of the
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

## Next phase (Phase 6)

Sound / Input. Phase 5 wired AVM1 into a real scene graph with per-instance
playheads; Phase 6 gives that scene graph two things it's currently
missing entirely:

- **Sound.** `DefineSound` (14) / `SoundStreamHead`/`2` (18/45) /
  `SoundStreamBlock` (19) / `StartSound` (15) / `DefineButtonSound` (17) tag
  parsing (currently name/offset/length only). An `AudioManager` abstract
  interface (mirroring `IRenderer`'s design: `flash3ds_core` stays
  platform-independent, a desktop backend for testing, a Nintendo 3DS/ndsp
  backend later — Phase 10). AVM1's `Sound` object (`attachSound`, `start`,
  `stop`, `setVolume`, `getVolume`) wired against it.
- **Input.** A `Key`/`Mouse` model AVM1 can query (`Key.isDown()`,
  `Key.getCode()`, `_xmouse`/`_ymouse` — currently stubbed in
  `MovieClipHostBindings::getProperty`, see `docs/avm1-support.md`) and that
  drives `StartDrag`/`EndDrag` for real (currently recognized/forwarded
  no-ops — see `MovieClipInstance.h`'s documented limitations). Desktop
  input source now (keyboard/mouse via whatever the test harness needs),
  3DS touch/button input later (Phase 10).
- **`onClipEvent`/button `on()` handlers.** Needs `PlaceObject2`'s optional
  `ClipActionRecord` section parsed (currently skipped entirely — see
  `docs/swf-support.md`) AND the input model above for most useful
  triggers (`mouseDown`, `press`, `keyDown`, ...); `enterFrame`/`load`/
  `unload` don't need input and could be done as soon as `ClipActionRecord`
  parsing exists.

Do NOT implement AVM2/ActionScript 3 — out of scope per the project spec.
Keep following the TDD pattern: small test SWFs / programmatic fixtures,
regression test for every bug, build phase-by-phase.

## Build

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```
