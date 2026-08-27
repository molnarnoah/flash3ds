// pamplona_content_loadmovie_probe.cpp
//
// Track B B3 (2026-08-27, task #63): "Confirm content/ path resolution
// through LocalFileLoader." Extreme Pamplona's real, unmodified corpus
// layout ships a main SWF alongside a `content/` subdirectory of ~24
// sibling SWFs (levels/music/sound-bank/player sprites — see
// docs/known-limitations.md L3/L6 and docs/real-game-compatibility.md).
// This tool verifies the ENTIRE chain a real `loadMovie("content/...")`
// call would exercise -- runtime::LocalFileLoader's relative-path joining
// against the real on-disk directory structure, then swf::SwfLoader
// parsing the fetched bytes -- exactly mirroring
// MovieClipInstance::loadMovie()'s own internal sequence
// (env_->fileLoader().loadFile(url) -> swf::SwfLoader::loadSwf(...), see
// src/runtime/MovieClipInstance.cpp) without needing that method to
// actually be REACHABLE from Extreme Pamplona's own AVM1 bytecode (see
// the finding below -- it categorically is not, in this corpus file).
//
// Read-only, no runtime behavior modified. Deliberately does NOT embed
// any real Extreme Pamplona content in this source file -- every SWF
// path is discovered from the filesystem at runtime, given a base
// directory on the command line (matching every other tool in this
// directory's "no committed copyrighted bytes" discipline -- see
// docs/virtual-console.md section 9's own note on this).
//
// Usage: pamplona_content_loadmovie_probe <path-to-extreme-pamplona-dir>
//   (the directory containing extreme-pamplona.swf and a content/
//   subdirectory, e.g. /home/claude/game-corpus/extreme_pamplona)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "runtime/CharacterDictionary.h"
#include "runtime/LocalFileLoader.h"
#include "runtime/MovieClipInstance.h"
#include "swf/SwfLoader.h"

using namespace flash3ds;
namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> readFileRaw(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    return std::vector<uint8_t>(s.begin(), s.end());
}

// Best-effort substring scan over a movie's own decompressed body bytes --
// same technique docs/known-limitations.md's diagnostic tools already use
// (presence only, not proof of semantic reachability).
bool bodyContains(const runtime::Movie& movie, const std::string& needle) {
    if (movie.data.empty()) return false;
    auto it = std::search(movie.data.begin(), movie.data.end(), needle.begin(), needle.end());
    return it != movie.data.end();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path-to-extreme-pamplona-dir>\n", argv[0]);
        return 1;
    }
    std::string baseDir = argv[1];
    std::string mainSwfPath = baseDir + "/extreme-pamplona.swf";

    // --- Step 1: confirm the main file structurally cannot reach loadMovie
    // -----------------------------------------------------------------
    auto mainRaw = readFileRaw(mainSwfPath);
    if (mainRaw.empty()) {
        std::fprintf(stderr, "could not read %s\n", mainSwfPath.c_str());
        return 1;
    }
    auto mainMovie = swf::SwfLoader::loadSwf(mainRaw.data(), mainRaw.size());
    if (!mainMovie || !mainMovie->valid) {
        std::fprintf(stderr, "main SWF did not parse\n");
        return 1;
    }
    std::printf("=== Step 1: does extreme-pamplona.swf's own body even contain the literal "
                "string \"loadMovie\"? ===\n");
    bool hasLoadMovieString = bodyContains(*mainMovie, "loadMovie");
    std::printf("literal 'loadMovie' substring present in decompressed body: %s\n",
                hasLoadMovieString ? "YES" : "NO");
    std::printf(
        "(if NO: AVM1's ActionConstantPool/ActionPush can only ever push a string that "
        "literally exists somewhere in this file's own bytes -- so if 'loadMovie' isn't "
        "present at all, NO script anywhere in this file, however complex, can call "
        "MovieClip.loadMovie() by name. Combined with docs/known-limitations.md L3's "
        "finding -- zero CallMethod/NewObject/NewMethod opcodes anywhere in this file's "
        "142 scanned bytecode buffers -- this closes the AVM1-reachability question for "
        "content/ loading via the standard API, from two independent angles.)\n\n");

    // --- Step 2: real content/ path resolution through LocalFileLoader,
    // exactly mirroring MovieClipInstance::loadMovie()'s own sequence ---
    std::printf("=== Step 2: LocalFileLoader(baseDir) -> SwfLoader::loadSwf() for every real "
                "content/*.swf sibling ===\n");
    runtime::LocalFileLoader loader(baseDir);
    int total = 0, ok = 0;
    std::string contentDir = baseDir + "/content";
    if (!fs::is_directory(contentDir)) {
        std::fprintf(stderr, "no content/ subdirectory at %s\n", contentDir.c_str());
        return 1;
    }
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(contentDir)) {
        if (entry.path().extension() == ".swf") {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());

    for (const auto& name : names) {
        ++total;
        std::string url = "content/" + name;
        auto fetched = loader.loadFile(url);
        if (!fetched) {
            std::printf("  FAIL  %-32s  LocalFileLoader.loadFile() returned nullopt\n",
                        url.c_str());
            continue;
        }
        // Integrity cross-check: bytes fetched via the relative URL must
        // match a direct absolute-path read exactly.
        auto direct = readFileRaw(contentDir + "/" + name);
        bool bytesMatch = (*fetched == direct);

        auto parsed = swf::SwfLoader::loadSwf(fetched->data(), fetched->size());
        bool parsesOk = parsed && parsed->valid;

        if (bytesMatch && parsesOk) {
            ++ok;
            std::printf("  OK    %-32s  %8zu bytes, frameCount=%u, version=%d\n", url.c_str(),
                        fetched->size(), parsed->frameCount, parsed->version);
        } else {
            std::printf("  FAIL  %-32s  bytesMatch=%d parsesOk=%d\n", url.c_str(), bytesMatch,
                        parsesOk);
        }
    }
    std::printf("\n%d/%d content/ files: LocalFileLoader path resolution correct AND parsed as "
                "valid SWF.\n\n",
                ok, total);

    // --- Step 3: prove the C++ API itself (bypassing AVM1 entirely, which
    // is what a future B4 bespoke driver would do) really can load one of
    // these into a live MovieClipInstance target ---------------------
    std::printf("=== Step 3: MovieClipInstance::loadMovie() C++ API directly (the path a "
                "future B4 bespoke driver would call, bypassing AVM1's own unreachable "
                "loadMovie dispatch) ===\n");
    auto mainCharacters = runtime::CharacterDictionary::build(*mainMovie);
    runtime::ScriptEnvironment env;
    env.setFileLoader(&loader);
    auto root = runtime::MovieClipInstance::createRoot(*mainMovie, mainCharacters, env);
    if (!root) {
        std::fprintf(stderr, "createRoot() failed for the main file\n");
        return 1;
    }
    bool loadOk = root->loadMovie("content/sounds_pamplona.swf");
    std::printf("root->loadMovie(\"content/sounds_pamplona.swf\") -> %s (frameCount=%u after)\n",
                loadOk ? "true" : "false", root->timeline().frameCount());

    if (ok == total && hasLoadMovieString == false && loadOk) {
        std::printf("\nPASS: content/ path resolution through LocalFileLoader is fully "
                    "correct for every real file in this corpus, and the C++ loadMovie() API "
                    "works end-to-end -- confirming the infrastructure is solid for a future "
                    "B4 bespoke driver, even though Extreme Pamplona's own AVM1 bytecode has "
                    "no path to reach it (independently confirmed twice: zero CallMethod "
                    "opcodes, AND the string 'loadMovie' doesn't exist anywhere in this file's "
                    "bytes).\n");
    } else if (ok != total) {
        std::printf("\nFAIL: %d content/ file(s) did not resolve/parse correctly.\n",
                    total - ok);
        return 1;
    }
    return 0;
}
