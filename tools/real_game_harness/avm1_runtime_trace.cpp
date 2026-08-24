// avm1_runtime_trace.cpp
//
// Roadmap Phase 4 (2026-08-21), Step 1 continued: a static disassembly
// pass (avm1_loader_disasm.cpp) turned out to hit real obfuscation in
// Extreme Pamplona's compiled AS2 (dynamically-computed names/strings that
// a linear, non-executing pass cannot resolve). This tool instead RUNS
// the real avm1::Interpreter against the real movie (via the normal
// MovieClipInstance::createRoot()/advanceFrame() boot path -- the exact
// same path every other real-corpus tool in this project uses) with
// ScriptEnvironment::callTraceSink installed, so every CallFunction/
// CallMethod/NewMethod/NewObject/GetURL/GetURL2 the interpreter ACTUALLY
// executes is reported with its REAL, runtime-resolved values -- names
// that were built by decrypt/concat logic have already been computed by
// the time the trace fires, unlike a static guess.
//
// Usage: avm1_runtime_trace <path.swf> [frames]
// Ticks `frames` (default 20) real advanceFrame() calls and prints every
// traced call, in execution order.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "runtime/CharacterDictionary.h"
#include "runtime/Movie.h"
#include "runtime/MovieClipInstance.h"
#include "swf/SwfLoader.h"

using namespace flash3ds;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path.swf> [frames]\n", argv[0]);
        return 2;
    }
    int numFrames = argc > 2 ? std::atoi(argv[2]) : 20;

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "could not open %s\n", argv[1]);
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();
    auto movie = swf::SwfLoader::loadSwf(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
    if (!movie || !movie->valid) {
        std::fprintf(stderr, "load failed: %s\n", movie ? movie->errorMessage.c_str() : "(null)");
        return 1;
    }
    auto dict = runtime::CharacterDictionary::build(*movie);

    runtime::ScriptEnvironment env;
    int eventCount = 0;
    env.callTraceSink = [&](const std::string& msg) {
        std::printf("[frame trace #%d] %s\n", eventCount++, msg.c_str());
    };

    auto root = runtime::MovieClipInstance::createRoot(*movie, dict, env);
    if (!root) {
        std::fprintf(stderr, "createRoot failed\n");
        return 1;
    }
    std::printf("=== %s (%d ticks, real interpreter, real HostBindings) ===\n\n", argv[1], numFrames);
    // Deliberately NOT capped at movie->frameCount: a root timeline this
    // short (Extreme Pamplona's is 2 frames) very plausibly loops via a
    // frame script's own GotoFrame (a classic "preloader" pattern: frame 2
    // checks load progress and gotoAndPlay(1)s back) rather than ever
    // reaching a real "end" -- ticking only frameCount times would never
    // let that loop's real logic run more than once, and onEnterFrame
    // clip-event handlers (dispatched every tick regardless of which
    // frame the playhead is on) need repeated ticks to reveal anything
    // beyond their very first firing too.
    for (int f = 0; f < numFrames; ++f) {
        std::printf("--- advanceFrame() #%d ---\n", f + 1);
        root->advanceFrame();
    }
    std::printf("\n(total traced calls: %d)\n", eventCount);
    return 0;
}
