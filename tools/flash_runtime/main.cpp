// flash_runtime — command-line SWF inspector.
//
// Phase 1 usage:
//   flash_runtime <file.swf> [--debug]
//
// Prints SWF version, compression, stage size, frame rate, frame count, the
// full tag list, and whether ActionScript bytecode is present — per the
// project spec (section 21).

#include <cstdio>
#include <cstring>
#include <string>

#include "platform/Log.h"
#include "swf/SwfLoader.h"

using flash3ds::Log;
using flash3ds::LogLevel;
using flash3ds::swf::SwfLoader;

namespace {

void printUsage(const char* argv0) {
    std::fprintf(stderr,
                  "Usage: %s <file.swf> [--debug] [--quiet]\n"
                  "\n"
                  "  --debug   enable verbose [SWF] tag-by-tag logging\n"
                  "  --quiet   suppress logging, print only the summary report\n",
                  argv0);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string path;
    LogLevel level = LogLevel::kInfo;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") {
            level = LogLevel::kDebug;
        } else if (arg == "--quiet") {
            level = LogLevel::kNone;
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

    return 0;
}
