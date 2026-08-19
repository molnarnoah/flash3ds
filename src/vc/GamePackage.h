// GamePackage.h
//
// Virtual Console resource layer: the top-level "one resource directory ->
// one runnable game" assembly step (the `GamePackage` node in
// docs/virtual-console.md's architecture diagram). This is deliberately
// thin -- it owns config PARSING (via GameConfig) and defers entirely to
// the EXISTING SwfLoader for the movie itself; no SWF/AVM1/renderer logic
// is duplicated or reimplemented here.
//
// Platform-agnostic by design: buildGamePackage() takes a RESOURCE
// FETCHER callback rather than touching a filesystem/RomFS itself. This is
// what makes it fully unit-testable on desktop (see
// tests/test_vc_game_package.cpp, which backs the fetcher with a plain
// in-memory map) even though its only real caller is the 3DS entry point
// (src/platform/nintendo3ds_main.cpp), which is the one place that knows
// HOW to actually fetch bytes (from an embedded RomFS section on 3DS --
// see src/platform/Nintendo3DSRomfs.h for why that's a bespoke reader
// rather than libctru's own romfsInit(), and docs/3ds-toolchain.md for
// why).
//
// A callback (rather than "here are the already-read bytes of config.ini
// and game.swf") is necessary, not just a style choice: the SWF's own
// filename is a config.ini VALUE, so which resource to fetch second is
// only known after the first fetch (config.ini) has been parsed —
// buildGamePackage() itself sequences "fetch config.ini -> parse it ->
// fetch config.swfFilename -> hand its bytes to SwfLoader unchanged".

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "runtime/Movie.h"
#include "vc/GameConfig.h"

namespace flash3ds::vc {

struct GamePackage {
    GameConfig config;

    // Always non-null -- check movie->valid before using it. If the
    // configured SWF resource wasn't found at all, movie->valid is false
    // and movie->errorMessage says so explicitly ("Resource not found:
    // ..."), distinct from SwfLoader's own generic "too small"/"invalid
    // signature" messages for a resource that WAS found but is empty or
    // corrupt -- see docs/virtual-console.md's error-handling table.
    std::unique_ptr<runtime::Movie> movie;
};

// Reads the full contents of a ROOT-level resource by name into
// `outBytes`, returning true on success or false if it doesn't exist (or
// couldn't be read). Implemented by the platform layer -- e.g.
// platform::Nintendo3DSRomfs::readFile on the 3DS target, or a small
// std::ifstream-backed lambda for desktop tools/tests. GamePackage.cpp
// itself never implements or assumes any particular filesystem.
using ResourceFetcher = std::function<bool(const std::string& name, std::vector<uint8_t>& outBytes)>;

// Builds a GamePackage: fetches "config.ini" via `fetch` (a fetch failure
// here is NOT an error -- it means "use documented defaults", per
// GameConfig::fromIniText("")'s own contract), then fetches whatever
// config.swfFilename resolved to (default "game.swf") and hands its bytes
// to the EXISTING SwfLoader unchanged.
GamePackage buildGamePackage(const ResourceFetcher& fetch);

}  // namespace flash3ds::vc
