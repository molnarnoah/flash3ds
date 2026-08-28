// hobo_frame_progression_probe.cpp
//
// Track A A1/A2 follow-up (2026-08-27, task from the "Hobo1 Track A" user
// prompt's Step 0 gap): docs/hobo-playability-verification.md's task #68
// addendum confirmed the interpreter can move ROOT's own timeline from
// frame 1 to frame 2 via character 32's CondKeyPress=4 (End) button
// nested inside "preloader" -- but ONLY once, using temporary debug
// instrumentation to time a single End-key press-edge against the exact
// tick where preloader's own local timeline happened to be sitting on
// local frame 4 (where that button lives). That confirmation explicitly
// left open whether root can be driven the rest of the way to frame 10
// (where the real "hobo" player-character clip, characterId=1913,
// actually lives -- see that document's Finding 5/6) using nothing but
// ordinary, repeated player input -- not one lucky timed press.
//
// hobo_end_key_probe.cpp already showed that HOLDING End down for the
// whole run only ever registers ONE press-edge (at tick 0, since
// InputState's edge detection needs an UP->DOWN transition, and a held
// key never produces a second one) -- and tick 0 is almost never the
// exact tick preloader's spinner is on local frame 4, so a naive "hold it
// down" run (as hobo_end_key_probe's default mode does) never reproduces
// the one press that worked. This tool instead TAPS End every other tick
// (down, up, down, up, ...) for a long run -- guaranteeing many real
// press-EDGES spread across the whole run, so at least one is very likely
// to land while preloader is on local frame 4, without needing to compute
// or guess the exact timing by hand. This is the ordinary way a human
// player mashing a key would actually behave, not a synthetic shortcut.
//
// Movement keys (Left/Up/Right/Down/'A'/'S' -- the exact set
// docs/hobo-playability-verification.md's Method section documents this
// movie polling) are held throughout the SAME run, since real gameplay at
// frame 10 needs them and there's no reason to withhold them while also
// tapping End -- a real player holding a direction while mashing
// confirm/continue is a completely ordinary input pattern, not a
// contrived one.
//
// READ-ONLY verification -- no runtime behavior modified, matching every
// other tool in this directory.
//
// Usage: hobo_frame_progression_probe <path.swf> [ticks=1200] [endPressTick=-1] [endTapPeriod=2]
//   endPressTick >= 0: press End on exactly that one tick, released every
//     other tick -- for a precisely-timed single press once preloader's
//     local-frame-4 window tick has been found from a no-press run's
//     logged cycle.
//   endPressTick < 0 (default): fall back to tapping End every
//     `endTapPeriod` ticks (down on tick%period==0, up otherwise) --
//     coarse sweep mode, useful for an initial no-press (period so large
//     it never fires, e.g. endTapPeriod > ticks) observation run.

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

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                      "usage: %s <path.swf> [ticks=1200] [endPressTick=-1] [endTapPeriod=2]\n", argv[0]);
        return 1;
    }
    int ticks = argc > 2 ? std::atoi(argv[2]) : 1200;
    int endPressTick = argc > 3 ? std::atoi(argv[3]) : -1;
    int endTapPeriod = argc > 4 ? std::atoi(argv[4]) : 2;
    if (endTapPeriod < 1) endTapPeriod = 1;

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
    auto characters = runtime::CharacterDictionary::build(*movie);
    runtime::ScriptEnvironment env;
    std::vector<std::string> trace;
    env.callTraceSink = [&](const std::string& line) { trace.push_back(line); };
    auto root = runtime::MovieClipInstance::createRoot(*movie, characters, env);
    if (!root) {
        std::fprintf(stderr, "createRoot failed\n");
        return 1;
    }

    int lastFrame = root->timeline().currentFrame();
    std::printf("tick 0 start: root frame=%d displayListSize=%zu\n", lastFrame,
                root->timeline().displayList().entries().size());

    // Find the "preloader" child clip (by name, same public children()
    // accessor every other runtime/ caller uses -- no private access) so
    // its own LOCAL frame can be logged every tick. This is what lets this
    // tool find preloader's real local-frame-3<->4 cycle empirically
    // instead of guessing/hand-timing it, unlike task #68's one-off manual
    // debug-instrumentation approach.
    auto findByName = [](runtime::MovieClipInstance* clip,
                          const std::string& target) -> runtime::MovieClipInstance* {
        for (const auto& [depth, child] : clip->children()) {
            (void)depth;
            if (child->name() == target) return child.get();
        }
        return nullptr;
    };
    runtime::MovieClipInstance* preloader = findByName(root.get(), "preloader");
    if (preloader) {
        std::printf("found 'preloader' child clip, local frameCount=%u\n", preloader->timeline().frameCount());
    } else {
        std::printf("WARNING: no direct root child named 'preloader' found -- local-frame logging disabled\n");
    }
    int lastPreloaderFrame = preloader ? preloader->timeline().currentFrame() : -1;

    // Full observability instead of inferring identity from the generic
    // "[object Object].method()" trace text (which can't distinguish
    // mutebutton from character 32's real button, or any other clip) --
    // enumerate EVERY named direct child of root once at startup, and track
    // each one's own local currentFrame() per tick, same as preloader above.
    // This turns "which clip actually got called" from a guess into a
    // direct observation.
    struct TrackedChild {
        std::string name;
        uint16_t characterId;
        int32_t depth;
        runtime::MovieClipInstance* clip;
        int lastFrame;
    };
    std::vector<TrackedChild> trackedChildren;
    std::printf("\n-- named direct children of root --\n");
    for (const auto& [depth, child] : root->children()) {
        if (child->name().empty()) continue;
        std::printf("  depth=%d name=\"%s\" characterId=%u localFrameCount=%u localCurrentFrame=%d\n", depth,
                    child->name().c_str(), child->characterId(), child->timeline().frameCount(),
                    child->timeline().currentFrame());
        trackedChildren.push_back(
            {child->name(), child->characterId(), depth, child.get(),
             static_cast<int>(child->timeline().currentFrame())});
    }
    std::printf("-- end named children --\n\n");

    size_t traceCursor = 0;
    for (int t = 0; t < ticks; ++t) {
        // Movement keys held the whole time (matches real hobo-playability
        // verification's own key set); End tapped every other tick.
        env.inputState().setKeyDown(runtime::InputState::kLeft, true);
        env.inputState().setKeyDown(runtime::InputState::kUp, true);
        env.inputState().setKeyDown(runtime::InputState::kRight, true);
        env.inputState().setKeyDown(runtime::InputState::kDown, true);
        env.inputState().setKeyDown('A', true);
        env.inputState().setKeyDown('S', true);
        bool pressEnd = (endPressTick >= 0) ? (t == endPressTick) : ((t % endTapPeriod) == 0);
        env.inputState().setKeyDown(runtime::InputState::kEnd, pressEnd);
        env.inputState().commitFrame();
        root->advanceFrame();

        int frame = root->timeline().currentFrame();
        if (frame != lastFrame) {
            std::printf("tick %4d: root frame %d -> %d (displayListSize=%zu)\n", t, lastFrame, frame,
                        root->timeline().displayList().entries().size());
            lastFrame = frame;
        }
        if (preloader) {
            int pf = preloader->timeline().currentFrame();
            if (pf != lastPreloaderFrame) {
                std::printf("tick %4d: preloader local frame %d -> %d%s\n", t, lastPreloaderFrame, pf,
                            pressEnd ? "  [End pressed this tick]" : "");
                lastPreloaderFrame = pf;
            }
        }
        for (auto& tc : trackedChildren) {
            int cf = tc.clip->timeline().currentFrame();
            if (cf != tc.lastFrame) {
                std::printf("tick %4d: \"%s\" (characterId=%u, depth=%d) local frame %d -> %d%s\n", t,
                            tc.name.c_str(), tc.characterId, tc.depth, tc.lastFrame, cf,
                            pressEnd ? "  [End pressed this tick]" : "");
                tc.lastFrame = cf;
            }
        }
        // Print any newly-appended gotoAndStop/CallMethod trace lines right
        // when they happen, tagged with the tick they occurred on, rather
        // than dumping the whole trace at the end undifferentiated by time
        // -- this run is long enough (1200 ticks default) that an
        // end-of-run dump would be unreadable.
        for (; traceCursor < trace.size(); ++traceCursor) {
            const std::string& line = trace[traceCursor];
            if (line.find("gotoAndStop") != std::string::npos ||
                line.find("gotoAndPlay") != std::string::npos ||
                line.find("GetURL") != std::string::npos) {
                std::printf("  [tick %4d] %s\n", t, line.c_str());
            }
        }
    }

    std::printf("\nfinal: root frame=%d (movie frameCount=%u), total trace lines=%zu\n",
                root->timeline().currentFrame(), movie->frameCount, trace.size());
    return 0;
}
