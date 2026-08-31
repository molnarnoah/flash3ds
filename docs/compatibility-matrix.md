# Compatibility Matrix

**Compatibility / Limitation Discovery phase — 2026-08-18.** This is the
required first deliverable of that phase (see `CLAUDE.md`/session history):
a subsystem-by-subsystem status matrix built by **tracing actual execution
paths in the current source**, not by trusting prior phase docs (several of
which turned out to be stale or, in one case, actively wrong — see
`docs/externalinterface.md`'s correction and the "Corrections to prior
docs" section below). Every row cites the file/function that was actually
read to determine status.

Status legend: **WORKING** (real, tested behavior) · **PARTIALLY WORKING**
(some real paths, real gaps) · **NOT IMPLEMENTED** (recognized/parsed at
most, no behavior) · **BROKEN** (exists but produces wrong output) ·
**UNKNOWN** (not traced this phase) · **NOT TESTED** (implemented per code,
never run against real content).

## 1. SWF container / loading

| Feature | Status | Evidence |
|---|---|---|
| `FWS` (uncompressed) | WORKING | `SwfLoader.cpp:99,135` |
| `CWS` (zlib) | WORKING | real `inflate()` loop, `SwfLoader.cpp:24-72,126-133` |
| `ZWS` (LZMA) | NOT IMPLEMENTED | signature recognized, then hard-rejected: `SwfLoader.cpp:115-118` |
| `FrameSize`/`FrameRate`/`FrameCount` header fields | WORKING | `SwfLoader.cpp:140-142` |
| Declared file length cross-checked against actual size | NOT IMPLEMENTED | read and stored (`SwfLoader.cpp:93-95,111`) but never validated against real buffer size anywhere in the codebase |
| Truncated/malformed tag stream handling | WORKING | stops scan, logs, returns partial result — `SwfLoader.cpp:144-147` and `SwfReader`'s bounds-checked primitives |

## 2. Tag parsing / dispatch — every tag the task spec lists

**Ground truth note:** there is no central "TagDispatcher" that routes tag
bodies to parsers (despite the class name suggesting one) — `TagDispatcher`
only reads generic tag headers and classifies ActionScript-presence.
Dispatch to actual body parsers happens via three independent ad-hoc
`if`/`else if` chains: `CharacterDictionary::scanTagsForCharacters()`
(character-defining tags), `Timeline::applyFrame()`
(PlaceObject/RemoveObject family), and `Timeline`/`MovieClipInstance`'s
DoAction/DoInitAction/StartSound extraction. See
`docs/architecture.md` for whether this three-chain design should be
consolidated — out of scope for this audit, noted as an architectural
observation only.

| Tag | Body parsed? | Resolved into CharacterDictionary / used? | Status |
|---|---|---|---|
| ShowFrame (1) | N/A (no body) | frame-boundary marker | WORKING |
| PlaceObject (4) | yes | `DisplayList::applyPlaceObject` | WORKING |
| PlaceObject2 (26) | yes, incl. `ClipActionRecord`s | WORKING (add/replace/update-in-place all real, re-applied every frame replay — see §9) | WORKING |
| PlaceObject3 (70) | **no parser exists** | — | NOT IMPLEMENTED |
| RemoveObject (5) | yes | `DisplayList::remove` | WORKING |
| RemoveObject2 (28) | yes | `DisplayList::remove` | WORKING |
| DefineShape (2) | yes | yes | WORKING |
| DefineShape2 (22) | yes | yes | WORKING |
| DefineShape3 (32) | yes (RGBA) | yes | WORKING |
| DefineShape4 (83) | **no** — explicit `nullopt` early-out (`DefineShapeTag.cpp:13`) | — | NOT IMPLEMENTED |
| DefineSprite (39) | yes (header + nested tag scan) | yes, recursively (nested `Timeline::build`) | WORKING |
| DefineBits (6) | **no** — needs external JPEGTables(8), zero corpus evidence | — | NOT IMPLEMENTED |
| DefineBitsJPEG2 (21) | yes — full JPEG decode via vendored `jpgd` (`src/swf/DefineBitsTag.h/.cpp`) | yes, rendered (bitmap fill sampling — see §8) | WORKING (Priority Fix List item #2, 2026-08-31) |
| DefineBitsJPEG3 (35) | yes — JPEG decode + optional separate alpha channel | yes, rendered | WORKING (Priority Fix List item #2, 2026-08-31) |
| DefineBitsJPEG4 (90) | **no** — zero corpus evidence | — | NOT IMPLEMENTED |
| DefineBitsLossless (20) | yes — zlib-inflated, BitmapFormat 3/4/5 | yes, rendered | WORKING (Priority Fix List item #2, 2026-08-31) |
| DefineBitsLossless2 (36) | yes — zlib-inflated, BitmapFormat 3/5 (un-premultiplied) | yes, rendered | WORKING (Priority Fix List item #2, 2026-08-31) |
| DefineText (11) | yes, full `TEXTRECORD` | yes | WORKING (rendered — see §10) |
| DefineText2 (33) | yes, RGBA | yes | WORKING |
| DefineEditText (37) | yes, structural | yes | PARTIALLY WORKING — parses fully; renders only when the field embeds a DefineFont2-with-code-table font and has literal text (see §10); no variable binding/input/word-wrap |
| DefineFont (10, v1) | yes (glyph outlines, no code table) | yes | PARTIALLY WORKING — renders for `DefineText`/`2` (glyph-index lookup), NOT for `DefineEditText` (needs code table `DefineFont2` only, see §10) |
| DefineFont2 (48, v2) | yes (glyphs + code table + optional layout) | yes | WORKING |
| DefineFont3 (75) | **no** — explicit reject, 20x em-square mismatch documented (`DefineFontTag.cpp:75-81`) | — | NOT IMPLEMENTED |
| DefineFontInfo/2 (13/62) | **no** | — | NOT IMPLEMENTED |
| DefineButton (7, v1) | yes (records + `actionsV1` bytecode) | yes | PARTIALLY WORKING — renders Up state only; `actionsV1` never dispatched (see §8) |
| DefineButton2 (34, v2) | yes (records w/ per-record CXFORM + `condActionsV2`) | yes | PARTIALLY WORKING — same as v1; `HasFilterList` aborts parsing the rest of that button's records |
| DefineButtonCxform (23) | **no** | — | NOT IMPLEMENTED |
| DefineButtonSound (17) | **no** | — | NOT IMPLEMENTED |
| DefineSound (14) | yes, header fields only | yes | PARTIALLY WORKING — no codec decode, see §11 |
| StartSound (15) | yes, full `SOUNDINFO` | dispatched per-frame to `IAudioBackend` | PARTIALLY WORKING — dispatch is real, backend can't actually play audio yet (§11) |
| SoundStreamHead/2 (18/45) | **no** | — | NOT IMPLEMENTED |
| SoundStreamBlock (19) | **no** | — | NOT IMPLEMENTED |
| DoAction (12) | raw bytes extracted, decoded by AVM1 interpreter | executed | WORKING |
| DoInitAction (59) | raw bytes extracted (spriteId + body) | executed once per character | WORKING |
| FrameLabel (43) | yes (name only) | used by `gotoAndStop("label")` | WORKING |
| SetBackgroundColor (9) | **no parser; renderer always clears to white** | — | NOT IMPLEMENTED |
| DefineMorphShape (46) | **yes** (`src/swf/DefineMorphShapeTag.h/.cpp`) — v1 only | resolved into CharacterDictionary and rendered (SceneRenderer), START-side geometry/colors only (ratio=0) — real-corpus evidence (`morph_ratio_scan.cpp`) confirms every placement across all 7 Hobo files uses ratio=0, so this is exactly correct for this corpus, not an approximation | WORKING (Roadmap Phase 9, 2026-08-25; start-shape-only by design, see `docs/known-limitations.md` L7) |
| DefineMorphShape2 (84) | **no** | zero occurrences found in real corpus (tag histogram, all 8 games) — deliberately out of scope, see `docs/known-limitations.md` L7 | NOT IMPLEMENTED (confirmed zero real-content evidence) |
| ExportAssets (56) | **no** | blocks `Sound.attachSound(name:String)` linkage-name resolution | NOT IMPLEMENTED |
| DoABC/DoABC2 (72/82) | not parsed (AVM2/AS3 — explicitly out of project scope per `CLAUDE.md`) | — | NOT IMPLEMENTED (by design) |

## 3. AVM1 interpreter — internals

See `docs/avm1-compatibility.md` for the full 100-opcode table. Summary:

| Feature | Status | Evidence |
|---|---|---|
| Opcode dispatch coverage | WORKING | all 100 `ActionCode` values have an explicit `case`; nothing silently falls to `default` |
| Stack / registers / scope bounds-safety against malformed bytecode | WORKING | every risky path (stack, registers, constant pool, jump targets, prototype-chain walks, recursion depth, instruction count) is explicitly bounds-checked or capped — see `docs/avm1-compatibility.md` §"Safety" |
| Arithmetic/comparison/bitwise/string ops | WORKING | real, tested |
| `GetVariable`/`SetVariable`/`DefineLocal`/`DefineLocal2`/`Delete`/`Delete2` | WORKING | |
| Objects/Arrays (`InitObject`/`InitArray`/`GetMember`/`SetMember`/`Enumerate`/`2`/`NewObject`) | WORKING | `NewObject` special-cases `"Object"`/`"Array"` directly in the interpreter (`GlobalObject::create()` itself installs **zero** named built-ins — see §3a) |
| `DefineFunction`/`DefineFunction2`/`CallFunction`/`CallMethod`/`NewMethod`/`Return` | WORKING | closures, recursion (capped, `kMaxCallDepth=256`), register/named param binding all real |
| `Extends`/`InstanceOf`/`CastOp`/`ImplementsOp` | PARTIALLY WORKING | `Extends`'s stack pop order is an unverified assumption (flagged in-code); `ImplementsOp` doesn't enforce interfaces |
| `Try`/`Catch`/`Finally` | NOT IMPLEMENTED | parsed and entirely skipped — no exception mechanism at all (`Interpreter.cpp:1188-1195`) |
| `Throw` | NOT IMPLEMENTED | pops value, logs, continues — no propagation |
| `MovieClip`/timeline actions (`GotoFrame`/`Play`/`Stop`/`GetProperty`/`SetProperty`/`CloneSprite`/`RemoveSprite`/`SetTarget`/`2`) | WORKING | real via `MovieClipHostBindings` |
| `StartDrag`/`EndDrag` | WORKING | real drag tracking incl. constraint rect (verified in ground-truth trace, not just docs) |

### 3a. Global built-ins (`GlobalObject`)

**Corrected finding:** `GlobalObject::create()` (`src/avm1/GlobalObject.cpp`)
constructs and returns a single bare, empty `Object` — **no** `Math`,
`Date`, `Number`, `String`, `Boolean`, or `Array`/`Object` constructor
function is registered there. This is by design for `NewObject` (which
special-cases `"Object"`/`"Array"` literal names directly in the
interpreter), but it means **`Math.*`, `Date`, `Number()`, `String()`,
`Boolean()` as callable global constructors/namespaces do not exist at
all** — any content calling `Math.random()`, `Math.floor()`,
`new Date()`, etc. will fail with "not a function" or "undefined is not an
object". This was not previously documented as a gap anywhere. **Flagged as
priority-2 candidate** — see `docs/known-limitations.md`.

`ScriptEnvironment` (`src/runtime/MovieClipInstance.cpp`) separately
populates `_global` with `Key`, `Mouse`, `Sound`, `ExternalInterface` (see
§7/§9/§11) — these exist independently of `GlobalObject`.

## 4. `_root` / `_global` / `_parent`

| Feature | Status | Evidence |
|---|---|---|
| `_root`, `_parent`, named-child access | WORKING | `MovieClipInstance::handleNativeGet()`, `MovieClipInstance.cpp:513-522,588-596` |
| `_global` | WORKING | resolves to `ScriptEnvironment`'s shared object |
| Dynamic property names (`obj["prop" + x]`, per Hobo's `_root["color" + UnlockBonusIndex]` pattern) | **NOT TESTED** | `GetMember`/`SetMember` bytecode take a computed string key off the stack generically (`Interpreter.cpp:878-899`) — no code-level reason this wouldn't work, but never exercised against this exact pattern; add a targeted test (see `docs/test-results.md`) |

## 5. MovieClip API

| Feature | Status | Evidence |
|---|---|---|
| `_x`/`_y`/`_xscale`/`_yscale`/`_rotation`/`_alpha`/`_visible`/`_currentframe`/`_totalframes`/`_name`/`_target`/`_framesloaded` | WORKING | `MovieClipInstance.cpp:495-522` |
| `_width`/`_height` | **WORKING (fixed in interactivity phase, 2026-08-18)** | recursive bounding-box union (`MovieClipInstance::computeBoundsInOwnSpace()`/`width()`/`height()`, `swf::transformRect()`) — see `docs/known-limitations.md`'s STEP 1-10 writeup. Previously hardcoded to `0` |
| `_droptarget`/`_url` | NOT IMPLEMENTED | both hardcoded `""` |
| OOP methods `stop()`/`play()`/`nextFrame()`/`prevFrame()`/`gotoAndStop()`/`gotoAndPlay()`/`getBytesLoaded()`/`getBytesTotal()` | WORKING | `MovieClipInstance.cpp:536-586` (Phase 9 addition) |
| `swapDepths()`/`hitTest()`/`duplicateMovieClip()`/`attachMovie()`/`loadMovie()`/`getURL()` (as method) | NOT IMPLEMENTED | not present in `handleNativeGet()` at all |
| `onClipEvent(Load)`/`(Unload)`/`(EnterFrame)` | WORKING | `runClipEvent()`, 3 call sites, `MovieClipInstance.cpp:626,678,736,929` |
| `onClipEvent(MouseMove/MouseDown/MouseUp/KeyDown/KeyUp/Data/Initialize/Press/Release/ReleaseOutside/RollOver/RollOut/DragOver/DragOut/KeyPress/Construct)` | **NOT IMPLEMENTED** | all 16 remaining flags are parsed into `clipActions_` but never passed to `runClipEvent()` anywhere — confirmed by exhaustive call-site grep |
| Button `on()` handler dispatch (`actionsV1`/`condActionsV2`) | **NOT IMPLEMENTED** | confirmed by codebase-wide grep — these fields are read only by the `swf/` parser and `SceneRenderer`'s render-only `ButtonDef` consumer; nothing ever runs their bytecode |
| Hit testing (any point-in-object test) | **WORKING — interactivity phase (2026-08-19, sub-fix 4/N)** | see row below and §6; design recorded in `docs/hit-testing.md` |
| Generic input-event dispatch (mouse press/release/rollOver/rollOut -> AVM1) | **NOT IMPLEMENTED** | design recorded in `docs/events.md`; no `InputEvent`/dispatcher type exists anywhere |
| Per-placement Button "instance" (transform, depth, visibility, hit area, UP/OVER/DOWN state) | **NEW, WORKING — ButtonInstance phase (2026-08-19)** | `ButtonInstance`/`MovieClipInstance::buttonInstances_` — a real runtime instance is now created for every placed `DefineButton`/`DefineButton2`, with its own transform, independent state, and hit-testing via the existing `hitTestPoint()` machinery; 16 regression tests (`tests/test_button_instance.cpp`); see `docs/buttons.md`. **AS2-settable `onRelease`/etc. are still NOT implemented** — `ButtonInstance::scriptObject_` is a bare identity object only, no properties/methods wired, and no event dispatch of any kind fires yet (see `docs/buttons.md`'s "Not implemented yet") |
| Nested MovieClips | WORKING | recursive `Timeline::build`, independent playheads (Phase 5) |
| Frame scripts (`DoAction` per frame) | WORKING | run every intermediate frame during replay, confirmed against real `hobo.swf` (Phase 9) |
| Timeline/AVM1 execution order vs real Flash | UNKNOWN | not independently cross-checked against a real Flash Player's frame/script/render ordering this phase |

**Direct consequence for Hobo-style content:** clicking a "PLAY!" button
(or any button) still does **nothing** right now — hit-testing and a real
per-placement `ButtonInstance` (with UP/OVER/DOWN state) both now exist
and are proven against real `hobo.swf` `DefineButton2` content (see
`docs/buttons.md`'s "Real Hobo `DefineButton2` findings"), but no
`Press`/`Release`/`onClipEvent`/button `on(release)` handler is dispatched
by anything yet — that's the next phase. This is very likely why Phase 9's
manual walk of `hobo.swf` never got past the title screen. See
`docs/known-limitations.md`'s priority list.

## 6. Input (`Key`, `Mouse`, touch)

| Feature | Status | Evidence |
|---|---|---|
| `Key.isDown(code)` | WORKING | `MovieClipInstance.cpp:40-46`, backed by `InputState` |
| `Key.getCode()` | WORKING | `MovieClipInstance.cpp:47-52` |
| `Key.getAscii()` | **NOT IMPLEMENTED** | confirmed absent by grep — only a comment mentions it exists conceptually |
| Named `Key.*` constants | WORKING | 18 constants wired, `MovieClipInstance.cpp:53-70` |
| `Mouse.show()`/`hide()` | WORKING (as a no-op) | no cursor rendering model exists — deliberate |
| `_xmouse`/`_ymouse` | **WORKING — fixed, interactivity phase (2026-08-18)** | via `GetProperty`(20/21) and bare member access; now converts `InputState`'s raw input-viewport-pixel coordinates into the loaded movie's own stage-pixel space (`MovieClipInstance::stageMouseX()`/`stageMouseY()`), correct even when a movie's stage size doesn't match the render target's pixel size (verified against `hobo.swf`'s real 600x450 stage); see `docs/input.md` and `docs/known-limitations.md`'s "Sub-fix 2/N" |
| 3DS touch -> `InputState` mouse position/down | WORKING, but flagged | `Nintendo3DSInput.cpp:19-38` — gated on `KEY_TOUCH`, which libctru's own header documents as "not actually provided by HID"; **not independently confirmed against real hardware or Azahar this phase** |
| 3DS D-Pad/Circle Pad -> `Key.LEFT/RIGHT/UP/DOWN` | WORKING (compiles/links; not hardware-confirmed for AS2-side effect) | `Nintendo3DSInput.cpp:41-44` |
| Edge-detected input state (`InputState::commitFrame()`/`isKeyPressed()`/`isKeyReleased()`/`isMousePressed()`/`isMouseReleased()`/`isTouchPressed()`/`isTouchReleased()`) | **NEW, WORKING — input-transitions phase (2026-08-19)** | not yet consumed by hit-testing/button dispatch (that's the NEXT phase — button-instance object); 18 desktop unit tests (`tests/test_input_state.cpp`), 3DS build clean, not hardware/Azahar-confirmed; see `docs/input.md`'s "Input-transitions phase" section |
| 3DS L/R shoulder buttons -> `InputState` (`'L'`/`'R'` ASCII codes) | WORKING (compiles/links; not hardware-confirmed) | `Nintendo3DSInput.cpp` — new this phase, previously unmapped |
| `MovieClip.hitTest(x, y)` (2-argument, bounding-box form) | **NEW, WORKING — interactivity phase (2026-08-19, sub-fix 4/N)** | `MovieClipInstance::hitTestBounds()`, dispatched via `handleNativeGet()`'s OOP-callable-method pattern; tests this clip's own full aggregate bounding box, deliberately IGNORES `_visible` (matches real Flash's actual, sometimes-surprising behavior); 5 dedicated regression tests including the visibility-ignoring case; `hitTest(target)` (1-arg) and `hitTest(x,y,shapeFlag=true)` (exact-shape, 3-arg) are explicitly **NOT IMPLEMENTED** (flagged via `LOG_WARN`, not guessed at) |
| Internal `hitTestPoint()` primitive (topmost-object-under-a-point, bounding-box) | **NEW, WORKING — interactivity phase (2026-08-19, sub-fix 4/N)** | `MovieClipInstance::hitTestPoint()`/`hitTestPointInOwnSpace()`; respects `_visible`, recurses into `MovieClip` children's own content (not their aggregate bounds), topmost-depth-wins ordering, handles degenerate/zero-scale matrices correctly; NOT yet AS2-visible or consumed by any event dispatch — a pure query primitive for a future button/mouse-dispatch phase; 12 regression tests (`tests/test_movieclip_instance.cpp`) including 2-level nested-`MovieClip` recursion |
| 3DS A/START -> `Key.ENTER`, B/SELECT -> `Key.ESCAPE`, X/Y -> ASCII | WORKING (same caveat) | `Nintendo3DSInput.cpp:53-58` |

## 7. ExternalInterface

**Corrected finding — see `docs/externalinterface.md`.** A prior doc
(`docs/externalinterface.md` itself) claimed "not started"; ground-truth
tracing this phase confirms it is real and fully wired.

| Feature | Status | Evidence |
|---|---|---|
| `ExternalInterface.available` | WORKING | always `true`, `MovieClipInstance.cpp:190` |
| `ExternalInterface.call(name, ...args)` | WORKING | dispatches to `registerHostFunction()`-registered C++ functions, `MovieClipInstance.cpp:192-199` |
| `ExternalInterface.addCallback(name, instance, fn)` | WORKING | native -> AS2, `MovieClipInstance.cpp:201-213` |
| Hobo's exact pattern (`addCallback("SetUnlockedBonusIndex", this, SetUnlockedBonusIndex)`, `call("OnBonusCancel")`, `call("color", 1)`) | **NOT TESTED** | mechanism is real and unit-tested generically; this EXACT call shape has not been run |
| `invokeCallback()` scene-graph access (`GotoFrame`/`Play`/etc. called directly inside a callback body) | NOT IMPLEMENTED | runs with no `HostBindings` bound — documented limitation, unchanged this phase |

## 8. Rendering

| Feature | Status | Evidence |
|---|---|---|
| Matrix transforms (scale/rotate/translate), world-transform composition | WORKING | `concatMatrix`, `SceneRenderer.cpp:89,96` |
| **`ColorTransform`/`_alpha` application to rendered pixels** | **FIXED THIS PHASE (was NOT IMPLEMENTED)** | see `docs/known-limitations.md` priority #1 — `swf::concatColorTransform`/`swf::applyColorTransform`, threaded through every `SceneRenderer` leaf-render call; regression tests `SceneRenderer_MovieClipInstanceAlpha_*` in `tests/test_scene_renderer.cpp` |
| Visibility (`_visible`) | WORKING | `renderClip` checks `clip.visible()`, `SceneRenderer.cpp:77` |
| Clipping (`HasClipDepth` / clip layers / masks) | **NOT IMPLEMENTED** | `clipDepth` is parsed and stored but read nowhere else in the codebase — confirmed by grep |
| Solid fills | WORKING | |
| Gradient fills — linear | **FIXED 2026-08-28 (was BROKEN/simplified)** | real per-pixel 256-stop gradient, gradientMatrix + world-transform-aware (see `docs/renderer.md`'s "Gradient rendering" section); real-corpus scope evidence in `docs/known-limitations.md`/`ShapeTessellator.h`'s header comment |
| Gradient fills — radial/focal-radial | **BROKEN/simplified (deliberately, by evidence)** | still rendered as a flat *average* of all gradient stop colors (`ShapeTessellator.cpp`'s `toFlatColor()`) — real hobo.swf corpus scan found zero radial/focal-radial fills, so real rendering isn't implemented against no evidence (same discipline as this project's other evidence-scoped decisions) |
| Bitmap fills | **FIXED 2026-08-31 (was NOT IMPLEMENTED)** | real per-pixel nearest-neighbor sampling of decoded bitmap character data (see §2 for the 4 supported `DefineBits*` tags), `ColorTransform`-applied per sampled pixel; real-corpus verification also found and fixed a separate JPEG-decode bug affecting most of the corpus's `DefineBitsJpeg2`/`3` tags (see `docs/renderer.md`'s "Bitmap rendering" section); `smoothed`/bilinear filtering still not implemented (nearest-neighbor only) |
| Shape tessellation topology correctness | **BROKEN for some real content** | "one closed polygon per MoveTo run, no edge-boundary merging" — shapes with holes (e.g. letter "O") or one fill region authored across multiple `StyleChangeRecord` runs render wrong (overlapping opaque polygons instead of a merged/subtracted region); confirmed by reading the actual tessellation algorithm, not assumed |
| Stroke rendering | WORKING (crude) | naive Bresenham + square-stamp thickness, no joins/caps/anti-aliasing |
| Text rendering (`DefineText`/`2`) | WORKING | both DefineFont v1 and v2 glyphs render (glyph-index lookup doesn't need a code table) |
| Dynamic text rendering (`DefineEditText`) | PARTIALLY WORKING | only when embedding a DefineFont2-with-code-table font; no word-wrap/scrolling/alignment/variable-binding |
| Button rendering | PARTIALLY WORKING | `SceneRenderer` still renders only the Up-state records, unchanged and deliberately so (ButtonInstance phase, 2026-08-19 — see `docs/buttons.md`'s "Rendering — deliberately unchanged"); hit-testing and an UP/OVER/DOWN state machine now exist at the runtime-instance level (see §5) but nothing yet feeds that state back into what gets rendered |
| `SetBackgroundColor` | NOT IMPLEMENTED | renderer hardcodes white always |
| Nintendo 3DS framebuffer blit | WORKING (compiles/links; visually unconfirmed pixel-exact) | real `gfxGetFramebuffer` write, rotated/column-major indexing — confirmed booting in Azahar per user report, not yet pixel-verified |

## 9. Timeline / Display list

| Feature | Status | Evidence |
|---|---|---|
| Depth-indexed display list (add/replace/update-in-place/remove) | WORKING | `DisplayList.cpp:12-29` |
| PlaceObject2 "update in place" (Move=1, no HasCharacter) applied on every later frame, not just at creation | **CORRECTED finding — WORKING, contrary to what a stale confidence note might suggest** | `Timeline::applyFrame()` fully replays frames 1..N from scratch on every `gotoAndStop`/`gotoAndPlay`/`advanceOneFrame` call, so an update-in-place tag on frame N is genuinely re-applied every time the playhead reaches or passes N — confirmed by reading `Timeline.cpp:72-91` directly |
| Nested `DefineSprite` timelines, independent playheads | WORKING | recursive `Timeline::build`, confirmed at 2 call sites in `MovieClipInstance.cpp` |
| Frame labels (`gotoAndStop("label")`) | WORKING | |

## 10. Audio

| Feature | Status | Evidence |
|---|---|---|
| `DefineSound`/`StartSound` tag parsing + per-frame dispatch to `IAudioBackend` | WORKING | header/`SOUNDINFO` fields fully parsed |
| Audio codec decode (any format — uncompressed/ADPCM/MP3) | **NOT IMPLEMENTED** | zero codec decode exists anywhere in the codebase; confirmed no jpeg/decode-adjacent audio library linked either |
| `Nintendo3DSAudioBackend::playSound()` actually queuing audible PCM | **NOT IMPLEMENTED** | confirmed by reading the exact function body — it reserves an ndsp channel and calls `ndspChnSetPaused(false)`, but never calls `ndspChnWaveBufAdd()`; there is no decoded buffer to queue |
| `Sound.attachSound(numeric id)` | WORKING (resolution only — see above for playback) | |
| `Sound.attachSound(String linkage name)` | NOT IMPLEMENTED | needs `ExportAssets`, not parsed |
| `Sound.start()`/`.stop()` | WORKING (dispatch only) | calls through to `IAudioBackend`, which can't play anything yet |
| `Sound.setVolume()`/`.getVolume()` | PARTIALLY WORKING | round-trips a stored `_volume` property; has **zero effect on any backend** |
| `StopSounds` legacy action | NOT IMPLEMENTED | recognized/logged only, not wired to `stopAllSounds()` |
| `Nintendo3DSAudioBackend::playTestTone()` diagnostic tone | WORKING, confirmed audible | not part of the SWF pipeline — proves the ndsp plumbing itself works, independent of the codec-decode gap above |

## 11. 3DS platform specifics

See `docs/3ds-limitations.md` for the full writeup. Summary: boots and runs
in Azahar (user-confirmed); real hardware never tested this session; top/
bottom screen dual-render pipeline is real and exercised (Phase 10 test
app); pixel-exact framebuffer orientation/format is "implemented per public
libctru documentation" but not independently pixel-verified.

## Corrections to prior docs found this phase

1. **`docs/externalinterface.md` was stale and wrong** ("not started") —
   fixed in place; see that file.
2. **`docs/compatibility.md`'s Phase 9 claim that hobo.swf's "PLAY!" button
   fades in by frame 5" does not appear to be caused by any ColorTransform/
   `_alpha` mechanism.** This phase's fix makes `ColorTransform`/`_alpha`
   genuinely affect rendering for the first time — before this fix, it had
   **zero** effect on any rendered pixel. A before/after byte-for-byte
   comparison of `hobo.swf` frames 1-5, rendered via the desktop CLI, is
   **100% identical** with and without the fix applied (see
   `docs/known-limitations.md`'s priority #1 writeup for the exact
   reproduction). There IS a real visual change between frame 1 and frame 5
   in the button region (device px bbox roughly (167,308)-(301,355), 4189
   changed pixels) — but it is not attributable to color-transform
   application. Most likely explanation (not confirmed further this phase):
   a different/additional shape is placed on a later frame rather than an
   authored alpha tween. **Not fully resolved** — flagged for a future
   frame-by-frame investigation, not asserted as fact either way.
3. **`GlobalObject::create()` installs zero named built-ins** (`Math`,
   `Date`, `Number`, `String`, `Boolean` as global objects/constructors) —
   not previously called out as a gap in any doc. See §3a above.

## 12. Real-content cross-game matrix (Hobo 1–7, Extreme Pamplona)

**Real-game-corpus phase (2026-08-18).** Everything in this document
above was traced against source only, mostly with `hobo.swf` as the one
real-content reference point. This phase adds a second, wider axis: a
tag/opcode/API/button/sound/rendering feature matrix across all 8 real
games in the compatibility corpus (`tests/games/`), so future
prioritization decisions ("if we implement X, which games does it help?")
are evidence-based across the corpus, not extrapolated from hobo.swf
alone. Full per-game breakdown, the cross-game YES/NO/UNKNOWN matrix, and
the Hobo-family common/unique-feature comparison all live in
`docs/real-game-compatibility.md` — not duplicated here to avoid drift
between two copies of the same table. Headline finding relevant to this
document's existing §9 (input/interactivity) and `docs/known-
limitations.md` priority #2: button event dispatch, the current top
blocker, has **two independent mechanisms** to cover across the corpus —
native `DefineButton2` `condActionsV2` (all 7 Hobo games, 0 use of
AS2-source-level handler properties) and `object.onPress`/`onRelease`
property-handler assignment (Extreme Pamplona only, 0 Hobo files use it)
— see `docs/real-game-compatibility.md`'s "If we implement feature X..."
section for the full per-feature breakdown.
