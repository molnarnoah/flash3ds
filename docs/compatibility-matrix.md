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
| DefineBits (6) | **no** — no parser file exists | — | NOT IMPLEMENTED |
| DefineBitsJPEG2 (21) | **no** | — | NOT IMPLEMENTED |
| DefineBitsJPEG3 (35) | **no** | — | NOT IMPLEMENTED |
| DefineBitsJPEG4 (90) | **no** | — | NOT IMPLEMENTED |
| DefineBitsLossless (20) | **no** | — | NOT IMPLEMENTED |
| DefineBitsLossless2 (36) | **no** | — | NOT IMPLEMENTED |
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
| DefineMorphShape (46) | **no** — recognized by TagCode only | not resolved into CharacterDictionary; confirmed 19 occurrences in real `hobo.swf` | NOT IMPLEMENTED (confirmed real-content gap) |
| DefineMorphShape2 (84) | **no** | same | NOT IMPLEMENTED (confirmed real-content gap) |
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
| `_width`/`_height` | **NOT IMPLEMENTED** | hardcoded to return `0` in both `handleNativeGet` (line 510) and `GetProperty` opcode path (lines 349-350) |
| `_droptarget`/`_url` | NOT IMPLEMENTED | both hardcoded `""` |
| OOP methods `stop()`/`play()`/`nextFrame()`/`prevFrame()`/`gotoAndStop()`/`gotoAndPlay()`/`getBytesLoaded()`/`getBytesTotal()` | WORKING | `MovieClipInstance.cpp:536-586` (Phase 9 addition) |
| `swapDepths()`/`hitTest()`/`duplicateMovieClip()`/`attachMovie()`/`loadMovie()`/`getURL()` (as method) | NOT IMPLEMENTED | not present in `handleNativeGet()` at all |
| `onClipEvent(Load)`/`(Unload)`/`(EnterFrame)` | WORKING | `runClipEvent()`, 3 call sites, `MovieClipInstance.cpp:626,678,736,929` |
| `onClipEvent(MouseMove/MouseDown/MouseUp/KeyDown/KeyUp/Data/Initialize/Press/Release/ReleaseOutside/RollOver/RollOut/DragOver/DragOut/KeyPress/Construct)` | **NOT IMPLEMENTED** | all 16 remaining flags are parsed into `clipActions_` but never passed to `runClipEvent()` anywhere — confirmed by exhaustive call-site grep |
| Button `on()` handler dispatch (`actionsV1`/`condActionsV2`) | **NOT IMPLEMENTED** | confirmed by codebase-wide grep — these fields are read only by the `swf/` parser and `SceneRenderer`'s render-only `ButtonDef` consumer; nothing ever runs their bytecode |
| Nested MovieClips | WORKING | recursive `Timeline::build`, independent playheads (Phase 5) |
| Frame scripts (`DoAction` per frame) | WORKING | run every intermediate frame during replay, confirmed against real `hobo.swf` (Phase 9) |
| Timeline/AVM1 execution order vs real Flash | UNKNOWN | not independently cross-checked against a real Flash Player's frame/script/render ordering this phase |

**Direct consequence for Hobo-style content:** clicking a "PLAY!" button
(or any button) does **nothing** right now — no `Press`/`Release`
`onClipEvent`, no button `on(release)` handler, and hit-testing itself is
impossible without `_width`/`_height`. This is very likely why Phase 9's
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
| `_xmouse`/`_ymouse` | WORKING | via `GetProperty`(20/21) and bare member access |
| 3DS touch -> `InputState` mouse position/down | WORKING, but flagged | `Nintendo3DSInput.cpp:19-38` — gated on `KEY_TOUCH`, which libctru's own header documents as "not actually provided by HID"; **not independently confirmed against real hardware or Azahar this phase** |
| 3DS D-Pad/Circle Pad -> `Key.LEFT/RIGHT/UP/DOWN` | WORKING (compiles/links; not hardware-confirmed for AS2-side effect) | `Nintendo3DSInput.cpp:41-44` |
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
| Gradient fills (linear/radial/focal) | **BROKEN/simplified** | rendered as a flat *average* of all gradient stop colors — no gradient shape, matrix, or interpolation at all (`ShapeTessellator.cpp:40-58`) |
| Bitmap fills | **NOT IMPLEMENTED** | flat gray (160,160,160,255) placeholder — no bitmap character even resolves (see §2), so this path is effectively unreachable for now anyway |
| Shape tessellation topology correctness | **BROKEN for some real content** | "one closed polygon per MoveTo run, no edge-boundary merging" — shapes with holes (e.g. letter "O") or one fill region authored across multiple `StyleChangeRecord` runs render wrong (overlapping opaque polygons instead of a merged/subtracted region); confirmed by reading the actual tessellation algorithm, not assumed |
| Stroke rendering | WORKING (crude) | naive Bresenham + square-stamp thickness, no joins/caps/anti-aliasing |
| Text rendering (`DefineText`/`2`) | WORKING | both DefineFont v1 and v2 glyphs render (glyph-index lookup doesn't need a code table) |
| Dynamic text rendering (`DefineEditText`) | PARTIALLY WORKING | only when embedding a DefineFont2-with-code-table font; no word-wrap/scrolling/alignment/variable-binding |
| Button rendering | PARTIALLY WORKING | Up state only, no hit-testing/state machine (see §5) |
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
