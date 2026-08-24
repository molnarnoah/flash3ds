// MemoryDiagnostics.h
//
// Minimal, debug-only heap-usage checkpoint mechanism (M2 RAM-validation
// phase, 2026-08-24 — see docs/memory-audit.md and docs/known-limitations.md
// for the measured context this exists to keep validating going forward).
//
// This turns the ad hoc, standalone-desktop-tool-only measurement approach
// used so far (tools/mem_profile_check/main.cpp) into something the ACTUAL
// runtime — including the real 3DS build — can report from itself, so a
// person running the real `.3dsx` can get real heap numbers without needing
// a separate host-side tool. It is diagnostic-only: calling checkpoint()
// when disabled (the default) costs one branch and nothing else, and
// nothing in this file changes any allocation, ownership, or lifetime
// decision anywhere else in the runtime.
//
// Desktop (Linux) implementation reads /proc/self/status's VmRSS line —
// same source and same caveats as tools/mem_profile_check/main.cpp (a
// process-wide RSS proxy, not a 3DS-accurate number — see
// docs/memory-audit.md §5 for why the two numbers don't match 1:1).
//
// 3DS implementation uses libctru's osGetMemRegionUsed(MEMREGION_APPLICATION)/
// osGetMemRegionFree(MEMREGION_APPLICATION) — the actual on-device
// application-heap accounting, not a proxy. This is the real number task01
// asked for ("actual 3DS application heap", as distinct from host RSS/
// emulator memory/linear heap/GPU-VRAM, which this does NOT report — see
// this file's own doc comments on currentResidentKb()/currentFreeKb() for
// exactly which of those five categories each platform's implementation
// measures).
//
// Any other platform (or a desktop build where /proc is unavailable, e.g.
// non-Linux) reports -1 ("unavailable") from both functions rather than
// guessing — never silently returns a fabricated number.

#pragma once

namespace flash3ds::platform {

// Current resident memory, in KB:
//   - Desktop Linux: VmRSS from /proc/self/status (process-wide host RSS —
//     a PROXY for likely 3DS heap pressure, not a direct 3DS measurement;
//     see docs/memory-audit.md §5).
//   - 3DS: osGetMemRegionUsed(MEMREGION_APPLICATION) — the REAL on-device
//     application-heap usage, not a proxy.
//   - Anything else: -1 ("unavailable" — never fabricated).
long currentResidentKb();

// Currently free heap, in KB, within the same region currentResidentKb()
// reports on:
//   - Desktop Linux: not meaningfully defined (a host process's "free
//     heap" isn't the same concept as a capped embedded app-heap) — always
//     -1 here, deliberately, rather than reporting something misleading.
//   - 3DS: osGetMemRegionFree(MEMREGION_APPLICATION) — the real number.
//   - Anything else: -1.
long currentFreeKb();

// Enable/disable checkpoint() logging at runtime. Defaults to DISABLED, so
// a normal run (release or otherwise) pays zero extra log output and the
// only cost of a checkpoint() call anywhere in the codebase is one branch.
// The 3DS main loop and the desktop CLI both expose a way to turn this on
// (see nintendo3ds_main.cpp/tools/flash_runtime/main.cpp) rather than it
// being compiled out entirely, so a real device/emulator run can produce
// these numbers without a special build.
void setEnabled(bool enabled);
bool isEnabled();

// Resets peak-tracking and the "cumulative since start" baseline. Call
// once at the true start of the sequence being measured (session/process
// start) — see checkpoint()'s own doc comment for what gets reset.
void resetPeak();

// Highest currentResidentKb() observed across every checkpoint() call
// since the last resetPeak(), or -1 if resetPeak() was never called or
// currentResidentKb() is unavailable on this platform.
long peakKb();

// Logs one checkpoint via LOG_INFO("MEMDIAG", ...) when enabled (a no-op
// otherwise): `label`, current resident KB, delta from the immediately
// previous checkpoint, cumulative delta since resetPeak(), current free KB
// (platform-permitting), and updates peakKb(). Intended call sites (per
// task01.txt's own requested checkpoint set): startup, after SWF load,
// after first frame, after first render, after audio backend init, and
// anywhere else a caller wants a data point — this is a generic mechanism,
// not hardcoded to those five call sites.
void checkpoint(const char* label);

}  // namespace flash3ds::platform
