// mem_profile_check/main.cpp
//
// STANDALONE diagnostic (not part of flash3ds_core, not registered in
// CMakeLists.txt — compiled and run ad hoc). Rewritten from scratch
// 2026-08-21 for the COMPLETE CURRENT-STATE AUDIT: the previous version of
// this tool (and its previously-measured "145MB" finding) existed only in
// an earlier session state that this workspace has since reverted away
// from (see docs/current-state-audit.md's "Environment discontinuity"
// section) — that number must NOT be reused. This is a fresh measurement
// against the ACTUAL current source tree (HEAD 6b44405 + uncommitted
// event-dispatch/real-game-corpus changes).
//
// Reads /proc/self/status's VmRSS line (Linux-specific; this is a desktop
// diagnostic, not a 3DS one — see docs/memory-audit.md for why desktop RSS
// is used as a PROXY for likely 3DS heap pressure, not a direct measurement
// of 3DS behavior, and for the known ways the two differ).
//
// Usage: mem_profile_check <path-to-swf> [<path-to-swf> ...]
// Prints one checkpoint table per file.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "renderer/SceneRenderer.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/Movie.h"
#include "runtime/MovieClipInstance.h"
#include "swf/SwfLoader.h"

using namespace flash3ds;

namespace {

long vmRssKb() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            long kb = 0;
            std::sscanf(line.c_str(), "VmRSS: %ld kB", &kb);
            return kb;
        }
    }
    return -1;
}

struct Checkpoint {
    std::string label;
    long rssKb;
};

std::vector<Checkpoint> g_checkpoints;
long g_baseline = 0;

void checkpoint(const std::string& label) {
    long kb = vmRssKb();
    g_checkpoints.push_back({label, kb});
    long deltaFromPrev = g_checkpoints.size() > 1
        ? kb - g_checkpoints[g_checkpoints.size() - 2].rssKb
        : kb - g_baseline;
    long deltaFromBaseline = kb - g_baseline;
    std::printf("  [%-42s] RSS=%8ld KB  delta=%+8ld KB  cumulative=%+8ld KB\n",
                label.c_str(), kb, deltaFromPrev, deltaFromBaseline);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path-to-swf> [<path-to-swf> ...]\n", argv[0]);
        return 1;
    }

    g_baseline = vmRssKb();
    std::printf("startup baseline RSS = %ld KB\n\n", g_baseline);

    for (int i = 1; i < argc; ++i) {
        const std::string path = argv[i];
        std::printf("=== %s ===\n", path.c_str());
        g_checkpoints.clear();

        checkpoint("process start (this file's turn)");

        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "  could not open %s\n", path.c_str());
            continue;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string raw = ss.str();
        checkpoint("after reading raw SWF bytes into memory");
        std::printf("  raw file size = %zu bytes\n", raw.size());

        auto movie = swf::SwfLoader::loadSwf(
            reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
        checkpoint("after SwfLoader::loadSwf (decompress + tag scan)");
        if (!movie || !movie->valid) {
            std::fprintf(stderr, "  SwfLoader failed: %s\n",
                        movie ? movie->errorMessage.c_str() : "(null)");
            continue;
        }
        std::printf("  decompressed tag-stream bytes = %zu, tag count = %zu\n",
                    movie->data.size(), movie->tags.size());

        auto dict = runtime::CharacterDictionary::build(*movie);
        checkpoint("after CharacterDictionary::build (parse all character defs)");
        std::printf("  characters resolved = %zu\n", dict.size());

        // Free the raw compressed-file buffer explicitly — it is not
        // needed after loadSwf() and its lifetime is scoped to this loop
        // iteration; freeing it here isolates whether it (rather than
        // Movie::data or CharacterDictionary) is a significant holder.
        std::string().swap(raw);
        checkpoint("after freeing raw-file buffer (raw.swap)");

        runtime::ScriptEnvironment env;
        auto root = runtime::MovieClipInstance::createRoot(*movie, dict, env);
        checkpoint("after MovieClipInstance::createRoot (root clip + Timeline)");
        if (!root) {
            std::fprintf(stderr, "  createRoot returned nullptr\n");
            continue;
        }

        renderer::SceneRenderer renderer(*movie, dict);
        checkpoint("after SceneRenderer construction");

        // Advance a few frames (scripts run, display list populates) to
        // approximate what actually happens before a real render, since a
        // real boot sequence does not stop at frame 0.
        for (int f = 0; f < 5 && f < movie->frameCount; ++f) {
            root->advanceFrame();
            checkpoint("after advanceFrame() #" + std::to_string(f + 1));
        }

        std::printf("\n");
    }

    return 0;
}
