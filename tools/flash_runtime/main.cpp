// flash_runtime — command-line SWF inspector.
//
// Usage:
//   flash_runtime <file.swf> [--debug] [--quiet] [--timeline]
//                            [--render <frame> <out.ppm>]
//
// Prints SWF version, compression, stage size, frame rate, frame count, the
// full tag list, and whether ActionScript bytecode is present — per the
// project spec (section 21). --timeline additionally builds a Phase 2
// Timeline and prints a per-frame display-list summary. --render (Phase 3)
// resolves characters, walks the given frame's display list through
// SceneRenderer/SoftwareRenderer, and writes a binary PPM snapshot.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "platform/Log.h"
#include "platform/MemoryDiagnostics.h"
#include "renderer/SceneRenderer.h"
#include "renderer/SoftwareRenderer.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/MovieClipInstance.h"
#include "runtime/Timeline.h"
#include "swf/SwfLoader.h"

using flash3ds::Log;
using flash3ds::LogLevel;
using flash3ds::renderer::SceneRenderer;
using flash3ds::renderer::SoftwareRenderer;
using flash3ds::runtime::CharacterDictionary;
using flash3ds::runtime::MovieClipInstance;
using flash3ds::runtime::ScriptEnvironment;
using flash3ds::runtime::Timeline;
using flash3ds::swf::SwfLoader;

namespace {

void printUsage(const char* argv0) {
    std::fprintf(stderr,
                  "Usage: %s <file.swf> [--debug] [--quiet] [--timeline]\n"
                  "                     [--render <frame> <out.ppm>]\n"
                  "\n"
                  "  --debug             enable verbose [SWF] tag-by-tag logging\n"
                  "  --quiet             suppress logging, print only the summary report\n"
                  "  --timeline          print a per-frame display-list summary (Phase 2)\n"
                  "  --render F OUT.ppm  render frame F to a binary PPM file (Phase 3)\n"
                  "  --memdiag           log MemoryDiagnostics checkpoints (see\n"
                  "                      src/platform/MemoryDiagnostics.h) during --render;\n"
                  "                      this is the same mechanism the real 3DS build uses\n"
                  "                      (there, held by holding L at boot) -- useful for\n"
                  "                      verifying the checkpoint machinery itself on desktop\n"
                  "                      before trusting its on-device numbers\n",
                  argv0);
}

// Renders `frameIndex` (1-based) of `movie`'s root timeline to a PPM file
// at `outPath`, sized to the movie's stage dimensions in pixels (minimum
// 1x1 so a degenerate/zero-size stage still produces a file).
//
// Builds a full MovieClipInstance tree (Phase 5) and TICKS it forward to
// `frameIndex` (running frame 1's scripts at construction, then advancing
// one frame + running that frame's scripts, `frameIndex - 1` more times) —
// rather than jumping straight there like Phase 3's timeline->gotoAndStop()
// did — so DoAction scripts on every frame in between actually run, exactly
// like a real player stepping through the movie. If a script calls stop()
// along the way, later frames simply won't be reached, which is correct
// behavior, not a bug.
//
// Returns false (and prints an error) on any failure.
bool renderFrameToPpm(const flash3ds::runtime::Movie& movie, uint32_t frameIndex,
                       const std::string& outPath) {
    namespace memdiag = flash3ds::platform;
    memdiag::checkpoint("startup (renderFrameToPpm entered)");

    CharacterDictionary characters = CharacterDictionary::build(movie);
    memdiag::checkpoint("after CharacterDictionary::build");
    ScriptEnvironment env;

    auto root = MovieClipInstance::createRoot(movie, characters, env);
    if (!root) {
        std::fprintf(stderr, "--render: could not build the MovieClip tree (movie invalid or has no frames)\n");
        return false;
    }
    memdiag::checkpoint("after MovieClipInstance::createRoot");
    uint32_t totalFrames = root->timeline().frameCount();
    if (frameIndex < 1 || frameIndex > totalFrames) {
        std::fprintf(stderr, "--render: frame %u out of range [1, %u]\n", frameIndex, totalFrames);
        return false;
    }

    for (uint32_t f = 1; f < frameIndex && f < totalFrames; ++f) {
        root->advanceFrame();
    }
    memdiag::checkpoint("after first frame (advanceFrame to target)");

    int width = std::max(1, static_cast<int>(std::lround(movie.frameSize.widthPixels())));
    int height = std::max(1, static_cast<int>(std::lround(movie.frameSize.heightPixels())));

    SoftwareRenderer renderer(width, height);
    SceneRenderer scene(movie, characters);
    scene.render(*root, renderer, width, height);
    memdiag::checkpoint("after first render (SceneRenderer::render)");

    if (!renderer.writePpm(outPath)) {
        std::fprintf(stderr, "--render: failed to write '%s'\n", outPath.c_str());
        return false;
    }
    memdiag::checkpoint("shutdown (peak reflects this render)");

    std::printf("\n-- Render -- wrote frame %u of %u (%dx%d px) to %s\n", root->timeline().currentFrame(),
                 totalFrames, width, height, outPath.c_str());
    return true;
}

void printTimeline(const flash3ds::runtime::Movie& movie) {
    auto timeline = Timeline::build(movie);
    if (!timeline) {
        std::printf("\n-- Timeline -- (could not build: movie invalid)\n");
        return;
    }

    std::printf("\n-- Timeline -- (%u frame%s", timeline->frameCount(),
                 timeline->frameCount() == 1 ? "" : "s");
    if (movie.frameCount != timeline->frameCount()) {
        std::printf(", header declared %u", movie.frameCount);
    }
    std::printf(")\n");

    for (uint32_t f = 1; f <= timeline->frameCount(); ++f) {
        timeline->gotoAndStop(f);
        std::printf("  Frame %2u: %zu object(s) on display list", f,
                     timeline->displayList().size());
        for (const auto& [depth, entry] : timeline->displayList().entries()) {
            std::printf("  [depth=%d char=%u", depth, entry.characterId);
            if (entry.name) std::printf(" name=\"%s\"", entry.name->c_str());
            std::printf(" x=%.1f y=%.1f]", entry.matrix.translateXPixels(),
                         entry.matrix.translateYPixels());
        }
        std::printf("\n");
    }

    if (!timeline->labels().empty()) {
        std::printf("\n-- Frame labels --\n");
        for (const auto& [name, frameIndex] : timeline->labels()) {
            std::printf("  \"%s\" -> frame %u\n", name.c_str(), frameIndex);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string path;
    LogLevel level = LogLevel::kInfo;
    bool showTimeline = false;
    bool doRender = false;
    bool memDiag = false;
    uint32_t renderFrame = 0;
    std::string renderOutPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") {
            level = LogLevel::kDebug;
        } else if (arg == "--quiet") {
            level = LogLevel::kNone;
        } else if (arg == "--timeline") {
            showTimeline = true;
        } else if (arg == "--memdiag") {
            memDiag = true;
        } else if (arg == "--render") {
            if (i + 2 >= argc) {
                std::fprintf(stderr, "--render requires two arguments: <frame> <out.ppm>\n");
                printUsage(argv[0]);
                return 1;
            }
            renderFrame = static_cast<uint32_t>(std::strtoul(argv[i + 1], nullptr, 10));
            renderOutPath = argv[i + 2];
            doRender = true;
            i += 2;
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            printUsage(argv[0]);
            return 1;
        } else {
            path = arg;
        }
    }

    if (path.empty()) {
        printUsage(argv[0]);
        return 1;
    }

    Log::setLevel(level);
    if (memDiag) {
        flash3ds::platform::setEnabled(true);
        flash3ds::platform::resetPeak();
    }

    auto movie = SwfLoader::loadSwfFile(path);

    std::printf("\n==== flash3ds SWF Inspector ====\n");
    std::printf("File:            %s\n", path.c_str());

    if (!movie->valid) {
        std::printf("Status:          INVALID\n");
        std::printf("Error:           %s\n", movie->errorMessage.c_str());
        return 2;
    }

    std::printf("Status:          OK\n");
    std::printf("SWF version:     %u\n", movie->version);
    std::printf("Compression:     %s\n", movie->compressionName());
    std::printf("Declared length: %u bytes\n", movie->declaredFileLength);
    std::printf("Stage size:      %.1f x %.1f px (twips: %d x %d)\n",
                 movie->frameSize.widthPixels(), movie->frameSize.heightPixels(),
                 movie->frameSize.widthTwips(), movie->frameSize.heightTwips());
    std::printf("Frame rate:      %.2f fps\n", movie->frameRateFps());
    std::printf("Frame count:     %u\n", movie->frameCount);
    std::printf("ActionScript:    %s\n", movie->hasActionScript ? "present" : "not detected");
    std::printf("Tag count:       %zu\n", movie->tags.size());
    std::printf("\n-- Tags --\n");
    for (const auto& tag : movie->tags) {
        std::printf("  [%4zu] id=%-3u  %-28s len=%u\n", tag.bodyOffset, tag.code,
                     tag.name.c_str(), tag.bodyLength);
    }
    std::printf("\n");

    if (showTimeline) {
        printTimeline(*movie);
    }

    if (doRender) {
        if (!renderFrameToPpm(*movie, renderFrame, renderOutPath)) {
            return 3;
        }
    }

    return 0;
}
