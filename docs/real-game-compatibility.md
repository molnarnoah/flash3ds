# Real-Game Compatibility Report — Hobo 1–7 + Extreme Pamplona

**Real-game-corpus phase (2026-08-18).** This is the required deliverable
of the "EXPAND THE REAL-GAME TEST CORPUS" phase: analysis-only, no new
Flash runtime feature was implemented while producing it (per that phase's
explicit prohibition — see `CLAUDE.md`/session history). Every number
below comes directly from `swf_diagnostic`'s output for the exact file
recorded in that game's `tests/games/<name>/manifest.md`
(checksum-pinned) — nothing here is guessed or interpolated. Raw output:
`tests/games/<name>/diagnostic.txt`.

**Files found vs. missing:** all 8 requested games were located and
analyzed. Hobo 1 = the pre-existing `hobo.swf` baseline (no separate file
exists — confirmed by exact byte-size + MD5 match against the user's
device copy). Hobo 2–7 and the full 24-file Extreme Pamplona package
(1 main loader + 23 `loadMovie`-style content sub-SWFs) were staged this
phase from the user's connected device (`G:\3DS\...`) and are now pinned
by MD5 in `tests/games/*/manifest.md`. Nothing was invented or downloaded
from the internet; nothing was modified, recompressed, or re-exported.

## How to read "runtime compatibility status" below

Per the phase's explicit instruction, **parser-level tag recognition is
tracked separately from actual runtime behavior.** Example worked in the
spec itself: `DefineButton2` **parsing** = implemented; `ButtonInstance`
(the runtime object) = implemented; hit-testing = implemented; **button
event dispatch** (the part that actually calls a handler when the user
presses a button) = **missing**. All four of those are independent facts
and are reported as such, not collapsed into one "buttons: yes/no" line.

---

## Hobo 1 (`hobo.swf` — pre-existing baseline)

| Field | Value |
|---|---|
| File | `/home/claude/hobo-testing/hobo.swf` (4,967,978 bytes) |
| SWF version | 6 |
| Compression | zlib (CWS), declared 7,729,012 → decompressed 7,729,004 bytes |
| Stage | 600×450 px |
| FPS | 25.00 |
| Frame count (declared) | 13 |
| AVM1/AVM2 | AVM1 only (2,884 `DoAction`, 0 `DoInitAction`, 0 `DoABC`) |

**Tags** (57,474 total tag occurrences, recursive incl. nested
`DefineSprite` streams): dominated by `PlaceObject2` (35,172),
`ShowFrame` (11,706), `DoAction` (2,884), `RemoveObject2` (2,690),
`DefineShape`/`2`/`3` (1,758/475/757), 529 `DefineSprite` + matching `End`,
368 `StartSound`, 35 `DefineSound`, 19 `DefineMorphShape`, 16
`DefineButton2`, 3 `DefineText`, 2 `DefineEditText`, 1 `DefineFont2`, 1
`FrameLabel`, 1 `SetBackgroundColor`. **No `DefineButton` (v1), no
`DefineShape4`, no `PlaceObject3`, no `DoInitAction`, no `DoABC`.**

**AVM1 opcode profile** (2,897 bytecode buffers, 16,934 opcodes, 0 nested
function bodies, 0 `With` blocks — this file never defines a user
function or uses `with{}`): top opcodes `Push` 6,192, `GetVariable` 2,420,
`Stop` 2,289, `GetMember` 1,869, `SetMember` 948, `CallMethod` 585,
`Pop` 571, plus arithmetic/comparison/control-flow (`Subtract`, `Not`,
`If`, `Equals2`, `ConstantPool`, `Jump`, `Add2`, `And`, `Less2`,
`Greater`). **Every opcode this file uses has a real `case` in
`Interpreter.cpp` — 0 unsupported opcodes.**

**AS2 API scan (best-effort substring):** FOUND — `Key`, `Sound`,
`_root`, `gotoAndPlay`, `gotoAndStop`, `play`, `stop`,
`removeMovieClip`, `_x`, `_y`, `_xscale`, `_yscale`, `_visible`.
NOT FOUND — `Mouse`, `MovieClip`, `_parent`, `_global`,
`createEmptyMovieClip`, `duplicateMovieClip`, `attachMovie`,
`loadMovie`, `_rotation`, `_alpha`, `_width`, `_height`, `onClipEvent`,
`onPress`, `onRelease`, `onRollOver`, `onRollOut`, `onMouseDown`,
`onMouseUp`, `onMouseMove`, `ExternalInterface`.

**Buttons:** 0 `DefineButton` (v1), 16 `DefineButton2`, 113 total button
state records, all 16 with explicit `HitTest` state geometry (0 falling
back to the Up-state hit area). 3 button instances placed on frame 1, all
**unnamed** (`(unnamed)`): depth 25/char 12 at (372.75px, 327.35px),
depth 44/char 85 at (267.50px, 370.55px), and depth 1/char 88 nested
under `/mutebutton` at (581.45px, 430.90px) — the mute toggle. Unnamed
does not mean unreachable: AVM1 can still target these via
`_root`/timeline path expressions or by depth; this file's own AS2 scan
shows no `duplicateMovieClip`/`attachMovie` dynamic-creation calls, so
these 3 are very likely the complete, statically-placed frame-1 button
set (title-screen PLAY button, a second button, and the mute button).

**Sound:** 35 `DefineSound` (all MP3), 368 `StartSound`. No codec decode
exists in this runtime (`docs/known-limitations.md` priority #5) — sound
data is fully parsed but never audible.

**Rendering:** 2,990 shape tags (all v1–3; **0 `DefineShape4`**), 161
gradient fills, 0 bitmap fills, 528 sprites, 19 morph shapes (**not
resolved by the parser — `DefineMorphShapeTag` doesn't exist in this
codebase**), 0 bitmap tags, 5 text tags, 1 font, **0 `PlaceObject3`**
(no blend-mode/filter usage).

**Interactivity summary:** `Button2_present: yes`. All handler-style
string identifiers (`onPress`/`onRelease`/`onRollOver`/`onRollOut`/
`onClipEvent`) **not found** — this file's button interactivity is
carried entirely by `DefineButton2`'s native, binary `condActionsV2`
action records (bit-flag-keyed press/release/rollOver/etc. action lists
compiled directly into the tag, not AS2 source text), not by
`object.onPress = function(){}`-style property assignment. `Key` string
present (context for `Key.isDown()` — not confirmed used without deeper
disassembly).

**Runtime compatibility status (source-verified, not just parser
support):**

| Feature this file uses | Parser support | Runtime behavior |
|---|---|---|
| `DefineButton2` | Implemented | `ButtonInstance` object implemented; hit-testing implemented (Sub-fix 4/N, 5/N); **`condActionsV2` action-list dispatch on press/release: MISSING** — parsed and stored, never executed (`docs/known-limitations.md` priority #2) |
| `DefineSprite` (nested MovieClips) | Implemented | Implemented (recursive `Timeline`) |
| `DefineMorphShape` | **NOT parsed** (no `DefineMorphShapeTag`) | N/A — 19 characters in this file are simply absent from rendering |
| MP3 sound (`DefineSound`/`StartSound`) | Implemented (header+data) | **Never decoded/played** (priority #5) |
| Alpha/ColorTransform | Implemented | Implemented (fixed this project, priority #1) — confirmed frame 1-5 byte-identical before/after, i.e. this file never triggers a non-identity cxform in its first 5 frames |
| `_x`/`_y`/`_xscale`/`_yscale`/`_visible` | N/A | Implemented |
| `Key.isDown()` | N/A (string only found, opcode-level usage not independently confirmed here) | Not confirmed exercised by this file's first-5-frame path |

**Blockers specific to progressing this file past its title screen:**
button `condActionsV2` dispatch (priority #2) is the single blocker —
everything else in this file (rendering, sprite nesting, shape/text/font
resolution) already works.

---

## Hobo 2 — Prison Brawl

| Field | Value |
|---|---|
| File | `/home/claude/game-corpus/hobo2/hobo2.swf` (5,147,172 bytes) |
| SWF version | 6 · Stage 600×450 · FPS 25.00 · Frames 13 |
| Compression | zlib, declared 9,132,144 → decompressed 9,132,136 bytes |
| AVM1/AVM2 | AVM1 only (2,939 `DoAction`, 0 `DoInitAction`, 0 `DoABC`) |

**Tags** (59,329 total): `PlaceObject2` 35,020, `ShowFrame` 12,733,
`RemoveObject2` 3,032, `DoAction` 2,939, `DefineShape`/`2`/`3`
1,876/611/788, 596 `DefineSprite`/`End`, 442 `StartSound`, 36
`DefineSound`, 17 `DefineButton2`, 16 `DefineMorphShape`, 14
`FrameLabel`, **9 `DefineBitsLossless`** (new vs. Hobo 1 — 0 bitmap tags
there), 4 `DefineText`, 2 `DefineEditText`, 1 `DefineBitsJpeg3`, 1
`DefineFont2`. Same absent set as Hobo 1 (`DefineShape4`, `PlaceObject3`,
`DoInitAction`, `DoABC`, `DefineButton` v1).

**AVM1 opcodes** (2,954 buffers, 19,149 opcodes, 0 nested functions, 0
`With`): same shape as Hobo 1 plus, newly, `Play` (7) and a fourth
`GetURL` call — all opcodes supported.

**AS2 scan:** same FOUND set as Hobo 1, **plus `_parent` now FOUND**
(Hobo 1 did not use `_parent`). Handler-property strings
(`onPress`/etc.) still all not found — same native-button-handler pattern
as Hobo 1.

**Buttons:** 17 `DefineButton2` (up from 16), 122 total records, all 17
with explicit hit-test state. 3 placed on frame 1, same structure as
Hobo 1 (2 unnamed root-level + 1 unnamed under `/mutebutton`).

**Sound:** 36 `DefineSound` (MP3), 442 `StartSound`.

**Rendering:** 3,275 shape tags (0 v4), 123 gradients, **9 bitmap fills**
(new — Hobo 1 had 0), 595 sprites, 16 morph shapes, **10 bitmap tags**
(new), 6 text tags, 1 font, 0 `PlaceObject3`.

**Interactivity:** identical pattern to Hobo 1 — `Button2_present: yes`,
all handler-property strings absent, `Key` present.

**Delta vs. Hobo 1:** adds bitmap image content (`DefineBitsLossless` x9,
`DefineBitsJpeg3` x1) and `_parent` usage; otherwise structurally
identical (same AVM1 opcode vocabulary, same 3-button frame-1 layout,
same native-button-handler interactivity model). **Same blocker**
(`condActionsV2` dispatch) applies.

---

## Hobo 3 — Wanted

| Field | Value |
|---|---|
| File | `/home/claude/game-corpus/hobo3/hobo3.swf` (5,694,921 bytes) |
| SWF version | 6 · Stage 600×450 · FPS 25.00 · Frames 13 |
| Compression | zlib, declared 10,426,613 → decompressed 10,426,605 bytes |
| AVM1/AVM2 | AVM1 only (4,019 `DoAction`, 0 `DoInitAction`, 0 `DoABC`) |

**Tags** (68,728 total): `PlaceObject2` 39,913, `ShowFrame` 14,576,
`DoAction` 4,019, `RemoveObject2` 3,351, `DefineShape`/`2`/`3`
2,785/750/842, 634 `DefineSprite`/`End`, 468 `StartSound`, 37
`DefineSound`, 34 `FrameLabel`, 24 `DefineMorphShape`, 19
`DefineButton2`, 4 `DefineText`, 2 `DefineEditText`, 1 `DefineBitsJpeg3`,
1 `DefineFont2`. **No bitmap-lossless tags this time** (unlike Hobo 2).

**AVM1 opcodes** (4,036 buffers, 23,435 opcodes, 0 nested functions, 0
`With`): same vocabulary as Hobo 2, all supported; `GetURL` now 6.

**AS2 scan:** same as Hobo 2 (`_parent` FOUND, handler-property strings
absent).

**Buttons:** 19 `DefineButton2`, 132 records, all with hit-test state. 3
placed frame-1 buttons, same layout pattern.

**Sound:** 37 `DefineSound` (MP3), 468 `StartSound`.

**Rendering:** 4,377 shapes (0 v4), 114 gradients, 0 bitmap fills, 633
sprites, 24 morph shapes, 1 bitmap tag, 6 text, 1 font, 0 `PlaceObject3`.

**Delta vs. Hobo 2:** larger (more frames' worth of content packed into
the same 13-frame/25fps structure — more `DoAction`/shape/sprite counts
throughout), drops the bitmap-lossless images Hobo 2 introduced, keeps
everything else (button model, opcode vocabulary, 3-button layout)
identical. **Same blocker.**

---

## Hobo 4 — Total War

| Field | Value |
|---|---|
| File | `/home/claude/game-corpus/hobo4/hobo4.swf` (6,330,237 bytes) |
| SWF version | 6 · Stage 600×450 · FPS 25.00 · Frames 13 |
| Compression | zlib, declared 12,021,813 → decompressed 12,021,805 bytes |
| AVM1/AVM2 | AVM1 only (4,511 `DoAction`, 0 `DoInitAction`, 0 `DoABC`) |

**Tags** (75,002 total): `PlaceObject2` 42,609, `ShowFrame` 16,117,
`DoAction` 4,511, `RemoveObject2` 3,936, `DefineShape`/`2`/`3`
3,129/1,032/880, 709 `DefineSprite`/`End`, 519 `StartSound`, 50
`FrameLabel`, 37 `DefineSound`, 27 `DefineMorphShape`, 20
`DefineButton2`, 4 `DefineText`, 2 `DefineEditText`, 1 `DefineBitsJpeg3`,
1 `DefineFont2`.

**AVM1 opcodes** (4,529 buffers, 25,913 opcodes, 0 nested functions, 0
`With`) — same vocabulary, all supported, `GetURL` now 7.

**AS2 scan:** identical pattern to Hobo 2/3.

**Buttons:** 20 `DefineButton2`, 137 records, all with hit-test state, 3
placed frame-1 (same layout).

**Sound:** 37 `DefineSound` (MP3), 519 `StartSound`.

**Rendering:** 5,041 shapes (0 v4), 113 gradients, 0 bitmap fills, 708
sprites, 27 morph shapes, 1 bitmap tag, 6 text, 1 font, 0 `PlaceObject3`.

**Delta vs. Hobo 3:** monotonically larger content volume, otherwise the
same structural/interactivity model. **Same blocker.**

---

## Hobo 5 — Space Brawl: Attack of the Hobo Clones

| Field | Value |
|---|---|
| File | `/home/claude/game-corpus/hobo5/hobo5.swf` (8,110,172 bytes) |
| SWF version | 6 · Stage 600×450 · FPS 25.00 · Frames 13 |
| Compression | zlib, declared 17,736,510 → decompressed 17,736,502 bytes |
| AVM1/AVM2 | AVM1 only (5,012 `DoAction`, 0 `DoInitAction`, 0 `DoABC`) |

**Tags** (94,008 total — the largest Hobo file): `PlaceObject2` 51,199,
`ShowFrame` 21,363, `RemoveObject2` 5,167, `DoAction` 5,012,
`DefineShape`/`2`/`3` 3,766/2,480/1,469, 841 `DefineSprite`/`End`, 670
`StartSound`, 260 `FrameLabel`, 43 `DefineSound`, 26 `DefineMorphShape`,
21 `DefineButton2`, 4 `DefineText`, 2 `DefineEditText`, 1
`DefineBitsJpeg3`, 1 `DefineFont2`, **1 `DefineFontName`** (new tag not
seen in Hobo 1–4).

**AVM1 opcodes** (5,031 buffers, 34,387 opcodes, 0 nested functions, 0
`With`) — first appearance of **`GotoLabel`** (1 occurrence) in the Hobo
family; otherwise the same vocabulary, all supported.

**AS2 scan:** identical pattern to Hobo 2–4.

**Buttons:** 21 `DefineButton2`, 142 records, all with hit-test state. 3
placed frame-1 buttons — same 3-button layout, though world positions of
the first two are swapped relative to Hobo 1–4 (character IDs 12/92 vs.
92/81 elsewhere) — a cosmetic layout difference, not a structural one.

**Sound:** 43 `DefineSound` (MP3, most of any Hobo file), 670
`StartSound`.

**Rendering:** 7,715 shapes (0 v4), 113 gradients, 0 bitmap fills, 840
sprites, 26 morph shapes, 1 bitmap tag, 6 text, 1 font, 0 `PlaceObject3`.

**Delta vs. Hobo 4:** the largest and most content-dense Hobo file
analyzed; introduces `DefineFontName` and a single `GotoLabel` AVM1
opcode not seen elsewhere in the family. Structurally and
interactivity-wise identical. **Same blocker.**

---

## Hobo 6 — Hell

| Field | Value |
|---|---|
| File | `/home/claude/game-corpus/hobo6/hobo6.swf` (6,783,068 bytes) |
| SWF version | 6 · Stage 600×450 · FPS 25.00 · Frames 13 |
| Compression | zlib, declared 13,044,741 → decompressed 13,044,733 bytes |
| AVM1/AVM2 | AVM1 only (4,748 `DoAction`, 0 `DoInitAction`, 0 `DoABC`) |

**Tags** (79,199 total): `PlaceObject2` 44,029, `ShowFrame` 17,360,
`DoAction` 4,748, `RemoveObject2` 4,433, `DefineShape`/`2`/`3`
3,394/1,183/926, 781 `DefineSprite`/`End`, 577 `StartSound`, 114
`FrameLabel`, 37 `DefineSound`, 23 `DefineButton2`, 22
`DefineMorphShape`, 4 `DefineText`, 2 `DefineEditText`, 1
`DefineBitsJpeg2` (new — first appearance of this tag in the family), 1
`DefineBitsJpeg3`, 1 `DefineFont2`, 1 `DefineFontName`.

**AVM1 opcodes** (4,770 buffers, 29,368 opcodes, 0 nested functions, 0
`With`) — first appearance of **`InitObject`** (4 occurrences) in the
family; `GotoLabel` (1) also present (as in Hobo 5). All opcodes
supported.

**AS2 scan:** identical pattern to Hobo 2–5.

**Buttons:** 23 `DefineButton2` (most of any Hobo file), 152 records, all
with hit-test state, 3 placed frame-1 (same layout as Hobo 2–4).

**Sound:** 37 `DefineSound` (MP3), 577 `StartSound`.

**Rendering:** 5,503 shapes (0 v4), 119 gradients, **2 bitmap fills**
(back after Hobo 3/4 had 0), 780 sprites, 22 morph shapes, 2 bitmap
tags, 6 text, 1 font, 0 `PlaceObject3`.

**Delta vs. Hobo 5:** introduces `DefineBitsJpeg2` and the `InitObject`
AVM1 opcode (both new to the family), has the most `DefineButton2`
characters of any Hobo file so far (23). Structurally/interactivity-wise
identical. **Same blocker.**

---

## Hobo 7 — Heaven

| Field | Value |
|---|---|
| File | `/home/claude/game-corpus/hobo7/hobo7.swf` (7,170,232 bytes) |
| SWF version | 6 · Stage 600×450 · FPS 25.00 · Frames 13 |
| Compression | zlib, declared 13,898,200 → decompressed 13,898,192 bytes |
| AVM1/AVM2 | AVM1 only (4,864 `DoAction`, 0 `DoInitAction`, 0 `DoABC`) |

**Tags** (83,756 total): `PlaceObject2` 46,149, `ShowFrame` 18,854,
`RemoveObject2` 4,905, `DoAction` 4,864, `DefineShape`/`2`/`3`
3,492/1,226/938, 819 `DefineSprite`/`End`, 646 `StartSound`, 127
`FrameLabel`, 39 `DefineSound`, **27 `DefineButton2`** (the most of any
Hobo file), 22 `DefineMorphShape`, 4 `DefineText`, 2 `DefineEditText`, 1
`DefineBitsJpeg2`, 1 `DefineBitsJpeg3`, 1 `DefineFont2`, 1
`DefineFontName`.

**AVM1 opcodes** (4,890 buffers, 31,746 opcodes, 0 nested functions, 0
`With`) — same vocabulary as Hobo 6 (`InitObject` 4, `GotoLabel` 1), all
supported. `Play` now 11 (highest in the family).

**AS2 scan:** identical pattern to Hobo 2–6.

**Buttons:** 27 `DefineButton2`, 170 records, all with hit-test state, 3
placed frame-1 (same layout as Hobo 2–4/6).

**Sound:** 39 `DefineSound` (MP3), 646 `StartSound`.

**Rendering:** 5,656 shapes (0 v4), 113 gradients, 2 bitmap fills, 818
sprites, 22 morph shapes, 2 bitmap tags, 6 text, 1 font, 0
`PlaceObject3`.

**Delta vs. Hobo 6:** the most `DefineButton2` characters (27) of the
whole family; otherwise a continuation of the same content-volume growth
trend with no new tag types or opcodes. **Same blocker.**

---

## Extreme Pamplona (treated as a fully separate compatibility target)

This game is **not Hobo-shaped**. Confirmed by direct analysis, not
assumed: different SWF version, different stage/frame-rate, a
`loadMovie`-style multi-file architecture, `DefineFunction`-based AVM1
code (Hobo has none), legacy `Equals`/no `Equals2` opcode usage (Hobo
uses only `Equals2`), `DefineShape4`/`PlaceObject3` usage (0 in every
Hobo file), and **`onPress`/`onRelease` as literal AS2 property-handler
strings** (never found in any Hobo file) — a structurally different
button-interactivity model from Hobo's native `condActionsV2`.

### Main loader

| Field | Value |
|---|---|
| File | `/home/claude/game-corpus/extreme_pamplona/extreme-pamplona.swf` (1,000,458 bytes) |
| SWF version | **8** (vs. Hobo's 6) |
| Compression | zlib, declared 1,390,857 → decompressed 1,390,849 bytes |
| Stage | **800×400 px** (vs. Hobo's 600×450) |
| FPS | **24.00** (vs. Hobo's 25.00) |
| Frame count (declared) | **2** (vs. Hobo's 13) |
| AVM1/AVM2 | AVM1 (25 `DoAction`, **117 `DoInitAction`** — Hobo has 0 of these; 0 `DoABC` — no AVM2/ABC anywhere) |

**Tags** (6,070 total, main loader only): `PlaceObject2` 2,001,
`ShowFrame` 1,905, `RemoveObject2` 450, **`PlaceObject3` 345** (0 in every
Hobo file), 220 `End`, 219 `DefineSprite`, **`DefineShape4` 135** (0 in
every Hobo file), 131 `ExportAssets` (not seen in any Hobo file — used for
linkage-name-based dynamic instantiation, consistent with the
`attachMovie` string hit below), **126 `Unknown(253)`** + 1
`Unknown(255)` (tag IDs this codebase's `TagCode` enum does not
recognize at all — see "Parser gaps specific to this file" below), 117
`DoInitAction`, 103 `SoundStreamHead2`, 98/42/33 `DefineShape`/`3`/`2`, 38
`DefineEditText`, **37 `CsmTextSettings`** (not seen in any Hobo file), 25
`DoAction`, 7 `FrameLabel`, 5 `DefineButton2`, 5 `DefineFont3`, 5
`DefineFontAlignZones`, 4 `DefineBitsLossless`, 4 `DefineText`, 3
`DefineBitsLossless2`, 3 `StartSound`, 2 `DefineBitsJpeg3`, 2
`DefineSound`, 1 each of `DefineBitsJpeg2`/`FileAttributes`/`Protect`/
`SetBackgroundColor`/`Unknown(255)`.

**AVM1 opcode profile** (142 bytecode buffers, 33,741 total opcodes,
**126 nested `DefineFunction` bodies** — Hobo files have 0 — 0 `With`
blocks): `Push` 12,949, `GetVariable` 4,840, `Jump` 2,752, `SetVariable`
2,530, `If` 2,530, `Not` 2,312, **`Equals` 2,193** (the legacy/Flash-5-era
numeric-equals opcode — **not** `Equals2`, which every Hobo file uses
exclusively; this file uses `Equals2` zero times), `Subtract` 1,309,
`Add` 1,221 (again the legacy opcode, not `Add2` — though `Add2` also
appears 126 times, once per function body, likely string-concatenation
inside functions), `ConstantPool` 341, `DefineLocal` 241,
`DefineFunction`/`CallFunction`/`Return` 126 each, `Stop` 11, `GotoFrame`
4, `Play` 4. **Notably absent: `GetMember`/`SetMember`/`CallMethod`** —
this file's dot-property/method-call-style object access, if any, is not
happening in these 142 top-level/`DoInitAction` buffers (unlike every
Hobo file, where `GetMember`/`SetMember`/`CallMethod` are all
top-10-frequency opcodes). **All opcodes present have a real case in
`Interpreter.cpp` — 0 unsupported opcodes**, and the tool's
`DefineFunction`-body-extent handling was exercised for real here (126
times) with no desync in the resulting counts.

**AS2 scan — the single biggest content difference from Hobo:** FOUND —
`Key`, **`MovieClip`** (never found in any Hobo file), `Sound`, `_root`,
`_parent`, **`_global`** (never found in Hobo), `gotoAndPlay`,
`gotoAndStop`, `play`, `stop`, `removeMovieClip`,
**`createEmptyMovieClip`, `duplicateMovieClip`, `attachMovie`** (all
three never found in any Hobo file), `_x`, `_y`, `_xscale`, `_yscale`,
**`_rotation`, `_alpha`, `_width`, `_height`** (none found in Hobo),
`_visible`, **`onPress`, `onRelease`** (never found in Hobo — see button
model note above). NOT FOUND — `Mouse`, `loadMovie` (interesting: the
multi-SWF architecture is real, per the 23 separate content files, but
apparently not driven by the literal string `loadMovie` in this main
loader — possibly `MovieClipLoader` or a different loading call not
covered by this scan's identifier list), `onClipEvent`, `onRollOver`,
`onRollOut`, `onMouseDown`, `onMouseUp`, `onMouseMove`,
`ExternalInterface`.

**Buttons:** 5 `DefineButton2` (far fewer than any Hobo file's 16-27), 23
total records, all 5 with explicit hit-test state. **0 button instances
placed on the static frame-1 display list** — with only `createEmptyMovieClip`/
`duplicateMovieClip`/`attachMovie` all confirmed present and a 2-frame
loader structure, this strongly suggests Extreme Pamplona's UI/buttons
are built **dynamically at runtime** (via `attachMovie`-style linkage,
consistent with the 131 `ExportAssets` entries) rather than statically
placed the way every Hobo file's 3 frame-1 buttons are. **This is a real
limitation of static analysis, not a claim that this file has no
buttons** — a dynamic/runtime trace (out of scope for this analysis-only
phase) would be needed to see them.

**Sound:** 2 `DefineSound` (MP3) in the main loader — the bulk of this
game's audio lives in the separate `sounds_*.swf`/`music_*.swf` content
files (see below), not the loader itself. 3 `StartSound` in the loader.

**Rendering:** 308 shape tags total, of which **135 are `DefineShape4`**
— unlike every Hobo file (0 v4 shapes), roughly 44% of this file's shapes
are a version this runtime's parser explicitly does not support
(`DefineShapeTag.cpp` early-outs on v4). 46 gradient fills / 20 bitmap
fills found in the 173 v1-3 shapes that *are* parsed. 219 sprites, 0
morph shapes (none in the loader — content sub-SWFs not individually
checked for this). 10 bitmap tags, 42 text tags (dominated by the 38
`DefineEditText`), 5 fonts (`DefineFont3`, a version this project's font
parser has not been independently confirmed against — see
`docs/known-limitations.md` for font-parser scope). **345
`PlaceObject3`** occurrences — every one of them carries potential
blend-mode/filter data this runtime's `PlaceObject3` gap (no parser at
all — `compatibility-matrix.md` §2) means is silently dropped.

**Interactivity summary:** `Button2_present: yes`, **`onPress_string_found: yes`,
`onRelease_string_found: yes`** (both a first among this corpus),
`onRollOver`/`onRollOut`/`onClipEvent` not found, `Key` present
(`Key.isDown()` context), `Sound` present.

**Parser gaps specific to this file (not seen at all in any Hobo file):**

- **126 `Unknown(253)` + 1 `Unknown(255)` tag occurrences** — tag IDs this
  codebase's `swf::TagCode` enum has no entry for at all (not merely
  "recognized but unparsed" like `DefineShape4`/`PlaceObject3` — the
  generic tag-header scanner logs them as fully unknown and skips their
  body). Total unknown-tag body bytes are substantial (several of the
  253-tag bodies exceed 5–10 KB each — see `diagnostic_main_loader.txt`'s
  raw `[WARN]` trace for the full offset/length list). **Not
  investigated further this phase** (analysis-only) — candidate for a
  focused follow-up: identifying what tag ID 253/255 actually is in this
  SWF (worth checking against the official SWF spec's currently-unused ID
  ranges, or whether this is a 3rd-party/compiler-specific extension) is
  a prerequisite to knowing whether it matters for compatibility.
- **`DefineShape4` (135 occurrences, ~44% of this file's shapes) and
  `PlaceObject3` (345 occurrences) are both real, heavily-used tags in
  this file** — in stark contrast to their total absence (0 each) across
  all 7 Hobo files. Implementing either would measurably help Extreme
  Pamplona rendering and have **zero effect on any Hobo game**.
- **`CsmTextSettings` (37 occurrences)** — not parsed by this codebase
  (no reference found in `src/swf/`); affects this file's 38
  `DefineEditText` fields' text-rendering quality settings
  (anti-aliasing/thickness/sharpness), not parsed at all elsewhere.

**Content sub-SWF findings (see `tests/games/extreme_pamplona/manifest.md`
for the full 24-file breakdown):** all 23 content sub-SWFs (`level-*`,
`music_*`, `player*`, `sounds_*`) show `Button2_present: no` and no
`Key`/`Mouse`/handler-string hits — they are asset/animation containers,
not interactive logic. **The main loader alone is where this game's
event-dispatch requirements live.**

**Runtime compatibility status:**

| Feature this file uses | Parser support | Runtime behavior |
|---|---|---|
| `DefineButton2` | Implemented | `ButtonInstance` implemented; hit-testing implemented; **event dispatch (both `condActionsV2` AND `object.onPress=fn`-style property handlers) MISSING** |
| `DefineFunction`/`CallFunction` (user-defined AVM1 functions) | Implemented (`Interpreter.cpp`'s `DefineFunction`/`DefineFunction2` cases) | Implemented — same interpreter path as every other AVM1 opcode |
| `createEmptyMovieClip`/`duplicateMovieClip`/`attachMovie` | Opcode-level (`NewObject`/method-call machinery) implemented in general | **Not independently confirmed working for this file's specific dynamic-instantiation pattern** — would require dynamic execution tracing, out of scope this phase |
| `DefineShape4` | **NOT parsed** | 135 shapes in this file are simply absent from rendering |
| `PlaceObject3` | **NOT parsed at all** (no code path reads this tag) | 345 placements — this file likely fails to display sprites/characters placed exclusively via `PlaceObject3` rather than `PlaceObject2`, pending confirmation via the render harness (see below) |
| `CsmTextSettings` | **NOT parsed** | text-rendering-quality hints ignored (text itself, via `DefineEditText`, still renders) |
| `Unknown(253)`/`Unknown(255)` tags | **NOT recognized at all** | unknown effect — 127 occurrences, not investigated this phase |
| MP3 sound | Implemented (header+data) | **Never decoded/played** (priority #5, same as Hobo) |

**Blockers specific to this game, in addition to the shared button-event-
dispatch gap:** `DefineShape4`, `PlaceObject3`, and `CsmTextSettings`
parser gaps are all real, measurable, **and Extreme-Pamplona-specific** —
none of them would move the needle on any Hobo game. Whether the same
generic-event-dispatcher work that unblocks Hobo's `condActionsV2`
buttons would *also* need to cover `object.onPress = function(){}`-style
property-handler dispatch (this file's pattern) is an open design
question for that future phase — the two are related but not identical
mechanisms (see cross-game matrix below).

---

## Real-content render harness results (frames 1–5, all 8 games)

Run via `tools/real_game_harness/run_harness.sh build /tmp/real_game_harness_out`
(2026-08-18) — baseline recorded at
`tests/games/_harness_baseline/harness_summary_2026-08-18.txt`:

| Game | Init | Frame 1 | Frame 2 | Frame 3 | Frame 4 | Frame 5 |
|---|---|---|---|---|---|---|
| hobo1 | OK | OK | OK | OK | OK | OK |
| hobo2 | OK | OK | OK | OK | OK | OK |
| hobo3 | OK | OK | OK | OK | OK | OK |
| hobo4 | OK | OK | OK | OK | OK | OK |
| hobo5 | OK | OK | OK | OK | OK | OK |
| hobo6 | OK | OK | OK | OK | OK | OK |
| hobo7 | OK | OK | OK | OK | OK | OK |
| extreme_pamplona | OK | OK | OK | **OUT OF RANGE** (declares only 2 frames) | **OUT OF RANGE** | **OUT OF RANGE** |

All 8 games load, parse, and initialize with **zero crashes and zero
runtime exceptions**. Extreme Pamplona's frames 3-5 "failures" are the
expected, correct behavior of `flash_runtime --render` rejecting an
out-of-range frame index against a file whose header declares only 2
frames (`--render: frame 3 out of range [1, 2]`) — not a parser or
runtime bug. Every Hobo file (13 declared frames each) renders all 5
requested frames cleanly. Per-frame MD5 checksums are recorded in the
baseline file as this phase's golden-output reference; they are not
required to stay byte-identical across future renderer changes with
known nondeterministic behavior, but any *unexpected* change is worth
investigating the same way the ButtonInstance-phase's frame-1-5
before/after diffs were used in `docs/test-results.md`.

---

## Hobo-family comparison — common vs. game-unique features

**Common to all 7 Hobo files** (a safe target for Hobo-driven fixes —
none of these differ across the family): SWF version 6; 600×450 stage;
25 fps; 13 declared frames; zlib compression; AVM1-only, `DoInitAction`
always 0, `DoABC` always 0; **`Equals2` used exclusively (never legacy
`Equals`)**; **0 `DefineFunction`/`DefineFunction2` bodies and 0 `With`
blocks in every file** (no user-defined AVM1 functions anywhere in the
Hobo family — a real, verified, and somewhat surprising finding, not an
assumption); 0 `DefineShape4`; 0 `PlaceObject3`; every AVM1 opcode used
is supported by `Interpreter.cpp`; every file places exactly 3 buttons
on frame 1 in the same structural pattern (2 unnamed root-level buttons +
1 unnamed mute button nested one level under a clip named
`/mutebutton`); `Key` string present in all 7 but `Mouse`/`MovieClip`/
`_parent` (Hobo 1 only)/`_global`/`onClipEvent`/any
`onPress`/`onRelease`/`onRollOver`/`onRollOut`/`ExternalInterface`
absent in all 7; all button interactivity is carried by native
`DefineButton2` `condActionsV2` records, never AS2
`object.onPress = function(){}` property assignment.

**Game-unique/varying features across the Hobo family:**

| Feature | Hobo 1 | Hobo 2 | Hobo 3 | Hobo 4 | Hobo 5 | Hobo 6 | Hobo 7 |
|---|---|---|---|---|---|---|---|
| `_parent` string used | no | yes | yes | yes | yes | yes | yes |
| Bitmap tags (any Define­Bits*) | 0 | 10 | 1 | 1 | 1 | 2 | 2 |
| `DefineButton2` count | 16 | 17 | 19 | 20 | 21 | 23 | 27 |
| `DefineMorphShape` count | 19 | 16 | 24 | 27 | 26 | 22 | 22 |
| `DefineFontName` tag | no | no | no | no | yes | yes | yes |
| `InitObject` opcode | no | no | no | no | no | yes (4) | yes (4) |
| `GotoLabel` opcode | no | no | no | no | yes (1) | yes (1) | yes (1) |
| File/decompressed size (MB) | 4.97 / 7.73 | 5.15 / 9.13 | 5.69 / 10.43 | 6.33 / 12.02 | 8.11 / 17.74 | 6.78 / 13.04 | 7.17 / 13.90 |

**Practical implication (why this matters for the next event-dispatch
phase):** because every single Hobo file uses the *same* button-event
mechanism (native `condActionsV2`, zero `onPress`/`onRelease`
property-style handlers, zero user-defined AVM1 functions anywhere),
**one dispatcher implementation targeting `condActionsV2` covers the
entire Hobo family identically** — there is no risk of a "Hobo 1-specific
hack" here, because Hobo 1–7 are, for this specific feature, structurally
indistinguishable. The growing `DefineButton2`/morph-shape/bitmap counts
across the family are content-volume differences, not
mechanism differences.

---

## Cross-game compatibility matrix

Values are **YES** (confirmed present by direct tool output) / **NO**
(confirmed absent) / **UNKNOWN** (not determinable from static analysis
alone — e.g. dynamically-constructed content). Nothing here is guessed.

| Feature | Hobo1 | Hobo2 | Hobo3 | Hobo4 | Hobo5 | Hobo6 | Hobo7 | Extreme Pamplona |
|---|---|---|---|---|---|---|---|---|
| SWF version | 6 | 6 | 6 | 6 | 6 | 6 | 6 | 8 |
| AVM1 | YES | YES | YES | YES | YES | YES | YES | YES |
| `DefineButton2` | YES | YES | YES | YES | YES | YES | YES | YES |
| Button placement (static, frame 1) | YES(3) | YES(3) | YES(3) | YES(3) | YES(3) | YES(3) | YES(3) | NO (0 static; likely dynamic — UNKNOWN count) |
| `onPress` (property-handler string) | NO | NO | NO | NO | NO | NO | NO | YES |
| `onRelease` (property-handler string) | NO | NO | NO | NO | NO | NO | NO | YES |
| `onRollOver` | NO | NO | NO | NO | NO | NO | NO | NO |
| `onRollOut` | NO | NO | NO | NO | NO | NO | NO | NO |
| `onClipEvent` | NO | NO | NO | NO | NO | NO | NO | NO |
| Native button `condActionsV2` (inferred: `DefineButton2` present + 0 handler-property strings) | YES | YES | YES | YES | YES | YES | YES | UNKNOWN (5 `DefineButton2` present, but this file *also* has `onPress`/`onRelease` strings — could be either or both mechanisms) |
| `enterFrame` (via `onClipEvent`) | NO | NO | NO | NO | NO | NO | NO | NO |
| `Key.isDown` context (`Key` string) | YES | YES | YES | YES | YES | YES | YES | YES |
| `Mouse` | NO | NO | NO | NO | NO | NO | NO | NO |
| `Sound` | YES | YES | YES | YES | YES | YES | YES | YES |
| `DefineMorphShape` | YES(19) | YES(16) | YES(24) | YES(27) | YES(26) | YES(22) | YES(22) | NO (0 in loader) |
| `DefineShape4` | NO | NO | NO | NO | NO | NO | NO | YES(135) |
| Bitmap tags | NO | YES(10) | YES(1) | YES(1) | YES(1) | YES(2) | YES(2) | YES(10, loader only) |
| Text tags | YES | YES | YES | YES | YES | YES | YES | YES |
| Fonts | YES(1) | YES(1) | YES(1) | YES(1) | YES(1) | YES(1) | YES(5, `DefineFont3`) | — |
| `ExternalInterface` | NO | NO | NO | NO | NO | NO | NO | NO |
| `gotoAndPlay`/`gotoAndStop` | YES | YES | YES | YES | YES | YES | YES | YES |
| `removeMovieClip` | YES | YES | YES | YES | YES | YES | YES | YES |
| `_alpha` | NO | NO | NO | NO | NO | NO | NO | YES |
| `_width`/`_height` (as script identifiers) | NO | NO | NO | NO | NO | NO | NO | YES |
| Nested MovieClips (`DefineSprite`) | YES | YES | YES | YES | YES | YES | YES | YES |
| `createEmptyMovieClip`/`duplicateMovieClip`/`attachMovie` | NO | NO | NO | NO | NO | NO | NO | YES |
| User-defined AVM1 functions (`DefineFunction`) | NO | NO | NO | NO | NO | NO | NO | YES(126) |
| `PlaceObject3` | NO | NO | NO | NO | NO | NO | NO | YES(345) |
| `CsmTextSettings` | NO | NO | NO | NO | NO | NO | NO | YES(37) |

---

## "If we implement feature X, which games does it help?"

This is the question this corpus exists to answer, per the phase's
success criterion. Based strictly on the matrix above:

- **Button `condActionsV2` dispatch** → helps **all 7 Hobo games**
  identically (uniform mechanism across the family, confirmed above);
  **does not by itself help Extreme Pamplona**, whose visible buttons use
  `onPress`/`onRelease` property-handler strings instead (though Extreme
  Pamplona's 5 `DefineButton2` characters mean *some* condActionsV2
  content may exist there too — UNKNOWN without deeper disassembly).
- **`object.onPress`/`onRelease` property-handler dispatch** (the
  `MovieClip`/generic-object event-handler-property mechanism) → helps
  **only Extreme Pamplona** among this corpus; **0 effect on any Hobo
  file** (none use this pattern).
- **A full generic event dispatcher** covering both mechanisms above
  would be required to unblock interactivity in **both** Hobo and
  Extreme Pamplona — confirming the spec's framing that the event
  dispatcher must be designed against this whole matrix, not hobo.swf
  alone, since hobo.swf's own native-button pattern does not exercise
  the property-handler path Extreme Pamplona needs.
- **MP3 audio decode** → helps all 8 games (every one uses `DefineSound`
  with MP3-format data); no game in this corpus uses a codec other than
  MP3.
- **`DefineMorphShape` rendering** → helps **all 7 Hobo games**
  (16–27 characters each); **0 effect on Extreme Pamplona's main loader**
  (0 morph shapes there — content sub-SWFs not individually re-checked
  for morph-shape usage, so "0 effect" is scoped to the loader only).
- **`DefineShape4` parsing** → helps **only Extreme Pamplona** (135
  shapes, ~44% of that file's shape content); **0 effect on any Hobo
  file** (all use v1-3 exclusively).
- **`PlaceObject3` parsing** (blend modes/filters) → helps **only
  Extreme Pamplona** (345 occurrences); **0 effect on any Hobo file**.
- **`GlobalObject` built-ins (`Math`/`Date`/`Number`/`String`/`Boolean`)**
  → likely helps the Hobo family more directly (585-1,092 `CallMethod`
  opcodes per file, a plausible built-in-method-call vector), though this
  was not independently disassembled to confirm which specific methods
  are called; Extreme Pamplona's main loader shows **0 `CallMethod`**
  opcodes at all in its 142 analyzed buffers, suggesting this specific
  gap may matter less for that file's loader (its 23 content sub-SWFs
  were not individually opcode-profiled this phase).
- **`CsmTextSettings` parsing** → helps only Extreme Pamplona (37
  occurrences; text-quality-only, not a functional blocker).
- **Resolving the two unrecognized `Unknown(253)`/`Unknown(255)` tag
  IDs** → relevant only to Extreme Pamplona; impact unknown until the
  tag's actual purpose is identified (not investigated this phase).

---

## Regression check

**253/253 `TEST_CASE`s passing** (`./build/tests/flash3ds_tests`, same
count as the end of the ButtonInstance phase — **zero regressions**).
This phase's only source-tree changes were additive: a new
`tools/swf_diagnostic/main.cpp` (a new, separate executable — links
against the existing `flash3ds_core` library unchanged) and a new
`tools/real_game_harness/run_harness.sh` test-harness script; no existing
`.cpp`/`.h` file in `src/` was modified. `CMakeLists.txt` gained one new
`add_executable(swf_diagnostic ...)` block alongside the pre-existing
`flash_runtime` registration.

## Next blocker recommendation

Per this corpus's own cross-game matrix (not hobo.swf alone, per the
phase's explicit instruction): **button event dispatch remains the
single highest-leverage next blocker**, exactly as `docs/known-
limitations.md` priority #2 already identified — but this phase adds
the specific, evidence-based refinement that a complete implementation
needs **two** dispatch mechanisms, not one: (1) `DefineButton2`
`condActionsV2` native action-list dispatch, which alone unblocks **all 7
Hobo games**, and (2) `object.onPress`/`onRelease`-style property-handler
dispatch (part of the generic event dispatcher already scoped in
`docs/events.md`), which is required for **Extreme Pamplona**. Implementing
only (1) fully solves Hobo-family interactivity but leaves Extreme
Pamplona blocked; implementing only (2) does the reverse. This corpus
did not exist before this phase, so this two-mechanism distinction was
not previously visible from hobo.swf analysis alone.

## Cat Ninja — added to corpus 2026-08-24

Staged from the user's device (`G:\3DS\Új mappa\CatNinja.swf`) into
`/home/claude/game-corpus/cat_ninja/cat_ninja.swf`, per task01.txt's
instruction making it mandatory corpus. `swf_diagnostic` findings (all
confirmed matching the previously-reported facts):

- SWF version 10, zlib-compressed (CWS), stage 800x600px, 60fps,
  `FrameCount=2`.
- AVM2/AS3 (`DoABC2` x2) — **zero** `DefineShape`/`DefineSprite`/
  `DefineButton` tags anywhere.
- 63 `DefineBitsLossless2` bitmap tags, 18 `DefineSound` (MP3) tags.
- `CharacterDictionary::build()` currently resolves only the 18 sound
  characters (18, not 63+18) — bitmap tag parsing is roadmap Phase 10,
  not started, so the 63 bitmaps are recognized by tag but produce no
  character-dictionary entry at all yet (matches every other corpus
  game's bitmap-tag handling — see `docs/compatibility-matrix.md`).
- `--render 1`/`--render 2` succeed (identical output — a static 2-frame
  file with no AVM1 timeline animation this runtime executes, since it's
  AVM2 content); `--render 3` correctly fails ("frame out of range"),
  matching its own declared `FrameCount=2` — not a bug.
- Peak RSS (isolated `mem_profile_check`): 13.33MB before this phase's
  Option-B change, 13.31MB after (near-zero change expected — see
  `docs/memory-audit.md` §10 for why). **Its bitmap-heavy real memory
  cost remains unmeasured** until roadmap Phase 10 (bitmap decode) lands.
- Per this project's standing AVM1-priority scope (top-level `CLAUDE.md`):
  Cat Ninja's AVM2 content is NOT executed and is not a target for AVM2
  implementation — it is used purely as a RAM/loader/bitmap-tag-presence
  diagnostic target, exactly as task01.txt specified.
