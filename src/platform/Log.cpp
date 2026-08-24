#include "platform/Log.h"

namespace flash3ds {

namespace {
LogLevel g_level = LogLevel::kInfo;
FILE* g_sink = stderr;
// Second, optional output path alongside g_sink -- entirely generic/
// portable (a plain function pointer, no platform type anywhere in this
// file), so flash3ds_core stays exactly as platform-agnostic as every
// other file in it. Ported forward 2026-08-24 from the Virtual Console
// layer's own 2026-08-19 diagnostic fix (see CLAUDE.md's "Virtual Console
// layer" section) so a 3DS-only file (which DOES have libctru access --
// e.g. nintendo3ds_main.cpp, already part of the separate flash3ds_3ds
// target) can additionally route every log line through
// svcOutputDebugString, visible in Citra/Azahar's own Log Viewer even
// when fprintf(stderr, ...) is a silent no-op on that target (see
// docs/3ds-toolchain.md -- newlib's _write is deliberately left
// unimplemented there). Discovered while diagnosing a "loads then
// silently quits" report: every LOG_ERROR on a failure path was
// completely invisible on-device before this.
Log::DebugCallback g_debugCallback = nullptr;

const char* levelPrefix(LogLevel level) {
    switch (level) {
        case LogLevel::kError: return "ERROR";
        case LogLevel::kWarn:  return "WARN ";
        case LogLevel::kInfo:  return "INFO ";
        case LogLevel::kDebug: return "DEBUG";
        default: return "?????";
    }
}
}  // namespace

void Log::setLevel(LogLevel level) { g_level = level; }
LogLevel Log::level() { return g_level; }
void Log::setSink(FILE* sink) { g_sink = sink ? sink : stderr; }
void Log::setDebugCallback(DebugCallback callback) { g_debugCallback = callback; }

void Log::log(LogLevel level, const char* category, const char* fmt, ...) {
    if (level == LogLevel::kNone || level > g_level) {
        return;
    }

    if (g_sink) {
        std::fprintf(g_sink, "[%s] [%s] ", levelPrefix(level), category);
        va_list args;
        va_start(args, fmt);
        std::vfprintf(g_sink, fmt, args);
        va_end(args);
        std::fprintf(g_sink, "\n");
    }

    if (g_debugCallback) {
        char buf[512];
        va_list args2;
        va_start(args2, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args2);
        va_end(args2);
        g_debugCallback(level, category, buf);
    }
}

}  // namespace flash3ds
