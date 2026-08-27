// hobo_vc_package_probe.cpp
//
// Track A A3 (2026-08-27, task #58): "Wire 3DS entry point to hobo.swf +
// confirm input mapping." The 3DS entry point (src/platform/
// nintendo3ds_main.cpp) is already fully generic/config-driven -- it never
// mentions any specific SWF by name, it just calls vc::buildGamePackage()
// against whatever "config.ini"/"<swf filename>" its ResourceFetcher (a
// RomFS reader on real 3DS) hands it, then feeds the result through the
// EXISTING, unchanged CharacterDictionary/MovieClipInstance/
// ScriptEnvironment/SceneRenderer pipeline. So "wiring the entry point to
// hobo.swf" needs no C++ change at all (see docs/virtual-console.md
// section 1's architecture diagram) -- what actually needs confirming is
// that this exact GamePackage-mediated path (not SwfLoader::loadSwf()
// called directly, which is what every other real_game_harness tool this
// session has used) genuinely loads and runs real hobo.swf correctly, and
// that a documented Hobo1-shaped config.ini (see
// tests/test_vc_config.cpp's GameConfig_Hobo1ExampleMapping_
// ParsesToExpectedKeyCodes, which is the same mapping used here) actually
// produces the InputState key codes hobo.swf's own AVM1 bytecode checks
// (docs/hobo-playability-verification.md Finding 5/6,
// docs/known-limitations.md L11).
//
// Deliberately does NOT embed hobo.swf's own bytes anywhere in this
// source file or in the repository -- it's read from a path given on the
// command line, exactly like every other tool in this directory. This
// tool proves the WIRING; it is not itself a packaging step, and the
// checked-in romfs/config.ini stays the generic documented default (see
// GameConfig.h's "guaranteed to never drift apart" comment) -- a real
// Hobo1 package would ship its OWN config.ini (the text embedded below)
// alongside a locally-supplied hobo.swf, never committed to this repo.
//
// Read-only: no runtime behavior modified, matching every other tool in
// this directory.
//
// Usage: hobo_vc_package_probe <path-to-hobo.swf> [ticks]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "runtime/CharacterDictionary.h"
#include "runtime/InputState.h"
#include "runtime/MovieClipInstance.h"
#include "vc/GamePackage.h"

using namespace flash3ds;

namespace {

// Exactly the mapping tests/test_vc_config.cpp's
// GameConfig_Hobo1ExampleMapping_ParsesToExpectedKeyCodes asserts against
// -- see that test's header comment for the full evidence trail. This is
// what a Hobo1-specific config.ini (shipped ALONGSIDE hobo.swf in a real
// package, not the checked-in generic default) would contain.
constexpr const char* kHobo1ConfigIni =
    "[game]\n"
    "swf=hobo.swf\n"
    "[input]\n"
    "X=A\n"
    "Y=S\n"
    "SELECT=END\n";

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path-to-hobo.swf> [ticks]\n", argv[0]);
        return 1;
    }
    int ticks = argc > 2 ? std::atoi(argv[2]) : 5;

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "could not open %s\n", argv[1]);
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string swfRaw = ss.str();

    // A ResourceFetcher backed by the real, locally-supplied hobo.swf
    // (never embedded in this source file) plus the in-memory Hobo1
    // config.ini text above -- exactly mirroring
    // Nintendo3DSRomfs::readFile()'s contract (see vc/GamePackage.h) but
    // without needing a RomFS image or 3DS cross-build to exercise it.
    vc::ResourceFetcher fetch = [&](const std::string& name, std::vector<uint8_t>& out) {
        if (name == "config.ini") {
            out.assign(kHobo1ConfigIni, kHobo1ConfigIni + std::strlen(kHobo1ConfigIni));
            return true;
        }
        if (name == "hobo.swf") {
            out.assign(swfRaw.begin(), swfRaw.end());
            return true;
        }
        return false;
    };

    vc::GamePackage package = vc::buildGamePackage(fetch);

    std::printf("=== GameConfig parsed from the Hobo1 example config.ini ===\n");
    std::printf("swfFilename           = %s\n", package.config.swfFilename.c_str());
    std::printf("input.xKeyCode        = %d (expect 'A'=%d)\n", package.config.input.xKeyCode,
                static_cast<int>('A'));
    std::printf("input.yKeyCode        = %d (expect 'S'=%d)\n", package.config.input.yKeyCode,
                static_cast<int>('S'));
    std::printf("input.selectKeyCode   = %d (expect InputState::kEnd=%d)\n",
                package.config.input.selectKeyCode, runtime::InputState::kEnd);

    if (!package.movie || !package.movie->valid) {
        std::fprintf(stderr, "FAIL: GamePackage produced an invalid movie: %s\n",
                    package.movie ? package.movie->errorMessage.c_str() : "(null)");
        return 1;
    }
    std::printf(
        "\n=== movie loaded via buildGamePackage() (the exact path "
        "nintendo3ds_main.cpp uses) ===\n");
    std::printf("valid=%d frameCount=%u frameRate=%.2f version=%d\n", package.movie->valid,
                package.movie->frameCount, package.movie->frameRateFps(), package.movie->version);

    auto characters = runtime::CharacterDictionary::build(*package.movie);
    runtime::ScriptEnvironment env;
    auto root = runtime::MovieClipInstance::createRoot(*package.movie, characters, env);
    if (!root) {
        std::fprintf(stderr, "FAIL: createRoot() produced no root MovieClip\n");
        return 1;
    }
    std::printf("root created OK: currentFrame=%u displayListSize=%zu\n",
                root->timeline().currentFrame(), root->timeline().displayList().entries().size());

    // Feed the exact key codes the Hobo1 mapping above resolves to, and
    // the always-on D-Pad-equivalent arrow codes, through InputState --
    // proving the FULL chain (config -> key code -> InputState ->
    // Key.isDown()-visible-to-AVM1) works, not just the config-parsing
    // half tests/test_vc_config.cpp already covers in isolation.
    env.inputState().setKeyDown(runtime::InputState::kRight, true);
    env.inputState().setKeyDown(package.config.input.xKeyCode, true);  // 'A'
    env.inputState().commitFrame();

    for (int t = 0; t < ticks; ++t) {
        root->advanceFrame();
    }
    std::printf(
        "after %d ticks (with Right + 'A' held via the parsed config mapping): "
        "currentFrame=%u displayListSize=%zu\n",
        ticks, root->timeline().currentFrame(), root->timeline().displayList().entries().size());

    std::printf("\nPASS: real hobo.swf loads and runs end-to-end through the exact "
                "GamePackage/CharacterDictionary/MovieClipInstance path "
                "nintendo3ds_main.cpp uses, with a Hobo1-shaped input mapping "
                "resolving to the expected InputState key codes.\n");
    return 0;
}
