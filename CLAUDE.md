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

**Phase 1, Phase 2, and Phase 3 complete and committed** (see `git log`).
Phase 1: SWF loading (FWS/CWS), header parsing, generic tag scan with
logging, CLI inspector. Phase 2: MATRIX/CXFORM record readers,
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
a CLI `--render <frame> <out.ppm>` flag. 70 passing unit tests, zero
compiler warnings (`-Wall -Wextra`) on a full clean rebuild.

Read `docs/architecture.md` for the full 10-phase plan, `docs/swf-support.md`
for the current feature matrix, and `docs/renderer.md` for the renderer's
specific (documented, deliberate) limitations before starting new work.
**Do not jump ahead of the current phase** — the project spec is explicit
about working phase-by-phase, building + testing + documenting at the end
of each one.

## Workflow for every phase

1. Implement only the current phase's scope.
2. `cmake --build build -j && ctest --test-dir build --output-on-failure`
3. Fix everything — never leave a failing test or a build warning.
4. Update the relevant `docs/*.md` file(s) with what now works and what
   still doesn't (see `swf-support.md` for the expected format).
5. `git add -A && git commit` with a clear summary of what shipped in that
   phase.

## Next phase (Phase 4)

AVM1 VM — basic opcode set. This is the first phase that needs an actual
bytecode interpreter: `Value`/`Stack`/`Scope`/`GlobalObject`/
`ExecutionContext` types, and an opcode dispatch loop over `DoAction`
tag bodies (currently parsed as raw bytes and skipped — see
`docs/avm1-support.md`, still a stub). Do NOT implement AVM2/ActionScript 3
— out of scope per the project spec.

Builds on Phase 3's `CharacterDictionary`/`Timeline`/`SceneRenderer` — AVM1
will eventually need to call back into `Timeline` (`gotoAndStop`, etc.) and
`DisplayList` (property access on `MovieClip` instances), but Phase 4 itself
should focus on getting the interpreter loop and a basic opcode set correct
in isolation first (per the project spec's "small test SWFs, regression test
for every bug" TDD approach) before wiring it into the scene graph — that
wiring, plus the MovieClip API surface (`_root`, per-instance playheads,
`onClipEvent`), is Phase 5.

## Build

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```
