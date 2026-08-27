// hobo_movement_key_trace.cpp
//
// Track A, A1 follow-up (2026-08-27 task): hobo_playability_probe.cpp found
// that holding Left/Up/Right/Down + 'A'/'S' (the exact keys
// docs/hobo-title-progression.md found hobo.swf polling every tick) for 90
// ticks produces a render PIXEL-IDENTICAL to a same-tick-count no-input
// control at every sampled tick -- i.e. no observable visual effect from
// those keys, even though the movie itself is not static (an intro/idle
// animation plays regardless of input). This tool captures the actual AVM1
// call trace (ScriptEnvironment::callTraceSink, same mechanism
// hobo_end_key_probe.cpp/avm1_runtime_trace.cpp use) for a held-movement-
// keys run vs the same inert-key control hobo_end_key_probe.cpp uses (key
// code 999), to see WHAT the interpreter actually does with those key
// polls -- e.g. whether any SetProperty(_x)/SetProperty(_y) call ever
// fires (would mean something IS trying to move, just not visibly), or the
// polls are read and then gated behind a condition that's never true this
// early (would explain the render being pixel-identical to the control).
//
// READ-ONLY verification, no runtime behavior changed.
//
// Usage: hobo_movement_key_trace <path.swf> [ticks]

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "runtime/CharacterDictionary.h"
#include "runtime/InputState.h"
#include "runtime/Movie.h"
#include "runtime/MovieClipInstance.h"
#include "swf/SwfLoader.h"

using namespace flash3ds;

namespace {

struct RunResult {
    std::vector<int> framesByTick;
    std::vector<std::string> trace;
};

// Mirrors hobo_playability_probe.cpp's setGameplayKeys() exactly.
void setGameplayKeys(runtime::InputState& input, bool down) {
    input.setKeyDown(runtime::InputState::kLeft, down);
    input.setKeyDown(runtime::InputState::kUp, down);
    input.setKeyDown(runtime::InputState::kRight, down);
    input.setKeyDown(runtime::InputState::kDown, down);
    input.setKeyDown(65, down);  // 'A'
    input.setKeyDown(83, down);  // 'S'
}

RunResult run(const std::string& raw, int ticks, bool holdMovementKeys) {
    RunResult result;
    auto movie = swf::SwfLoader::loadSwf(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
    if (!movie || !movie->valid) {
        std::fprintf(stderr, "load failed\n");
        return result;
    }
    auto characters = runtime::CharacterDictionary::build(*movie);
    runtime::ScriptEnvironment env;
    env.callTraceSink = [&](const std::string& line) { result.trace.push_back(line); };
    auto root = runtime::MovieClipInstance::createRoot(*movie, characters, env);
    if (!root) {
        std::fprintf(stderr, "createRoot failed\n");
        return result;
    }

    for (int t = 0; t < ticks; ++t) {
        if (holdMovementKeys) setGameplayKeys(env.inputState(), true);
        // else: inert control run, key 999 (matches hobo_end_key_probe's
        // control convention), never mapped to anything.
        else
            env.inputState().setKeyDown(999, true);
        env.inputState().commitFrame();
        root->advanceFrame();
        result.framesByTick.push_back(root->timeline().currentFrame());
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path.swf> [ticks]\n", argv[0]);
        return 1;
    }
    int ticks = argc > 2 ? std::atoi(argv[2]) : 30;

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "could not open %s\n", argv[1]);
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();

    std::printf("=== holding Left/Up/Right/Down + 'A'/'S' for %d ticks ===\n", ticks);
    RunResult heldRun = run(raw, ticks, /*holdMovementKeys=*/true);
    std::printf("(trace: %zu entries)\n", heldRun.trace.size());
    for (const auto& line : heldRun.trace) std::printf("  %s\n", line.c_str());

    std::printf("\n=== inert control key (999) for %d ticks ===\n", ticks);
    RunResult controlRun = run(raw, ticks, /*holdMovementKeys=*/false);
    std::printf("(trace: %zu entries)\n", controlRun.trace.size());
    for (const auto& line : controlRun.trace) std::printf("  %s\n", line.c_str());

    std::printf("\nheld trace size=%zu control trace size=%zu (%s)\n", heldRun.trace.size(),
                controlRun.trace.size(),
                heldRun.trace.size() == controlRun.trace.size() ? "SAME" : "DIFFERENT");

    return 0;
}
