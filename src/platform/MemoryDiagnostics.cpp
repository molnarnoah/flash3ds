#include "platform/MemoryDiagnostics.h"

#include "platform/Log.h"

#ifdef __3DS__
#include <3ds.h>
#else
#include <cstdio>
#include <cstring>
#endif

namespace flash3ds::platform {

namespace {
bool g_enabled = false;
long g_peakKb = -1;
long g_firstKb = -1;
long g_prevKb = -1;
}  // namespace

#ifdef __3DS__

long currentResidentKb() {
    // osGetMemRegionUsed() returns bytes; this project's convention
    // (matching tools/mem_profile_check/main.cpp) reports KB throughout.
    return static_cast<long>(osGetMemRegionUsed(MEMREGION_APPLICATION) / 1024);
}

long currentFreeKb() {
    return static_cast<long>(osGetMemRegionFree(MEMREGION_APPLICATION) / 1024);
}

#else

long currentResidentKb() {
    std::FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long kb = -1;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "VmRSS:", 6) == 0) {
            std::sscanf(line, "VmRSS: %ld kB", &kb);
            break;
        }
    }
    std::fclose(f);
    return kb;
}

long currentFreeKb() {
    // Deliberately unavailable on desktop — see this file's header
    // comment on currentFreeKb() for why a host process's "free heap"
    // isn't a meaningful equivalent to the 3DS's capped app-heap.
    return -1;
}

#endif

void setEnabled(bool enabled) { g_enabled = enabled; }
bool isEnabled() { return g_enabled; }

void resetPeak() {
    g_peakKb = -1;
    g_firstKb = -1;
    g_prevKb = -1;
}

long peakKb() { return g_peakKb; }

void checkpoint(const char* label) {
    if (!g_enabled) return;

    long kb = currentResidentKb();
    if (kb < 0) {
        LOG_INFO("MEMDIAG", "%-40s resident=unavailable-on-this-platform", label);
        return;
    }

    if (g_firstKb < 0) g_firstKb = kb;
    if (kb > g_peakKb) g_peakKb = kb;
    long deltaFromPrev = (g_prevKb < 0) ? 0 : (kb - g_prevKb);
    long deltaFromFirst = kb - g_firstKb;
    g_prevKb = kb;

    long freeKb = currentFreeKb();
    if (freeKb >= 0) {
        LOG_INFO("MEMDIAG",
                  "%-40s resident=%8ld KB  delta=%+8ld KB  cumulative=%+8ld KB  "
                  "free=%8ld KB  peak=%8ld KB",
                  label, kb, deltaFromPrev, deltaFromFirst, freeKb, g_peakKb);
    } else {
        LOG_INFO("MEMDIAG",
                  "%-40s resident=%8ld KB  delta=%+8ld KB  cumulative=%+8ld KB  peak=%8ld KB",
                  label, kb, deltaFromPrev, deltaFromFirst, g_peakKb);
    }
}

}  // namespace flash3ds::platform
