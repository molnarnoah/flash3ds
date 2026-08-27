// hobo_playability_probe.cpp
//
// Track A, A1 (2026-08-27 task): "the single test that determines whether
// the game is already playable at the engine level." Renders REAL pixel
// frames (via SceneRenderer/SoftwareRenderer, not just AVM1 call traces
// like hobo_end_key_probe.cpp) for hobo.swf across many ticks while
// holding down the exact keys docs/hobo-title-progression.md found this
// file polling every onEnterFrame tick from tick 0 onward (Key.isDown for
// 37/38/39/40 = Left/Up/Right/Down and 65/83 = 'A'/'S'), and separately
// with no input held as a control (nested clips animate regardless of
// input — see click_probe.cpp/hobo_end_key_probe.cpp's own header
// comments on this exact pitfall, same reasoning applies to pixels).
//
// Writes one PPM per sampled tick under <outdir>/held/tickNNN.ppm and
// <outdir>/control/tickNNN.ppm. Diffing is done by a separate script
// (compare_playability_frames.py) rather than baked into this tool, so
// the diff logic can be iterated on without recompiling.
//
// Usage: hobo_playability_probe <path.swf> <outdir> [ticks] [sampleEvery]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "renderer/SceneRenderer.h"
#include "renderer/SoftwareRenderer.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/InputState.h"
#include "runtime/Movie.h"
#include "runtime/MovieClipInstance.h"
#include "swf/SwfLoader.h"

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

using namespace flash3ds;

namespace {

void ensureDir(const std::string& path) { MKDIR(path.c_str()); }

// The exact key set docs/hobo-title-progression.md found hobo.swf polling
// via Key.isDown() every tick from tick 0 onward: Left/Up/Right/Down
// (InputState's named arrow constants, which ARE the SWF-spec key codes
// 37/38/39/40 — see InputState.h) plus literal keyboard 'A' (65) and 'S'
// (83), which the game polls as plain ASCII codes, not AS2 Key.* named
// constants (there's no Key.A/Key.S in AS2 — see MovieClipInstance.cpp's
// key-code handling for how a bare Key.isDown(65) call resolves).
void setGameplayKeys(runtime::InputState& input, bool down) {
    input.setKeyDown(runtime::InputState::kLeft, down);
    input.setKeyDown(runtime::InputState::kUp, down);
    input.setKeyDown(runtime::InputState::kRight, down);
    input.setKeyDown(runtime::InputState::kDown, down);
    input.setKeyDown(65, down);  // 'A'
    input.setKeyDown(83, down);  // 'S'
}

// Runs `ticks` advanceFrame() calls, holding the gameplay keys down every
// tick iff `holdKeys` is true (the control run never holds them), writing
// a PPM snapshot every `sampleEvery` ticks (and always tick 0 and the
// final tick) to `outDir`/tickNNN.ppm. Returns the number of frames
// written, or -1 on any load/setup failure.
int runAndSample(const std::string& raw, const std::string& outDir, int ticks, int sampleEvery,
                  bool holdKeys) {
    auto movie = swf::SwfLoader::loadSwf(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
    if (!movie || !movie->valid) {
        std::fprintf(stderr, "load failed: %s\n", movie ? movie->errorMessage.c_str() : "(null)");
        return -1;
    }
    auto characters = runtime::CharacterDictionary::build(*movie);
    runtime::ScriptEnvironment env;
    auto root = runtime::MovieClipInstance::createRoot(*movie, characters, env);
    if (!root) {
        std::fprintf(stderr, "createRoot failed\n");
        return -1;
    }

    int width = std::max(1, static_cast<int>(std::lround(movie->frameSize.widthPixels())));
    int height = std::max(1, static_cast<int>(std::lround(movie->frameSize.heightPixels())));
    renderer::SceneRenderer scene(*movie, characters);

    int written = 0;
    auto writeSample = [&](int tick) {
        renderer::SoftwareRenderer sw(width, height);
        scene.render(*root, sw, width, height);
        char path[512];
        std::snprintf(path, sizeof(path), "%s/tick%03d.ppm", outDir.c_str(), tick);
        if (!sw.writePpm(path)) {
            std::fprintf(stderr, "  (failed to write %s)\n", path);
        } else {
            ++written;
        }
    };

    // tick 0 = frame-1 state, before any advanceFrame() call, matching
    // createRoot()'s own "frame 1's script has already run" contract.
    writeSample(0);

    for (int t = 1; t < ticks; ++t) {
        if (holdKeys) setGameplayKeys(env.inputState(), true);
        env.inputState().commitFrame();
        root->advanceFrame();
        if (t % sampleEvery == 0 || t == ticks - 1) {
            writeSample(t);
        }
    }

    return written;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <path.swf> <outdir> [ticks=90] [sampleEvery=10]\n", argv[0]);
        return 1;
    }
    std::string path = argv[1];
    std::string outDir = argv[2];
    int ticks = argc > 3 ? std::atoi(argv[3]) : 90;
    int sampleEvery = argc > 4 ? std::atoi(argv[4]) : 10;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "could not open %s\n", path.c_str());
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();

    ensureDir(outDir);
    std::string heldDir = outDir + "/held";
    std::string controlDir = outDir + "/control";
    ensureDir(heldDir);
    ensureDir(controlDir);

    std::printf("=== held run (Left/Up/Right/Down + 'A'/'S' held every tick) ===\n");
    int heldWritten = runAndSample(raw, heldDir, ticks, sampleEvery, /*holdKeys=*/true);
    std::printf("wrote %d frames to %s\n", heldWritten, heldDir.c_str());

    std::printf("=== control run (no input) ===\n");
    int controlWritten = runAndSample(raw, controlDir, ticks, sampleEvery, /*holdKeys=*/false);
    std::printf("wrote %d frames to %s\n", controlWritten, controlDir.c_str());

    if (heldWritten < 0 || controlWritten < 0) return 2;
    return 0;
}
