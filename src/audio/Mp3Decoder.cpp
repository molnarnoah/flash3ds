// Mp3Decoder.cpp
//
// See Mp3Decoder.h for the public API/design notes. This is the ONLY
// translation unit in the project that includes minimp3.h (and the only
// one that defines MINIMP3_IMPLEMENTATION, which must happen in exactly
// one .cpp per the single-header-library pattern minimp3.h uses).

#include "audio/Mp3Decoder.h"

#include "platform/Log.h"

// The 3DS's ARM11 CPU is ARMv6 (no NEON) — minimp3.h's own SIMD gating
// already falls back to scalar C for any target that isn't x86 SSE2/
// AArch64 NEON, so this define is not strictly required for a correct
// build, but it documents the intent explicitly and guarantees the
// scalar path is used rather than relying on the absence of an SSE/NEON
// macro on this particular cross-compiler.
#ifdef __3DS__
#define MINIMP3_NO_SIMD
#endif

#define MINIMP3_IMPLEMENTATION
// minimp3.h is third-party, vendored code (see third_party/minimp3/
// README.md) — it is not held to this project's own -Wall -Wextra
// zero-warnings bar, and pulling it in as a plain #include means the
// compiler attributes any of its internal warnings to this translation
// unit. Silence warnings for exactly this one include, not the rest of
// the file.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include "minimp3.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace flash3ds::audio {

std::optional<DecodedAudio> decodeMp3(const uint8_t* data, size_t length) {
    if (!data || length == 0) {
        return std::nullopt;
    }

    mp3dec_t dec;
    mp3dec_init(&dec);

    DecodedAudio result;
    // MINIMP3_MAX_SAMPLES_PER_FRAME is samples-per-channel*2 (i.e. already
    // sized for the worst case, stereo) — see minimp3.h's own comment on
    // the constant.
    int16_t frameBuf[MINIMP3_MAX_SAMPLES_PER_FRAME];

    const uint8_t* cursor = data;
    size_t remaining = length;
    bool decodedAnyFrame = false;

    // Defense-in-depth against a pathological input driving this into an
    // unbounded loop: real MP3 frames are at most a few thousand bytes, so
    // a stream this size could never legitimately contain more frames than
    // this — matches the style of other defense-in-depth caps in this
    // codebase (e.g. ShapeRecords.cpp's kMaxRecords).
    constexpr size_t kMaxFrames = 2'000'000;
    size_t framesDecoded = 0;

    while (remaining > 0 && framesDecoded < kMaxFrames) {
        mp3dec_frame_info_t info;
        int samplesPerChannel = mp3dec_decode_frame(
            &dec, cursor, static_cast<int>(remaining), frameBuf, &info);

        if (info.frame_bytes <= 0) {
            // No frame sync found anywhere in the remaining bytes — stop.
            // (mp3dec_decode_frame's own contract: frame_bytes==0 only
            // when it couldn't even find a candidate frame header, at
            // which point further calls would make no more progress.)
            break;
        }

        if (samplesPerChannel > 0) {
            if (!decodedAnyFrame) {
                // First successfully decoded frame establishes the output
                // format.
                result.sampleRate = info.hz;
                result.channels = info.channels;
                decodedAnyFrame = true;
            } else if (info.channels != result.channels) {
                // Mixed channel counts mid-stream — stop rather than
                // mis-interleave (see header comment). Advance past this
                // frame's bytes is unnecessary since we're stopping.
                break;
            }
            const size_t totalSamples = static_cast<size_t>(samplesPerChannel) *
                                         static_cast<size_t>(info.channels);
            result.samples.insert(result.samples.end(), frameBuf, frameBuf + totalSamples);
        }
        // samplesPerChannel == 0 with frame_bytes > 0 means minimp3
        // recognized/skipped a frame it couldn't decode (e.g. a
        // free-format edge case) — just advance past it, matching a real
        // player's tolerance for the occasional bad frame.

        cursor += info.frame_bytes;
        remaining -= static_cast<size_t>(info.frame_bytes);
        ++framesDecoded;
    }

    if (!decodedAnyFrame) {
        LOG_WARN("AUDIO", "decodeMp3: no valid MP3 frame found in %zu bytes", length);
        return std::nullopt;
    }

    LOG_DEBUG("AUDIO", "decodeMp3: decoded %zu frames -> %zu samples (%d Hz, %d ch)",
              framesDecoded, result.samples.size(), result.sampleRate, result.channels);
    return result;
}

std::optional<DecodedAudio> decodeSwfMp3Sound(const uint8_t* data, size_t length) {
    // MP3SOUNDDATA: SeekSamples (SI16, 2 bytes, little-endian) then
    // MP3Frames — see Mp3Decoder.h's doc comment and the public SWF File
    // Format Specification's DefineSound section.
    constexpr size_t kSeekSamplesBytes = 2;
    if (!data || length < kSeekSamplesBytes) {
        return std::nullopt;
    }
    return decodeMp3(data + kSeekSamplesBytes, length - kSeekSamplesBytes);
}

}  // namespace flash3ds::audio
