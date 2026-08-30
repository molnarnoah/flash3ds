// FramePacer.h
//
// Fidelity-audit TASK 2, divergence #1 (docs/flash-fidelity-audit.md,
// 2026-08-30): fixes the systematic frame-rate error in the 3DS entry
// point's movie-timeline pacing.
//
// Platform-independent (no libctru dependency) so it can be unit-tested on
// desktop, matching this project's established pattern of keeping the
// timing/logic core testable and only the libctru glue
// (src/platform/nintendo3ds_main.cpp) untestable.
//
// The bug this replaces: `nintendo3ds_main.cpp` computed a single integer
// "advance the movie every N real vblanks" divisor via
// `std::lround(60.0 / swfFrameRate)`, then advanced the movie's own
// timeline exactly once every N vblanks forever. Whenever the SWF's
// declared frame rate isn't an exact divisor of the 3DS's ~60Hz vblank
// rate, this rounds to the nearest achievable divisor and STAYS there --
// a permanent, systematic playback-speed error, not just occasional
// jitter. Confirmed on real content: `hobo.swf` declares 25.00fps;
// `lround(60/25) = lround(2.4) = 2`, so the old code advanced the movie
// every 2 vblanks -> 30fps actual, a +20% speedup for the whole game,
// forever.
//
// The fix: a classic fractional accumulator (Bresenham/DDA-style frame
// pacer). Every real host tick (vblank), add the logical (SWF-declared)
// rate to a running accumulator; whenever the accumulator reaches or
// exceeds the host rate, subtract the host rate and report one logical
// frame's worth of advance. Over any long run this converges to EXACTLY
// the declared rate on average (verified: FramePacerTest below simulates
// many real-world frame rates over both a single second and many seconds
// and confirms zero cumulative drift), at the cost of slightly uneven
// per-tick spacing (typically alternating between advancing on N and N+1
// consecutive ticks) -- imperceptible in practice, and unconditionally
// more accurate than the old fixed, permanently-wrong divisor.
//
// TASK 2 divergence #2 (2026-08-30, docs/flash-fidelity-audit.md): real
// wall-clock-driven frame-skip/catch-up, added via `advanceElapsed()`
// below. `advanceTick()` above assumes exactly one host tick's worth of
// real time (1/hostHz seconds) elapsed between calls -- true when
// rendering keeps up with vsync, false whenever a frame's render work
// takes longer than one vblank's budget (this project's own
// `docs/performance-pacing.md` measured real render times well over one
// vblank before its own fixes, and could again with heavier content).
// When that happens, calling `advanceTick()` once per loop iteration
// under-advances the movie relative to real elapsed time: the whole
// timeline's effective playback rate silently slows down in lockstep
// with rendering, rather than staying at the SWF's declared rate with
// the VISUAL frame rate being what drops. `advanceElapsed()` fixes this
// by taking the actual measured elapsed real time as input instead of
// assuming it, so a slow render still produces the correct number of
// logical-frame advances (running scripts/sounds the right number of
// times) even though only one `render()` call happens per loop
// iteration -- i.e. it's the "tick scripts to catch up, but the caller
// naturally only renders the resulting final state once" pattern this
// divergence's audit-doc entry describes, achieved for free by NOT
// rendering per intervening logical frame (nintendo3ds_main.cpp's loop
// already renders exactly once per real iteration; only the *tick count*
// changes here).
//
// `advanceElapsed()` shares `advanceTick()`'s accumulator and internal
// units (both compare accumulated progress against `hostHz_`, not
// against 1.0) so the two methods stay mutually consistent if ever
// called on the same instance -- deliberately NOT implemented by having
// one call the other, since `advanceTick()`'s existing behavior (and its
// own tests, which predate this divergence) must not change by even a
// floating-point rounding ULP; see FramePacer.cpp for why a
// division-based reformulation was tried and rejected (real, confirmed
// off-by-one drift for some rates over long runs -- caught by simulating
// the reformulation in Python before writing any C++, not discovered via
// a failing test after the fact).
//
// Catch-up is deliberately CAPPED (`kMaxCatchUpSeconds`): a very long
// stall between calls (the app suspended via the HOME menu, a debugger
// breakpoint, anything far outside normal frame-to-frame timing) would
// otherwise try to instantly replay an enormous backlog of logical
// frames in one burst -- itself potentially slow (each advanced frame
// runs real AVM1 scripts/sound triggers) and not how a real player
// generally behaves after a long suspend anyway (resume at the current
// moment, don't fast-forward through everything that was missed). Time
// beyond the cap is simply dropped, not queued for a later call.

#pragma once

namespace flash3ds::runtime {

class FramePacer {
public:
    // `logicalHz` is the movie's own declared frame rate (e.g. from
    // `Movie::frameRateFps()`); a value <= 0 falls back to 12.0, matching
    // `nintendo3ds_main.cpp`'s own pre-existing "movie declares a bogus
    // rate" fallback. `hostHz` is the real tick rate `advanceTick()` will
    // be called at (the 3DS's vblank rate, nominally 60.0 -- see
    // divergence #3 in docs/flash-fidelity-audit.md for why the *actual*
    // 3DS LCD refresh, 59.83Hz, isn't used here; not worth the precision
    // for this fix). A `hostHz` <= 0 also falls back to 60.0.
    explicit FramePacer(double logicalHz, double hostHz = 60.0);

    // Call exactly once per real host tick (once per vblank in
    // production). Returns how many logical (SWF) frames should be
    // advanced as a result of this tick -- almost always 0 or 1; can
    // exceed 1 if `logicalHz` is greater than `hostHz` (an SWF declaring a
    // frame rate faster than the host's own tick rate, unusual but legal
    // per the SWF spec's FrameRate field).
    int advanceTick();

    // Call once per real loop iteration with the ACTUAL elapsed real time
    // (in seconds) since the previous call -- see this file's top-of-file
    // comment for the full TASK 2 divergence #2 design/rationale. Returns
    // how many logical (SWF) frames should be advanced; can be 0, 1, or
    // more than 1 if enough real time passed to owe multiple frames (a
    // slow-rendering iteration, or `logicalHz` exceeding `hostHz`).
    // `elapsedSeconds` is clamped to [0, kMaxCatchUpSeconds] before use.
    int advanceElapsed(double elapsedSeconds);

    // Maximum real elapsed time a single `advanceElapsed()` call will
    // treat as "owed" catch-up -- see this file's top-of-file comment for
    // why longer stalls are capped rather than fully caught up.
    static constexpr double kMaxCatchUpSeconds = 0.25;

    double logicalHz() const { return logicalHz_; }
    double hostHz() const { return hostHz_; }

    // Exposed for tests only (confirms no unbounded growth/drift over a
    // long run) -- production code has no reason to read this.
    double accumulatorForTesting() const { return accumulator_; }

private:
    double logicalHz_;
    double hostHz_;
    double accumulator_ = 0.0;
};

}  // namespace flash3ds::runtime
