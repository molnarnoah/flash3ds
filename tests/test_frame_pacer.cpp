// test_frame_pacer.cpp
//
// Fidelity-audit TASK 2, divergence #1 (docs/flash-fidelity-audit.md,
// 2026-08-30): unit tests for runtime::FramePacer, the fractional-
// accumulator replacement for nintendo3ds_main.cpp's old fixed
// `std::lround(60.0 / swfFrameRate)` vblank divisor.
//
// The property under test throughout is DRIFT-FREE LONG-RUN AVERAGING:
// summing FramePacer::advanceTick()'s return value over any run of ticks
// that's a multiple of the rate's "beat cycle" (lcm(logicalHz, hostHz) /
// logicalHz ticks) must equal EXACTLY logicalHz * ticks / hostHz logical
// frames -- not approximately, exactly, since the accumulator is pure
// double arithmetic over small integer-valued rates with no rounding
// step. This is the core guarantee the old lround-based code did NOT
// have (see FramePacer.h's top-of-file comment for the concrete
// `hobo.swf` 25fps -> 30fps counterexample that motivated this fix).

#include <cmath>
#include <vector>

#include "TestFramework.h"
#include "runtime/FramePacer.h"

using flash3ds::runtime::FramePacer;

namespace {

// Runs `ticks` calls to advanceTick(), returning the total number of
// logical-frame advances reported across all of them.
int totalAdvancesOver(FramePacer& pacer, int ticks) {
    int total = 0;
    for (int i = 0; i < ticks; ++i) {
        total += pacer.advanceTick();
    }
    return total;
}

}  // namespace

TEST_CASE(FramePacer_Hobo25FpsAt60HzHost_AveragesToExactly25Over1SecondNotThirty) {
    // The concrete bug this fix addresses: hobo.swf declares 25fps. The
    // OLD code (lround(60/25)=lround(2.4)=2) advanced every 2 vblanks,
    // i.e. 30fps -- a systematic +20% error. The fractional accumulator
    // must average to exactly 25 advances over 60 ticks (one second of
    // 60Hz host ticks), not 30.
    FramePacer pacer(25.0, 60.0);
    CHECK_EQ(totalAdvancesOver(pacer, 60), 25);
}

TEST_CASE(FramePacer_24FpsAt60HzHost_AveragesToExactly24Over1SecondNotTwenty) {
    // Another concrete divergence-table entry from the audit doc: the OLD
    // code gave lround(60/24)=lround(2.5)=3 (round-half-away-from-zero)
    // -> 20fps actual, a -16.7% error. Must average to exactly 24.
    FramePacer pacer(24.0, 60.0);
    CHECK_EQ(totalAdvancesOver(pacer, 60), 24);
}

TEST_CASE(FramePacer_ExactDivisorRates_StillExactlyRight) {
    // Rates that exactly divide 60 (20/15/12/30/60) were already correct
    // under the old fixed-divisor code -- confirm the new accumulator
    // doesn't regress them.
    for (double rate : {20.0, 15.0, 12.0, 30.0, 60.0, 10.0}) {
        FramePacer pacer(rate, 60.0);
        int total = totalAdvancesOver(pacer, 60);
        CHECK_EQ(total, static_cast<int>(rate));
    }
}

TEST_CASE(FramePacer_NonIntegerRate_ZeroDriftOverManyTicks) {
    // A frame rate that doesn't even divide 60 as a whole number (12.5) --
    // over a single second (60 ticks) the running total can legitimately
    // sit half a frame off (12 instead of 12.5, since you can't emit half
    // an advance), but summed over enough ticks to complete a full beat
    // cycle (600 ticks = 10 seconds, since 12.5 * 10 = 125 exactly), the
    // total must land exactly on the mathematically expected count with
    // zero cumulative drift.
    FramePacer pacer(12.5, 60.0);
    int total = totalAdvancesOver(pacer, 600);
    CHECK_EQ(total, 125);
}

TEST_CASE(FramePacer_LongRun_NeverAccumulatesUnboundedDrift) {
    // Regression guard against a naive implementation bug (e.g. resetting
    // the accumulator to 0 instead of subtracting hostHz, which would
    // silently discard fractional carry and reintroduce drift): run many
    // thousands of ticks at a non-exact-divisor rate and confirm the
    // accumulator itself never grows outside a single host-tick's width,
    // and the average rate over the whole run matches the declared rate
    // to within one frame (the maximum possible instantaneous phase
    // error for a correct fractional accumulator).
    FramePacer pacer(25.0, 60.0);
    int total = totalAdvancesOver(pacer, 60000);  // 1000 seconds
    double expected = 25.0 * 60000.0 / 60.0;      // = 25000.0 exactly
    CHECK(std::abs(static_cast<double>(total) - expected) <= 1.0);
    CHECK(pacer.accumulatorForTesting() >= 0.0);
    CHECK(pacer.accumulatorForTesting() < pacer.hostHz());
}

TEST_CASE(FramePacer_LogicalRateExceedsHostRate_CanAdvanceMoreThanOncePerTick) {
    // An SWF declaring a frame rate faster than the host's own tick rate
    // is unusual but legal per the SWF FrameRate field -- FramePacer must
    // be able to report more than one advance for a single advanceTick()
    // call in that case (this is what lets a future frame-skip/catch-up
    // implementation reuse this same accumulator, per FramePacer.h's
    // scope note), and the long-run average must still be exact.
    FramePacer pacer(90.0, 60.0);
    int sawMultiAdvanceTick = 0;
    int total = 0;
    for (int i = 0; i < 60; ++i) {
        int advances = pacer.advanceTick();
        if (advances > 1) ++sawMultiAdvanceTick;
        total += advances;
    }
    CHECK_EQ(total, 90);
    CHECK(sawMultiAdvanceTick > 0);
}

TEST_CASE(FramePacer_NonPositiveLogicalRate_FallsBackTo12Hz) {
    // Matches nintendo3ds_main.cpp's own pre-existing "movie declares a
    // bogus/zero rate" fallback (a malformed or absent SWF FrameRate
    // field) -- FramePacer must apply the same fallback internally so a
    // caller doesn't need to duplicate the check.
    FramePacer zero(0.0, 60.0);
    CHECK_EQ(zero.logicalHz(), 12.0);
    FramePacer negative(-5.0, 60.0);
    CHECK_EQ(negative.logicalHz(), 12.0);
}

TEST_CASE(FramePacer_NonPositiveHostRate_FallsBackTo60Hz) {
    FramePacer zero(25.0, 0.0);
    CHECK_EQ(zero.hostHz(), 60.0);
    FramePacer negative(25.0, -1.0);
    CHECK_EQ(negative.hostHz(), 60.0);
}

TEST_CASE(FramePacer_FreshInstance_StartsAtZeroAccumulator) {
    FramePacer pacer(25.0, 60.0);
    CHECK_EQ(pacer.accumulatorForTesting(), 0.0);
}

// Fidelity-audit TASK 2, divergence #2 (2026-08-30): real wall-clock-
// driven frame-skip/catch-up. See FramePacer.h's top-of-file comment for
// the full design. All expected values below were derived by simulating
// the exact same formula in Python first (not guessed, not back-derived
// from a first failing run) -- see this task's own session notes.

TEST_CASE(FramePacer_AdvanceElapsed_OneVblankWorthOfTime_ReportsOneFrameAt60Hz) {
    // The simplest real-world case: rendering keeps up, so each loop
    // iteration really is ~1/60s apart. At a 60fps movie this should
    // report exactly 1 frame every call, matching intuition even though
    // this uses the independent advanceElapsed() formula, not
    // advanceTick()'s.
    FramePacer pacer(60.0, 60.0);
    for (int i = 0; i < 10; ++i) {
        CHECK_EQ(pacer.advanceElapsed(1.0 / 60.0), 1);
    }
}

TEST_CASE(FramePacer_AdvanceElapsed_SlowRenderIteration_ReportsMultipleFramesAtOnce) {
    // The actual bug divergence #2 fixes: if one loop iteration's render
    // work took 3 vblanks' worth of real time (a slow frame) before the
    // next call, the movie owes 3 vblanks' worth of logical progress in
    // that single call, not just 1 -- unlike advanceTick(), which has no
    // way to know real time diverged from its 1/hostHz assumption.
    FramePacer pacer(60.0, 60.0);
    CHECK_EQ(pacer.advanceElapsed(3.0 / 60.0), 3);
    CHECK_EQ(pacer.accumulatorForTesting(), 0.0);
}

TEST_CASE(FramePacer_AdvanceElapsed_LongStall_ClampsToMaxCatchUpSecondsNotFullBacklog) {
    // A very long gap (app suspended, debugger breakpoint, ~5 real
    // seconds) must NOT try to instantly replay rate*5 logical frames in
    // one call -- it should clamp to kMaxCatchUpSeconds worth instead,
    // and that clamped amount should divide out with zero remainder
    // (confirming the clamp happens before accumulation, not after).
    FramePacer pacer(60.0, 60.0);
    int frames = pacer.advanceElapsed(5.0);
    CHECK_EQ(frames, static_cast<int>(60.0 * FramePacer::kMaxCatchUpSeconds));  // 15
    CHECK_EQ(pacer.accumulatorForTesting(), 0.0);
}

TEST_CASE(FramePacer_AdvanceElapsed_NegativeElapsed_ClampsToZero) {
    // A wall-clock read going "backwards" (e.g. crossing an RTC
    // adjustment, or simple defensive coding against a bad timer read)
    // must not corrupt the accumulator or report a negative/nonsensical
    // frame count.
    FramePacer pacer(25.0, 60.0);
    CHECK_EQ(pacer.advanceElapsed(-1.0), 0);
    CHECK_EQ(pacer.accumulatorForTesting(), 0.0);
}

TEST_CASE(FramePacer_AdvanceElapsed_InterleavedWithAdvanceTick_SharesConsistentAccumulator) {
    // advanceTick() and advanceElapsed() are documented to share the same
    // accumulator and unit scale (both measured against hostHz_, not
    // 1.0) -- confirm that interleaving them on one instance produces the
    // same total as an equivalent run of pure advanceTick() calls, i.e.
    // they really are unit-compatible, not just individually correct.
    // 24fps/60Hz split as 45 advanceTick() calls (=45/60s of real time)
    // plus one advanceElapsed(15.0/60.0) call for the remaining 15/60s --
    // together exactly one full second, matching 60 pure advanceTick()
    // calls.
    FramePacer mixed(24.0, 60.0);
    int mixedTotal = 0;
    for (int i = 0; i < 45; ++i) mixedTotal += mixed.advanceTick();
    mixedTotal += mixed.advanceElapsed(15.0 / 60.0);

    FramePacer pureTick(24.0, 60.0);
    int pureTotal = 0;
    for (int i = 0; i < 60; ++i) pureTotal += pureTick.advanceTick();

    CHECK_EQ(mixedTotal, pureTotal);
    CHECK_EQ(mixedTotal, 24);
    CHECK_EQ(mixed.accumulatorForTesting(), 0.0);
}

TEST_CASE(FramePacer_AdvanceElapsed_VariableIrregularElapsedTimes_ZeroDriftOverTwoSeconds) {
    // Real production calls won't report perfectly uniform elapsed times
    // every iteration (that's the whole point of this divergence) -- feed
    // a repeating pattern of uneven-but-in-cap elapsed values (three
    // 1/60s ticks then one 2/60s tick, 24 times = exactly 2.0 real
    // seconds total) and confirm the cumulative frame count still lands
    // exactly on the mathematically expected total with zero drift.
    FramePacer pacer(20.0, 60.0);
    int total = 0;
    for (int cycle = 0; cycle < 24; ++cycle) {
        total += pacer.advanceElapsed(1.0 / 60.0);
        total += pacer.advanceElapsed(1.0 / 60.0);
        total += pacer.advanceElapsed(1.0 / 60.0);
        total += pacer.advanceElapsed(2.0 / 60.0);
    }
    CHECK_EQ(total, 40);  // 20fps * 2.0s
    CHECK_EQ(pacer.accumulatorForTesting(), 0.0);
}
