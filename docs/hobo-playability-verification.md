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

## Addendum — 2026-08-27, static disassembly closes Finding 3 (task #66)

The prior section's open question ("what do the `Key.isDown()` `if`-branch
bodies actually do") is now answered by directly reading them, not
inferred from absence of traced side effects — and the answer explains
Finding 2's zero-pixel-effect result completely, plus surfaces a second,
still-open engine question that blocks Track A3-A5 for a different reason
than originally suspected.

### The tool gap that hid the answer

`tools/real_game_harness/avm1_loader_disasm.cpp` (a pre-existing, not-yet-
CMake-registered static AVM1 disassembler) only ever walked `DoAction`/
`DoInitAction` tag bodies. Hobo1's `onEnterFrame`/`Key.isDown()` polling
lives inside `PlaceObject2`'s own `ClipActionRecord` bytecode (Phase 6's
`onClipEvent` mechanism — see `src/swf/PlaceObjectTag.h`), which the tool
never scanned at all — a structural gap, not a bug in what it did scan.
Fixed by extending the tool to also parse each `PlaceObject2` via
`swf::parsePlaceObject()` and disassemble every `ClipActionRecord.
actionBytes`, labeled with its event flags, placement depth/name/
characterId, and record index. Also fixed two smaller gaps found along the
way that would have corrupted or hidden output once the ClipActionRecord
scan started surfacing real conditional code: the `If` action (0x9D) wasn't
popping its one condition value (silently desyncing the symbolic stack for
the rest of every conditional script — nearly all of them), and `Jump`,
`GotoFrame` (0x81), `SetTarget` (0x8B), and the bare `Play`/`Stop`/
`NextFrame`/`PreviousFrame` action codes were all silently dropped instead
of emitted. Finally, a *third* previously-unscanned bytecode container was
added: `DefineButton`/`DefineButton2`'s own click-handler bytecode
(`ButtonCondAction` records for v2, the single implicit block for v1) —
this lives inside the character *definition* itself, not in any
`DoAction` tag or `ClipActionRecord`, and turned out to matter a lot (see
below).

### Finding 5 — the real "hobo" is a different character on a different frame

Re-running the extended disassembler against `hobo.swf` finds every
`Key.isDown()` call from Finding 3, and this time shows what surrounds
them. Two entirely different characters are involved, both named/known as
"hobo" in some sense, on two different root frames:

- **Root frame 1** (the screen A1/A2 tested): an *unnamed* clip,
  characterId=80, whose `onClipEvent(enterFrame)` handlers check
  `Key.isDown(37/38/39/40/65/83)` and, on each, call
  `_root.hobo.gotoAndStop(N)` (movement/facing frames) or set
  `_root.punchallowed`/`kickallowed` flags (attack gating). This is a
  decorative title-screen preview icon, not the player character.
- **Root frame 10**: a real, *named* `"hobo"` clip, characterId=1913 —
  confirmed via a small standalone frame-locator diagnostic that counts
  `ShowFrame` tags against each `PlaceObject2`'s absolute tag offset — is
  placed here alongside `enemy1`, `bg1`/`bg2`/`fg1`/`fg2` (parallax
  layers), `hobohud`, a `"levels"` menu (29 `ClipActionRecord`s), `"go"`,
  and other unmistakably-real-gameplay objects. This "hobo" instance's own
  `onClipEvent(enterFrame)` handlers set `_root.downallowed`/
  `upallowed` and call `this.swapDepths(this._y)` — real player-character
  logic.

Since `_root.hobo` does not exist as a display-list member until root
reaches frame 10, every `_root.hobo.gotoAndStop(N)` call fired by the
frame-1 preview icon during A1/A2's 90-tick test is a silent no-op against
an undefined target (matches the pre-existing `[WARN] [AVM1] CallMethod:
target is not an object` log line, previously unexplained) — **this is
the full, direct explanation for Finding 2's zero pixel effect.** Holding
every movement key really did dispatch real branching AVM1 code (Finding
3's own trace numbers were correct), but that code's entire effect was
aimed at a MovieClip that will not exist until 9 more root frames are
reached, which nothing in the 90-tick/click-sweep window ever reached.
This also resolves the open item this document's Recommendation section
flagged as the next step — done, via static disassembly exactly as
proposed there.

### Finding 6 — a plausible frame1→4→10 progression path, and a new open question

Frame membership was checked for every relevant placement (same
frame-locator diagnostic): root frame 1 places the title/instructions
screen, frame 4 places a 40-frame, unnamed sprite (characterId=1275) whose
own local timeline ends in a `DoAction` calling `_root.gotoAndStop(10)`
(along with resetting `_root.hobo._x/._y` to a spawn point and various
combo/state flags) — consistent with a "get ready"/transition clip. What
would carry root from frame 1 to frame 4 was the open question the new
`DefineButton2` scan answered: nearly every button in `hobo.swf` (13
`ButtonCondAction` records sampled) is gated by `CondKeyPress=4` ("End"
per the SWF spec's key table) with **zero** mouse-transition bits — this
specific pattern was already known real and dispatching correctly (see
`docs/hobo-title-progression.md`'s Roadmap Phase 7 finding), but that
investigation characterized its only observed effect as "a pause/quit-to-
portal menu." The new disassembly shows that conclusion was incomplete:
different End-key buttons, placed on different root frames, call
`_root.gotoAndStop(2)`, `(3)`, `(6)`, `(9)`, **`(10)`** (real gameplay!),
`(11)`, `(12)`, `(13)` — i.e. **End appears to be the game's actual
"confirm/continue" screen-navigation key**, not a Hobo1-specific quirk.
Concretely, the button whose action is `_root.gotoAndStop(2)`
(characterId=32, byte-level-verified: raw tag body bytes `00 00 08 00` at
its `ActionOffset` decode to `condActionSize=0`, `rawConditions=0x0008` →
`CondKeyPress=4`, zero mouse bits, exactly as disassembled) is placed one
level nested inside the "preloader" sprite (characterId=33, root frame 1,
depth 27) — i.e. pressing End while the preloader is up is a plausible
first domino toward frame 4, then frame 10.

Re-running the existing `hobo_end_key_probe.cpp` (tap-only End, 90 ticks)
against real `hobo.swf` confirms the call **fires**: its trace shows
`CallMethod [object Object].gotoAndStop(2)` immediately after the tap,
alongside a mute (`setVolume(0)` in place of `setVolume(100)`) and a
`GetURL` to armorgames.com from a sibling End-triggered button on the same
frame — matching Phase 7's "mute audio... open the armorgames.com portal
link" observation exactly, and very plausibly explaining its "freeze an
overlay clip at frame 2" observation too (i.e. Phase 7 likely already saw
this exact effect and described it in different terms before this static
evidence existed to name it). **But** `root->timeline().currentFrame()`
never leaves 1 across all 90 ticks in that same run — the call happens,
per the trace, yet the root timeline does not visibly move.

Two new regression tests were written to isolate this
(`tests/test_event_dispatch.cpp`:
`EventDispatch_CondKeyPress_RootGotoAndStop_MovesRootPlayhead` and
`..._FromNestedButton_MovesRootPlayhead`), reproducing the identical
shape — a `CondKeyPress`-only button, calling `_root.gotoAndStop(2)` via
`CallMethod`, both placed directly on root and nested one level inside
another clip (matching the real preloader→button nesting exactly). **Both
pass** — the engine moves the root playhead correctly in isolation. This
rules out the two most likely culprits (broken `_root` resolution from a
nested execution context; broken keyboard-triggered dispatch through a
nested button) and narrows the real-corpus-only discrepancy to something
that only manifests with `hobo.swf`'s full complexity — most likely
another End-key or `EnterFrame` handler elsewhere in the same tick
reverting the goto, or an interaction with the preloader's own bare
`GotoFrame`/`Play`/`Stop` action-code bytecode (now visible via this same
disassembly pass, previously silently dropped by the tool). Filed as task
#68.

## Addendum — 2026-08-27, task #68 closed: root cause found, fixed, and the fix confirmed against real content

Task #68's investigation (full writeup: `docs/known-limitations.md` L11)
found and fixed a genuine, previously-untested, systemic interpreter bug:
`MovieClipHostBindings::gotoFrame()`/`gotoLabel()` (the handlers for bare
`ActionGotoFrame`/`ActionGotoLabel`, which back `gotoAndPlay(literalFrame)`
in its standard AS2-compiler form) were calling `Timeline::gotoAndStop()`
directly, unconditionally force-stopping the target timeline. This is
exactly what was silently keeping preloader's own local timeline stuck:
its frame-1 script is `gotoAndPlay(3)`, compiled as bare `GotoFrame` with
no trailing `Stop`, so it was being force-stopped the instant it ran, and
`Timeline::advanceOneFrame()`'s `!playing_` guard then permanently blocked
any further auto-advance past local frame 1.

**This section's own prior framing needs one correction first:** the
"the call happens, per the trace, yet the root timeline does not visibly
move" observation above was itself built on a misattribution, caught via
pointer-identity debug tracing during task #68's investigation (temporary
`fprintf` instrumentation on live `Timeline*` addresses, reverted before
the real fix). Every occurrence of the recurring `gotoAndStop(2)` trace
line this document and `docs/hobo-title-progression.md` had observed
turned out to land on `"mutebutton"` (root's own pause/mute overlay,
depth 313) — never on character 32's actual `_root.gotoAndStop(2)` call
at all. The two regression tests cited just above (isolated, minimal
fixtures) were correctly passing the whole time; they were simply testing
a mechanism that was never actually the one running in the real-corpus
probe.

**With the real fix applied and re-verified against real `hobo.swf`:**
preloader's own local timeline now correctly reaches local frame 4 (where
character 32's button lives) and cycles as a normal loading-spinner
animation (`3 -> 4 -> 3 -> 4 ...`), exactly as its bytecode intends.
Character 32's real, byte-verified `_root.gotoAndStop(2)` button was
confirmed to genuinely fire and move ROOT's own `currentFrame()` from 1 to
2 — the first time this investigation ever observed root's own timeline
move — once the End-key press edge was timed to land on a tick where
preloader had already reached local frame 4 (a standard `hobo_end_key_probe`
tap-on-tick-0 run misses this window; see `docs/known-limitations.md` L11's
"narrower, separate note" for the exact per-tick ordering reason why, which
is documented as a real, demonstrated interaction rather than "fixed",
since it isn't yet known whether it's spec-accurate).

**Answer to this document's own open question:** yes, the engine's
`_root`/nested-button/`CondKeyPress` machinery was correct the whole time
(as the isolated tests already showed); the real-corpus blocker was a
genuine interpreter bug, now fixed, and the specific frame1(local)→4(local)
→`_root` frame 2 hop this document traced is now demonstrated working
end-to-end for the first time. **A3 (#58) is no longer blocked on this
task** — see `docs/hobo-title-progression.md`'s own established finding
that Hobo1 doesn't need a "title screen dismissed" gate at all (gameplay
key-polling is already running from tick 0 regardless); this fix closes
the one concrete interpreter-level doubt that had been holding A3 back
pending real evidence the engine could actually execute this exact
gotoAndPlay/CondKeyPress/nested-button chain correctly. Whether the full
frame1→4→10 chain documented in Finding 6 above plays out further (the
"levels" menu, `_root.hobo` at frame 10, etc.) is separate, un-investigated
follow-up work, not part of #68's scope.

### Regression / build (this addendum)

Fix: `src/runtime/Timeline.h/.cpp` (new neutral `Timeline::gotoFrame()`
pair) and `src/runtime/MovieClipInstance.cpp`
(`MovieClipHostBindings::gotoFrame()`/`gotoLabel()` rewired to call it).
Three new regression tests in `tests/test_movieclip_instance.cpp` (see
`docs/known-limitations.md` L11 for their names and what each proves).
Full clean rebuild: zero warnings. `ctest`/`flash3ds_tests`: 374/374
passing (up from 371 — the 3 new tests here), zero regressions.
Re-verification against real `hobo.swf` used temporary, reverted-before-
commit debug instrumentation only (no permanent runtime behavior changed
beyond the fix itself) — read-only per this project's standing rule.

### Regression / build (the static-disassembly addendum, 2026-08-27)

`tools/real_game_harness/avm1_loader_disasm.cpp`: added `PlaceObject2`/
`ClipActionRecord` scanning, `DefineButton`/`DefineButton2`/
`ButtonCondAction` scanning, and `If`/`Jump`/`GotoFrame`/`SetTarget`/
`Play`/`Stop`/`NextFrame`/`PreviousFrame` opcode emission (still not
CMake-registered — built/run standalone via `g++`, matching how this tool
has always been used). Two new tests in `tests/test_event_dispatch.cpp`.
Full clean rebuild: zero warnings. `ctest`/`flash3ds_tests`: 371/371
passing (369 baseline as of the prior commit + the 2 new tests here — the
369 baseline is itself 1 higher than this document's earlier "368/368"
line, from a test added by the sibling Track B commit made the same
session after that line was written).

## Addendum — 2026-08-29: realistic repeated-tap simulation answers Finding 6's open question, negatively

Finding 6's addendum above confirmed character 32's `_root.gotoAndStop(2)`
button (nested in `preloader`, gated on `CondKeyPress=4`) genuinely fires
and moves root's own playhead — but only once, using a single End-key
press hand-timed to land on the exact tick `preloader`'s own local
timeline was sitting on local frame 4. It explicitly left open whether
ordinary, repeated player input (not one lucky timed press) can reach the
same window in practice.

`tools/real_game_harness/hobo_frame_progression_probe.cpp` (built,
uncommitted at the time the environment reset below hit — recovered from
disk and re-verified 2026-08-29) answers this: it taps End every other
tick (a real press-EDGE every 2 ticks, not one continuous hold) for 1,200
ticks (~48s of simulated real-time at hobo.swf's frame rate) against real
`hobo.swf`, holding the documented movement keys throughout. Result:
**root's own `currentFrame()` never leaves 1 across all 1,200 ticks and
5,411 trace lines.** Every single `End`-press edge in the entire run is
consumed by `"mutebutton"` (characterId=91, depth=313, root's own always-
live mute-toggle overlay) — its `CallMethod ....gotoAndStop(1/2)` calls
are `mutebutton`'s own two-frame icon toggle (muted/unmuted icon), not
`_root`'s. Character 32 (the actual `_root.gotoAndStop(2)` button) never
fires even once in this run — `grep -c characterId=32` on the full trace
is 0. `preloader`'s own local timeline does cycle correctly (`3 -> 4 ->
3 -> 4 ...`, the fix from the addendum above working exactly as intended)
and character 32's button genuinely IS on preloader's local frame 4 when
End is pressed on several of those ticks — but `mutebutton`'s own,
timing-independent End handler appears to take the press before/instead
of character 32's nested one on every single occurrence in this run, not
just some.

**Reconciling with the single-press addendum finding above:** both are
real and reproducible; they aren't in conflict. The single-press case
does move root. What this addendum adds is that under sustained, realistic
repeated input, the path is not merely "narrow" but was not observed to
fire even once in 1,200 ticks — i.e. as far as this simulation shows, an
ordinary player using End normally will only ever see the mute toggle and
the armorgames.com portal link, never root frame 2.

Combined with Finding 4 above (no point on the entire stage responds to a
mouse click, 108-point grid, already ruling that path out) and Finding 2
(all 6 documented movement keys, individually and combined, produce zero
pixel/display effect), **the honest current status is: no known,
reliably-reproducible input sequence takes a player from Hobo1's title
screen into gameplay.** This reproduces identically in the desktop engine
(not a 3DS-specific input bug) — confirmed independently by a real user's
own on-hardware/Azahar test session the same day, holding End (`SELECT`
in the corrected input mapping) continuously and observing the same
"stuck" behavior this probe predicts for a hold (a hold produces exactly
one press-edge at tick 0, and tick 0 is essentially never the tick
`mutebutton` happens to lose the race to character 32, if it ever does).

**Not yet investigated (real next steps, not guesses):**
1. Whether AVM1 button/clip dispatch order should give a same-frame,
   same-key priority to a MORE DEEPLY NESTED button (character 32, nested
   one level inside `preloader`) over a ROOT-level sibling
   (`mutebutton`) — if real Flash Player's actual dispatch order differs
   from this engine's, that would be a genuine, fixable interpreter bug,
   not a content limitation. This needs checking against the SWF
   spec/real Flash Player behavior, not assumed.
2. Whether the original browser game genuinely requires End specifically
   (vs. this being one of several redundant paths in the original,
   e.g. a real mouse-driven "PLAY" flow this project's engine doesn't yet
   reproduce for some other reason not yet identified).

### Regression / build (this addendum)

No source changes this addendum — `hobo_frame_progression_probe.cpp` and
its `CMakeLists.txt` registration already existed (built earlier in the
session that hit the environment reset documented in
`docs/virtual-console.md`; recovered from disk, since untracked files
survive a git-history revert the same way uncommitted `Edit`-tool changes
do). Full clean rebuild: zero warnings. `ctest`/`flash3ds_tests`: still
passing at whatever count the current commit reflects (see this repo's
`git log` for the exact number — this addendum doesn't add or remove
tests).
