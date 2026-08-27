// pamplona_click_trace.cpp
//
// Track B, B1 (2026-08-27 task): re-verify Extreme Pamplona's L6 "proven
// negative" (docs/known-limitations.md — zero CallMethod/NewObject/
// loadMovie anywhere in the file) before writing any new engine code. The
// prior evidence for that finding came from two places, both incomplete:
// (1) a static disassembly pass that only scanned top-level DoAction/
// DoInitAction/button buffers, not the method bodies of the 117 real
// #initclip-registered AS2 classes (__Packages.*) this file's content
// actually carries; (2) avm1_runtime_trace.cpp's live trace, which only
// ticks idle frames -- no mouse simulated at all -- so it can never reach
// whatever code path only runs after a player clicks "Play" on the
// FrontPage.
//
// This tool combines click_probe.cpp's real hover->press->release mouse
// simulation with avm1_runtime_trace.cpp's full ScriptEnvironment::
// callTraceSink capture, at every point in a coordinate grid across the
// full stage (800x400 per flash_runtime --quiet), each preceded by a few
// idle "let the preloader/frame-1 setup run" ticks and followed by a few
// idle "let any dispatched handler's own effects play out" ticks -- the
// exact same hover/press/release-then-settle shape click_probe.cpp uses,
// just with the full call trace captured instead of a render MD5/
// fingerprint (which wouldn't show loadClip()/MovieClipLoader activity
// the way a CallMethod/NewObject trace would).
//
// READ-ONLY verification -- no runtime behavior changed.
//
// Usage: pamplona_click_trace <path.swf> <x> <y> [preTicks] [postTicks]
//   Runs ONE candidate (fresh load) at stage-pixel (x,y). Call once per
//   coordinate from a driving shell loop for a full grid sweep, since each
//   call needs a fresh, uncontaminated load (matches click_probe.cpp's own
//   per-candidate-fresh-load convention).

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "runtime/CharacterDictionary.h"
#include "runtime/Movie.h"
#include "runtime/MovieClipInstance.h"
#include "swf/SwfLoader.h"

using namespace flash3ds;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <path.swf> <x> <y> [preTicks=5] [postTicks=10]\n", argv[0]);
        return 2;
    }
    std::string path = argv[1];
    double x = std::strtod(argv[2], nullptr);
    double y = std::strtod(argv[3], nullptr);
    int preTicks = argc > 4 ? std::atoi(argv[4]) : 5;
    int postTicks = argc > 5 ? std::atoi(argv[5]) : 10;

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
        std::fprintf(stderr, "load failed: %s\n", movie ? movie->errorMessage.c_str() : "(null)");
        return 1;
    }
    auto characters = runtime::CharacterDictionary::build(*movie);
    runtime::ScriptEnvironment env;
    std::vector<std::string> trace;
    env.callTraceSink = [&](const std::string& line) { trace.push_back(line); };

    auto root = runtime::MovieClipInstance::createRoot(*movie, characters, env);
    if (!root) {
        std::fprintf(stderr, "createRoot failed\n");
        return 1;
    }

    // Let frame-1/preloader setup run before we click anything -- matches
    // avm1_runtime_trace.cpp's own reasoning about Extreme Pamplona's
    // 2-frame root timeline plausibly looping via a preloader pattern.
    for (int t = 0; t < preTicks; ++t) {
        env.inputState().commitFrame();
        root->advanceFrame();
    }
    size_t preClickTraceCount = trace.size();

    // Hover -> press -> release, same edges click_probe.cpp uses.
    env.inputState().setMousePosition(x, y);
    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();

    env.inputState().setMouseDown(true);
    env.inputState().commitFrame();
    root->advanceFrame();

    env.inputState().setMouseDown(false);
    env.inputState().commitFrame();
    root->advanceFrame();

    // Let any dispatched handler's own effects (loadClip, gotoAndPlay,
    // etc.) actually play out across a few more ticks.
    for (int t = 0; t < postTicks; ++t) {
        env.inputState().commitFrame();
        root->advanceFrame();
    }

    size_t postClickNewCalls = trace.size() - preClickTraceCount;
    std::printf("@(%.0f,%.0f) pre-click calls=%zu post-click NEW calls=%zu\n", x, y, preClickTraceCount,
                postClickNewCalls);
    std::printf("--- pre-click trace ---\n");
    for (size_t i = 0; i < preClickTraceCount; ++i) {
        std::printf("  %s\n", trace[i].c_str());
    }
    std::printf("--- post-click trace ---\n");
    for (size_t i = preClickTraceCount; i < trace.size(); ++i) {
        std::printf("  %s\n", trace[i].c_str());
    }
    return 0;
}
