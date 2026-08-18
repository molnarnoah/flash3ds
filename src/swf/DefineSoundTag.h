// DefineSoundTag.h
//
// Clean-room structural parser for DefineSound (tag 14). Phase 6 scope is
// deliberately "structural only" (see CLAUDE.md's Phase 6 plan): this reads
// the fixed header fields (SoundId, the bit-packed format/rate/size/type
// byte, SampleCount) and records where the compressed sample data lives in
// Movie::data, but does NOT decode any audio codec (ADPCM/MP3/etc — that's
// an IAudioBackend implementation's job, not this parser's).

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "swf/SwfReader.h"

namespace flash3ds::swf {

// SoundFormat values (DefineSound's 4-bit SoundFormat field). Names/values
// per the public SWF spec's sound-format table.
enum class SoundFormat : uint8_t {
    kUncompressedNative = 0,
    kAdpcm = 1,
    kMp3 = 2,
    kUncompressedLittleEndian = 3,
    kNellymoser16k = 4,
    kNellymoser8k = 5,
    kNellymoser = 6,
    kSpeex = 11,
};

// SoundRate values (DefineSound's 2-bit SoundRate field): sample rate in Hz.
enum class SoundRate : uint8_t {
    k5512Hz = 0,
    k11025Hz = 1,
    k22050Hz = 2,
    k44100Hz = 3,
};

struct SoundDef {
    uint16_t soundId = 0;
    SoundFormat format = SoundFormat::kUncompressedNative;
    SoundRate rate = SoundRate::k5512Hz;
    bool is16Bit = false;
    bool stereo = false;
    uint32_t sampleCount = 0;

    // Absolute offset/length of the raw (still-compressed, per `format`)
    // sample data within Movie::data — an IAudioBackend that supports
    // `format` can read it directly via these; a backend that doesn't
    // (or NullAudioBackend, which never plays anything) can ignore them.
    size_t dataOffset = 0;
    size_t dataLength = 0;
};

// `bodyAbsoluteOffset` is the tag body's own absolute offset into
// Movie::data (i.e. TagRecord::bodyOffset) — needed to compute
// SoundDef::dataOffset as an absolute offset rather than one relative to
// `reader`'s own (tag-body-scoped) start.
std::optional<SoundDef> parseDefineSound(SwfReader& reader, size_t bodyAbsoluteOffset);

double soundRateHz(SoundRate rate);

}  // namespace flash3ds::swf
