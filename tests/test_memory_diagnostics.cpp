// test_memory_diagnostics.cpp — M2 RAM-validation phase (2026-08-24).
//
// This runs as a desktop (non-__3DS__) build, so it exercises the
// /proc/self/status VmRSS implementation specifically (see
// MemoryDiagnostics.cpp) -- the 3DS osGetMemRegionUsed() path can't be
// exercised outside a real 3DS/emulator, same standing limitation as every
// other 3DS-specific code path in this project (documented, not silently
// assumed correct).

#include "platform/MemoryDiagnostics.h"

#include "TestFramework.h"

namespace memdiag = flash3ds::platform;

TEST_CASE(MemoryDiagnostics_DisabledByDefault) {
    // A fresh process (this test running first, or any test after a prior
    // test forgot to disable) could observe either state depending on test
    // order, so explicitly set it rather than asserting the ambient
    // default -- the real assertion here is that setEnabled()/isEnabled()
    // round-trip correctly.
    memdiag::setEnabled(false);
    CHECK(!memdiag::isEnabled());
    memdiag::setEnabled(true);
    CHECK(memdiag::isEnabled());
    memdiag::setEnabled(false);
    CHECK(!memdiag::isEnabled());
}

TEST_CASE(MemoryDiagnostics_CurrentResidentKb_ReturnsPlausibleValueOnLinux) {
    // On desktop Linux (this test's actual build target), VmRSS should be
    // readable and positive -- a real running test binary always has some
    // resident memory. This is the direct check that the /proc parsing
    // path works at all, independent of the enabled/checkpoint machinery.
    long kb = memdiag::currentResidentKb();
    CHECK(kb > 0);
}

TEST_CASE(MemoryDiagnostics_ResetPeak_ClearsPeakUntilNextCheckpoint) {
    memdiag::setEnabled(true);
    memdiag::resetPeak();
    CHECK_EQ(memdiag::peakKb(), -1L);  // no checkpoint() has run since reset

    memdiag::checkpoint("test checkpoint 1");
    long firstPeak = memdiag::peakKb();
    CHECK(firstPeak > 0);

    memdiag::checkpoint("test checkpoint 2");
    CHECK(memdiag::peakKb() >= firstPeak);  // peak never decreases within a session

    memdiag::resetPeak();
    CHECK_EQ(memdiag::peakKb(), -1L);
    memdiag::setEnabled(false);
}

TEST_CASE(MemoryDiagnostics_Checkpoint_NoOpAndSafeWhenDisabled) {
    // The whole point of the enabled/disabled gate: calling checkpoint()
    // while disabled must not crash, must not allocate meaningfully, and
    // must not perturb peakKb()/isEnabled() state -- it's simply a no-op.
    memdiag::setEnabled(false);
    memdiag::resetPeak();
    memdiag::checkpoint("should be ignored");
    CHECK_EQ(memdiag::peakKb(), -1L);
    CHECK(!memdiag::isEnabled());
}
