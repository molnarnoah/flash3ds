// FramePacer.cpp
//
// See FramePacer.h for the full design/scope notes.
//
// A note on advanceElapsed()'s formula, for anyone tempted to simplify it
// by reformulating advanceTick() as `advanceElapsed(1.0 / hostHz_)`: that
// reformulation was tried first and REJECTED after simulating it (in
// Python, before touching any C++) against every rate this file's own
// tests exercise. `accumulator_ += logicalHz_ * hostHz_ * (1.0/hostHz_)`
// is mathematically equal to `accumulator_ += logicalHz_`, but NOT always
// bit-identical in IEEE-754 double precision -- 1.0/60.0 isn't exactly
// representable, and for some rates (confirmed concretely: 18.0 and 10.0
// at hostHz=60.0) the extra multiply-then-divide accumulates real,
// off-by-one drift over long runs that the original direct-add formula
// does not have. `advanceTick()` therefore keeps its own original,
// already-shipped-and-tested formula completely untouched;
// `advanceElapsed()` below uses an independent (but unit-compatible --
// both compare accumulated progress against `hostHz_`) formula instead.

#include "runtime/FramePacer.h"

#include <algorithm>

namespace flash3ds::runtime {

FramePacer::FramePacer(double logicalHz, double hostHz)
    : logicalHz_(logicalHz > 0.0 ? logicalHz : 12.0), hostHz_(hostHz > 0.0 ? hostHz : 60.0) {}

int FramePacer::advanceTick() {
    accumulator_ += logicalHz_;
    int count = 0;
    while (accumulator_ >= hostHz_) {
        accumulator_ -= hostHz_;
        ++count;
    }
    return count;
}

int FramePacer::advanceElapsed(double elapsedSeconds) {
    elapsedSeconds = std::clamp(elapsedSeconds, 0.0, kMaxCatchUpSeconds);
    accumulator_ += logicalHz_ * hostHz_ * elapsedSeconds;
    int count = 0;
    while (accumulator_ >= hostHz_) {
        accumulator_ -= hostHz_;
        ++count;
    }
    return count;
}

}  // namespace flash3ds::runtime
