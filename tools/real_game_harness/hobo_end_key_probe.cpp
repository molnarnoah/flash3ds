// hobo_end_key_probe.cpp
//
// Roadmap Phase 7 (docs/implementation-roadmap-2026-08-21-part2.md):
// "Resolve Hobo's title-screen progression trigger". button_scan/
// button_debug already established (docs/hobo_button_diagnostic.txt) that
// every frame-1 DefineButton2 in hobo.swf carries a single condActionsV2
// record with CondKeyPress=4 ("End" per the SWF spec's own key-code table
// — see src/runtime/MovieClipInstance.cpp's condKeyPressToInputKeyCode()
// doc comment) and zero mouse-transition bits. click_probe already showed
// holding InputState::kEnd down for a short hover/press/release sequence
// produces a render DIFFERENT from a same-tick-count inert control key —
// real evidence End is dispatched and has *some* effect — but click_probe
// only runs ~5 ticks and doesn't report root timeline state, so it can't
// say whether that effect is "the title screen actually progresses"
// (Phase 7's actual question) or just a button depressing visually.
//
// This tool holds End down continuously across MANY ticks (default 90 —
// well past the movie's own FrameCount=13, long enough to see whether the
// root timeline advances past its last authored frame, loops, or the
// button's action bytecode instead calls something like gotoAndPlay to a
// frame label / a nested clip / getURL / loadMovie) and prints, every
// tick: root timeline currentFrame(), display-list size, and every
// CallFunction/CallMethod/NewMethod/NewObject/GetURL/GetURL2 the
// interpreter actually executes that tick (via ScriptEnvironment::
// callTraceSink, same mechanism avm1_runtime_trace.cpp uses — runtime-
// resolved, not a static guess). A "control" run with an inert key code
// runs the identical tick count for comparison, since nested sprites tick
// their own animation regardless of input (see click_probe.cpp's header
// comment on this exact pitfall).
//
// This is READ-ONLY verification — no runtime behavior is modified,
// consistent with every other tool in tools/real_game_harness/.
//
// Usage: hobo_end_key_probe <path.swf> [ticks] [tap]
// Pass a third argument "tap" to press End only on tick 0 and release it
// for the rest of the run (see runWithKey()'s tapOnly doc comment).

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "runtime/CharacterDictionary.h"
#include "runtime/InputState.h"
#include "runtime/Movie.h"
#include "runtime/MovieClipInstance.h"
#include "swf/SwfLoader.h"

using namespace flash3ds;

namespace {

struct RunResult {
    std::vector<int> framesByTick;
    std::vector<size_t> displayListSizeByTick;
    std::vector<std::string> trace;
};

RunResult runWithKey(const std::string& raw, int keyCode, int ticks, bool tapOnly) {
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
        // Key held down the whole time -> commitFrame() only reports a
        // press EDGE on tick 0 (UP->DOWN); ticks 1..N-1 are "still down",
        // matching how a player actually holds End rather than tapping it
        // once, in case the trigger needs sustained key-down state rather
        // than just the edge. If tapOnly, the key is released again after
        // tick 0 -- distinguishes "effect needs the key held" from "effect
        // is a one-shot state flip that then persists in later ticks
        // regardless of key state" (e.g. a `paused` variable the button's
        // condActionsV2 sets once, that the per-tick game loop then reads
        // every tick on its own).
        env.inputState().setKeyDown(keyCode, !tapOnly || t == 0);
        env.inputState().commitFrame();
        root->advanceFrame();
        result.framesByTick.push_back(root->timeline().currentFrame());
        result.displayListSizeByTick.push_back(root->timeline().displayList().entries().size());
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path.swf> [ticks]\n", argv[0]);
        return 1;
    }
    int ticks = argc > 2 ? std::atoi(argv[2]) : 90;
    bool tapOnly = argc > 3 && std::string(argv[3]) == "tap";

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "could not open %s\n", argv[1]);
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();

    std::printf("=== %s End (InputState::kEnd=%d) for %d ticks ===\n",
                tapOnly ? "tapping" : "holding", runtime::InputState::kEnd, ticks);
    RunResult endRun = runWithKey(raw, runtime::InputState::kEnd, ticks, tapOnly);
    std::printf("=== %s inert control key (999) for %d ticks ===\n", tapOnly ? "tapping" : "holding",
                ticks);
    RunResult controlRun = runWithKey(raw, 999, ticks, tapOnly);

    std::printf("\ntick  frame(End)  dlSize(End)  frame(ctrl)  dlSize(ctrl)  differs?\n");
    for (int t = 0; t < ticks; ++t) {
        bool differs = endRun.framesByTick[t] != controlRun.framesByTick[t] ||
                       endRun.displayListSizeByTick[t] != controlRun.displayListSizeByTick[t];
        std::printf("%4d  %10d  %11zu  %11d  %12zu  %s\n", t, endRun.framesByTick[t],
                    endRun.displayListSizeByTick[t], controlRun.framesByTick[t],
                    controlRun.displayListSizeByTick[t], differs ? "YES" : "");
    }

    std::printf("\n--- End-run trace (%zu entries) ---\n", endRun.trace.size());
    for (const auto& line : endRun.trace) std::printf("  %s\n", line.c_str());

    std::printf("\n--- control-run trace (%zu entries) ---\n", controlRun.trace.size());
    for (const auto& line : controlRun.trace) std::printf("  %s\n", line.c_str());

    int maxFrameEnd = 0, maxFrameControl = 0;
    for (int f : endRun.framesByTick) maxFrameEnd = std::max(maxFrameEnd, f);
    for (int f : controlRun.framesByTick) maxFrameControl = std::max(maxFrameControl, f);
    std::printf("\nmax root frame reached: End=%d control=%d (movie authored frame count printed above by SwfLoader's own log)\n",
                maxFrameEnd, maxFrameControl);

    return 0;
}
