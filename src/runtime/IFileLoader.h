// IFileLoader.h
//
// Roadmap Phase 4 (2026-08-21, loadMovie/multi-SWF — see
// docs/known-limitations.md L4): abstract "fetch bytes for a URL/path"
// seam, mirroring audio::IAudioBackend's design (src/audio/IAudioBackend.h)
// — flash3ds_core stays platform-independent, a desktop implementation
// exists for testing/real use (LocalFileLoader), and a Nintendo 3DS
// implementation (reading from the cartridge/SD filesystem) can arrive
// later without touching runtime/ or avm1/. Deliberately NOT built yet —
// see this header's own note below for why.
//
// The only current caller is MovieClipInstance::loadMovie() (AS2's
// MovieClip.loadMovie(url)), which needs raw SWF bytes for a URL/path it
// doesn't otherwise know how to interpret — same "seam so the core stays
// platform-independent" reasoning IAudioBackend/IRenderer already
// established, not a new architectural pattern.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flash3ds::runtime {

class IFileLoader {
public:
    virtual ~IFileLoader() = default;

    // Returns the full contents of `url` (this project's own convention:
    // effectively a filesystem path — no protocol/network layer of any
    // kind, matching real Flash's own local-file loadMovie() usage far
    // more than its http:// case, which is explicitly out of scope), or
    // std::nullopt if it can't be read (missing file, permission error,
    // whatever a concrete implementation's own I/O layer reports) — a
    // caller must treat nullopt as "this load failed" and NOT crash or
    // proceed with a partial/garbage buffer.
    virtual std::optional<std::vector<uint8_t>> loadFile(const std::string& url) = 0;
};

// Default IFileLoader: always fails (logs a warning naming the URL) and
// loads nothing. This is what ScriptEnvironment uses when nothing else is
// wired up (every existing test and the CLI, until a caller explicitly
// opts in) — the same "logs what it was asked to do, does nothing real"
// role NullAudioBackend already plays for audio, and a HostBindings-less
// ExecutionContext already plays for scene-graph actions (Phase 4's
// original precedent). A missing/unwired file loader is NOT a crash or an
// exception — attachMovie()-style scripts against static library symbols
// keep working exactly as before; only loadMovie() calls actually need a
// real loader, and those simply no-op (logged) without one.
class NullFileLoader : public IFileLoader {
public:
    std::optional<std::vector<uint8_t>> loadFile(const std::string& url) override;
};

}  // namespace flash3ds::runtime
