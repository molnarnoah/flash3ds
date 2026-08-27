# Hobo1 pixel-level playability verification — Track A A1/A2 finding (2026-08-27)

This is the direct pixel-level follow-up `docs/hobo-title-progression.md`'s
own "What this probe cannot determine" section called for: that phase
(call-trace/root-timeline only, no rendering) left open whether the
already-polling `Key.isDown()` game loop is simply playable underneath
whatever visual "title card" a player sees, with no dismissal required at
all. This phase answers that with real rendered pixels, across both
keyboard and mouse input, and the answer is a well-evidenced **no** for
every input tried so far.

## Method

`tools/real_game_harness/hobo_playability_probe.cpp` (new) loads real
`hobo.swf` (`/home/claude/hobo-testing/hobo.swf`), builds the full
`MovieClipInstance`/`SceneRenderer`/`SoftwareRenderer` stack (the same
pattern `tools/flash_runtime/main.cpp --render` uses), and renders real
PPM frames across 90 ticks (sampled every 10) — once while holding
`Key.LEFT`/`Key.UP`/`Key.RIGHT`/`Key.DOWN` (codes 37/38/39/40) plus literal
`'A'`/`'S'` (65/83) down every tick (the exact key set
`hobo-title-progression.md` found this file polling from tick 0), and once
as a same-tick-count no-input control. `tools/real_game_harness/
compare_playability_frames.py` (new) diffs the two PPM sequences
byte-for-byte per sampled tick, plus each run against its own tick-0 frame
(to separate "does input change anything" from "does anything change at
all").

A follow-up tool, `tools/real_game_harness/hobo_movement_key_trace.cpp`
(new), captures the actual AVM1 call trace
(`ScriptEnvironment::callTraceSink`) for the same held-vs-control
comparison, to see *why* pixels don't move even though the keys are read.
A one-off ad hoc probe (not added to the tree — see below) additionally
tested each of the 6 keys **individually** rather than all held at once,
and `click_probe.cpp` was run in a 108-point grid sweep across the full
600×450 stage (every 50px) simulating a real hover→press→release at each
point, to test mouse input exhaustively rather than guessing coordinates.

## Finding 1: what's on screen is a title/instructions card, not gameplay

Rendering tick 0 shows a static card: the "HOBO" wordmark, a "CONTROLS"
panel with icon glyphs, the idle character art, and (per Phase 9's own
already-documented finding) "PLAY MORE GAMES!"/Armor Games branding text.
By tick ~10 (0.4s), a red "PLAY!" prompt fades/pops in near the bottom of
the character panel — this is the same appearance Phase 9's original
compatibility report and the later compatibility-audit phase both noted
("PLAY! button fading in by frame 5") without identifying its cause. It is
now fully characterized here: **it happens identically whether any input
is held or not** — it is a self-timed intro animation, not a response to
anything the player does.

## Finding 2: holding the documented gameplay keys produces ZERO pixel difference from doing nothing

Every one of the 10 sampled ticks (0, 10, 20, ..., 89) is **byte-identical**
between the held-keys run and the no-input control. This holds for:

- All 6 keys held together (the original A1 test, matching
  `hobo-title-progression.md`'s exact polled key set).
- Each of the 6 keys held **individually** (Left/Up/Right/Down/'A'/'S',
  tested separately, 60 ticks each) — ruling out "holding all 4 directions
  at once creates a contradictory/cancelling state that masks a real
  single-key effect."

Meanwhile, the movie is demonstrably **not static**: `control[tick0]` vs
`control[tick89]` differs by 5,306 px (1.97%, bbox
`(173,120)-(297,350)`) — exactly the "PLAY!" fade-in plus a small
repeating idle animation in the character's head/hair region (confirmed
via consecutive-tick diffing: a jump at tick 0→10, another at 10→20 and
again at 50→60, static in between — a looping idle-blink cadence, not
continuous motion). The **same** diff, same bounding box, same byte count,
appears in the held run — proving the animation is entirely independent of
input, not gameplay the input fails to influence differently.

## Finding 3: the key polls ARE read by AVM1 and DO branch control flow — but produce no traceable display-affecting action

This is the part that could have been "the input isn't reaching the game"
— it is not. `hobo_movement_key_trace` (30 ticks) shows:

- **149** `Key.isDown()` calls in the held-keys run vs **240** in the
  control run — a real, reproducible difference in how many `isDown()`
  checks execute per tick, meaning the interpreter's control flow
  genuinely branches differently depending on real key state (almost
  certainly an `if (isDown(RIGHT)) ... else if (isDown(LEFT)) ...`-shaped
  chain that short-circuits once an early branch is true).
- Despite that branching, **zero** `SetProperty`/`SetMember` calls
  targeting any underscore-prefixed AS2 display property (`_x`, `_y`,
  `_alpha`, `_visible`, `_rotation`, `_xscale`, `_yscale`, etc.) occur in
  either run, and **zero** difference in `CallMethod`/`NewObject`/
  `CallFunction`/`NewMethod`/`GetURL` counts or shapes beyond the
  `isDown()` count itself — same single `gotoAndStop()` call in both runs,
  same 31 `new Sound()` calls, same 30 `setVolume(100)` calls.

To make this check possible at all, this phase added diagnostic-only trace
hooks for the legacy `SetProperty` opcode and for underscore-prefixed
`SetMember` writes in `src/avm1/Interpreter.cpp` (guarded by
`if (ctx.callTraceSink)`, identical pattern to the existing `CallMethod`/
`NewObject`/`CallFunction`/`NewMethod`/`GetURL` trace call sites — **no
behavior change**: these fire only when a sink is installed, and no
runtime code installs one outside these diagnostic tools). Before this
addition, a real per-tick property mutation on a display object would have
been **completely invisible** to every existing trace-based tool
(`hobo_end_key_probe`, `avm1_runtime_trace`) — this was a real blind spot
in the tracing infrastructure, now closed. 368/368 tests still pass, zero
new compiler warnings.

## Finding 4: no point on the entire stage responds to a mouse click either

`click_probe.cpp` run across a 108-point grid (every 50px, full 600×450
stage) with a real hover→press→release sequence at each point: **every
single point** produces the identical after-render MD5
(`04e44f2d43ef5e954e72c6c65cac6f4b`) — indistinguishable from the 3-tick
ambient-animation baseline. This generalizes `docs/real-game-readiness.md`'s
earlier, narrower finding (none of Hobo1's 3 documented frame-1
`DefineButton2` characters carry a mouse-transition `condActionsV2`
condition) to **no coordinate on the stage triggers any observable
click-driven effect at all**, whether via `condActionsV2` or the (already
implemented, see `src/runtime/MovieClipInstance.cpp`'s `onPress`/
`onRelease`/`onRollOver`/`onRollOut` property-handler dispatch) generic
mouse-event mechanism.

## What this rules out, and what it doesn't

**Ruled out** (with concrete evidence, not assumption):

- Input not reaching the interpreter — false; `isDown()` call counts
  provably differ between held and control.
- A "hold all 4 directions confuses the logic" artifact — false; each key
  individually also produces zero pixel difference.
- The originally-flagged tessellation/multi-contour shape bug
  (`docs/renderer.md`) — **not implicated by this finding at all**. The
  rendered title card (character art, "HOBO" wordmark letterforms
  including the ring-shaped "O"s, the CONTROLS panel icons) shows no
  visible tessellation artifacts, and the actual problem A1 surfaced is
  behavioral (no input produces a traceable effect), not a rendering
  defect. **A2, as originally scoped around the tessellation bug, does
  not apply here** — there is nothing to confirm-and-fix in that specific
  area for this finding.
- Mouse click dispatch being unimplemented — false (contradicts a stale
  note in `CLAUDE.md`'s older "Compatibility-audit phase" section, which
  predates work that evidently landed later); `onPress`/`onRelease`
  dispatch demonstrably exists in `MovieClipInstance.cpp` — it simply has
  nothing on this screen assigned to respond to it, corpus-wide (per
  Finding 4 and `real-game-readiness.md`'s button census).

**Still open** (needs real investigation before more engine code is
written, not a guess):

- What input, if any, this screen actually expects. Candidates not yet
  tested: `Enter`/`Space` (common "press any key to start" idiom, distinct
  from the polled gameplay keys), a `getBytesLoaded()`/`getBytesTotal()`
  streaming-load-completion gate (unlikely to matter here since this
  runtime loads the whole file synchronously before any tick runs, but not
  independently confirmed against the actual bytecode), or literally no
  further trigger exists and what's rendered as a static "title card" is
  simply frame 1's permanent decoration with the real game logic
  (elsewhere in this movie's 2,884 `DoAction` buffers) driving something
  this specific probe never observes because it's gated behind a state
  variable this short window never flips.
- Whether the specific `DoAction` buffer(s) that call `Key.isDown(37/38/
  39/40/65/83)` are reachable from disassembly to directly read what their
  `if`-branch bodies actually do (a static-disassembly read, not another
  dynamic probe) — this would settle Finding 3's open question
  definitively rather than inferring it from absence of traced side
  effects.

## Recommendation

Do not proceed to Track A3-A5 (3DS entry-point wiring / on-device run /
packaging) on top of this content yet — shipping a build where no tested
input produces any observable effect would not be "a first playable game,"
it would be a static screen. The next concrete step (not started this
phase, to avoid scope creep past what A1 itself actually found) is a
static-disassembly read of the specific `DoAction` buffer(s) identified by
`button_debug.cpp`/`swf_diagnostic` as containing these `Key.isDown()`
polls, to find out what their branch bodies actually do and whether a
different, untested input (most likely `Enter`/`Space`, or a state
variable set by something outside this probe's 90-tick window) is the real
gate.

## Regression / build

Three new tools, all `FLASH3DS_BUILD_TOOLS` targets in `CMakeLists.txt`:
`hobo_playability_probe`, `compare_playability_frames.py` (not a build
target — a plain script, invoked directly), `hobo_movement_key_trace`. Two
small diagnostic-only trace additions in `src/avm1/Interpreter.cpp`
(`SetProperty`, underscore-prefixed `SetMember`). Full clean rebuild: zero
warnings. `ctest`/`flash3ds_tests`: 368/368 passing, unchanged from before
this phase (no new unit tests added — this is a real-corpus investigation
matching the pattern of every other `real_game_harness` tool, not new
production functionality).
