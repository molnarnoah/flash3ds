// audio_mem_check.cpp
//
// Roadmap Phase 3 (2026-08-21, MP3 audio decode) — measures the REAL memory
// cost of the new decode-on-demand-and-cache PCM path
// (runtime::ScriptEnvironment::playSoundById()/decodedSoundCache_), which
// docs/implementation-roadmap.md's Phase 3 entry explicitly requires
// ("Memory implications: New — must be measured, not assumed; feed back
// into docs/memory-audit.md") and which was NOT measured when the decode
// path itself was implemented and verified non-silent (see
// docs/known-limitations.md L1's history).
//
// STANDALONE diagnostic (not part of flash3ds_core, not registered in
// CMakeLists.txt — compiled and run ad hoc), same convention as
// tools/mem_profile_check/main.cpp, which this tool deliberately mirrors
// (same VmRSS-checkpoint methodology) so the numbers are directly
// comparable to docs/memory-audit.md's existing tables.
//
// Unlike main.cpp (which stops after 5 frames — enough to exercise
// CharacterDictionary::build()/createRoot/SceneRenderer construction, but
// NOT enough to guarantee a StartSound tag has actually fired), this tool
// ticks up to `--frames` (default 20, comfortably past the frame 13 window
// real corpus StartSound tags were confirmed to fire in during Part B's
// /tmp/real_sound_smoke.cpp verification) so the decode-on-demand cache
// actually gets populated by real gameplay content, not a synthetic
// trigger.
//
// Usage: audio_mem_check <path-to-swf> [--frames N]

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

// A minimal IAudioBackend that mirrors what a real backend does with
// loadSound() closely enough to be a fair memory proxy: it keeps its OWN
// copy of the PCM (same as Nintendo3DSAudioBackend::loadSound() -- see
// that file's comment on why a copy, not a view, is required: the DSP
// reads via DMA from a dedicated linearAlloc'd buffer, and a desktop
// backend copying into a plain std::vector is the equivalent memory-cost
// stand-in without a 3DS-only linearAlloc dependency). This deliberately
// means a real end-to-end run pays for PCM storage TWICE (once in
// ScriptEnvironment::decodedSoundCache_, once in the backend) -- exactly
// as it does on real 3DS hardware via Nintendo3DSAudioBackend -- so this
// tool's RSS delta is the real total cost, not just one copy's.
struct CopyingAudioBackend : audio::IAudioBackend {
    size_t loadCalls = 0;
    size_t totalBytesCopied = 0;
    std::vector<std::vector<int16_t>> retained;  // keeps copies alive, like a real backend would

    void loadSound(uint16_t /*soundId*/, const int16_t* samples, size_t sampleCount,
                    int /*sampleRate*/, int /*channels*/) override {
        loadCalls++;
        totalBytesCopied += sampleCount * sizeof(int16_t);
        retained.emplace_back(samples, samples + sampleCount);
    }
    void playSound(uint16_t, int) override {}
};

struct Checkpoint {
    std::string label;
    long rssKb;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path-to-swf> [--frames N]\n", argv[0]);
        return 1;
    }
    const std::string path = argv[1];
    int numFrames = 20;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            numFrames = std::atoi(argv[++i]);
        }
    }

    std::vector<Checkpoint> checkpoints;
    long baseline = vmRssKb();
    auto checkpoint = [&](const std::string& label) {
        long kb = vmRssKb();
        long deltaFromPrev = checkpoints.empty() ? kb - baseline : kb - checkpoints.back().rssKb;
        checkpoints.push_back({label, kb});
        std::printf("  [%-46s] RSS=%8ld KB  delta=%+8ld KB  cumulative=%+8ld KB\n",
                    label.c_str(), kb, deltaFromPrev, kb - baseline);
    };

    std::printf("=== %s (audio-focused, %d frames) ===\n", path.c_str(), numFrames);
    std::printf("startup baseline RSS = %ld KB\n\n", baseline);

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "could not open %s\n", path.c_str());
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();

    auto movie = swf::SwfLoader::loadSwf(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
    if (!movie || !movie->valid) {
        std::fprintf(stderr, "SwfLoader failed: %s\n", movie ? movie->errorMessage.c_str() : "(null)");
        return 1;
    }
    auto dict = runtime::CharacterDictionary::build(*movie);
    std::string().swap(raw);
    checkpoint("after load+CharacterDictionary::build (pre-audio baseline)");

    runtime::ScriptEnvironment env;
    CopyingAudioBackend backend;
    env.setAudioBackend(&backend);
    auto root = runtime::MovieClipInstance::createRoot(*movie, dict, env);
    if (!root) {
        std::fprintf(stderr, "createRoot failed\n");
        return 1;
    }
    checkpoint("after createRoot (pre-tick, no sound triggered yet)");

    for (int f = 0; f < numFrames && f < movie->frameCount; ++f) {
        root->advanceFrame();
        // Only checkpoint every frame up to 15 (where sound activity is
        // expected per Part B's smoke test), then every 5th, to keep the
        // table readable for long-frame-count runs without losing the
        // interesting region's resolution.
        if (f < 15 || (f + 1) % 5 == 0) {
            checkpoint("after advanceFrame() #" + std::to_string(f + 1));
        }
    }

    std::printf("\nCopyingAudioBackend::loadSound() call count = %zu\n", backend.loadCalls);
    std::printf("CopyingAudioBackend total PCM bytes copied   = %zu (%.2f KB / %.4f MB)\n",
                backend.totalBytesCopied, backend.totalBytesCopied / 1024.0,
                backend.totalBytesCopied / (1024.0 * 1024.0));
    std::printf("Final RSS = %ld KB, total delta from baseline = %+ld KB (%.4f MB)\n",
                checkpoints.back().rssKb, checkpoints.back().rssKb - baseline,
                (checkpoints.back().rssKb - baseline) / 1024.0);
    return 0;
}
