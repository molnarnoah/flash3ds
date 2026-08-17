// flash_runtime — command-line SWF inspector.
//
// Usage:
//   flash_runtime <file.swf> [--debug] [--quiet] [--timeline]
//
// Prints SWF version, compression, stage size, frame rate, frame count, the
// full tag list, and whether ActionScript bytecode is present — per the
// project spec (section 21). --timeline additionally builds a Phase 2
// Timeline and prints a per-frame display-list summary.

#include <cstdio>
#include <cstring>
#include <string>

#include "platform/Log.h"
#include "runtime/Timeline.h"
#include "swf/SwfLoader.h"

using flash3ds::Log;
using flash3ds::LogLevel;
using flash3ds::runtime::Timeline;
using flash3ds::swf::SwfLoader;

namespace {

void printUsage(const char* argv0) {
    std::fprintf(stderr,
                  "Usage: %s <file.swf> [--debug] [--quiet] [--timeline]\n"
                  "\n"
                  "  --debug     enable verbose [SWF] tag-by-tag logging\n"
                  "  --quiet     suppress logging, print only the summary report\n"
                  "  --timeline  print a per-frame display-list summary (Phase 2)\n",
                  argv0);
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

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") {
            level = LogLevel::kDebug;
        } else if (arg == "--quiet") {
            level = LogLevel::kNone;
        } else if (arg == "--timeline") {
            showTimeline = true;
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

    return 0;
}
