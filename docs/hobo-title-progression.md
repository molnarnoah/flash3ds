# Hobo title-screen progression trigger — Phase 7 finding (2026-08-25)

Roadmap Phase 7 (`docs/implementation-roadmap-2026-08-21-part2.md`, "Resolve
Hobo's title-screen progression trigger") asked a verification question:
`button_scan`/`button_debug` had already established (see
`docs/hobo_button_diagnostic.txt`) that every `DefineButton2` on hobo.swf's
frame 1 carries a `condActionsV2` record whose only condition is a bare
`CondKeyPress` with zero mouse-transition bits, and that field is `4` in
every single case — the SWF spec's own legacy key-code table (NOT AS2's
`Key.*` class constants — see `condKeyPressToInputKeyCode()`'s doc comment
in `src/runtime/MovieClipInstance.cpp`) maps `4` to `End`. The prior audit
(`docs/current-state-audit-2026-08-21-part2.md` §I) recorded that "a real
`CondKeyPress` ('End') trigger produces a measurably different render" but
left open *what* that difference actually is, and whether it's really the
title-screen-progression trigger. This phase answers both.

## Method

A new read-only tool, `tools/real_game_harness/hobo_end_key_probe.cpp`
(built as `hobo_end_key_probe`), loads real `hobo.swf`
(`/home/claude/hobo-testing/hobo.swf` in this environment — not part of the
repo; a copy also lives in `/mnt/user-data/uploads/`), installs
`ScriptEnvironment::callTraceSink` (the same runtime-resolved AVM1 call
trace `avm1_runtime_trace.cpp` uses), and ticks `MovieClipInstance::
advanceFrame()` many times (default 90 — well past the movie's own
`FrameCount=13`) while holding a key down, reporting every tick's root
`Timeline::currentFrame()` and display-list size plus the full call trace.
A second, identical run holds an inert key code (`999`, mapped by nothing)
as a control, since nested clips tick their own animation every frame
regardless of input (the same pitfall `click_probe.cpp`'s header comment
already documents) — only a difference *from the control*, not from
"before", is real evidence of an effect. A `tap` mode additionally presses
End on tick 0 only and releases it immediately, to distinguish "needs the
key held" from "one-shot state flip."

## Finding: End is a pause/quit-to-portal trigger, not a "start game" trigger

Holding (or even just tapping once) `InputState::kEnd` produces a real,
reproducible difference from the control run, visible in the AVM1 call
trace from the very first tick onward:

- Two `GetURL url="http://www.armorgames.com" target="_blank"` calls fire
  once, immediately after the key-press tick's button dispatch.
- A nested clip's `gotoAndStop(2)` is called on **every** subsequent tick
  (92 times across a 90-tick hold; still firing after a single-tick
  **tap** with the key released for the remaining 29 ticks of a 30-tick
  run) — the control run calls `gotoAndStop` exactly once, at frame-1
  setup, never again.
- A `Sound` object's `setVolume(0)` (muted) is called every tick from then
  on, versus the control run's steady `setVolume(100)`.

Because the tap-only run shows the exact same persistent every-tick
`gotoAndStop(2)`/`setVolume(0)` pattern as the held-down run, this is a
**one-shot state flip**, not a level-triggered "while End is held" effect:
the button's `condActionsV2` handler (dispatched once, on the press edge,
via `MovieClipInstance::dispatchButtonKeyPressesRecursive()`) almost
certainly sets a persistent flag (e.g. a `_root`-scoped `paused` variable)
that the game's own already-running per-tick loop then reads every tick
independently — no `Key.isDown()` call for End/code 4/code 35 appears
anywhere in either trace, confirming the per-tick repetition is state-
driven, not a direct key poll.

Taken together — mute audio, freeze an overlay clip on frame 2, and open
the game portal's own URL — this is the signature of a **pause menu with a
"back to portal"/sponsor link**, a common pattern for games embedded on a
Flash portal (armorgames.com here), not a "dismiss title screen and start
playing" trigger. The original audit's "measurably different render" was
real, but its likely cause is this pause overlay appearing, not the game
becoming playable.

## Second finding: there may be no real gate to solve

In both the End and the control run, the root timeline's `currentFrame()`
stays at `1` for all 90 ticks — `.stop()` is called once at the very start
(the trace's first entry in every run) and nothing ever calls
`gotoAndPlay`/`gotoAndStop` on the root. More notably, **gameplay-shaped
input polling is already happening from tick 0 in both runs**, regardless
of End: `Key.isDown(37/38/40)` (left/up/down arrows), `Key.isDown(65)`
("A"), `Key.isDown(83)` ("S"), `Key.isDown(39)` (right arrow) are polled
every tick from the start, alongside a fresh `new Sound()` most ticks. This
is the shape of an already-running `onEnterFrame` game loop, not a loop
gated behind a frame-advance or a menu dismissal.

This means the premise behind Phase 7 — that pressing some key
"progresses past the title screen" via a root-timeline frame change — does
not hold for hobo.swf as actually traced: there is no frame-13-to-later
transition to find, because the root timeline never leaves frame 1 at all,
by design (a single-frame Flash game, the common "everything lives in
frame 1, `onEnterFrame` drives the whole loop" architecture). Whatever
visual "title card" a player sees is most likely a nested clip's own
overlay state, already coexisting with a live, already-polling game loop
underneath it — consistent with `click_probe`'s original mouse-click
findings from `docs/real-game-readiness.md` (this file's own frame-1
buttons don't respond to mouse events at all, only to the End keypress,
and now we know what End actually does).

**What this probe cannot determine** (root-timeline/call-trace visibility
only, no pixel rendering was captured this pass): whether the nested
"title card" overlay clip is dismissed by some *other* input this
investigation didn't test (a different key, a click on a specific hit
region `click_probe`'s coordinate sweep didn't happen to hit, or simply
time/`getBytesLoaded`-based auto-dismissal), or whether — given the
already-active `onEnterFrame` polling — the game is simply playable
underneath the overlay without any dismissal being strictly required by
the engine at all. Confirming either would need an actual pixel-level
render (`SceneRenderer`) diff across several untested keys/clicks, which
is out of this phase's scope (a verification pass, not a rendering
investigation) but would be a reasonable, narrowly-scoped follow-up if a
specific game session still looks visually stuck.

## Answer to Phase 7's question

**No single key was found that progresses Hobo1 past a title screen**,
because no such root-timeline gate exists to progress past — End is
confirmed, with concrete reproducible evidence, to be a pause/portal-link
trigger instead. This is an honest negative result on the original
framing, not a runtime gap: `condActionsV2`/`CondKeyPress` dispatch is
demonstrably working correctly (this is precisely how we could observe
End's real effect at all). Per the roadmap's own Phase 7 completion
criteria, this finding should feed back into Phase 8's scoping rather than
be treated as a blocker to resolve.

## Regression / build

`hobo_end_key_probe` is a new, always-built `FLASH3DS_BUILD_TOOLS` target
(`CMakeLists.txt`), read-only (no runtime behavior changed by this phase).
Full rebuild: zero warnings. `ctest`: 352/352 passing, unchanged from
before this phase (this phase added no new unit tests — it is a real-
corpus investigation tool, matching Phase 7's own spec, which calls a
"targeted `real_game_harness` invocation" itself the test).
