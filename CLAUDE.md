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

Phase 9: Hobo compatibility testing. Ran the full pipeline against a real
copy of `hobo.swf` (~4.97 MB, SWF6/CWS, the actual game, not a synthetic
proxy) end-to-end for the first time — see `docs/compatibility.md` for the
full report. Found and fixed two real bugs neither Phase 1-8's synthetic
fixtures had exercised: (1) `readShapeRecordStream()` wasn't byte-aligning
before a mid-stream `StyleChangeRecord`'s new fill/line style arrays (a
spec requirement), which was silently corrupting every shape with more
than one style region — before the fix, rendering all 13 title-screen
frames logged ~1.54 million bogus "Unknown fill style type" warnings;
after, zero; (2) `MovieClipInstance` had no OOP-callable methods at all
(`someClip.stop()`/`.gotoAndPlay()`/`.getBytesLoaded()` etc. via
`CallMethod` bytecode all failed) — only the bare unqualified action-code
forms worked. Added `stop`/`play`/`nextFrame`/`prevFrame`/`gotoAndStop`/
`gotoAndPlay`/`getBytesLoaded`/`getBytesTotal` as real native methods.
Both fixes are regression-tested. The rendered title screen was visually
confirmed correct (character art, wordmark, button, panels all in place)
and one shape's parsed bounds/fill-color were independently hand-verified
against the raw SWF bytes. 187 passing unit tests, zero compiler warnings
on a full clean rebuild.

**Known Phase 9 limitations / open findings (see `docs/compatibility.md`
for the full prioritized list):** `DefineMorphShape`/`2` (confirmed
present, 19 occurrences) still isn't resolved into `CharacterDictionary`;
a handful of `DefineSprite` tag streams end without a trailing `ShowFrame`
(handled gracefully, likely benign); only the specific OOP MovieClip
methods a real failing script needed were added, not the full AS2 surface
(`swapDepths`/`hitTest`/`duplicateMovieClip`/etc. still missing); the rest
of the Hobo series, Extreme Pamplona, and `hobo.swf`'s own gameplay frames
(only the 13-frame title/menu screen was actually tested) remain untested.

Read `docs/architecture.md` for the full 10-phase plan, `docs/swf-support.md`
for the current SWF feature matrix, `docs/renderer.md` for the renderer's
specific (documented, deliberate) limitations, `docs/avm1-support.md` for
the AVM1 opcode support matrix, documented confidence levels, and known
Phase 6/7/8/9 limitations, and `docs/compatibility.md` for the real-content
compatibility report before starting new work. **Do not jump ahead of the
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

## Phase 10 complete — confirmed booting in Azahar; extended into a dual-screen/input/sound test app

Nintendo 3DS backend — the final phase per the original 10-phase plan. All
of the code is real: `Nintendo3DSRenderer` (`IRenderer` over a wrapped
`SoftwareRenderer`, blitting to the real LCD framebuffer via libctru
`gfxGetFramebuffer`, now driving BOTH the top and bottom screens
simultaneously), `Nintendo3DSInput` (polls libctru `hid`, feeds
`InputState` — touch->mouse, D-Pad/Circle Pad->arrow keys, face
buttons->reasonable-effort key stand-ins), and `Nintendo3DSAudioBackend`
(real `ndsp` channel reservation/play/stop plumbing — `playSound()` still
can't play actual SWF audio, same root cause as `NullAudioBackend`: no
codec-decode step exists anywhere yet — but a new diagnostic-only
`playTestTone()` synthesizes and queues a real, audible sine-wave tone,
proving the ndsp pipeline itself works independent of that gap), plus a
real 3DS entry point (`src/platform/nintendo3ds_main.cpp`) playing an
embedded demo movie on top while a live button/circle-pad/touch test
picture runs on bottom.

**The toolchain:** this project's environment could not reach devkitPro's
own package servers, so Phase 10 first had to bootstrap a devkitARM-
equivalent toolchain entirely from source (Ubuntu's generic ARM
cross-compiler + libctru/citro3d/devkitarm-crtls built straight from their
public GitHub repos) — see `docs/3ds-toolchain.md` for the complete
writeup, every issue hit, and exactly how each was fixed.

**Confirmed working:** the user reported the initial (top-screen-only)
`.3dsx` **boots and runs in Azahar** (a Citra-based 3DS emulator) — the
first real hardware-emulator confirmation this project has had. Building
on that, the test app was extended to exercise more surface at once (both
screens, live button/circle-pad/touch visualization, audible A/B/X/Y test
tones) — this surfaced and fixed a real bug along the way:
`gfxFlushBuffers()`/`gfxSwapBuffers()` are GLOBAL, both-screens libctru
calls (confirmed in `source/gfx.c`), not per-screen, so calling them once
per `Nintendo3DSRenderer::endFrame()` (correct with one screen) would
double-swap once a second screen was added — fixed via a new static
`Nintendo3DSRenderer::presentFrame()`, called once per real frame after
both screens are drawn. This dual-screen build compiles/links cleanly
(same zero-undefined-non-weak-symbols result as before) but has not yet
been separately re-confirmed running by the user — see
`docs/3ds-toolchain.md`'s "What's verified vs. not" section for the exact,
currently-open boundary (pixel-correctness of both screens, whether the
test tones are actually audible, whether the button/touch picture tracks
real input correctly).

Two genuine (if narrow) portability bugs in the platform-independent core
were found and fixed while bringing up the cross-compile — `uint32_t` is
not the same type as `unsigned int`/`int` on this ARM target the way it
happens to be on x86_64 desktop, which silently-but-incorrectly compiled
in two `std::clamp`/`std::min`/`std::max` call sites
(`Timeline.cpp`/`SoftwareRenderer.cpp`) — both now explicit about their
types on every platform. All 187 desktop tests still pass.

Phase 9 (Hobo compatibility testing) is not "done" in the same sense the
numbered phases are — its own charter is ongoing real-content testing, and
`docs/compatibility.md`'s "Not yet tested" list (the rest of the Hobo
series, Extreme Pamplona, `hobo.swf`'s own gameplay frames) is still open.
It can continue in parallel with Phase 10, or resume afterward — whichever
a target title's needs make more useful at the time.

Carry-overs / explicitly deferred from earlier phases, in case a target
title needs one of these sooner than its "natural" later phase:

- `Sound.attachSound(name: String)` — the real AS2 linkage-name form —
  needs `ExportAssets` tag parsing (currently unimplemented; only numeric
  `attachSound(id)` resolves).
- Audio codec decode (ADPCM/MP3/etc.) — needed for actual sound output;
  neither `NullAudioBackend` nor Phase 10's `Nintendo3DSAudioBackend`
  (real `ndsp` channel plumbing, but nothing to queue into it) can play
  anything without this. The single highest-value carry-over if a target
  title needs audible sound.
- 3DS hardware/emulator verification (see `docs/3ds-toolchain.md`'s "What's
  verified vs. not") — Phase 10's code is real and toolchain-verified but
  has never actually run on a 3DS or Citra; this environment had access to
  neither.
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
- `DefineMorphShape`/`DefineMorphShape2` (confirmed present in real
  `hobo.swf` content, 19 occurrences) are recognized by `TagCode` but not
  resolved into `CharacterDictionary` — a real chunk of new parsing +
  ratio-interpolated rendering work, not a quick fix (Phase 9 finding, see
  `docs/compatibility.md`).
- Only the specific OOP `MovieClip` methods a real failing script needed
  were added in Phase 9 (`stop`/`play`/`nextFrame`/`prevFrame`/
  `gotoAndStop`/`gotoAndPlay`/`getBytesLoaded`/`getBytesTotal`) — the rest
  of the AS2 `MovieClip` method surface (`swapDepths`/`hitTest`/
  `duplicateMovieClip`/`attachMovie`/`loadMovie`/...) is still missing.

Do NOT implement AVM2/ActionScript 3 — out of scope per the project spec.
Keep following the TDD pattern: small test SWFs / programmatic fixtures,
regression test for every bug, build phase-by-phase.

## Compatibility-audit phase (started 2026-08-18, ongoing)

Phase 10 (3DS backend) is done; the project entered a new,
not-phase-numbered "compatibility / limitation discovery and fixing" mode
per explicit user instruction: audit the CURRENT source against actual
execution paths (not prior docs' claims), build a compatibility matrix,
then fix ONE highest-priority limitation at a time with a full
repro-fix-test-regression-document cycle, never batching multiple fixes.

**Required reading for a new session picking this up:**
`docs/compatibility-matrix.md` (subsystem-by-subsystem ground truth),
`docs/avm1-compatibility.md` (per-opcode table), `docs/known-limitations.md`
(prioritized findings + the full STEP 1-10 writeup for the one fix already
done), `docs/3ds-limitations.md`, `docs/test-results.md`.

**Done so far:** Priority #1 — `ColorTransform`/`_alpha` was parsed/stored/
script-mutable but had ZERO effect on any rendered pixel (confirmed by
tracing `SceneRenderer.cpp` line by line — not assumed). Fixed:
`swf::concatColorTransform`/`swf::applyColorTransform` (new,
`src/swf/SwfRecords.h/.cpp` + `src/swf/ShapeRecords.h/.cpp`), threaded
through every `SceneRenderer` render call alongside the existing
`worldMatrix`. Two new regression tests
(`SceneRenderer_MovieClipInstanceAlpha_*`, `tests/test_scene_renderer.cpp`).
189/189 tests passing, zero regressions. 3DS build (`build_3ds`) rebuilds
clean with the fix; new `.3dsx` delivered but **not yet confirmed on
Azahar/hardware**. Found and fixed a real doc bug along the way:
`docs/externalinterface.md` claimed "not started" when ExternalInterface
has actually been real since Phase 7 — corrected in place.

**Also found, a real (surprising) discrepancy worth a future session's
attention:** `hobo.swf` frames 1-5 render byte-for-byte identical before
and after the `_alpha` fix — so Phase 9's "PLAY! button fading in by frame
5" observation is NOT caused by `ColorTransform`. There IS a real visual
difference between those frames in the button region, cause
uncharacterized. See `docs/compatibility-matrix.md`'s "Corrections to prior
docs" section — don't just assume this is resolved.

**Top of the priority queue for the next limitation (see
`docs/known-limitations.md` for full detail, ranked):**

1. ~~ColorTransform/`_alpha` rendering~~ — **done**.
2. No mouse/button interactivity at all — **CURRENT, SOLE FOCUS per
   explicit user instruction (2026-08-18); priorities #3-5 below are on
   hold until this one reaches a stable state.** Sub-fix 1/N
   (`_width`/`_height`, previously hardcoded to 0), Sub-fix 2/N (the
   `_xmouse`/`_ymouse` device-pixel -> stage-pixel coordinate conversion),
   Sub-fix 3/N (edge-detected input state — `InputState::commitFrame()`/
   `isKeyPressed()`/`isKeyReleased()`/`isMousePressed()`/`isMouseReleased()`/
   `isTouchPressed()`/`isTouchReleased()`), Sub-fix 4/N (bounding-box
   hit-testing — `MovieClipInstance::hitTestPoint()`, an internal
   topmost-object-under-a-point primitive, plus real AS2-visible
   `MovieClip.hitTest(x, y)`), and Sub-fix 5/N (`ButtonInstance` — a real
   per-placement runtime object for placed buttons, with its own
   transform/depth/visibility/hit-area/UP-OVER-DOWN state, wired into
   hit-testing/display-list-lifetime/AS2-identity via the existing
   machinery, NOT via a second implementation) are **all done** — see
   `docs/known-limitations.md`'s STEP 1-10 writeups for all five,
   `docs/interactivity-audit.md` (full 8-part trace, kept up to date as
   each sub-fix lands), `docs/input.md` (coordinate-flow diagram +
   edge-detection model/semantics), `docs/hit-testing.md` (design +
   implementation summary — the original design held up unchanged),
   `docs/buttons.md` (ButtonInstance architecture, real Hobo
   `DefineButton2` diagnostic findings, explicit "not implemented yet"
   list), `docs/events.md` (design doc for the next sub-fix, not yet
   implemented), `docs/onclipevent-compatibility.md` (all 19 flags
   individually tabulated). **Next sub-fix, not yet started:** a generic
   event dispatcher (design: `docs/events.md`) wiring `onRelease`/
   `onPress`/`onRollOver`/`onRollOut`/`onClipEvent(press)` to the
   `hitTestPoint()`/`ButtonInstance::updateState()`/edge-detection
   primitives that now all exist. **Do not claim button clicking/Hobo
   interaction works yet — it does not**, per `docs/buttons.md`'s explicit
   stop condition. After that: `onClipEvent`'s remaining 15 mouse/key
   flags + button `on()`/`condActionsV2` dispatch.
3. `GlobalObject` has zero named built-ins — no `Math`/`Date`/`Number()`/
   `String()`/`Boolean()` at all. **ON HOLD** per user instruction until
   priority #2 is stable.
4. `DefineMorphShape`/`2` not resolved (confirmed 19x in real `hobo.swf`).
   **ON HOLD.**
5. Audio codec decode. **ON HOLD.**

## Build

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

For the Nintendo 3DS cross-compile, see `docs/3ds-toolchain.md` for the
toolchain bootstrap; once built:

```sh
cmake -S . -B build_3ds \
    -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-3DS.cmake \
    -DFLASH3DS_3DS_TOOLCHAIN_ROOT=/path/to/3ds-toolchain
cmake --build build_3ds -j
# -> build_3ds/flash3ds_3ds.3dsx
```
