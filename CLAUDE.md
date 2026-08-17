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

**Phase 1, Phase 2, Phase 3, and Phase 4 complete and committed** (see
`git log`). Phase 1: SWF loading (FWS/CWS), header parsing, generic tag
scan with logging, CLI inspector. Phase 2: MATRIX/CXFORM record readers,
PlaceObject/PlaceObject2/RemoveObject/RemoveObject2/FrameLabel tag-body
parsing, depth-indexed `DisplayList` (add/replace/update-in-place/remove),
and `Timeline` with full playhead control (`gotoAndStop`/`gotoAndPlay` by
frame or label, `nextFrame`/`prevFrame`/`play`/`stop`/`advanceOneFrame`).
Phase 3: `DefineShape`/`2`/`3` parsing (fill/line styles + SHAPERECORD
stream), `CharacterDictionary` (characterId -> Shape/Sprite, including
nested `DefineSprite` tag streams reusing `Timeline`), `concatMatrix`
world-transform composition, a deliberately simplified `ShapeTessellator`,
an `IRenderer`/`SoftwareRenderer` (scanline fill, PPM output), and a
`SceneRenderer` that walks the display list and recurses into sprites, plus
a CLI `--render <frame> <out.ppm>` flag. Phase 4: the AVM1 VM — a
`Value`/`Object` dynamic-type model (prototype chain, Array semantics),
`Stack`/`Scope`/`GlobalObject`/`ExecutionContext`, and a tree-walking
`Interpreter` covering the full AVM1 opcode set (arithmetic/comparison/
bitwise/string ops, variables, objects/arrays, function definition/calling
including `DefineFunction2` register params + preload flags, closures,
bounded-depth recursion, and `Jump`/`If` control flow). `HostBindings`
exists as the seam to MovieClip/Timeline actions but is all no-op stubs —
the VM is tested purely in isolation against raw bytecode buffers and is
**not yet wired into `DoAction` tag dispatch or the scene graph**. 114
passing unit tests, zero compiler warnings (`-Wall -Wextra`) on a full
clean rebuild.

Read `docs/architecture.md` for the full 10-phase plan, `docs/swf-support.md`
for the current SWF feature matrix, `docs/renderer.md` for the renderer's
specific (documented, deliberate) limitations, and `docs/avm1-support.md`
for the AVM1 opcode support matrix and documented confidence levels before
starting new work. **Do not jump ahead of the current phase** — the
project spec is explicit about working phase-by-phase, building + testing
+ documenting at the end of each one.

## Workflow for every phase

1. Implement only the current phase's scope.
2. `cmake --build build -j && ctest --test-dir build --output-on-failure`
3. Fix everything — never leave a failing test or a build warning.
4. Update the relevant `docs/*.md` file(s) with what now works and what
   still doesn't (see `swf-support.md` for the expected format).
5. `git add -A && git commit` with a clear summary of what shipped in that
   phase.

## Next phase (Phase 5)

MovieClip API / scene-graph wiring. Phase 4 built and thoroughly tested the
AVM1 interpreter in isolation; Phase 5 connects it to the rest of the
runtime:

- Dispatch `DoAction` (and `DoInitAction`) tag bodies through
  `Interpreter::execute` during `Timeline` frame advance, instead of the
  current parse-and-skip.
- Implement `HostBindings` for real against `Timeline`/`DisplayList`:
  `GotoFrame`/`GotoLabel`/`Play`/`Stop`/`NextFrame`/`PreviousFrame`,
  `GetProperty`/`SetProperty` (`_x`/`_y`/`_alpha`/`_visible`/`_rotation`/
  etc.), `CloneSprite`/`RemoveSprite`, `StartDrag`/`EndDrag`, `SetTarget`.
- Give each `MovieClip` (sprite) **instance** its own independent playhead
  — `SceneRenderer` currently caches one shared `Timeline` per *character*,
  documented as a Phase 3 limitation; Phase 5 needs one per *instance*.
- Expose the display-list tree as AVM1 objects: `_root`, `_parent`,
  named child references (`this.childName`), so `GetVariable`/`SetVariable`
  and `GetMember` can resolve MovieClip properties and nested clips.
- `onClipEvent`/button `on()` handler wiring (event-driven AVM1 code, not
  just frame-script `DoAction`).

Do NOT implement AVM2/ActionScript 3 — out of scope per the project spec.
Keep following the TDD pattern: small test SWFs / programmatic fixtures,
regression test for every bug, build phase-by-phase.

## Build

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```
