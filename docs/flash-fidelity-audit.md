# Flash Player fidelity audit — sound, timing, rendering (2026-08-29)

Requested as a "Ghidra investigation" of the flash3ds-runtime binary. Ghidra
itself was not usable for this — in this environment its MCP tools are
bound to the separate Shift-DX reverse-engineering project's binary (see
the top-level `/home/claude/CLAUDE.md`), not this project's own compiled
output. More fundamentally, flash3ds-runtime is a from-source project with
the real code and comments already in the tree — decompiling our own
binary would reconstruct strictly worse information than reading the
source directly, so that's what this audit does instead. The deliverable
shape (function map, key snippets, ranked divergence list) is the same as
requested; the method is direct source reading, verified line-by-line, not
disassembly.

Every function/line reference below was read directly, not inferred.
Everything under "not implemented" was confirmed by a full-repo grep, not
by absence of evidence during a partial search.

---

## TASK 1 — Sound subsystem

### Function map

| Location | Symbol | Purpose |
|---|---|---|
| `src/audio/IAudioBackend.h:55` | `IAudioBackend::loadSound()` | Register PCM16 for a soundId (default no-op) |
| `src/audio/IAudioBackend.h:74` | `IAudioBackend::playSound()` | Start playback for a soundId |
| `src/audio/IAudioBackend.h:81,85` | `stopSound()`/`stopAllSounds()` | Stop primitives |
| `src/audio/Nintendo3DSAudioBackend.cpp:22-35` | `Nintendo3DSAudioBackend()` (ctor) | `ndspInit()` + output mode setup |
| `src/audio/Nintendo3DSAudioBackend.cpp:59-77` | `channelFor()` | Allocate/reuse one of 23 ndsp channels for a soundId |
| `src/audio/Nintendo3DSAudioBackend.cpp:79-108` | `loadSound()` | `linearAlloc` + `memcpy` + `DSP_FlushDataCache` |
| `src/audio/Nintendo3DSAudioBackend.cpp:110-177` | `playSound()` | Configure format/rate/mix, queue one `ndspWaveBuf` |
| `src/audio/Nintendo3DSAudioBackend.cpp:211-282` | `playTestTone()` | Diagnostic-only sine synth, unreachable from SWF content |
| `src/audio/Mp3Decoder.cpp:45-119` | `decodeMp3()` | minimp3 wrapper, frame-by-frame decode loop |
| `src/audio/Mp3Decoder.cpp:121-130` | `decodeSwfMp3Sound()` | Skips SWF's 2-byte `SeekSamples` header, delegates to `decodeMp3()` |
| `src/runtime/MovieClipInstance.cpp:673-713` | `ScriptEnvironment::ensureSoundDecoded()` | Decode-on-demand-and-cache; MP3 only |
| `src/runtime/MovieClipInstance.cpp:715-732` | `ScriptEnvironment::playSoundById()` | `loadSound()` on first reference, `playSound()` always |
| `src/runtime/Timeline.cpp:200-216` | `Timeline::currentFrameStartSoundEvents()` | Frame-scoped StartSound lookup (real frame-sync) |
| `src/runtime/MovieClipInstance.cpp:1392-1403` | `runCurrentFrameSounds()` | Dispatches current frame's StartSound records |
| `src/swf/DefineSoundTag.cpp:5-19` | `parseDefineSound()` | Structural-only header parse; no decode here |
| `src/swf/TagCode.h:32-33,49` | `SoundStreamHead`/`Block`/`Head2` | **Enum values + name strings only — no parser, no dispatch, no handling anywhere** |

### Key snippets

**Channel allocation has no eviction — exhaustion is silent** (`Nintendo3DSAudioBackend.cpp:114-119`):
```cpp
const int channel = channelFor(soundId);
if (channel < 0) {
    LOG_WARN("AUDIO", "Nintendo3DSAudioBackend: no free ndsp channel for soundId=%u", soundId);
    return;
}
```
A channel is only ever freed by an explicit `stopSound()`/`stopAllSounds()` call — nothing frees a channel automatically when its one-shot wavebuf finishes. A fire-and-forget `StartSound` permanently occupies a channel for the backend's lifetime.

**Loop count is honored as "play once," logged, not silently dropped** (`Nintendo3DSAudioBackend.cpp:157-167`):
```cpp
if (loopCount > 1) {
    LOG_DEBUG("AUDIO", "...soundId=%u requested loopCount=%d -- playing once "
              "(counted-repeat queuing not implemented, see header comment)", soundId, loopCount);
}
...
sound.waveBuf.looping = false;
```

**No volume control exists in the backend interface at all** — `IAudioBackend.h`'s full method list is `loadSound`/`playSound`/`stopSound`/`stopAllSounds`; there is no `setVolume`. The mix matrix is hardcoded full-volume:
```cpp
float mix[12] = {0.0f};
mix[0] = mix[1] = 1.0f;  // full volume to both front-left/front-right mix slots
ndspChnSetMix(channel, mix);   // Nintendo3DSAudioBackend.cpp:143-145
```

### Divergence list (audio), ordered by likely impact

1. ~~**`Sound.setVolume()` is a complete no-op on the 3DS backend.**~~ — **DONE (2026-08-29), see `docs/audio.md`'s "Fidelity-audit TASK 1, divergence #1" section for the full fix writeup.** `IAudioBackend::setVolume()` now exists, `Nintendo3DSAudioBackend` threads it into `ndspChnSetMix` (both at `playSound()` time and as a live update on an already-playing channel), and AS2 `Sound.setVolume`'s native impl forwards to it (0-100 -> normalized [0,1]) whenever `_soundId` is resolved. 2 new regression tests, 384/384 passing, 3DS cross-build clean. Not yet separately confirmed audible on-device.
2. ~~**Channel allocation has no eviction policy.**~~ — **DONE (2026-08-29), see `docs/audio.md`'s "Fidelity-audit TASK 1, divergences #2–#7" section, item #2.** `reclaimFinishedChannels()` (checked via `ndspChnIsPlaying()`, called lazily at the top of `channelFor()`) now reclaims a finished one-shot's channel without needing an explicit `stopSound()`. 3DS-only (no desktop test surface — same as every other real-ndsp method in this file); clean 3DS cross-build.
3. ~~**No counted `StartSound` loop repeat.**~~ — **DONE (2026-08-29), see `docs/audio.md`, item #3.** Confirmed against libctru's own `ndsp-channel.c` source (not assumed) that `ndspChnWaveBufAdd()` builds a real singly-linked play-queue with no fixed depth limit — queuing N `ndspWaveBuf`s at the same PCM, capped at `kMaxQueuedRepeats=32`, now gives a real counted repeat. Same verification note as #2.
4. ~~**`SyncNoMultiple` is parsed but never consulted.**~~ — **DONE (2026-08-29), see `docs/audio.md`, item #4.** New `IAudioBackend::isPlaying(soundId)`, consulted in `runCurrentFrameSounds()` before a SyncNoMultiple-flagged retrigger. 2 new regression tests, desktop-verified.
5. **`SoundStreamHead`/`SoundStreamBlock` entirely unimplemented — investigated 2026-08-29, DELIBERATELY NOT IMPLEMENTED.** A corpus-wide scan (all 8 Hobo files + Extreme Pamplona's main file + all 21 of its music/sounds/level sub-SWFs — 30 files total, not just `hobo.swf`) found ZERO `SoundStreamBlock` tags ANYWHERE (5025 `SoundStreamHead2` stub headers, always header-only, corpus-wide, not just `hobo.swf`'s already-known case). This is real corpus-wide evidence, not a guess — matches this project's established "don't build against a hypothetical" precedent (see `docs/audio.md`'s writeup). Revisit only if a future target title's corpus is shown to actually have real streaming data.
6. ~~**Non-MP3 codecs (ADPCM/Nellymoser/Speex/uncompressed) never decoded.**~~ — **Uncompressed + ADPCM DONE (2026-08-29), Nellymoser/Speex still explicitly out of scope — see `docs/audio.md`'s "Item #6, Uncompressed + ADPCM sub-scope" section for the full writeup.** Same corpus-wide scan found 430 real `DefineSound` tags total, 100% format=MP3, zero of any other codec anywhere in the real corpus — the initial "investigated, deliberately not implemented" call stands as evidence-correct, but the user explicitly asked to proceed with Uncompressed+ADPCM anyway as forward-looking capability (choosing that sub-scope over Nellymoser, which has no public spec, and Speex, deferred). `src/audio/PcmSoundDecoder.h/.cpp` (new), dispatched from `ScriptEnvironment::ensureSoundDecoded()` the same way MP3 already was. Verified via hand-built synthetic SWF bitstreams cross-checked against an independently-transcribed reference algorithm (no real corpus content exists for either format to test against) — 10 new decoder tests + 2 new runtime-wiring integration tests, 400/400 passing, zero regressions, both desktop and 3DS cross-builds clean with zero new warnings.
7. ~~**SOUNDINFO envelope + in/out points parsed but never applied.**~~ — **InPoint/OutPoint DONE (2026-08-29), envelope explicitly deferred — see `docs/audio.md`, item #7 for the full writeup.** This item's original "rare in practice... low priority" assessment was WRONG, not evidence-checked: a real corpus-wide scan found InPoint on ~35% of all real StartSound triggers (1304/3693), OutPoint on ~9% (337/3693), envelope on ~9.6% (355/3693) — the "rare" framing above should not be trusted for future audit items either; always evidence-check before ranking impact. InPoint/OutPoint fixed (`IAudioBackend::playSound()` gained `startFrame`/`endFrame` params, pure pointer/length trim against already-loaded PCM, 2 new regression tests). Envelope (real per-sample volume/pan automation) intentionally left unimplemented: it needs either a live ndsp mixing callback or a per-trigger PCM pre-bake, both genuinely new runtime-mixing behavior needing real on-device audio-quality listening this environment can't do — a different risk category from InPoint/OutPoint's pure arithmetic, scoped as its own future follow-up rather than rushed.

---

## TASK 2 — FPS / timing

### Function map

| Location | Symbol | Purpose |
|---|---|---|
| `src/platform/nintendo3ds_main.cpp:450-462` | pacing setup (inline in `main()`) | Computes `vblanksPerSwfFrame` once from the SWF's declared rate |
| `src/platform/nintendo3ds_main.cpp:464-511` | main loop (inline in `main()`) | Input poll → gated `advanceFrame()` → render both screens → `gspWaitForVBlank()` |
| `src/runtime/Timeline.cpp:218-229` | `Timeline::advanceOneFrame()` | Integer +1 frame step (or wrap to 1), no time concept |
| `src/runtime/Timeline.cpp:110-134` | `gotoAndStop()`/`gotoAndPlay()` | Scripted jumps, set `playing_` |
| `src/runtime/Timeline.cpp:159-168` | `gotoFrame()` (neutral) | Bare `ActionGotoFrame` semantics, doesn't touch `playing_` |
| `src/runtime/MovieClipInstance.cpp:1580-1639` | `MovieClipInstance::advanceFrame()` | One host tick: advance own timeline, run scripts/sounds, recurse into children |
| `src/runtime/Movie.h:41,52` | `frameRateFixed8` / `frameRateFps()` | Raw 8.8 fixed-point SWF header field → fps |
| `src/swf/SwfLoader.cpp:138-142` | header parse | `reader.readFixed8()` for the frame-rate field |

### Key snippet — the pacing bug

`nintendo3ds_main.cpp:450-462`:
```cpp
const double swfFrameRate = movie->frameRateFps() > 0.0 ? movie->frameRateFps() : 12.0;
constexpr double kVBlankHz = 60.0;
int vblanksPerSwfFrame = std::max(1, static_cast<int>(std::lround(kVBlankHz / swfFrameRate)));
int vblankCounter = 0;
```
```cpp
if (++vblankCounter >= vblanksPerSwfFrame) {
    vblankCounter = 0;
    root->advanceFrame();
    ...
}
```

`hobo.swf` declares **`FrameRate=25.00 fps`** (confirmed this session via a real load — `SwfLoader`'s own INFO log). `60.0 / 25.0 = 2.4`, and `std::lround(2.4) = 2`. So `vblanksPerSwfFrame = 2`, meaning the movie's timeline actually advances at **60/2 = 30 fps** — a **20% speedup** over its declared 25 fps, systematic and permanent for the life of this build, not just occasional jitter. This directly affects every tween, animation, and timed sound trigger in the movie, running the entire game noticeably fast relative to real Flash Player.

This isn't a one-off for this title either: any SWF whose declared frame rate doesn't divide 60 evenly gets silently retimed to the nearest achievable divisor, with no compensation. A quick table of the divergence this causes:

| Declared fps | `60/fps` | `lround` | Actual fps | Error |
|---|---|---|---|---|
| 25 (hobo.swf) | 2.400 | 2 | 30.00 | **+20.0%** |
| 24 | 2.500 | 3 | 20.00 | **−16.7%** |
| 20 | 3.000 | 3 | 20.00 | 0% (exact) |
| 18 | 3.333 | 3 | 20.00 | +11.1% |
| 15 | 4.000 | 4 | 15.00 | 0% (exact) |

(Verified with a standalone script computing `std::lround`'s actual round-half-away-from-zero behavior, not assumed.)

Only frame rates that are exact divisors of 60 (60/30/20/15/12/10...) are unaffected; anything else drifts, and there's no fractional-accumulator correction anywhere in the loop.

### Divergence list (timing), ordered by likely impact

1. ~~**The `lround`-rounded vblank divisor causes a systematic, permanent frame-rate error whenever the SWF's declared rate isn't an exact divisor of 60.**~~ — **DONE (2026-08-30).** New `runtime::FramePacer` (`src/runtime/FramePacer.h/.cpp`, platform-independent so it's unit-testable) replaces the fixed `std::lround(60.0/swfFrameRate)` divisor with a fractional accumulator (classic Bresenham/DDA timing): every real vblank, add `swfFrameRate` to a running accumulator and report one logical-frame advance for every `kVBlankHz` crossed, subtracting as it goes. Verified (`tests/test_frame_pacer.cpp`, 9 new tests) to average to EXACTLY the declared rate with zero cumulative drift over long runs, for both the concrete `hobo.swf` case (25fps -> exactly 25, not the old code's 30) and every other rate in this section's own divergence table (24, 20, 18, 15), plus non-integer rates and rates exceeding the host tick rate. Wired into `nintendo3ds_main.cpp`'s main loop in place of the old `vblankCounter`/`vblanksPerSwfFrame` pair. 421/421 tests passing (up from 412), zero regressions, 3DS cross-build clean with zero new warnings. Note: `docs/performance-pacing.md`'s Step 2 asserted this same code was "mathematically correct, not a bug" — that was true for the FPS/throttle question that doc was investigating (nothing in this code artificially caps the loop's real tick rate), but the code had a separate, real bug in playback-speed *accuracy* that doc wasn't looking for; corrected in place there, see that doc's own note.
2. ~~**No frame-skip/catch-up logic when rendering falls behind.**~~ — **DONE (2026-08-30).** New `FramePacer::advanceElapsed(double elapsedSeconds)` (shares its accumulator/units with divergence #1's `advanceTick()`, verified unit-compatible by a dedicated interleaving test rather than assumed) takes the ACTUAL measured elapsed real time between loop iterations instead of assuming exactly 1/60s — `svcGetSystemTick()`/`SYSCLOCK_ARM11` (the exact pairing this entry's own original text named as the missing wall-clock read) now measures real elapsed time each iteration in `nintendo3ds_main.cpp`. A slow-rendering iteration now correctly produces multiple logical-frame advances (scripts/sounds tick the right number of times) in a single call rather than silently falling one vblank behind real time; `scene.render()` still only runs once per real loop iteration either way (only the *tick count* feeding `advanceFrame()` changed), which is exactly the "tick to catch up, render once" pattern this entry originally called for. Catch-up is deliberately capped at `kMaxCatchUpSeconds` (0.25s) — a very long stall (HOME-menu suspend, a debugger breakpoint) drops the excess "lost" time rather than replaying an enormous frame backlog in one burst, matching how a real player generally behaves after a long suspend. 6 new tests (`tests/test_frame_pacer.cpp`), all expected values derived by simulating the exact formula in Python before writing any C++ (this caught a real floating-point-precision pitfall in an initial design draft — reformulating `advanceTick()` itself in terms of `advanceElapsed()` looked cleaner but introduced confirmed off-by-one drift for some rates over long runs, e.g. 18fps and 10fps at a 60Hz host, due to `1.0/60.0` not being exactly representable; caught in simulation, not via a failing test, so `advanceTick()`'s already-shipped formula was deliberately left untouched and `advanceElapsed()` uses its own independent-but-compatible formula instead — see `FramePacer.cpp`'s own top-of-file comment for the full story). 427/427 tests passing (up from 421), zero regressions, 3DS cross-build clean with zero new warnings.
3. **Fixed, hardcoded `kVBlankHz = 60.0`, not queried from hardware — LOW impact in practice (3DS's LCD refresh is a fixed 59.83 Hz canonically, this is a well-known, usually-ignored approximation), but worth noting since it compounds with finding #1 for extreme precision work.** Still not worth fixing on its own — left as-is per this entry's own original assessment, unchanged by #1/#2 landing.
4. **Sound triggering is frame-gated (correct), but playback itself is decoupled once triggered — NEUTRAL, not a bug.** `runCurrentFrameSounds()` fires exactly when its owning `advanceFrame()` call happens (i.e. subject to the same #1/#2 pacing errors above for *when* a sound starts), but once queued the DSP hardware's own scheduler drives playback with no further coupling to subsequent vblanks. This means if #1/#2 cause the video to run at the wrong rate, *audio start times* will drift with it (sounds trigger early relative to true wall-clock time in the 25→30fps case), but individual sounds themselves won't stutter or resample — worth mentioning as a consequence of #1, not a separate defect.

---

## TASK 3 — Rendering vs. Flash Player fidelity

### Function map

| Location | Symbol | Purpose |
|---|---|---|
| `src/renderer/IRenderer.h:36-42` | `fillPolygon()`/`strokePolyline()` | Flat-color-only draw primitives, device-pixel space |
| `src/renderer/SoftwareRenderer.cpp:60-111` | `fillPolygon()` | Even-odd scanline fill, **1 sample/row, no AA** |
| `src/renderer/SoftwareRenderer.cpp:113-148` | `strokePolyline()` | Bresenham + square-stamp thickness, no joins/caps/AA |
| `src/renderer/SoftwareRenderer.cpp:11-16,46-58` | `blendChannel()`/`blendPixel()` | Straight-alpha "normal" over-compositing only |
| `src/renderer/ShapeTessellator.cpp:40-62` | `toFlatColor()` | **Radial/focal-radial gradients only → averaged flat color** (linear gradients and bitmaps render for real now — see `docs/renderer.md`'s "Gradient rendering"/"Bitmap rendering" sections, 2026-08-28/2026-08-31) |
| `src/renderer/ShapeTessellator.cpp:64-194` | `tessellateShape()` | One polygon per MoveTo run — **no hole/multi-contour support** |
| `src/renderer/SceneRenderer.cpp:68-107` | `renderClip()` | Recursive parent→child matrix + color-transform composition |
| `src/renderer/SceneRenderer.cpp:239-319` | `renderGlyph()` | Real embedded font outlines, reuses `ShapeTessellator` (inherits its hole gap) |
| `src/swf/SwfRecords.cpp:8-22` | `concatMatrix()` | Correct parent-then-child affine composition |
| `src/swf/SwfRecords.cpp:24-47` | `concatColorTransform()` | Correct parent-then-child mult/add composition |
| `src/swf/ShapeRecords.cpp:20-33` | `applyColorTransform()` | Final per-pixel `input*mult+add`, clamped |
| `src/swf/ShapeRecords.cpp:59-84` | `readGradient()` | Parses spread/interpolation/focal correctly — **all three then discarded** |
| `src/swf/DefineButtonTag.cpp:56,81-82` | button v2 parse | `BlendMode` byte read-and-discarded; `FilterList` aborts remaining parse |

### Key snippets

**No anti-aliasing anywhere — single-sample scanline fill** (`SoftwareRenderer.cpp:81-97`):
```cpp
double scanY = y + 0.5;  // sample at pixel center
...
if ((scanY >= ay && scanY < by) || (scanY >= by && scanY < ay)) {
    double t = (scanY - ay) / (by - ay);
    double x = a.x + t * (b.x - a.x);
    intersections.push_back(x);
}
...
int xStart = static_cast<int>(std::lround(intersections[i]));
int xEnd = static_cast<int>(std::lround(intersections[i + 1]));
```
One sample per pixel row, edge crossings rounded to the nearest integer pixel — this is the textbook "aliased/blocky" scanline fill, the opposite of Flash Player's coverage-based AA.

**Gradients are discarded to a flat average before any pixel is ever drawn** (`ShapeTessellator.cpp:40-62`, paraphrased structure — averages `r/g/b/a` across every `GradientRecord` in `gradient.records` unweighted by stop position, then returns one `RgbaColor`). The parsed `spreadMode`/`interpolationMode`/focal point (`ShapeRecords.cpp:59-84`) are read correctly off the wire and then never referenced again anywhere in the renderer.

**Correct composition order (this is NOT a divergence — confirmed matching Flash's convention)** (`SceneRenderer.cpp:91-93`):
```cpp
swf::Matrix childWorld = swf::concatMatrix(worldMatrix, child.localMatrix());
swf::ColorTransform childColor = swf::concatColorTransform(worldColorTransform, child.colorTransform());
```
`concatMatrix(parent, child)`'s own doc comment (`SwfRecords.h`) confirms "applies `child` first, then `parent`" — standard nested-display-object math, matching Flash.

### Divergence list (rendering), ordered by likely impact

1. ~~**No anti-aliasing on any vector fill or stroke**~~ — **DONE (2026-08-30), edge-pixel coverage AA only — see scope note below.** Implemented option (b) from this entry's own original recommendation: `SoftwareRenderer::fillPolygon()`'s scanline loop no longer rounds each span's two edge crossings to the nearest integer pixel (`roundToInt()`/`lround`) and bulk-fills the inclusive range at full alpha. It now computes the exact fractional X-coverage of the one or two boundary pixel columns per span (`leftPixel = floor(xLeft)`, `rightPixel = ceil(xRight) - 1`, coverage = the overlap length between the pixel's `[x, x+1)` column and the span) and blends just those columns via a new `blendPixelCoverage(x, y, color, coverage)` (alpha scaled by `coverage`, deferring straight to the existing `blendPixel()` when `coverage >= 1.0` — the common case for axis-aligned/integer-coordinate shapes, so it costs exactly what it did before). The interior of the span (pixels strictly between the two boundary columns) stays on the unchanged, full-cost `fillSpan()` bulk path — this is what keeps the fix cheap: a fill pays for at most 2 extra `blendPixelCoverage()` calls total per scanline span, not a per-row AA kernel. Coverage formula verified against a brute-force per-pixel overlap integration over 200,000 random spans in Python before writing any C++ (this project's established simulate-first discipline), and against fillPolygon() itself via 3 new hand-derived-and-cross-checked pixel tests (`test_software_renderer.cpp`: `SoftwareRenderer_FillPolygon_ExactIntegerBoundary_DoesNotOverfillByOneColumn`, `SoftwareRenderer_FillPolygon_SlantedEdge_BlendsBoundaryColumnAtHalfCoverage`, `SoftwareRenderer_FillPolygon_NarrowTriangleApex_BlendsBothBoundaryColumnsNoFullInterior`). Incidental find: the old inclusive-`[xStart,xEnd]`-both-ends rounding over-filled by exactly one pixel column whenever a span landed on an exact integer boundary (verified: a shape from x=2.0 to x=12.0, geometrically 10 pixels wide, used to fill 11 columns) — the coverage redesign fixes this for free, confirmed via the `ExactIntegerBoundary` test above. All 430 desktop tests pass (up from 427), zero regressions — every pre-existing pixel-exact assertion in `test_software_renderer.cpp`/`test_scene_renderer.cpp` samples a point well inside or well outside a shape's integer-coordinate boundary, never the exact boundary pixel itself, so none of them could detect (or be broken by) this change; confirmed by reading every such assertion before writing the fix, not assumed. 3DS cross-build clean, zero new compiler warnings. **Scope limitation, stated plainly (this is deliberate, not a remaining gap in the same sense as the other divergences below):** this smooths near-vertical/diagonal silhouette edges via X-direction sub-pixel coverage at each scanline's crossings only. It does **NOT** anti-alias purely horizontal top/bottom edges of a shape — rendering still samples exactly one scanline row per integer Y, with no Y-direction coverage/supersampling, so a shape's top and bottom edges remain hard-edged. It also does **NOT** apply to `fillPolygonGradient()` (gradients, divergence #2 below, remain unaffected — deliberately out of scope for this pass, since that path is a separate, rarely-hit copy of the same scanline logic per its own doc comment) or to `strokePolyline()` (divergence #7 below, unaffected).
2. **Gradients render as one flat averaged color — HIGH visual impact for any gradient-using content (extremely common in Flash-era UI: buttons, glows, backgrounds), MODERATE fix effort.** **Fix:** build a small (e.g. 256-entry) color-ramp LUT per gradient at tessellation time (interpolating between `GradientRecord` stops by `ratio`, honoring `interpolationMode`), then in `fillPolygon`'s scanline loop compute each pixel's gradient-space parametric position (needs the gradient's own matrix, already parsed) and index the LUT — respecting `spreadMode` (`kPad`/`kReflect`/`kRepeat`) for out-of-`[0,1]` positions. This requires `IRenderer::fillPolygon`'s flat-`RgbaColor` signature to grow a gradient-fill variant (or a new `fillGradientPolygon` entry point) — a real interface change, not just an internal `SoftwareRenderer` tweak.
3. **Shapes with holes render as overlapping solid fills instead of correct counter-regions — HIGH impact specifically for text (letters like O/A/B/D/etc. render as solid blobs, not showing their counter) and any shape using a hole, MODERATE-HIGH fix effort.** Since `renderGlyph()` reuses `ShapeTessellator`, this is doubly important — it affects every piece of text this runtime draws, not just occasional shape art. **Fix:** replace the current "one polygon per MoveTo run" tessellation with a proper same-fill-style edge-collection pass — gather ALL edges sharing a given `fillStyle` across the whole shape record stream (not just one contiguous run) into a single multi-contour polygon set, and switch `fillPolygon`'s scanline rule from even-odd-per-polygon to a true nonzero/even-odd winding rule evaluated across all contours together in one scanline pass. This is a real tessellator rewrite, not a small patch — but given it affects all text rendering, likely worth prioritizing above bitmap decode (#5) and filters (#6) despite the effort.
4. **No blend modes beyond normal alpha "over" — MODERATE impact (corpus-dependent; matters wherever a title deliberately uses multiply/screen/add for lighting or glow effects), LOW-MODERATE fix effort once gradients (#2) establish a non-flat-color pixel path.** **Fix:** thread a `BlendMode` enum through `fillPolygon`/`blendPixel` (currently hardcoded to the one "over" formula at `SoftwareRenderer.cpp:11-16`) and implement the small set Flash actually exposes (normal/multiply/screen/add/darken/lighten/difference/etc. — each is a simple per-channel formula, this is mostly mechanical once the plumbing exists). Currently blocked from mattering at all by button/clip `BlendMode` bytes being read-and-discarded at parse time (`DefineButtonTag.cpp:81-82`) — parsing would need to actually store the value first.
5. ~~**Bitmap fills never decoded, rendered as flat gray.**~~ — **DONE (2026-08-31, Priority Fix List item #2).** `DefineBitsLossless`/`2`/`DefineBitsJpeg2`/`3` are now decoded to real RGBA8 pixel data (`src/swf/DefineBitsTag.h/.cpp`, a newly vendored `third_party/jpgd` JPEG decoder for the two JPEG variants) and rendered with real UV-mapped nearest-neighbor sampling through the full `ShapeTessellator` -> `SceneRenderer` -> `SoftwareRenderer`/`Nintendo3DSRenderer` pipeline — see `docs/renderer.md`'s "Bitmap rendering" section for the full writeup. Real-corpus verification found and fixed a second, genuinely surprising bug along the way: most of the corpus's `DefineBitsJpeg2`/`3` tags embed a known Flash-encoder quirk (a spurious mid-stream "EOI SOI" marker pair splitting shared JPEG tables from the actual scan data within one tag) that a strict decoder can't parse as-is — `stripErroneousEoiSoiMarkers()` fixes this, taking real-corpus JPEG bitmap decode success from ~14% (1 of 7 tags) to 100%. 446/446 tests passing (up from 434), zero regressions, 3DS cross-build clean. **Remaining scope limitation, stated plainly:** sampling is nearest-neighbor only, matching this entry's sibling AA/blend-mode divergences' "smaller, well-scoped follow-up" pattern rather than blocking on it — `smoothed` (bilinear) filtering stays unimplemented, no established texture-filtering precedent exists yet to extend.
6. **No filters (DropShadow/Blur/Glow/Bevel/etc.) — MODERATE impact for menu-heavy content (soft shadows/glows are a very common Flash-era UI polish element), HIGH fix effort (each filter is a real image-processing pass, not a simple flag).** Lowest priority of the visual gaps given the effort-to-corpus-relevance ratio is worse than #1-3; revisit only once a specific title is shown to visibly need one.
7. **No stroke joins/caps, thickness approximated by square-stamping — LOW-MODERATE impact (mostly visible on thick or sharply-angled strokes), LOW-MODERATE fix effort** once #1's AA work exists (stroke quality naturally improves once the underlying fill primitive supports proper edge coverage).

---

## Overall priority ranking (across all three tasks)

Ordered by estimated (visual/audio impact) × (how directly it's already confirmed to affect the specific title under test), highest first:

1. ~~**Timing: fractional-accumulator frame pacing** (Task 2, #1)~~ — **DONE (2026-08-30)**, see Task 2's own divergence list entry above. Concretely confirmed as a +20% speed error on `hobo.swf`; fixed with `runtime::FramePacer`, 9 new tests, zero regressions.
2. **Audio: wire `Sound.setVolume()` into the ndsp mix matrix** (Task 1, #1) — concretely confirmed as a dead code path the corpus already exercises every frame (the mute-toggle button), small fix once the pipeline itself is proven working (already done this session).
3. ~~**Rendering: anti-aliasing**~~ (Task 3, #1) — **DONE (2026-08-30), edge-pixel coverage AA — see Task 3's own divergence list entry above for the full writeup and its explicit horizontal-edge/gradient/stroke scope limitations.** Coverage-based scanline AA scoped to boundary pixel columns only (per this ranking entry's own original recommendation to prototype option (b) given the tight FPS budget), 3 new tests, 430/430 desktop tests passing, zero regressions, 3DS cross-build clean.
4. **Rendering: real gradient rendering** (Task 3, #2) — high visual impact, moderate cost, independent of the AA work.
5. **Rendering: shape-hole / multi-contour tessellation** (Task 3, #3) — high impact specifically because it also fixes all text rendering, but the largest architectural change of the rendering items; worth scoping carefully before starting.
6. ~~**Timing: real frame-skip/catch-up using a wall-clock read** (Task 2, #2)~~ — **DONE (2026-08-30)**, see Task 2's own divergence list entry above. Implemented ahead of this ranking's original sequencing (which had it waiting on the AA/gradient work first) per explicit user instruction to continue through Task 2's remaining items now; `FramePacer::advanceElapsed()`, 6 new tests, zero regressions.
7. Everything else (channel eviction, loop-count repeats, `SyncNoMultiple`, blend modes, bitmap fills, filters, non-MP3 codecs) — real gaps, correctly scoped, but lower-confidence impact without a specific target title's content confirming they're actually hit.

With #1 and #2 done, **Task 2 (FPS/timing) is fully resolved** except divergence #3 (hardcoded `kVBlankHz`), which both this audit and the fix session confirmed isn't worth its own effort, and divergence #4, which was never a bug. With #3 also done (edge-pixel coverage AA), the next item in this ranking is #4, Task 3's gradient-rendering work (divergence #2) — note that this pass deliberately did NOT touch `fillPolygonGradient()`, so that divergence remains exactly as described, unaffected by the AA fix.
