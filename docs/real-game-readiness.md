# Real-Game Readiness Matrix

Synthesized from `docs/real-game-compatibility.md` (the 2026-08-18
real-game-corpus phase — full per-game tag/opcode/AS2-API detail lives
there, not duplicated here) plus this audit's own correction: that
phase's "runtime compatibility status" tables call button event dispatch
"MISSING" for every game — it is **now implemented and tested**
(uncommitted; see `docs/current-state-audit.md` §5), which changes several
rows below from that source document.

## Phase 1 results (2026-08-21) — dispatcher verified working; a real, evidence-based reason found for "nothing happens on click"

`tools/real_game_harness/click_probe.cpp` simulated a real hover→press→
release mouse sequence at Hobo1's 3 documented frame-1 button positions,
each compared against a same-tick-count control (mouse held off any
button). **Result: no observable effect from clicking any of the 3 — and
none is expected**, per `tools/real_game_harness/button_debug.cpp`'s
direct dump of their `condActionsV2` records: **all 3 buttons' only
condActionsV2 record has `conditions=0x000` (zero mouse-transition bits)
and `keyCode=4` ("End" key) only.** The dispatcher has nothing to fire on
click for these specific buttons because the SWF's own binary never
attaches a mouse action to them — this is a real content property, not a
dispatcher bug.

**This was independently confirmed positive**, not just negative: pressing
the "End" key (`InputState::kEnd`, code 35) against a fresh `hobo.swf` load
produces a render output that differs from both the pre-press state and a
same-tick-count no-input control (`tools/real_game_harness/click_probe.cpp`
`key:` candidates) — i.e. **`CondKeyPress` dispatch demonstrably fires and
has a real effect against real game content.** The button/clip
event-dispatch mechanism itself is verified working end-to-end; it was
simply pointed at the wrong trigger (mouse) for these particular buttons.

**Corpus-wide census** (`tools/real_game_harness/button_scan.cpp`, run
against all 7 Hobo files): every file follows the same shape — exactly
1-3 `DefineButton2` characters per file carry a real mouse-transition
condition (Hobo1/2/3/4/5/6: exactly 1 each — char 1320/1210/1889/1864/
1886/1933 respectively; Hobo7: 3, char 4611 among them), the large
majority are keypress-only (12-23 per file), and 1-2 per file have no
condActionsV2 at all (purely visual Up/Over/Down buttons with zero
scripted action). **None of the mouse-conditioned buttons are among the 3
placed on frame 1** in any Hobo file checked — meaning Hobo's actual
title-screen "click PLAY" interaction, if it exists at all via
`condActionsV2`, is not what these 3 well-documented frame-1 buttons do;
their real interactivity (beyond Up/Over/Down visual state, already
implemented) is keyboard-driven. Extreme Pamplona's 5 `DefineButton2`
characters have **zero** condActionsV2 records of any kind — fully
consistent with its confirmed reliance on the separate `onPress`/
`onRelease` AS2 property-handler mechanism instead.

**Practical implication:** the previous framing ("condActionsV2 dispatch
is the single blocker to Hobo title-screen progression") was an
untested assumption. **It's corrected here:** the dispatch mechanism
works; Hobo1's title screen most likely does not progress via a mouse
click on any of its 3 visible frame-1 buttons at all — it may require a
specific keypress (worth trying "End" specifically, and worth checking
whether `Key.isDown()` is polled more broadly elsewhere in the 2,884
`DoAction` buffers this file has, not yet done), or the mouse-driven
progression this file supports lives on a later frame/button not yet
reachable from frame 1. **Not yet determined:** what actually causes
Hobo's title screen to advance to gameplay. That remains open — this
phase changed the question from "does the dispatcher work" (yes) to
"what real-world input does this game actually expect" (unknown,
worth a follow-up investigation before assuming any specific game is
stuck).

| Game | Loads/parses | Renders (5 frames) | Button dispatch mechanism | Dispatch implemented? | Confirmed playable past title screen? | Top game-specific blocker |
|---|---|---|---|---|---|---|
| Hobo 1 | YES | YES | `condActionsV2` only | YES — **verified working** (dispatches correctly; frame-1 buttons just use keypress not mouse, see Phase 1 results below) | **NO via mouse click on any frame-1 button (verified); unknown via keyboard** | `DefineMorphShape` not rendered (19 chars); real title-screen trigger still unidentified |
| Hobo 2 | YES | YES | `condActionsV2` only | YES (untested end-to-end) | UNKNOWN | Same + bitmap fills (9, now at least parsed) |
| Hobo 3 | YES | YES | `condActionsV2` only | YES (untested end-to-end) | UNKNOWN | `DefineMorphShape` (24 chars) |
| Hobo 4 | YES | YES | `condActionsV2` only | YES (untested end-to-end) | UNKNOWN | `DefineMorphShape` (27 chars) |
| Hobo 5 | YES | YES | `condActionsV2` only | YES (untested end-to-end) | UNKNOWN | Largest file — 453.9MB peak RSS (see memory-audit.md), `DefineMorphShape` (26) |
| Hobo 6 | YES | YES | `condActionsV2` only | YES (untested end-to-end) | UNKNOWN | `DefineMorphShape` (22), `DefineBitsJpeg2` (new tag, parse status not independently re-checked) |
| Hobo 7 | YES | YES | `condActionsV2` only | YES (untested end-to-end) | UNKNOWN | Most `DefineButton2` of the family (27) |
| Extreme Pamplona | YES (main loader; sub-SWFs never loaded — no `loadMovie`) | YES (2 declared frames only) | `onPress`/`onRelease` property handlers (confirmed present as literal strings) **+ possibly `condActionsV2` too** (5 `DefineButton2` present, mechanism UNKNOWN without disassembly) | YES for property handlers (untested end-to-end); `condActionsV2` also implemented but this file's use of it is unconfirmed | UNKNOWN — additionally blocked structurally: game needs `loadMovie`/multi-SWF (not implemented at all), `DefineShape4` (135 shapes, ~44% of file, not parsed), `PlaceObject3` (345 placements, not parsed at all) |

## Common blockers across the whole corpus

- **MP3 audio decode** — helps all 8 games identically (every one uses
  MP3-format `DefineSound`); zero games use any other codec. No codec
  decode exists anywhere in this codebase (`docs/current-state-audit.md`
  §3).
- **`GlobalObject` built-ins** (`Math`/`Date`/`Number`/`String`/`Boolean`)
  — `GlobalObject::create()` is a literal no-op stub. Likely relevant to
  the Hobo family's 585-1,092 `CallMethod` opcodes per file (not
  independently disassembled to confirm which specific built-in methods,
  if any, those calls target).

## Extreme-Pamplona-specific blockers (zero effect on any Hobo game)

`DefineShape4` (135 occurrences in the loader alone), `PlaceObject3` (345
occurrences — every one potentially carrying blend-mode/filter data this
runtime drops), `CsmTextSettings` (37 occurrences, text-quality-only, not
functional), two genuinely unrecognized tag IDs (253/255, ~126+1
occurrences, purpose not identified), and the entire multi-SWF/`loadMovie`
architecture (23 content sub-SWFs never loaded — asset/animation content
only, per that phase's own sub-SWF analysis, so this blocks *content*
availability, not core interactivity, but blocks it completely: no level
geometry, no player sprite, no music/sound banks reach the runtime at all
without it).

## Hobo-family-specific blockers (zero effect on Extreme Pamplona)

`DefineMorphShape`/`2` (16-27 characters per file, recognized by tag code
but never resolved into `CharacterDictionary` — simply invisible, not
mis-rendered).

## What would move each game the furthest, ranked by this matrix

1. **Verify + fix the event dispatcher against real Hobo content** (Phase
   1, near-zero new code expected, pure verification+bugfix) — if it
   works, unblocks all 7 Hobo titles' title-screen interaction
   simultaneously, the single highest game-count leverage available.
2. **MP3 decode** — audible sound for all 8 games simultaneously; second-
   highest leverage, independent of #1.
3. **`loadMovie`/multi-SWF** — the only way Extreme Pamplona becomes
   playable at all (its levels/player/music/sounds are 100% external to
   the loader); zero effect on any Hobo game.
4. **`DefineMorphShape` rendering** — cosmetic-completeness win across all
   7 Hobo games; not a functional blocker (nothing crashes or fails to
   progress without it).
5. **`DefineShape4`/`PlaceObject3`/`CsmTextSettings`** — Extreme-Pamplona-
   only, meaningful visual-completeness wins once loadMovie exists to
   even reach that content.
