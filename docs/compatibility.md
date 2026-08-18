# Compatibility Targets

## Hobo series (primary target)

**Phase 9 (2026-08-18): tested against a real copy of `hobo.swf`** (the
original 2008 Hobo, ArmorGames — ~4.97 MB, SWF6/CWS). The table below in
earlier revisions of this document was written from the project spec's
un-verified description of the series; it's been superseded by what
actually running the runtime against the real file found. The rest of the
series (`hobo 2` through `hobo 7`) and Extreme Pamplona are available for
the same treatment in a later pass — see "Not yet tested" below.

### Confirmed file characteristics (verified, not assumed)

- SWF version 6, CWS (zlib) compression — header parses correctly, `flash3ds
  Inspector` reports `Stage size: 600.0 x 450.0 px`, `Frame rate: 25.00
  fps`, `Frame count: 13`, `Tag count: 3745`.
- Tag histogram across the whole file (`flash_runtime hobo.swf --quiet`,
  tag names counted): 1758× `DefineShape`, 757× `DefineShape3`, 528×
  `DefineSprite`, 475× `DefineShape2`, 86× `PlaceObject2`, 36×
  `RemoveObject2`, 35× `DefineSound`, 19× `DefineMorphShape`, 16×
  `DefineButton2`, 13× `ShowFrame`, 13× `DoAction`, 3× `DefineText`, 2×
  `DefineEditText`, 1× `SoundStreamHead2`, 1× `SetBackgroundColor`, 1×
  `DefineFont2`, 1× `End`.
- **Correction to the old spec-derived table below:** the file uses
  `DefineFont2`, not `DefineFont3` — the earlier "confirmed characteristics"
  list in this doc (written from spec section 11, not from the actual
  file) was wrong on this point. Phase 8's `DefineFont3` rejection is not
  actually a blocker for this file.
- No `DefineBits*` (bitmap) tags at all in this file — every visual is
  vector shape or embedded (uncompressed PCM-ish/ADPCM, unverified) sound.

### What worked, end-to-end, on first real-content run

- Full load → `CharacterDictionary::build()` → `MovieClipInstance::createRoot()`
  → tick frames 1 through 13 (running every intermediate frame's `DoAction`)
  → `SceneRenderer`/`SoftwareRenderer` → PPM, with **no crash, no infinite
  loop, no `reader.failed()`** at any point, for the whole file.
- **Rendered output is recognizably correct**: frame 1 shows the Hobo
  title/menu screen (character illustration, "HOBO" wordmark, "CONTROLS"
  panel with icon tiles, "Armor Games" logo+text, "PLAY MORE GAMES!" /
  copyright text) with correct colors, positions, and a "PLAY!" button
  fading in by frame 5 — screenshots delivered alongside this Phase 9
  report. Cross-checked one shape (`characterId=1`, the full-stage black
  background rectangle) by hand-decoding its raw tag bytes independently
  in Python against the public spec: our parser's bounds/fill-color output
  matched exactly.
- AVM1 execution of the real frame-1 preloader script hit zero "unhandled
  action code" / "unknown operand" errors — every opcode the script uses is
  covered by the Phase 4 interpreter.

### Bugs found and fixed this phase

1. **`ShapeRecords.cpp` byte-alignment bug (fixed).** A mid-stream
   `StyleChangeRecord` with `StateNewStyles` set must byte-align before
   reading its new `FILLSTYLEARRAY`/`LINESTYLEARRAY` (both byte-level
   structures embedded in the otherwise bit-packed shape record stream) —
   `readShapeRecordStream()` was missing that `byteAlign()` call. No prior
   test fixture exercised this path (Phase 3's fixtures always wrote
   `StateNewStyles=0`), so it went undetected until tested against real
   content: shapes with more than one style region (common — every
   `DefineShape`/`2`/`3` with a genuinely multi-color fill area uses it)
   desynced the bitstream at that point, and every fill-style byte read
   after the first occurrence in a shape decoded as garbage, logging
   `[SHAPE] Unknown fill style type 0x??` for nonsense byte values. Before
   the fix: **~1.54 million** such warnings rendering all 13 frames once
   each. After the fix: **zero**. Regression test:
   `ShapeWithStyle_MidStreamNewStyles_ByteAlignsBeforeNewStyleArrays` in
   `tests/test_shape_records.cpp`.
2. **Missing OOP-callable `MovieClip` methods (fixed).** `_root.stop()`
   (and `.play()`/`.getBytesLoaded()`/`.getBytesTotal()`) called via AVM1
   `CallMethod` bytecode — the real syntax the frame-1 preloader-gate
   script uses — all failed with `CallMethod: '<name>' is not a function`,
   because `MovieClipInstance::handleNativeGet()` only exposed intrinsic
   *properties* (`_x`, `_y`, ...), never callable *methods*: only the bare
   unqualified action-code forms (`stop();`, dispatched through
   `HostBindings`) worked. Added `stop`/`play`/`nextFrame`/`prevFrame`/
   `gotoAndStop`/`gotoAndPlay` (numeric-frame and label forms, both) /
   `getBytesLoaded`/`getBytesTotal` as native `FunctionDef`s returned from
   `handleNativeGet()`, reusing the exact same `Timeline` primitives
   `HostBindings` already used. `getBytesLoaded()`/`getBytesTotal()` both
   return `Movie::declaredFileLength` — this runtime loads synchronously
   and never streams, so "loaded" is trivially "total" from frame 1.
   Regression tests: the four `MovieClipInstance_CallMethod_*` cases in
   `tests/test_movieclip_instance.cpp`.

### Remaining findings — not fixed, prioritized for later

- **`getBytesLoaded`/`getBytesTotal` gap was the one actually blocking
  script logic; the rest of Phase 9's leftover findings are all
  non-blocking** (nothing else prevented the movie from loading, ticking,
  or rendering recognizably).
- One `CallMethod: target is not an object` warning remains (down from 4
  distinct CallMethod failures before this phase's fixes) — a script tries
  to call a method on a target that resolves to `undefined` at frame 1.
  Not chased further this phase: it's exactly the situation a real Flash
  Player would also silently no-op on (calling a method on an unresolved
  path), so it may well be correct behavior rather than a bug. Revisit if
  a later frame/later Hobo-series title shows a visible symptom traceable
  to it.
- **`DefineMorphShape`/`DefineMorphShape2`** (19 occurrences in this file)
  are recognized by `TagCode` but not resolved into `CharacterDictionary`
  — any `PlaceObject2` referencing one silently places nothing. Not yet
  known whether any of the 19 are actually on a rendered frame's display
  list in this file. Natural next-phase scope: `MORPHFILLSTYLE`/
  `MORPHLINESTYLE`/two-shape (start+end) parsing, plus a ratio-interpolated
  render — a real chunk of new work, not a quick fix.
- **`[TIMELINE] 1 trailing tag(s) after the last ShowFrame`** fires for
  ~18-19 of the file's 528 `DefineSprite` instances per full render.
  Handled gracefully already (the trailing tag(s) are dropped, not
  crashed on) and doesn't visibly break anything in the frames inspected.
  Most likely a genuine encoder quirk (a single-frame/last-frame sprite
  whose tag stream ends without a final `ShowFrame`) rather than a parser
  bug — matches known Flash Player leniency — but not independently
  confirmed. Low priority unless a later title shows a visible symptom.
- `Sound.attachSound()`/audio codec decode/mouse-and-keyboard `onClipEvent`s
  and button `on()` handlers/`ExternalInterface.addCallback` HostBindings/
  `TextField` API — all still open per the Phase 8 carry-over list in
  `CLAUDE.md`; none of them produced a *new* finding this phase (Hobo's
  frame 1-13 script doesn't exercise any of them in a way that logged a
  warning), but they remain unverified for this file's *gameplay* frames
  (only the title/menu screen — frames 1-13 of the root timeline — was
  actually inspected this phase; gameplay likely starts inside a sprite
  reached via a `gotoAndPlay`/button click this phase didn't trigger).

### Not yet tested

- `hobo 2 - prison brawl.swf` through `hobo 7 - heaven.swf` (sequels,
  available in the same source folder as the primary `hobo.swf`).
- Gameplay frames of `hobo.swf` itself — Phase 9 only walked the root
  timeline's 13 frames (the title/menu screen); reaching actual gameplay
  needs either a `gotoAndPlay` triggered by the "PLAY!" button's
  `DefineButton2` `on(press)` handler (not dispatched yet — needs
  hit-testing) or a manually-scripted jump for testing purposes.

## Extreme Pamplona (secondary target)

Now available (`extreme-pamplona.rar`, alongside the Hobo series, not yet
extracted/tested). Add findings here once run through the same process.
