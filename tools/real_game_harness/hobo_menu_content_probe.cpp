// hobo_menu_content_probe.cpp
//
// Phase 3 investigation (2026-08-30, from the user's azahar_test_v21.mkv
// report after this session's SELECT-key fix): three concrete bugs were
// reported on the now-reachable splash/title/Choose-Difficulty screens --
// (1) "only base colors layer shows not everything" (rendering), (2)
// "sound not works from main menu screen" (audio), (3) FPS stuck at
// 19-20. This tool is a READ-ONLY diagnostic (no runtime behavior
// modified, matching every other tool in this directory) aimed at (1) and
// (2): it drives the SAME confirmed input pattern
// hobo_frame_progression_probe.cpp already used to reach the "CHOOSE
// DIFFICULTY" screen (movement keys held + End tapped every other tick),
// then:
//
//   - installs a TracingAudioBackend (a local IAudioBackend override, NOT
//     a production code change) that logs every loadSound/playSound/
//     stopSound/setVolume call with full arguments -- this is how we
//     observe StartSound-tag-triggered and AVM1 Sound.start()-triggered
//     playback separately from AS2-level call tracing, since a StartSound
//     TAG's dispatch (MovieClipInstance::runCurrentFrameSounds()) is pure
//     C++, never goes through the AVM1 interpreter, and so never appears
//     in callTraceSink;
//   - installs callTraceSink to catch every CallMethod (so a real
//     Sound.attachSound("name")/Sound.start() AS2-level call is visible
//     even if TracingAudioBackend never fires, e.g. because attachSound's
//     linkage-name form is unimplemented and never reaches playSoundById
//     at all -- see docs/audio.md's "Follow-up" section, which ruled this
//     out ONLY for frame-1 content, before this session's SELECT fix);
//   - dumps the FULL recursive display list (every MovieClipInstance in
//     the tree, not just root's direct children) at the end of the run,
//     reporting each placement's depth path, characterId, resolved
//     character kind (Shape/Sprite/Text/EditText/Button/Sound/Font/Morph/
//     unresolved), name, world-ish local matrix translation, and
//     visible() -- this directly answers Task #55 (are EASY/NORMAL/HARD
//     button characters even PLACED in the display list at all, or is
//     this a script/gating bug rather than a rendering bug) and indirectly
//     helps Task #56 (confirms which characters are the CHOOSE
//     DIFFICULTY/PASSWORD title text, so a follow-up tool can inspect
//     their real fill/gradient data).
//
// Usage: hobo_menu_content_probe <path.swf> [ticks=1200] [dumpEvery=0] [renderOutDir]
// dumpEvery > 0: also print a full recursive display-list dump every
// `dumpEvery` ticks during the run (not just at the end) -- lets a caller
// watch composition change as a long-running nested sprite's own local
// timeline advances, since Task #55/#57's content turned out to live
// inside one such sprite rather than on root's own (frame-1-only)
// timeline -- see this run's own findings.
// renderOutDir (optional, 4th arg): if given, also renders a real PPM
// pixel snapshot (via SceneRenderer/SoftwareRenderer, same as --render on
// the main flash_runtime CLI) at every `dumpEvery` ticks and at the final
// tick, to <renderOutDir>/tickNNNN.ppm -- this is the only way to actually
// SEE the auto-advancing nested sprite's content (e.g. the "CHOOSE
// DIFFICULTY" screen) since the main CLI's --render only addresses root's
// own (frame-1-only) frame number, not this sprite's independent local
// playhead.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "audio/IAudioBackend.h"
#include "platform/Log.h"
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

class TracingAudioBackend : public audio::IAudioBackend {
public:
    int loadCalls = 0;
    int playCalls = 0;
    int stopCalls = 0;
    int stopAllCalls = 0;
    int setVolumeCalls = 0;

    void loadSound(uint16_t soundId, const int16_t*, size_t sampleCount, int sampleRate,
                    int channels) override {
        ++loadCalls;
        std::printf("  [AUDIO] loadSound soundId=%u samples=%zu rate=%d ch=%d\n", soundId,
                    sampleCount, sampleRate, channels);
    }
    void playSound(uint16_t soundId, int loopCount, uint32_t startFrame,
                   uint32_t endFrame) override {
        ++playCalls;
        std::printf("  [AUDIO] playSound soundId=%u loopCount=%d startFrame=%u endFrame=%u\n",
                    soundId, loopCount, startFrame,
                    endFrame == kPlayToEnd ? 0xFFFFFFFFu : endFrame);
    }
    void stopSound(uint16_t soundId) override {
        ++stopCalls;
        std::printf("  [AUDIO] stopSound soundId=%u\n", soundId);
    }
    void stopAllSounds() override {
        ++stopAllCalls;
        std::printf("  [AUDIO] stopAllSounds\n");
    }
    void setVolume(uint16_t soundId, float volume) override {
        ++setVolumeCalls;
        std::printf("  [AUDIO] setVolume soundId=%u volume=%.3f\n", soundId,
                    static_cast<double>(volume));
    }
};

const char* characterKindName(const runtime::CharacterDictionary& characters, uint16_t id) {
    const runtime::CharacterDef* def = characters.find(id);
    if (!def) return "UNRESOLVED";
    if (std::holds_alternative<swf::ShapeDef>(*def)) return "Shape";
    if (std::holds_alternative<runtime::SpriteDef>(*def)) return "Sprite";
    if (std::holds_alternative<swf::SoundDef>(*def)) return "Sound";
    if (std::holds_alternative<swf::FontDef>(*def)) return "Font";
    if (std::holds_alternative<swf::TextDef>(*def)) return "Text";
    if (std::holds_alternative<swf::ButtonDef>(*def)) return "Button";
    if (std::holds_alternative<swf::EditTextDef>(*def)) return "EditText";
    return "OtherKind";
}

// Walks the RAW DisplayList entries at `clip`'s own timeline level, not
// just `clip.children()` -- children() only holds entries whose character
// resolved to a Sprite (i.e. got its own MovieClipInstance); a placed
// Shape/Text/EditText/Button character is a LEAF DisplayListEntry that
// SceneRenderer draws directly and NEVER appears in children() at all.
// Missing this distinction was this tool's first-draft bug: it silently
// showed zero Shape/Text/Button entries anywhere, which is not because
// none exist but because it was only looking at the wrong collection.
void dumpTree(runtime::MovieClipInstance& clip, const runtime::CharacterDictionary& characters,
              const std::string& path, int depthLevel) {
    const auto& children = clip.children();
    for (const auto& [depth, entry] : clip.timeline().displayList().entries()) {
        const char* kind = characterKindName(characters, entry.characterId);
        std::string label = entry.name.has_value() ? *entry.name : std::string("(unnamed)");
        std::string childPath = path + "/" + label;
        auto childIt = children.find(depth);
        if (childIt != children.end()) {
            auto& child = childIt->second;
            std::printf(
                "%*s[%s] characterId=%u kind=%-9s depth=%-4d visible=%-5s local=(%.1f,%.1f) "
                "localFrame=%u/%u\n",
                depthLevel * 2, "", childPath.c_str(), child->characterId(), kind, depth,
                child->visible() ? "true" : "false", child->localMatrix().translateXPixels(),
                child->localMatrix().translateYPixels(), child->timeline().currentFrame(),
                child->timeline().frameCount());
            dumpTree(*child, characters, childPath, depthLevel + 1);
        } else {
            // Leaf entry: Shape/Text/EditText/Button/unresolved -- no
            // MovieClipInstance, so no further recursion, but this IS
            // real placed, renderable content at this depth.
            std::printf("%*s[%s] characterId=%u kind=%-9s depth=%-4d (LEAF, no clip instance) "
                        "local=(%.1f,%.1f)\n",
                        depthLevel * 2, "", childPath.c_str(), entry.characterId, kind, depth,
                        entry.matrix.translateXPixels(), entry.matrix.translateYPixels());
        }
    }
    for (const auto& [depth, btn] : clip.buttonInstances()) {
        (void)btn;
        std::printf("%*s[%s/(button-instance)] depth=%-4d\n", depthLevel * 2, "", path.c_str(),
                    depth);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path.swf> [ticks=1200]\n", argv[0]);
        return 1;
    }
    int ticks = argc > 2 ? std::atoi(argv[2]) : 1200;
    int dumpEvery = argc > 3 ? std::atoi(argv[3]) : 0;
    std::string renderOutDir = argc > 4 ? argv[4] : "";
    if (!renderOutDir.empty()) MKDIR(renderOutDir.c_str());

    Log::setLevel(LogLevel::kDebug);  // surface AUDIO/MOVIECLIP LOG_WARN/LOG_DEBUG lines too

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
    TracingAudioBackend audioBackend;
    env.setAudioBackend(&audioBackend);
    std::vector<std::string> trace;
    env.callTraceSink = [&](const std::string& line) { trace.push_back(line); };

    auto root = runtime::MovieClipInstance::createRoot(*movie, characters, env);
    if (!root) {
        std::fprintf(stderr, "createRoot failed\n");
        return 1;
    }

    int renderWidth = std::max(1, static_cast<int>(std::lround(movie->frameSize.widthPixels())));
    int renderHeight = std::max(1, static_cast<int>(std::lround(movie->frameSize.heightPixels())));
    renderer::SceneRenderer scene(*movie, characters);
    auto renderSample = [&](int tick) {
        if (renderOutDir.empty()) return;
        renderer::SoftwareRenderer sw(renderWidth, renderHeight);
        scene.render(*root, sw, renderWidth, renderHeight);
        char path[512];
        std::snprintf(path, sizeof(path), "%s/tick%04d.ppm", renderOutDir.c_str(), tick);
        if (!sw.writePpm(path)) {
            std::fprintf(stderr, "  (failed to write %s)\n", path);
        } else {
            std::printf("  [render] wrote %s\n", path);
        }
    };

    std::printf("=== driving %d ticks (movement keys held, End tapped every other tick) ===\n",
                ticks);
    renderSample(0);
    size_t traceCursor = 0;
    for (int t = 0; t < ticks; ++t) {
        env.inputState().setKeyDown(runtime::InputState::kLeft, true);
        env.inputState().setKeyDown(runtime::InputState::kUp, true);
        env.inputState().setKeyDown(runtime::InputState::kRight, true);
        env.inputState().setKeyDown(runtime::InputState::kDown, true);
        env.inputState().setKeyDown('A', true);
        env.inputState().setKeyDown('S', true);
        env.inputState().setKeyDown(runtime::InputState::kEnd, (t % 2) == 0);
        env.inputState().commitFrame();
        root->advanceFrame();

        // Print CallMethod traces that mention Sound-ish members as they
        // happen, tagged by tick -- direct evidence of any AS2-level
        // attachSound()/start() call, distinct from the TracingAudioBackend
        // taps above (which only fire for tag-driven StartSound + whatever
        // AS2 Sound calls actually resolve to a numeric soundId).
        for (; traceCursor < trace.size(); ++traceCursor) {
            const std::string& line = trace[traceCursor];
            if (line.find("attachSound") != std::string::npos ||
                line.find(".start(") != std::string::npos ||
                line.find("Sound") != std::string::npos) {
                std::printf("  [tick %4d] TRACE: %s\n", t, line.c_str());
            }
        }

        if (dumpEvery > 0 && (t % dumpEvery) == 0) {
            std::printf("--- tree dump at tick %d (audio calls so far: load=%d play=%d) ---\n", t,
                        audioBackend.loadCalls, audioBackend.playCalls);
            dumpTree(*root, characters, "", 1);
            renderSample(t);
        }
    }
    renderSample(ticks);

    std::printf(
        "\n=== run complete: root frame=%u/%u, audio backend calls: load=%d play=%d stop=%d "
        "stopAll=%d setVolume=%d ===\n",
        root->timeline().currentFrame(), root->timeline().frameCount(), audioBackend.loadCalls,
        audioBackend.playCalls, audioBackend.stopCalls, audioBackend.stopAllCalls,
        audioBackend.setVolumeCalls);

    std::printf("\n=== full recursive display-list dump ===\n");
    std::printf("[/root] characterId=(root) depth=- visible=true\n");
    dumpTree(*root, characters, "", 1);

    std::printf("\n=== total CallMethod/CallFunction/etc. trace lines: %zu ===\n", trace.size());

    return 0;
}
