// Log.h
//
// Minimal, dependency-free, configurable logging facility.
//
// Clean-room implementation. This is a generic logging utility; it is not
// derived from, and shares no code with, any Shift-DX/gameswf binary.
//
// Usage:
//   LOG_INFO("SWF", "Loading movie '%s'", filename.c_str());
//   LOG_DEBUG("AVM1", "Push %d", value);
//
// Category strings follow the [SWF] / [AVM1] / [FLASH] / [INPUT] style
// requested by the project spec (docs/architecture.md, section 22).

#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>

namespace flash3ds {

enum class LogLevel {
    kNone = 0,
    kError = 1,
    kWarn = 2,
    kInfo = 3,
    kDebug = 4,
};

class Log {
public:
    // Global log level. Defaults to kInfo. Tools/tests can change this at
    // startup (e.g. `Log::setLevel(LogLevel::kDebug)` for verbose AVM1
    // opcode tracing once the VM exists).
    static void setLevel(LogLevel level);
    static LogLevel level();

    // Redirect log output. Defaults to stderr. Tests can point this at an
    // in-memory buffer to assert on emitted log lines.
    static void setSink(FILE* sink);

    // Optional SECOND output path, called with the already-vsnprintf'd
    // message (no trailing newline) in addition to (not instead of)
    // whatever setSink() points at. Generic/portable by design -- this
    // header has no platform-specific type anywhere in it -- so a
    // platform backend can register e.g. an on-device debug-output SVC
    // call without flash3ds_core itself gaining any platform dependency.
    // Pass nullptr to clear. Ported forward 2026-08-24 from the Virtual
    // Console layer's own 2026-08-19 diagnostic fix (see CLAUDE.md's
    // "Virtual Console layer" section): on the 3DS target, setSink()'s
    // default (stderr) is a silent no-op (newlib's _write is deliberately
    // left unimplemented -- see docs/3ds-toolchain.md), so without a
    // second path every LOG_ERROR on that target was completely invisible
    // -- discovered while diagnosing a "loads then silently quits" report.
    using DebugCallback = void (*)(LogLevel level, const char* category, const char* message);
    static void setDebugCallback(DebugCallback callback);

    static void log(LogLevel level, const char* category, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 3, 4)))
#endif
        ;
};

}  // namespace flash3ds

#define FLASH3DS_LOG(level, category, ...) \
    ::flash3ds::Log::log(level, category, __VA_ARGS__)

#define LOG_ERROR(category, ...) \
    FLASH3DS_LOG(::flash3ds::LogLevel::kError, category, __VA_ARGS__)
#define LOG_WARN(category, ...) \
    FLASH3DS_LOG(::flash3ds::LogLevel::kWarn, category, __VA_ARGS__)
#define LOG_INFO(category, ...) \
    FLASH3DS_LOG(::flash3ds::LogLevel::kInfo, category, __VA_ARGS__)
#define LOG_DEBUG(category, ...) \
    FLASH3DS_LOG(::flash3ds::LogLevel::kDebug, category, __VA_ARGS__)
