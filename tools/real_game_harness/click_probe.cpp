// click_probe.cpp
//
// Phase 1 of docs/implementation-roadmap.md ("Verify real button-dispatch
// works end-to-end against real games"). Loads a real corpus SWF fresh,
// lets frame 1's scripts run (same as flash_runtime --render's own
// construction step), captures a fingerprint of the resulting state,
// simulates a real hover -> press -> release mouse sequence at a given
// stage-pixel coordinate (the EXACT same InputState/commitFrame/
// advanceFrame() sequence tests/test_event_dispatch.cpp already exercises
// against synthetic fixtures -- see that file's header comment for the
// convention this mirrors), captures the fingerprint again, and reports
// whether anything actually changed.
//
// This tool does NOT modify runtime behavior -- it is read-only
// verification, per Phase 1's own scope ("verification, not new code").
//
// Usage: click_probe <path.swf> <candidate> [<candidate> ...]
//   candidate := click:<label>:<x>,<y>   -- simulate hover+press+release
//              | key:<label>:<code>      -- simulate a key press+release
//                                           (InputState::k* numeric codes,
//                                           e.g. 35 = kEnd)
//
// Each candidate is tested against a FRESH load of the movie (no
// cross-contamination between candidates), since a real click could
// mutate the display list (RemoveObject, gotoAndPlay, etc.) in ways that
// would make a second candidate's "before" state misleading if reused.
//
// IMPORTANT lesson from the first real run of this tool against hobo.swf
// (see docs/real-game-readiness.md): a raw before/after render-MD5 or
// display-list diff is NOT by itself evidence a click did anything --
// real corpus movies have nested sprites with their own independently
// ticking Timelines that animate every frame regardless of input. ALWAYS
// compare a candidate's "after" state against a same-tick-count "control"
// candidate with the mouse/key held at a position/code that hits nothing
// (e.g. click:control:5,5 or a key code no button's CondKeyPress uses) --
// only a difference FROM THE CONTROL, not from "before", is real evidence
// of an effect.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "renderer/SceneRenderer.h"
#include "renderer/SoftwareRenderer.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/Movie.h"
#include "runtime/MovieClipInstance.h"
#include "swf/SwfLoader.h"

using namespace flash3ds;
using flash3ds::renderer::SceneRenderer;
using flash3ds::renderer::SoftwareRenderer;
using flash3ds::runtime::CharacterDictionary;
using flash3ds::runtime::MovieClipInstance;
using flash3ds::runtime::ScriptEnvironment;

namespace {

struct Candidate {
    bool isKey = false;
    std::string label;
    double x = 0;
    double y = 0;
    int keyCode = 0;
};

std::vector<Candidate> parseCandidates(int argc, char** argv, int startIdx) {
    std::vector<Candidate> out;
    for (int i = startIdx; i < argc; ++i) {
        std::string arg = argv[i];
        size_t firstColon = arg.find(':');
        if (firstColon == std::string::npos) {
            std::fprintf(stderr, "skipping malformed candidate '%s'\n", arg.c_str());
            continue;
        }
        std::string kind = arg.substr(0, firstColon);
        std::string rest = arg.substr(firstColon + 1);
        size_t secondColon = rest.find(':');
        if (secondColon == std::string::npos) {
            std::fprintf(stderr, "skipping malformed candidate '%s'\n", arg.c_str());
            continue;
        }
        Candidate c;
        c.label = rest.substr(0, secondColon);
        std::string payload = rest.substr(secondColon + 1);
        if (kind == "key") {
            c.isKey = true;
            c.keyCode = std::atoi(payload.c_str());
        } else if (kind == "click") {
            size_t comma = payload.find(',');
            if (comma == std::string::npos) {
                std::fprintf(stderr, "skipping malformed click candidate '%s' (want click:label:x,y)\n", arg.c_str());
                continue;
            }
            c.x = std::strtod(payload.substr(0, comma).c_str(), nullptr);
            c.y = std::strtod(payload.substr(comma + 1).c_str(), nullptr);
        } else {
            std::fprintf(stderr, "skipping candidate '%s' with unknown kind '%s' (want click: or key:)\n",
                        arg.c_str(), kind.c_str());
            continue;
        }
        out.push_back(c);
    }
    return out;
}

// A compact, comparable snapshot of "did anything happen" -- root frame
// index plus the root display list's (depth -> characterId) pairs. Nested
// clips' own frame indices are intentionally NOT recursed into here (would
// need per-clip identification across two independently-built trees) --
// the root display list changing (a child appearing/disappearing/being
// replaced) or the root's own frame index changing are both strong,
// unambiguous signals that a script actually ran and did something.
std::string fingerprint(const MovieClipInstance& root) {
    std::ostringstream ss;
    ss << "frame=" << root.timeline().currentFrame();
    ss << " displayList={";
    for (const auto& [depth, entry] : root.timeline().displayList().entries()) {
        ss << depth << ":" << entry.characterId << ",";
    }
    ss << "}";
    return ss.str();
}

std::string renderMd5(const runtime::Movie& movie, const CharacterDictionary& characters,
                       const MovieClipInstance& root) {
    int width = std::max(1, static_cast<int>(std::lround(movie.frameSize.widthPixels())));
    int height = std::max(1, static_cast<int>(std::lround(movie.frameSize.heightPixels())));
    SoftwareRenderer renderer(width, height);
    SceneRenderer scene(movie, characters);
    scene.render(root, renderer, width, height);
    std::string tmpPath = "/tmp/click_probe_render.ppm";
    if (!renderer.writePpm(tmpPath)) return "(render-failed)";
    // Shell out to md5sum for simplicity -- this is a diagnostic tool, not
    // shipped runtime code.
    std::string cmd = "md5sum " + tmpPath + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "(md5-failed)";
    char buf[64] = {0};
    if (!std::fgets(buf, sizeof(buf), pipe)) { pclose(pipe); return "(md5-failed)"; }
    pclose(pipe);
    std::string result(buf);
    size_t sp = result.find(' ');
    return sp == std::string::npos ? result : result.substr(0, sp);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <path.swf> <label>:<x>,<y> [<label>:<x>,<y> ...]\n", argv[0]);
        return 1;
    }
    const std::string path = argv[1];
    auto candidates = parseCandidates(argc, argv, 2);
    if (candidates.empty()) {
        std::fprintf(stderr, "no valid candidates given\n");
        return 1;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "could not open %s\n", path.c_str());
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();

    for (const auto& c : candidates) {
        if (c.isKey) {
            std::printf("=== candidate '%s' (key code %d) ===\n", c.label.c_str(), c.keyCode);
        } else {
            std::printf("=== candidate '%s' @ (%.2f, %.2f) ===\n", c.label.c_str(), c.x, c.y);
        }

        auto movie = swf::SwfLoader::loadSwf(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
        if (!movie || !movie->valid) {
            std::printf("  LOAD FAILED\n\n");
            continue;
        }
        CharacterDictionary characters = CharacterDictionary::build(*movie);
        ScriptEnvironment env;
        auto root = MovieClipInstance::createRoot(*movie, characters, env);
        if (!root) {
            std::printf("  createRoot FAILED\n\n");
            continue;
        }

        std::string beforeFp = fingerprint(*root);
        std::string beforeMd5 = renderMd5(*movie, characters, *root);
        std::printf("  before: %s\n  before render md5: %s\n", beforeFp.c_str(), beforeMd5.c_str());

        if (c.isKey) {
            // Press.
            env.inputState().setKeyDown(c.keyCode, true);
            env.inputState().commitFrame();
            root->advanceFrame();
            // Release.
            env.inputState().setKeyDown(c.keyCode, false);
            env.inputState().commitFrame();
            root->advanceFrame();
            // One more idle tick so key candidates and click candidates
            // (3 ticks each) advance the SAME number of frames, keeping
            // ambient-animation noise comparable against a control.
            env.inputState().commitFrame();
            root->advanceFrame();
        } else {
            // Hover (establishes rollOver / OverUp state).
            env.inputState().setMousePosition(c.x, c.y);
            env.inputState().setMouseDown(false);
            env.inputState().commitFrame();
            root->advanceFrame();

            // Press (Idle/Over -> OverDown edge).
            env.inputState().setMouseDown(true);
            env.inputState().commitFrame();
            root->advanceFrame();

            // Release while still over the button (OverDown -> OverUp edge
            // -- the real "click" -- see docs/events.md's condition table
            // for why this specific edge is the one that fires
            // on(release)/onRelease).
            env.inputState().setMouseDown(false);
            env.inputState().commitFrame();
            root->advanceFrame();
        }

        std::string afterFp = fingerprint(*root);
        std::string afterMd5 = renderMd5(*movie, characters, *root);
        std::printf("  after:  %s\n  after render md5:  %s\n", afterFp.c_str(), afterMd5.c_str());

        bool fpChanged = (beforeFp != afterFp);
        bool renderChanged = (beforeMd5 != afterMd5);
        std::printf("  RESULT: fingerprint %s, render %s -> %s\n\n",
                    fpChanged ? "CHANGED" : "unchanged",
                    renderChanged ? "CHANGED" : "unchanged",
                    (fpChanged || renderChanged) ? "SOMETHING HAPPENED" : "NO OBSERVABLE EFFECT");
    }

    return 0;
}
