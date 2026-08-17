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

**Phase 1 complete and committed** (see `git log`). SWF loading (FWS/CWS),
header parsing, generic tag scan with logging, CLI inspector, 17 passing
unit tests, zero compiler warnings (`-Wall -Wextra`).

Read `docs/architecture.md` for the full 10-phase plan and
`docs/swf-support.md` for the current feature matrix before starting new
work. **Do not jump ahead of the current phase** — the project spec is
explicit about working phase-by-phase, building + testing + documenting at
the end of each one.

## Workflow for every phase

1. Implement only the current phase's scope.
2. `cmake --build build -j && ctest --test-dir build --output-on-failure`
3. Fix everything — never leave a failing test or a build warning.
4. Update the relevant `docs/*.md` file(s) with what now works and what
   still doesn't (see `swf-support.md` for the expected format).
5. `git add -A && git commit` with a clear summary of what shipped in that
   phase.

## Next phase (Phase 2)

Movie / Timeline / DisplayList: `ShowFrame`, `PlaceObject`, `PlaceObject2`,
`RemoveObject2` handling, plus `gotoAndStop()` / `gotoAndPlay()` /
`nextFrame()` / `prevFrame()` / `play()` / `stop()` on a per-timeline basis.
This builds on the `Movie`/`TagRecord` types already in `src/runtime/` and
`src/swf/` — extend, don't replace.

## Build

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```
