# Hobo1 7–12 FPS pacing investigation (2026-08-28)

Task: resolve the 7–12 FPS observed in a ManicEmu screen recording of the
Hobo1 `.3dsx` build (see `docs/hobo-playability-verification.md` and
`docs/virtual-console.md` §13 for the playability/packaging work this
follows on from). The recording's debug overlay showed, consistently
across both static and animated screens: App 7–12 FPS, frame compute time
~4.2–4.8ms, emulation speed 100%/100% (not host-throttled).

## Step 1 — declared FrameRate vs observed FPS

`hobo.swf`'s SWF header is already parsed into `Movie::frameRateFixed8`
(`src/runtime/Movie.h`, an 8.8 fixed-point value, exposed via
`frameRateFps()`) — confirmed directly by running any of the existing
`tools/real_game_harness/*` tools against the real file:

```
[INFO ] [SWF] FrameSize=600.0x450.0 px FrameRate=25.00 fps FrameCount=13
```

**Declared FrameRate is 25.00 fps.** The observed 7–12 FPS is meaningfully
lower — not the "this is just the authored tempo" case. Real mismatch,
proceeding to Step 2.

## Step 2 — looking for a fixed-interval throttle

Read `src/platform/nintendo3ds_main.cpp`'s tick/render loop in full (the
only place a fixed-interval throttle could live). **The existing
frame-rate-pacing logic is mathematically correct, not a bug:**

```cpp
const double swfFrameRate = movie->frameRateFps() > 0.0 ? movie->frameRateFps() : 12.0;
constexpr double kVBlankHz = 60.0;
int vblanksPerSwfFrame = std::max(1, static_cast<int>(std::lround(kVBlankHz / swfFrameRate)));
```

For 25.00 fps: `vblanksPerSwfFrame = round(60/25) = 2` — the movie
timeline advances every 2nd vblank (a 30fps tick, not slower than
declared), while `scene.render()` and both screens' present/swap happen on
**every** vblank regardless. As designed, this should yield a ~60Hz app
loop, not 7–12Hz. No hardcoded `sleep`/`svcSleepThread`, no independent
fixed-rate timer, no vsync/swap-interval misconfiguration anywhere in
`Nintendo3DSRenderer`/its `gfxInitDefault()` call site.

**No explicit throttle bug exists in this loop.** A real, evidence-based
negative result, not a dead end — it rules out the cheapest explanation
before spending anything more expensive, per this task's own instructions,
and points at Step 2's own fallback: broader (but targeted) profiling.

## Instrumentation added, and what it found

Since the recording's own numbers didn't add up on their own (~4.5ms
reported compute against an 83–142ms period leaves 80–135ms/frame
unaccounted for) and this sandbox has no 3DS hardware/emulator to profile
on directly, `nintendo3ds_main.cpp` was instrumented with real per-phase
timing (libctru `TickCounter`) covering `input`/`advance`/`renderTop`
(`SceneRenderer::render` on the top screen)/`renderBottom`
(`drawButtonTestScreen`)/`present` (`gfxFlushBuffers`/`gfxSwapBuffers`)/
`vblankWait`, plus the real measured loop period as ground truth. This is
drawn as horizontal bars in the bottom screen's unused middle column
(x=106..214) — visible directly in a screen recording, no debug log viewer
needed, following the same "no font rendering available" constraint
`showFatalErrorScreen()`'s counted-squares technique already established
in this file — and also logged via `LOG_INFO("PERF", ...)`.

**First version's mistake, and the fix:** the first build scaled each bar
by an absolute px/ms rate capped at 20ms, to guarantee nothing could
silently run off-panel. The very first real recording immediately
saturated that cap on BOTH the period bar (expected — 83–142ms is way past
20ms) AND the `renderTop` bar at the same time, which made it impossible
to tell whether `renderTop` was 20ms or 120ms — i.e. whether it explained
all of the missing time or just a fraction of it. **Fixed:** bars are now
scaled as a *fraction of the real measured period* (a fixed reference
width = 100% of one frame; each phase's bar length is
`phase_ms / period_ms` of that width), with the period itself drawn as a
brighter, always-full-width outline (the ruler, not a measurement) so
every phase bar is directly, unambiguously comparable to "the whole
frame."

**First real recording (pre-fix, absolute-scale build), read qualitatively
since the scale saturated:** on the Hobo1 title screen (static content),
the bars showed — top to bottom — `input` near-empty, `advance` short,
**`renderTop` full/capped**, `renderBottom` roughly 70–80% of the cap,
`present` near-empty, `vblankWait` roughly 40% of the cap, and the period
reference full/capped (as expected, since 83–142ms is always past a 20ms
cap). The key signal: `renderTop` (the actual SWF content
rendering — `SceneRenderer::render`, which drives `ShapeTessellator` and
`SoftwareRenderer`'s scanline fill) was the ONLY phase besides `period`
itself to hit the cap, while `present` (the buffer flush/swap syscalls)
was near-zero. That rules out the "slow per-frame syscall" explanation
this doc originally raised as equally plausible — `present` is cheap here.
It's consistent with the CPU-only software-rendering explanation: this
project makes zero GPU calls, so an emulator's "frame compute" stat (which
plausibly reflects GPU command time) would show ~0 for the real cost,
which is entirely inside `renderTop`'s CPU-side rasterization/tessellation
work. This is a real lead, not yet a confirmed root cause — the proportion
build (below) is what actually confirms or refutes it, since the capped
build couldn't distinguish "`renderTop` is most of the frame" from
"`renderTop` is merely `>=20ms`."

## Build/test

3DS cross-build: clean, zero new warnings/undefined symbols (same 8
pre-existing weak libctru/C++-runtime hooks). Desktop `ctest`/
`flash3ds_tests`: 382/382 passing throughout (this file is
`__3DS__`-guarded, so desktop is unaffected). A Hobo1-packaged `.3dsx`
with the proportion-scale instrumentation was built via the same
swap/build/verify/restore procedure `docs/virtual-console.md` §9b/§13
already established (`romfs/` restored and checksum-confirmed afterward)
and handed to the user.

## Second recording (proportion-scale build): renderTop confirmed dominant

The proportion-scale build's bars, read from the user's recording (title
screen, static content): `input`/`present` both near-empty (~1%),
`advance` ~4%, `renderTop` (yellow) ~60%, `renderBottom` (pink,
`drawButtonTestScreen` — the button/circle-pad/touch test picture PLUS
these timing bars themselves) ~16–18%, `vblankWait` (grey) ~6–8%. Those
six roughly sum to the full period (~90%+, within the imprecision of
reading bar lengths off a screenshot) — so the missing time from the
original recording's own numbers is NOT hiding in something this
instrumentation can't see; it's real CPU work, split mostly between
`renderTop` and `renderBottom`, with `present` (the once-suspected
per-frame syscall cost) ruled out as near-zero.

This confirms the "CPU-only software rendering" explanation from Step 2
above over the "slow syscall" one, and specifically points at `renderTop`
— `SceneRenderer::render()` on the top screen, i.e. `ShapeTessellator` +
`SoftwareRenderer`'s scanline fill — as the dominant single cost, at
roughly 60% of every real frame, **even on a static screen**. That's the
tell: a truly static picture shouldn't cost real CPU time to redraw
identically every tick, which is exactly what a naive
"tessellate-from-scratch on every render() call" implementation would do
regardless of whether anything changed.

## The fix: cache tessellated shape geometry per character

Confirmed in `src/renderer/SceneRenderer.cpp`:
`renderShapeCharacter()` called `tessellateShape(shapeDef.shape)` fresh on
every single call, once per placed shape character per `render()` (i.e.
up to 60×/sec per shape). `tessellateShape()` is a pure function of the
shape's own immutable local-twip-space geometry (`swf::ShapeDef`, parsed
once by `CharacterDictionary` and never mutated afterward — this runtime
has no drawing API that could change a shape's edges/styles post-parse);
the world transform is applied separately, AFTER tessellation, in
`toDevicePolyline()`. So the same `shapeDef` always tessellates to the
exact same `TessellatedShape`, frame after frame, whether or not the
picture on screen actually changed.

**Fix:** `SceneRenderer` gained a `shapeTessellationCache_` member
(`std::unordered_map<const swf::Shape*, TessellatedShape>`), keyed by the
address of the shape's own geometry (stable for the
`CharacterDictionary`'s lifetime — `parsedCharacters_` is itself an
`unordered_map`, whose element addresses survive insertion/rehashing, and
entries are never erased, per the M2/Roadmap-Phase-6 "no eviction"
decision already documented in `docs/memory-audit.md`).
`renderShapeCharacter()` now tessellates on a cache MISS only; a HIT costs
one hash lookup instead of a full re-tessellation.

**Correctness verified two ways**, not just assumed:

1. Rendered `hobo.swf` frames 1–5 through the desktop `flash_runtime
   --render` CLI before and after the fix — **byte-identical MD5s on every
   frame**, confirming the cache changes nothing about what gets drawn.
2. New regression test,
   `SceneRenderer_TessellationCache_RepeatedRenderStaysCorrectAndDistinguishesShapes`
   (`tests/test_scene_renderer.cpp`): two distinct shape characters,
   rendered twice through the same `SceneRenderer` (the second call is a
   pure cache hit for both), confirming the cache neither confuses two
   different shapes' geometry nor returns stale data on a hit.

**Desktop timing (informational only, not the real target):** a
200-iteration repeated-render micro-benchmark against `hobo.swf`'s title
screen showed only a modest ~10% improvement (1.08ms → 0.97ms per
`render()` call) on this x86_64 sandbox. That's a real but small number,
and it should NOT be read as "the fix barely helps" — desktop x86_64 (wide
superscalar, hardware FPU, large caches) and the 3DS's ARM11 (weak,
single-issue, in-order, no comparable FPU throughput) have very different
relative costs for floating-point-heavy tessellation math versus simple
per-pixel scanline writes, so this benchmark can't predict the on-device
delta — only an actual on-device (or emulator) recording can. That
recording is what actually confirms or refutes this fix, per the "test
something real, don't guess" standard this document has followed
throughout.

## Build/test (this fix)

383/383 desktop tests passing (up from 382 — the new cache regression
test), zero regressions, byte-identical render output. 3DS cross-build
clean, zero new undefined symbols. A Hobo1-packaged `.3dsx` with both the
proportion-scale instrumentation AND this fix was built via the
established swap/build/verify/restore procedure and handed to the user;
`romfs/` restored and checksum-confirmed afterward.

## What's needed to close this out

A recording of THIS build's bars (same procedure: hold steady on title
and on gameplay for ~5–8 seconds each). If `renderTop`'s share of the
period bar drops substantially and the real FPS climbs, this fix is
confirmed and this task is done. If `renderTop` is still large, the
remaining cost is inside `SoftwareRenderer`'s actual pixel-fill/blit path
rather than tessellation — a different, separately-scoped follow-up (not
blindly bundled into this fix, per this project's one-fix-at-a-time
discipline).

**Explicitly not done this task**, per its own scope: no blind
optimization committed to `src/` yet (the tessellation-caching fix above
is a hypothesis based on one qualitative reading of a saturated chart, not
yet confirmed), no touching input/navigation code (confirmed working per
`docs/hobo-playability-verification.md`), no RAM-related changes.
