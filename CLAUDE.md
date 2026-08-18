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

**Phase 1 through Phase 8 complete and committed** (see `git log`). Phase 1:
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

Phase 7: ExternalInterface (AS2 <-> native/host communication, both
directions), built entirely on Phase 6's `nativeImpl` seam plus one new
interpreter-level building block: a public `Interpreter::callFunction()`
static method (trivial wrapper around the existing anonymous-namespace
`invokeFunction()` helper in `Interpreter.cpp`) letting native/host code
invoke an already-constructed AS2 `Function` value directly, without going
through `CallFunction`/`CallMethod` bytecode dispatch. `ScriptEnvironment`
gained `registerHostFunction`/`callHostFunction` (AS2 -> native:
`ExternalInterface.call(name, ...args)` looks up and invokes a C++ function
registered ahead of time) and `hasCallback`/`invokeCallback` (native -> AS2:
`ExternalInterface.addCallback(name, instance, function)` registers an AS2
function; host code later finds and runs it via `invokeCallback`, which
builds a fresh top-level `Scope`/`ExecutionContext` with **no HostBindings
bound** — see "Known Phase 7 limitations" below). `ExternalInterface`
(`available`/`call`/`addCallback`) is built in `ScriptEnvironment`'s
constructor, same pattern as Phase 6's `Key`/`Mouse`/`Sound`. Since this
runtime embeds AS2 in the same process as its native/host code (no
browser, unlike real Flash's JS bridge), `avm1::Value` crosses the AS2 <->
native boundary directly in both directions — a deliberate, documented
simplification/improvement over real `ExternalInterface`'s JS/XML
marshalling. 158 passing unit tests, zero compiler warnings (`-Wall
-Wextra`) on a full clean rebuild.

**Known Phase 7 limitation:** `invokeCallback()` runs with no
`HostBindings` bound, so `GotoFrame`/`Play`/`GetProperty`/`SetProperty`/
`CloneSprite`/`RemoveSprite`/`StartDrag`/`EndDrag`/`SetTarget` called
DIRECTLY inside an `addCallback`-registered function's body are silently
no-ops (matches Phase 4's original host-less-context precedent) — ordinary
computation and calling other AS2 functions/objects still work normally.
See `docs/avm1-support.md`'s "Known Phase 7 limitations" for the full list.

Phase 8: Text/Font/Button. Four new character-defining tags, all resolved
into `CharacterDictionary` (`swf::FontDef`/`swf::TextDef`/`swf::ButtonDef`/
`swf::EditTextDef`) exactly like Phase 3's shapes and Phase 6's sounds:
`DefineFont`/`DefineFont2` (glyph outlines — a bare SHAPE record stream
with no fill/line style arrays of its own, hence `ShapeRecords.h/.cpp`
gained a small refactor, `readShapeRecordStream()`, factored out of
`readShapeWithStyle()` so glyphs reuse the exact same decoding logic — plus
`DefineFont2`'s code table and optional layout metrics; `DefineFont3` is
explicitly rejected, since its glyph coordinates use a 20x finer em-square
that would otherwise silently mis-scale every glyph), `DefineText`/
`DefineText2` (`TEXTRECORD`/`GLYPHENTRY` runs), `DefineButton`/
`DefineButton2` (per-state character placements + action bytecode —
captured but not dispatched, same as `onClipEvent`), and `DefineEditText`
(structural parsing only — no variable binding/word-wrap/scrolling).
`SceneRenderer` gained matching leaf-rendering: `renderCharacter()` now
dispatches on the resolved character's actual kind instead of assuming
Shape — text/edit-text glyph runs are drawn via `renderGlyph()` (looks up
each glyph's outline in its font, scales by `textHeight/1024` — see
`docs/avm1-support.md`'s "Text/Font" section for why one scale factor
covers both outlines and advances — and reuses `ShapeTessellator` via a
synthesized one-fill-style `Shape`), and buttons always draw their "Up"
state (no mouse hit-testing/state machine exists yet). 182 passing unit
tests, zero compiler warnings (`-Wall -Wextra`) on a full clean rebuild.

**Known Phase 8 limitations:** button `on()` handlers and the mouse-related
`onClipEvent`s deferred since Phase 6 are still not dispatched (both need
hit-testing/bounds infrastructure that doesn't exist yet, itself blocked on
`_width`/`_height`); there's no AS2 `TextField`/`TextFormat` API surface;
`DefineEditText` only renders when it embeds a `DefineFont2` font (one with
a code table) and has literal `initialText`, with no word-wrap/scrolling/
alignment/variable-binding. See `docs/avm1-support.md`'s "Known Phase 8
limitations" and `docs/swf-support.md`'s Phase 8 section for the full list.

Read `docs/architecture.md` for the full 10-phase plan, `docs/swf-support.md`
for the current SWF feature matrix, `docs/renderer.md` for the renderer's
specific (documented, deliberate) limitations, and `docs/avm1-support.md`
for the AVM1 opcode support matrix, documented confidence levels, and known
Phase 6/7/8 limitations before starting new work. **Do not jump ahead of
the current phase** — the project spec is explicit about working phase-by-
phase, building + testing + documenting at the end of each one.

## Workflow for every phase

1. Implement only the current phase's scope.
2. `cmake --build build -j && ctest --test-dir build --output-on-failure`
3. Fix everything — never leave a failing test or a build warning.
4. Update the relevant `docs/*.md` file(s) with what now works and what
   still doesn't (see `swf-support.md` for the expected format).
5. `git add -A && git commit` with a clear summary of what shipped in that
   phase.

## Next phase (Phase 9)

Hobo compatibility testing. Per the original 10-phase plan: run the runtime
against real target SWF content end-to-end, catalogue what actually breaks
or is missing when pointed at it, and prioritize fixes by what real content
needs rather than continuing to guess at spec coverage in the abstract.
Concrete scope (which file(s), what "passing" looks like, how failures get
triaged into new phases vs. quick fixes) to work out at the start of that
phase.

Carry-overs / explicitly deferred from earlier phases, in case a target
title needs one of these sooner than its "natural" later phase:

- `Sound.attachSound(name: String)` — the real AS2 linkage-name form —
  needs `ExportAssets` tag parsing (currently unimplemented; only numeric
  `attachSound(id)` resolves).
- Audio codec decode (ADPCM/MP3/etc.) — needed by any real
  `IAudioBackend` implementation; `NullAudioBackend` doesn't need it since
  it never actually plays anything.
- Mouse/keyboard `onClipEvent`s (`press`, `release`, `rollOver`, `mouseDown`,
  `keyDown`, ...) and button `on()` handlers — `DefineButton`/`DefineButton2`
  parsing exists now (Phase 8) and captures the action bytecode, but
  dispatching any of these still needs hit-testing/bounds, itself blocked
  on `_width`/`_height` (recursive subtree bounding-box computation, still
  not implemented).
- `invokeCallback()`-driven `ExternalInterface.addCallback` functions run
  with no `HostBindings` bound (Phase 7 limitation) — a callback that needs
  to affect the scene graph currently has to call out to an ordinary
  clip-scoped AS2 function rather than doing it directly.
- No AS2 `TextField`/`TextFormat` API surface, and `DefineEditText`
  rendering only covers the narrow case of an embedded `DefineFont2` font
  with literal `initialText` — no variable-binding, word-wrap, scrolling,
  or alignment (Phase 8 limitation).
- `DefineFont3`, `DefineFontInfo`/`DefineFontInfo2`, and `DefineButton2`
  records with a `FilterList` are all explicitly rejected/unsupported
  rather than approximated (Phase 8 limitation) — see
  `docs/swf-support.md`'s Phase 8 section for why.

Do NOT implement AVM2/ActionScript 3 — out of scope per the project spec.
Keep following the TDD pattern: small test SWFs / programmatic fixtures,
regression test for every bug, build phase-by-phase.

## Build

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```
