#include "platform/Log.h"

namespace flash3ds {

namespace {
LogLevel g_level = LogLevel::kInfo;
FILE* g_sink = stderr;

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

void Log::log(LogLevel level, const char* category, const char* fmt, ...) {
    if (level == LogLevel::kNone || level > g_level) {
        return;
    }
    if (!g_sink) {
        return;
    }
    std::fprintf(g_sink, "[%s] [%s] ", levelPrefix(level), category);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_sink, fmt, args);
    va_end(args);
    std::fprintf(g_sink, "\n");
}

}  // namespace flash3ds
