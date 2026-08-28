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

## Third recording: tessellation cache fix confirmed to have no visible effect

A recording of the `v3_tesscache` build (proportion bars + the tessellation
cache) showed `renderTop` (yellow) still at roughly 60% of every frame —
essentially unchanged from the pre-fix (`v2`) recording, across a static
title screen (10 FPS), the Armor Games splash (12 FPS), the SeethingSwarm
splash (9 FPS), and the HOBO title screen (8 FPS). The FPS overlay reads
consistently in the same 8–12 band the original task started from.

This ruled out a wrong hypothesis carefully rather than just leaving the
fix in place unverified: could the fix simply not be taking effect
on-device (e.g. an unstable cache key causing perpetual misses)? Read
`CharacterDictionary::find()`'s return type directly —
`const CharacterDef*`, a pointer into `parsedCharacters_`'s stable
`std::unordered_map` storage, not a by-value copy — so `renderShapeCharacter()`'s
`&shapeDef.shape` cache key is stable across calls. The cache should be
hitting correctly. So the flat "no change" result isn't a caching bug —
it means tessellation genuinely wasn't the dominant cost inside the
bundled `renderTop` measurement to begin with, exactly the risk this doc
already flagged above.

## Sub-phase instrumentation: splitting `renderTop` into tree-walk / raster / blit

The single `renderTop` timer in `nintendo3ds_main.cpp` wraps the entire
`SceneRenderer::render()` call, which (per its own doc comment) calls
`Nintendo3DSRenderer::beginFrame()`/`endFrame()` on the top-screen renderer
itself. That one number was bundling together three very different costs
with no way to see their individual shares from outside:

1. The `MovieClipInstance` tree walk + character resolution + (now-cached)
   tessellation lookups, done in `SceneRenderer` itself.
2. `SoftwareRenderer`'s CPU scanline-fill rasterization — every
   `fillPolygon()`/`strokePolyline()` call `SceneRenderer` makes while
   walking the tree.
3. `Nintendo3DSRenderer::endFrame()`'s per-pixel blit loop, copying the
   finished software-rendered buffer into the real/emulated 3DS LCD
   framebuffer (one function call + 3 byte writes per pixel, up to
   400×240 = 96,000 times for the top screen).

**Added:** `Nintendo3DSRenderer` now times its own `fillPolygon()`/
`strokePolyline()` calls (accumulated per frame, reset in `beginFrame()`)
and its own `endFrame()` blit loop, exposed via `lastRasterMs()`/
`lastBlitMs()`. `nintendo3ds_main.cpp`'s main loop reads both right after
the `renderTop`-timed `scene.render()` call and threads them through
`PhaseTimingWindow` as `renderTopRasterMs`/`renderTopBlitMs`; the
remainder (`renderTop` minus both, clamped to ≥0) is the tree-walk cost.
The bottom-screen bar chart now draws these as three separate rows
(`renderTop-other` / `renderTop-raster` / `renderTop-blit`) instead of one
combined `renderTop` bar, and the `PERF` log line reports all three
alongside the existing total. This is pure instrumentation — no rendering
behavior changed, nothing to regression-test beyond "still builds/links/
passes."

**Build/test:** 383/383 desktop tests passing (unaffected — these files
are `__3DS__`-guarded). 3DS cross-build clean, zero new undefined symbols
beyond the same pre-existing 8 weak libctru/C++-runtime hooks. A
Hobo1-packaged `.3dsx` with this instrumentation was built via the
established swap/build/verify/restore procedure; `romfs/` restored and
checksum-confirmed afterward.

## What's needed to close this out

A recording of THIS build's bars (same procedure: hold steady on title
and on gameplay for ~5–8 seconds each). Whichever of the three new
`renderTop` sub-bars (tree-walk, raster, blit) turns out to actually be
large tells us exactly where to aim the next fix:

- If **raster** (orange) is large: `SoftwareRenderer`'s scanline fill
  itself is the bottleneck (per-pixel/per-scanline CPU cost, likely
  compounded by the every-frame full-viewport clear in `beginFrame()`).
- If **blit** (purple) is large: the `endFrame()` pixel-copy loop itself
  (a `pixelAt()` call + 3 byte writes per pixel, ~96,000 times for a
  400×240 top screen) is the bottleneck — a much narrower, more
  mechanical fix (e.g. a direct buffer-to-buffer copy instead of a
  per-pixel function-call loop, if `SoftwareRenderer`'s internal layout
  can support it without changing its tested behavior).
- If neither is large and **tree-walk/other** (yellow) is still ~60%:
  the earlier hypothesis was wrong in a different way, and the real cost
  is somewhere this instrumentation still doesn't reach (e.g. inside
  `CharacterDictionary`'s per-call resolution itself, or the AVM1/
  `MovieClipInstance` traversal machinery) — would need a further,
  separately-scoped instrumentation pass, not a blind guess.

## Fourth recording: raster confirmed as the dominant real cost

A recording of the `v4_subphase` build was analyzed programmatically (not
just eyeballed) — sampled the bar-chart's actual pixel colors/widths
against the always-full-width period-reference row to get an exact
percentage per phase, on 4 different screens (HOBO title w/ fish
animation @10fps, Armor Games splash @12fps, SeethingSwarm splash @9fps,
HOBO title screen @7fps):

| phase                    | title (fish) | Armor splash | SeethingSwarm | HOBO title |
|---------------------------|:---:|:---:|:---:|:---:|
| input                      | 0%   | 0%   | 0%   | 0%   |
| advance                    | 7%   | 4%   | 0%   | 31%* |
| renderTop – tree-walk/other| 8%   | 5%   | 7%   | 4%   |
| **renderTop – raster**     | **40%** | **47%** | **52%** | **46%** |
| renderTop – blit           | 15%  | 15%  | 13%  | 9%   |
| renderBottom               | 17%  | 17%  | 15%  | 12%  |
| present                    | 1%   | 1%   | 1%   | 1%   |
| vblankWait                 | 11%  | 9%   | 8%   | 7%   |

(*advance's percentage is inflated by how it's averaged — cost-per-actual-
`advanceFrame()`-call divided into the period, not diluted by iterations
where it didn't run — a known artifact of the existing averaging scheme,
not a new finding.)

**This is the conclusive answer.** `renderTop – tree-walk/other` dropped
to single digits everywhere (confirming the tessellation cache from
commit `7dd6acb` IS working and IS cheap now), and `blit` is a real but
secondary cost (9–15%). `raster` — `SoftwareRenderer`'s
`fillPolygon()`/`strokePolyline()` scanline-fill work — is alone
40–52% of every single frame, on every screen tested, including fully
static ones. That's the actual bottleneck the tessellation fix never
touched, exactly as this doc's "what's needed to close this out" section
anticipated.

**Why this is plausible, read against `SoftwareRenderer::fillPolygon()`'s
actual code** (`src/renderer/SoftwareRenderer.cpp`): it's a textbook naive
scanline fill — for every scanline row inside a shape's bounding box, it
tests ALL of that shape's edges for a crossing (an O(rows × edges) loop,
not an active-edge-table algorithm), and each edge test does a
floating-point division (`t = (scanY - ay) / (by - ay)`) to find the
crossing x. On ARM11 (weak, in-order, no fast hardware divide) this is a
believable place for real per-frame cost to hide, especially since
nothing here skips unchanged content — every shape on screen is
rescanned and refilled from scratch every single frame, static or not
(no dirty-rect/damage-tracking exists anywhere in this renderer).

## The fix: active-edge-table scanline fill (user approved 2026-08-28)

Implemented in `SoftwareRenderer::fillPolygon()`. The original loop tested
every one of a polygon's edges on every scanline row of its bounding box
(`O(rows × edges)`). The fix builds the edge list once per call (skipping
horizontal edges, exactly as before), sorts it by each edge's lower `y`
bound, then sweeps rows top to bottom maintaining an **active edge
list** — only edges whose `[yLo, yHi)` range actually covers the current
row — adding edges as the sweep reaches their `yLo` and dropping them once
past their `yHi`, instead of re-testing every edge on every row.

**Deliberately unchanged:** the crossing test and `x`-intersection formula
themselves (`t = (scanY - ay) / (by - ay); x = ax + t * (bx - ax)`) are
copied byte-for-byte from the original per-scanline-per-edge code, just
applied to the pre-filtered active subset — so the floating-point
arithmetic producing each intersection's `x` value is identical to
before, not merely equivalent. `strokePolyline()` was NOT touched — its
Bresenham-plus-stamped-square approach doesn't have the same
all-edges-every-row structure, and this project's one-fix-at-a-time
discipline says not to bundle an untested, unevidenced second change into
this one.

**Correctness verified two ways**, matching the standard this document
has followed throughout:

1. Rendered `hobo.swf` frames 1–5 through the desktop `flash_runtime
   --render` CLI before and after the fix (via `git stash`/`git stash
   pop` to isolate exactly the one file change) — **byte-identical MD5s
   on every frame**.
2. New regression test,
   `SoftwareRenderer_FillPolygon_ConcaveShapeExercisesActiveEdgeAddAndRemove`
   (`tests/test_software_renderer.cpp`): a concave "U"-shaped polygon (a
   notch cut from the top-middle of a square) specifically chosen so two
   of its edges become active partway through the sweep and inactive
   again partway through, rather than spanning the whole shape height —
   a plain convex square can't exercise the active-edge list's add/remove
   logic at all, since it never changes membership mid-sweep. Confirms
   both the notch gap (two separate spans on one scanline) and the
   solid row below it (edges correctly dropped) render correctly.

**Build/test:** 384/384 desktop tests passing (up from 383 — the new
active-edge regression test), zero regressions, byte-identical render
output, zero new compiler warnings. 3DS cross-build clean, zero new
undefined symbols. A Hobo1-packaged `.3dsx` with this fix (on top of the
sub-phase instrumentation) was built via the established swap/build/
verify/restore procedure; `romfs/` restored and checksum-confirmed
afterward.

## What's needed to close this out

A recording of the `v5_rasterfix` build (same procedure — hold steady on
title and gameplay ~5–8s each). Read the sub-phase bars the same way the
third and fourth recordings were: if `raster`'s share of the frame drops
substantially and the real FPS climbs meaningfully above the 7–12 band
this task started from, that's this fix confirmed and the task's Step 3
report can be written. If `raster` is still large, the active-edge-table
change didn't move the needle enough on its own (possible — `blendPixel`
itself, called once per filled pixel, does real per-pixel work too, and
wasn't touched by this fix) and would need its own separately-scoped
look, not folded into this one.

**Explicitly not done this task**, per its own scope: `strokePolyline()`
was read but deliberately not modified (see above); no touching input/
navigation code (confirmed working per
`docs/hobo-playability-verification.md`); no RAM-related changes; the
reported Azahar audio silence is a separate, not-yet-investigated issue
(the `StartSound` → MP3-decode → `ndsp` pipeline is wired end-to-end in
code, so the silence is unexplained, not obviously expected) — tracked
separately from this pacing task, not folded in here.

## Fifth recording: the active-edge-table fix is verified correct but only a modest real-world win

A recording of the `v5_rasterfix` build was measured the same
programmatic way as the fourth recording, across all 22 sampled frames
(covering the Armor Games splash, SeethingSwarm splash, the HOBO title,
and — new, good news for Track A — the game actually reaching a
**"CHOOSE DIFFICULTY" screen with real background art**, reconfirming
forward progress past the title screen independent of this task).

**Result: raster's average share across all 22 samples was ~41% (range
26–51%), against ~46% average (range 40–52%) in the fourth recording
(pre-fix).** That's a real but modest ~5-percentage-point drop — nowhere
near enough to explain 40–52%'s worth of frame time, and the FPS overlay
is still reading in roughly the same band (mostly 8–12, one 15 and one
anomalous 1 spike — the latter on a screen-transition frame, not
representative of steady state). **This fix is correct (verified above)
and did produce a small measurable improvement, but it did not solve the
pacing problem.**

**Before proposing another algorithmic change, did a cheap sanity check
first** (same "check the cheap thing before the expensive thing"
discipline this whole task has followed): is the 3DS cross-build even
being compiled with optimizations on? Checked `build_3ds`'s actual
generated compile command directly (not just `CMakeLists.txt`'s stated
intent) — `CXX_FLAGS` includes `-O2 -g -DNDEBUG` (RelWithDebInfo, as
`CMakeLists.txt` line 8-9 sets by default when no build type is
specified). **Confirmed optimizations are on** — `CMakeCache.txt` shows
`CMAKE_BUILD_TYPE:STRING=` (empty) because the default is set via a
non-cache `set()` that shadows the cache for that configure run without
persisting to the cache file's display, which is misleading to glance at
but doesn't affect the actual build. Ruled out, not a red herring left
uninvestigated.

**Most likely remaining explanation, based on what's actually different
about `raster` between the two fixes:** the active-edge-table change
only removed redundant EDGE-TESTING work; it never touched
`blendPixel()`, which is called once per FILLED PIXEL regardless of edge
count — so if the real cost is dominated by the sheer number of pixels
touched (and, per `blendPixel()`'s code, especially by its alpha-blend
path — three integer divisions per channel per pixel for any fill with
alpha < 255, vs. a single direct write for fully opaque fills), then an
edge-testing fix was always going to be a second-order improvement, not
the main one. This is a plausible, not yet confirmed, next lead — the
idle character animation visibly includes a semi-transparent
smoke/particle effect (see the recording), which is exactly the kind of
content that would hit the slow alpha-blend path repeatedly, every
frame, for overlapping particles.

**Proposed next step (NOT started — needs a go-ahead):** instrument
`blendPixel()` (or `SoftwareRenderer` as a whole) to count/time opaque
vs. alpha-blended pixel writes per frame, the same "measure before
guessing" approach used for every fix so far in this task, before writing
any actual blend-path optimization.

## blendPixel() instrumentation (user approved): the alpha-blend hypothesis was WRONG

Added `SoftwareRenderer::lastOpaquePixelWrites()`/`lastBlendedPixelWrites()`
— counters incremented inside `blendPixel()` depending on which path a
given pixel write takes (opaque direct-write vs. the alpha-blend divide
path; a fully-transparent alpha=0 write is a real no-op and counts as
neither), reset each `beginFrame()`. New regression test
(`SoftwareRenderer_PixelWriteCounters_ClassifyOpaqueVsBlendedAndResetPerFrame`)
confirms the classification and the per-frame reset. 385/385 tests
passing (up from 384), 3DS cross-build clean.

**Then measured it directly against the real corpus file** — no on-device
recording needed for this part, since pixel-write composition is purely
geometric (which pixels get touched and by what alpha), not an ARM11
timing question, so a desktop measurement answers it exactly. A
throwaway harness (like the earlier tessellation desktop benchmark, not
committed) advanced and rendered all 13 of `hobo.swf`'s root-timeline
frames and printed the counters:

```
frame  1: opaque= 148022 blended=    658 total= 148680 blended%=0.4%
frame  2: opaque= 145677 blended=    658 total= 146335 blended%=0.4%
...(alternates between these two states across all 13 frames)...
```

**The alpha-blend hypothesis is wrong.** Blended pixels are 0.4% of all
writes — negligible. The smoke/particle effect visible in the user's
recording is composed of opaque pixel art, not real alpha compositing (at
least not on the root-timeline content this harness can reach without
live input). `blendPixel()`'s divide path is not where the time is going.

**What the counts DO show:** ~146,000–148,000 opaque writes per frame
against a 400×240 = 96,000-pixel top screen — roughly **1.5× overdraw**
(each screen pixel gets written about one and a half times per frame on
average, from overlapping shapes drawn back-to-front with no occlusion
culling — expected for this renderer's design, not a bug). So the real
cost is simply the raw volume of opaque pixel writes, each paying real
per-pixel overhead that's larger than it needs to be: `fillPolygon()`'s
inner loop calls `blendPixel(x, y, color)` for every pixel in an
already-computed, already-clamped `[xStart, xEnd]` span — but
`blendPixel()` re-checks `x`/`y` bounds itself, then (for the opaque
case) calls `setPixel()`, which checks bounds AGAIN — two fully redundant
bounds checks per pixel for work whose bounds were already established
once, at the span level, before the loop started.

## The fix: span-fill fast path (user approved 2026-08-28)

Added `SoftwareRenderer::fillSpan(y, xStart, xEnd, color)`, used only by
`fillPolygon()`'s inner loop (not `strokePolyline()`, whose
stamped-square points aren't pre-clamped the same way and weren't shown
to be a problem — left calling `blendPixel()` unchanged). Since
`fillPolygon()` already clamps `xStart`/`xEnd` to `[0, width_-1]` and `y`
is already known in-bounds from the `minY`/`maxY` clamp before the
per-pixel loop begins, `fillSpan()` skips the two redundant bounds
checks (`blendPixel()`'s, then `setPixel()`'s) entirely: a fully-opaque
span becomes one `std::fill()` call over the row range (no per-pixel
branch at all), and a partially-transparent span uses the same per-pixel
blend math as `blendPixel()`, just without re-checking bounds the caller
already guarantees.

**Correctness verified two ways**, same standard as every fix in this
task:

1. Rendered `hobo.swf` frames 1–5 before/after — byte-identical MD5s,
   matching all the way back to the pre-`fillPolygon`-fix baseline (this
   task has now made three real changes to the render path — tessellation
   cache, active-edge-table, span-fill — and every one of them has
   produced the exact same pixels).
2. Existing `SoftwareRenderer` test suite (including the pixel-write
   counter test and the concave-shape active-edge test) all still pass
   unmodified — `fillSpan()` reuses the exact same counters and blend
   formula, so nothing about what gets counted or how a blended pixel is
   computed changed, only how an already-known-safe span gets written.

**Build/test:** 385/385 desktop tests passing, zero regressions, zero new
warnings. 3DS cross-build clean, zero new undefined symbols. A
Hobo1-packaged `.3dsx` with this fix was built via the established
swap/build/verify/restore procedure; `romfs/` restored and
checksum-confirmed afterward.

## What's needed to close this out

A recording of the `v6_spanfill` build (same procedure as every prior
recording). This is the third targeted fix to the render path this task
has made (tessellation cache → modest, unmeasured on its own; active-edge
→ ~46%→~41% raster share; this one → not yet measured on-device). If
`raster`'s share drops substantially and FPS climbs meaningfully past the
7–12 band this task started from, that's confirmation. If it's still
large, the redundant-bounds-check waste this fix removed was real but
apparently not the dominant remaining cost either — at that point the
honest move is to step back and question whether the true bottleneck is
something structural (e.g. the ~1.5× overdraw itself, or per-pixel cost
being inherently what it is on ARM11 regardless of micro-optimization)
rather than continuing to chase individual functions one at a time.

## Sixth recording: span-fill fix confirmed — real, substantial FPS gain

A recording of the `v6_spanfill` build was measured the same
programmatic way as recordings four and five, across 23 sampled frames
(title screen, Armor Games splash, SeethingSwarm splash, HOBO title, and
the "CHOOSE DIFFICULTY" screen), plus 8 direct FPS-overlay readings
spread across those same screens.

**FPS: 15, 20, 15, 15, 18, 15, 15, 15 — average ~16 fps, every single
reading at or above the previous best (12 fps) and most well above it.**
That's roughly a 60–70% throughput improvement over the 7–12 fps band
this task started from, and it holds across every screen tested (title,
both splash screens, and real gameplay-adjacent content), not just one
favorable case.

**`raster`'s average share across the 23 samples dropped to ~17%**
(range 11–28%), down from ~41% in the `v5_rasterfix` recording and ~46%
before that — confirming the span-fill fix is the one that actually
mattered. With raster no longer dominant, `blit` (~17–30%) and
`renderBottom` (~17–31%) are now the two largest remaining shares —
**but this is Amdahl's-law arithmetic, not new cost**: their absolute
per-frame time likely didn't change; they just make up a bigger slice of
a now-smaller pie. Worth noting explicitly: `renderBottom` in THIS
build is entirely diagnostic (`drawButtonTestScreen()` — the button/
circle-pad/touch test picture plus the bar chart itself), not anything a
real released Hobo1 package would render, so its ~17–31% share doesn't
represent real-game cost at all — a non-instrumented build's actual
frame budget is smaller than this diagnostic build's.

**This confirms the original task's Step 3**: the 7–12 fps pacing was a
real, fixable defect (not the game's authored tempo — that question was
answered back in Step 1, 25.00 fps declared), and it is now measurably,
substantially better after three evidenced, verified, one-at-a-time
fixes to the render path (tessellation cache, active-edge-table scanline
fill, span-fill). It is not fully resolved to native 60 fps — `blit`
(the per-pixel hardware-framebuffer copy) is the next largest real cost
if further work is wanted — but the defect this task was opened to
investigate is fixed, evidenced, and reproducible.

## Revised target (2026-08-28): user set a numeric bar — "a solid 25 fps to 50 fps ... for now focus on the 25 fps"

The sixth recording's ~16 fps average, while a genuine 60–70% win over
the 7–12 fps starting band, is **not** the finish line. The user
responded to that result with an explicit target: a solid 25 fps as the
near-term goal (matching the SWF's own declared 25.00 fps frame rate,
confirmed back in Step 1), with 50 fps as a further stretch goal to defer
for now. Everything from here is measured against 25 fps specifically,
not against "better than before."

With `raster` down to ~17% average, the sixth recording's own numbers
point at the next target: `blit` and `renderBottom` are now the two
largest remaining shares (~17–30% and ~17–31% respectively). As noted
above, `renderBottom`'s size is mostly diagnostic-build-only overhead
(`drawButtonTestScreen()`), not something a real release build pays —
but `blit` is real, unconditional cost paid by **both** screens every
single frame (`Nintendo3DSRenderer::endFrame()` is called once per
screen per frame), making it the most broadly-applicable target
available: unlike `renderBottom`, fixing `blit` helps the top screen
(the one that actually matters for a released Hobo1 package) too.

### Seventh fix: `blit`-loop redundant bounds check + framebuffer write locality

Two changes to `Nintendo3DSRenderer::endFrame()`'s per-pixel copy from
the software-rendered frame into the real LCD framebuffer, both reasoned
directly from the existing indexing formula (see the in-code comments in
both files for the full reasoning — summarized here):

1. **`SoftwareRenderer::pixelAtUnchecked()`** (new, header-only,
   `src/renderer/SoftwareRenderer.h`): `endFrame()` already computes its
   loop bounds as `blitW = std::min(srcW, fbHeight)` /
   `blitH = std::min(srcH, fbWidth)`, so every `(x, y)` the blit loop
   visits is provably inside `[0, width_) x [0, height_)` before the
   loop even starts. `pixelAt()`'s own bounds check was therefore pure
   redundant work on every one of up to 96,000 pixels/frame — the exact
   same class of waste `fillSpan()` eliminated for `fillPolygon()` in
   the previous fix. `pixelAtUnchecked()` returns a `const&` (the caller
   only reads it once per pixel) instead of `pixelAt()`'s by-value copy,
   and is documented as UB-if-misused/caller-must-guarantee-bounds
   rather than relaxing `pixelAt()`'s own contract.

2. **Loop nesting swapped** from `y`-outer/`x`-inner to `x`-outer/
   `y`-inner, with `physIndex` computed incrementally (`-= 3` each `y`
   step) instead of recomputed from scratch every iteration. The
   physical framebuffer is column-major/rotated (documented in
   `Nintendo3DSRenderer.h`): `physIndex(x, y) = (x*fbWidth +
   (fbWidth-1-y)) * 3`. Under the *original* nesting (`y` outer), each
   step of the inner `x` loop jumps `fbWidth*3` bytes in `fb` — a
   strided write. Under the *new* nesting (`x` outer, `y` inner), each
   step of the inner `y` loop decreases `physIndex` by exactly 3 bytes —
   a sequential write. The trade being made deliberately: `software_`'s
   `pixels_` array (the read side) is ordinary cached system RAM, fairly
   tolerant of a strided access pattern, while `fb` (the write side) is
   real LCD display memory, plausibly write-combined/uncached, where
   sequential writes matter far more. Since `pixels_` is read-only in
   this loop regardless of which axis is outer, only `fb`'s access
   pattern is actually in this loop's control — so optimizing for `fb`'s
   locality at the cost of `pixels_`'s is the right trade either way.
   Every iteration still writes to the exact same `physIndex` for a
   given `(x, y)` as the original formula — verified algebraically, not
   just visually — and every iteration's target address is distinct
   with no cross-iteration dependency, so the reordering provably cannot
   change the final framebuffer contents, only the order they're
   produced in.

**Correctness — an important caveat unlike every prior fix in this
task**: `Nintendo3DSRenderer.cpp` is 3DS-only. Every previous fix in this
task (tessellation cache, active-edge-table, span-fill) touched
`SoftwareRenderer`, which is desktop-buildable and testable, so each had
a real automated check: byte-identical PPM/MD5 renders of `hobo.swf`
before and after. This fix has no such path — there is no desktop build
of `Nintendo3DSRenderer.cpp` to render and MD5-compare against. Its
correctness rests entirely on the indexing-formula reasoning above (both
changes are individually provably index-preserving) plus, ultimately,
the next on-device recording actually looking right — not on any
automated test this task has been able to run for every fix before it.

**Build/test:** desktop — clean rebuild, 385/385 tests passing, unchanged
(this fix only adds a header method desktop code never calls, plus edits
a `.cpp` file the desktop build doesn't compile at all). 3DS cross-build
— clean; confirmed via a forced incremental rebuild that
`Nintendo3DSRenderer.cpp` recompiled with **zero new compiler warnings**
(grepped the build log specifically for this file) — only the same
pre-existing weak-libc-symbol linker notes (`_close`, `_fstat`, etc.)
every prior 3DS build has had.

### What's needed to close this out

A recording of the `v7_blitfix` build, measured the same way as every
prior recording (bar-chart pixel measurement + direct FPS-overlay
readings across the same handful of screens). Two separate things need
checking, not just one: (1) is FPS meaningfully closer to the 25 fps
target, and (2) — new, because this fix is unverified any other way —
does the game still look correct on both screens, since a mistake in the
write-locality reasoning above would show up as visibly wrong pixels
(e.g. a rotated/mirrored/torn image) rather than as a silent slowdown.
If `blit`'s share drops and FPS climbs toward 25, and the picture still
looks right, that's confirmation and the next-largest remaining share
(likely `renderBottom`'s real non-diagnostic component, or something
structural like the ~1.5× overdraw noted in the pixel-write-counter
investigation) becomes the next target. If FPS is still well short of 25
after this fix, the honest move — same discipline as every fix in this
task — is to measure the new composition before guessing at the next
one, rather than assuming another micro-optimization is the answer.

## Seventh recording: blit fix confirmed correct AND a real, large FPS gain — plus an architectural finding about "25 fps" itself

A recording of the `v7_blitfix` build (`test6.mkv`, ~64s) was measured
the same programmatic way as recordings four through six: pixel-measured
bar chart across 24 sampled overlay frames, cross-checked against
Azahar's own built-in "App: N FPS" counter (OCR'd via `tesseract` across
all 32 extracted frames — a second, independent FPS source this
recording happened to make readable, not used in prior recordings).

**Correctness — checked first, since this fix uniquely had no desktop
verification path:** the top screen was visually inspected across the
Azahar game-list, the HOBO title/PLAY screen, the ArmorGames splash, the
SeethingSwarm splash, and the "CHOOSE DIFFICULTY" screen. All render
correctly — no mirroring, rotation, tearing, or corruption of any kind.
This confirms the loop-nesting/write-locality reasoning in the fix's own
comment was correct, not just plausible.

**FPS: a rock-solid steady-state 20 fps**, per Azahar's own counter, across
every one of the ~20 in-content OCR samples that weren't a scene-load
transition blip (a handful of single-frame outliers — 27, 13, 30, 23, 5 —
all land on the black loading transition between splash screens, the same
kind of transient dip prior recordings also saw and correctly treated as
not representative). This is a real, large jump from the sixth recording's
15/20/15/15/18/15/15/15 (avg ~16) — every single frame now sampled sits
at exactly 20, not just averaging near it.

**Sub-phase composition (24 samples, `renderBottom`'s HUD included since
this is still the diagnostic build):**

| phase | avg share | prior (6th recording) |
|---|---|---|
| `renderTopRaster` | 25.3% | ~17% |
| `vblankWait` | 20.5% | (not separately tracked before) |
| `renderBottom` | 16.4% | ~17–31% |
| `renderTopTree` | 14.5% | (part of a larger `renderTop`) |
| `renderTopBlit` | 10.0% | ~17–30% |
| `advance` | 7.4% (only ~half of frames advance the movie) | — |

**`blit` dropped from co-dominant (~17–30%) to a clear non-issue
(~10%)** — the fix worked exactly as reasoned. `raster` is now the
largest single real-work share again (Amdahl's-law arithmetic, same as
every previous fix: shrinking one phase makes the others a bigger slice
of a smaller pie).

**Architectural finding, worth being explicit about:** this loop calls
`gspWaitForVBlank()` exactly once per iteration and does not otherwise
throttle — so the *achievable* steady-state frame rate is always
`60 / n` for some integer `n` (60, 30, 20, 15, 12, ...), whichever rung
is just above how long one iteration's real CPU work actually takes.
**A literal, exact "25 fps" is not a value this architecture can produce
at all** — it can only land on 30 (if per-frame work drops under ~33.3ms)
or 20 (if it's between ~33.3ms and 50ms), never the number in between.
Given the SWF's own declared rate is 25.00 fps (Step 1, this task's very
first finding), the practically-correct interpretation of "a solid 25
fps" is **30 fps** — the nearest rung that actually clears the bar,
and itself sits inside the user's stated 25–50 fps band.

**How close 30 fps actually is:** converting the measured shares back to
absolute time at the observed ~50ms real period: `renderTopRaster`
≈12.7ms, `vblankWait` ≈10.3ms (idle, not work), `renderBottom` ≈8.2ms,
`renderTopTree` ≈7.3ms, `renderTopBlit` ≈5.0ms, `advance` ≈3.7ms — total
real work ≈37ms. The 30 fps rung needs real work under ~33.3ms — **only
about 3.5ms (~10%) more needs to come out.** Notably, `renderBottom`'s
~8.2ms is *entirely* the diagnostic HUD (`drawButtonTestScreen()` — the
button/circle-pad/touch picture and the bar chart itself, see the sixth
recording's note above) — not anything a real, non-instrumented Hobo1
build would ever pay. That alone is more than double the ~3.5ms needed:
**a non-diagnostic build is very likely already at or past 30 fps**, this
diagnostic build just can't show that number directly since the HUD that
measures the frame is itself part of what's being measured.

### What's needed to close this out

Two independent paths, not mutually exclusive: (1) measure a build with
the diagnostic HUD disabled/minimized to see the real, undistorted frame
rate a released Hobo1 package would actually get — this doesn't require
guessing at a new fix, just removing the measurement's own overhead; (2)
if the diagnostic HUD needs to stay for now, `renderTopRaster` (~12.7ms,
back to being the largest single real-work phase) is the next evidenced
target the same way it was before the active-edge-table and span-fill
fixes, though it's already been through two rounds of optimization and
further gains there may be smaller than before.

## Eighth fix: `std::lround()` in `fillPolygon()`'s span endpoints — real, verified, but smaller than the initial profile suggested

Per the user's explicit direction ("keep optimizing renderTopRaster"),
profiled `SoftwareRenderer::fillPolygon()`/`strokePolyline()` against real
`hobo.swf` content with `valgrind --tool=callgrind` (desktop x86 — reading
*instruction-count composition*, not wall-clock time, which is what
carries over to ARM11's very different clock/cache characteristics; the
*shape* of where time goes generally does, even if absolute cycles don't).
This is the first time this task has had an actual profiler available,
rather than the TickCounter sub-phase timers used for every prior
measurement — a strictly finer-grained tool for finding the next fix
*within* a single phase.

**Finding:** `std::lround()` — called twice per filled scanline span, to
compute `xStart`/`xEnd` from the floating-point edge-intersection x
values — accounted for **over 20% of total program instructions**,
dwarfing every other cost in the renderer, including the actual pixel
writes (`fillSpan`'s `std::fill()`, ~12.7%). glibc's `lround()`/
`llround()` is a real, non-inlined libm function call that has to handle
the full IEEE-754 contract — NaN/Inf, out-of-representable-range values,
and the current floating-point rounding-mode environment — none of which
this call site can ever actually hit: `x` here is always a finite,
small-magnitude screen-pixel coordinate.

**Fix:** `roundToInt()`, a small inline function matching `std::lround()`'s
own round-half-away-from-zero tie-breaking (`lround(2.5)==3`,
`lround(-2.5)==-3`) via `x >= 0 ? x+0.5 : x-0.5` then truncate, with no
libm call and no rounding-mode/exception-flag handling. This is
bit-identical to true round-half-away-from-zero for every magnitude this
renderer's coordinates can ever reach — a double's ~52-bit mantissa
covers both the integer part and the 0.5 tie-breaker exactly until many
orders of magnitude past any twip-derived pixel coordinate in this
codebase — so it's a safe, general replacement for this specific,
bounded use, not a hack that happens to work on today's test content.

**An honest finding from the same profiling run, not just the fix
itself:** most of that 20%+ program-wide `lround()` cost turned out to
be **outside raster entirely**. `grep -rn lround src/renderer/` shows it's
also called from `SceneRenderer.cpp`'s `toDevicePolyline()` and from
`ShapeTessellator.cpp` — both called once per shape *vertex* (curve
flattening, coordinate transforms), which for typical shape complexity
runs far more often than `fillPolygon()`'s twice-per-scanline-*span*
calls. Those call sites are part of the tree-walk phase (the difference
between `renderTop`'s total and raster+blit), not raster — this fix does
not touch them. Re-profiling after the change confirms the scope
precisely: total `lround` instructions across the whole profiled run
dropped by ~45M (444M → 399M), matching the two calls removed per
`fillPolygon()` span; the remaining ~399M is attributable to those other,
untouched call sites. **This fix captures exactly raster's own share of
the problem, not the whole 20%** — reporting that plainly rather than
implying a bigger win than actually landed. If more raster speed is
wanted after this, this particular finding is now exhausted; the
tree-walk-side `lround` cost is a real, separately-evidenced target, but
a different phase than the one asked for this round.

**Correctness:** rendered `hobo.swf` frames 1–5 before/after are
byte-identical MD5s (`656e1f19ca0e3faed597ed305b9cfd04` for frames 1/3/5,
`25e484e991e29af9b6dfad6ffc0e8d19` for frames 2/4) — matching every prior
fix in this task all the way back to before the active-edge-table change.
Full existing test suite (385/385) passes unmodified. 3DS cross-build
clean, zero new warnings in `SoftwareRenderer.cpp` (forced incremental
rebuild + grep, same method as every prior fix — the many "note: parameter
passing... changed in GCC 7.1" lines from `std::sort`/`std::vector`
internals in `fillPolygon()` are pre-existing, from the active-edge-table
fix's own `std::sort` call, not new).

### What's needed to close this out

A recording of the `v8_lroundfix` build. Given the honest scope finding
above, don't expect a dramatic jump — this removed a real but bounded
slice of raster's own cost, not the eye-catching 20% the initial
aggregate profile suggested. If the user wants to keep chasing raster
speed after this, tessellation/edge-count reduction or the sort call
itself are the remaining candidates visible in this profile; if they'd
rather chase the bigger win, the tree-walk-side `lround` cost
(`toDevicePolyline`/`ShapeTessellator`) is now a clearly evidenced,
separate target.
