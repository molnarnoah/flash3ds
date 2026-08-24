// LocalFileLoader.h
//
// A real, genuine IFileLoader implementation (Roadmap Phase 4, 2026-08-21):
// reads `url` as an ordinary filesystem path relative to (or, if absolute,
// exactly at) a caller-chosen base directory, via plain ifstream — no
// network/protocol layer, matching IFileLoader.h's own documented scope.
// Lives under src/runtime/ (not tools/), same precedent as
// audio::NullAudioBackend/Nintendo3DSAudioBackend both living under
// src/audio/ rather than being deferred to a test-only helper — this is
// meant to be a genuinely usable desktop/test backend, not a throwaway.
//
// A Nintendo3DSFileLoader (reading from the cartridge RomFS or SD card via
// libctru/libfs) is explicitly deferred — out of scope until a real target
// title actually needs loadMovie() to work on-device, matching this
// project's established precedent of not building 3DS backends ahead of
// verified need (see CLAUDE.md's Phase 10 notes on the same policy for
// rendering/audio/input).

#pragma once

#include <string>

#include "runtime/IFileLoader.h"

namespace flash3ds::runtime {

class LocalFileLoader : public IFileLoader {
public:
    // `baseDir` is prepended to every relative `url` passed to loadFile()
    // (empty by default, meaning "resolve relative to the process's
    // current working directory," the ifstream default). An absolute
    // `url` (starts with '/') is used as-is, ignoring `baseDir` — matches
    // ordinary filesystem path-joining semantics, not URL semantics.
    explicit LocalFileLoader(std::string baseDir = "");

    std::optional<std::vector<uint8_t>> loadFile(const std::string& url) override;

private:
    std::string baseDir_;
};

}  // namespace flash3ds::runtime
