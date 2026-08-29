// PcmSoundDecoder.cpp
//
// See PcmSoundDecoder.h for the public API and full design/scope notes.
//
// ADPCM table/algorithm provenance (2026-08-29, researched specifically
// for this implementation — public sources only, cross-checked against
// each other, no code copied from any of them):
//   - fad.sourceforge.net's "Alexis' SWF Reference" (ADPCMSOUNDDATA/
//     ADPCMPACKET field layout, transcribed from Adobe's SWF spec)
//   - wiki.multimedia.cx's "Flash IMA ADPCM" page (the four per-code-size
//     index tables, and confirmation the step table matches standard IMA)
//   - wiki.multimedia.cx's "IMA ADPCM" page (confirms the 4-bit index
//     table and 89-entry step table match the well-known public IMA
//     ADPCM constants)
//   - The general shift-and-add diff-computation algorithm (this
//     implementation's decodeAdpcmDiff() below) was verified by hand
//     against the classic 4-bit IMA ADPCM bit-by-bit form
//     (diff=step>>3; if(code&4)diff+=step; if(code&2)diff+=step>>1;
//     if(code&1)diff+=step>>2 — a form published in numerous public IMA
//     ADPCM references) BOTH matches this file's generalized loop when
//     bits=4, AND was specifically double-checked against a real
//     decoder's documented behavior (FFmpeg's adpcm_swf_decode path) for
//     the OTHER bit widths — an earlier draft of this file used a
//     closed-form `(step*(2*magnitude+1))>>(bits-1)` formula that looked
//     numerically equivalent by hand-checking magnitude=0 and
//     magnitude=max, but is NOT bit-exact for non-power-of-2 step values
//     (confirmed by a concrete counterexample: step=7, magnitude=7,
//     bits=4 — shift-and-add gives 11, the closed form gives 13). The
//     shift-and-add loop below is the corrected, verified form.
//
// Since real corpus content is 100% MP3 (see PcmSoundDecoder.h's own
// scope note), this decoder has never decoded a real SWF ADPCM/
// uncompressed sound — it's verified via self-consistent round-trip
// tests (tests/test_pcm_sound_decoder.cpp: a test-only encoder,
// implementing the exact same tables/algorithm in reverse, round-trips
// through this decoder and checks for an exact match) rather than against
// real content or an official test vector, which is the best available
// verification without either.

#include "audio/PcmSoundDecoder.h"

#include <algorithm>

#include "platform/Log.h"
#include "swf/SwfReader.h"

namespace flash3ds::audio {

namespace {

// 89-entry IMA ADPCM step-size table — identical to standard IMA ADPCM
// (SWF does not define its own; only the index tables below are
// SWF-specific, to support ADPCMCodeSize values other than the standard
// 4 bits). See this file's top-of-file comment for sourcing.
constexpr int16_t kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

// Index-adjustment tables, one per ADPCMCodeSize (bits = 2, 3, 4, 5).
// Each table has 2^(bits-1) entries — i.e. indexed directly by
// `magnitude` (the code with its sign bit already masked off), NOT by
// the full `bits`-wide code. The 4-bit table is bit-for-bit the same as
// standard IMA ADPCM's well-known 4-bit index table. See this file's
// top-of-file comment for sourcing.
constexpr int8_t kIndexTable2[2] = {-1, 2};
constexpr int8_t kIndexTable3[4] = {-1, -1, 2, 4};
constexpr int8_t kIndexTable4[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
constexpr int8_t kIndexTable5[16] = {-1, -1, -1, -1, -1, -1, -1, -1, 1, 2, 4, 6, 8, 10, 13, 16};

const int8_t* indexTableFor(int bits) {
    switch (bits) {
        case 2: return kIndexTable2;
        case 3: return kIndexTable3;
        case 4: return kIndexTable4;
        case 5: return kIndexTable5;
        default: return nullptr;  // bits is always 2-5 (ADPCMCodeSize is a 2-bit field, +2) — unreachable
    }
}

// Generalized IMA-style shift-and-add diff computation, for a `bits`-wide
// magnitude value (i.e. `bits-1` actual bits, the code with its sign bit
// already removed) against the current step value. See this file's
// top-of-file comment: this is the form confirmed against a real
// decoder's behavior, NOT the numerically-close-but-wrong closed-form
// multiply this file's own history flagged as a corrected mistake.
int32_t decodeAdpcmDiff(int32_t step, uint32_t magnitude, int bits) {
    int32_t vpdiff = 0;
    int32_t stepLocal = step;
    for (uint32_t k = 1u << (bits - 2); k != 0; k >>= 1) {
        if (magnitude & k) vpdiff += stepLocal;
        stepLocal >>= 1;
    }
    vpdiff += stepLocal;
    return vpdiff;
}

constexpr uint32_t kAdpcmBlockSize = 4096;  // samples per channel, per ADPCMPACKET

}  // namespace

std::optional<DecodedAudio> decodeSwfUncompressedSound(const uint8_t* data, size_t length,
                                                         double sampleRateHz, bool is16Bit,
                                                         bool stereo, uint32_t sampleCount) {
    if (!data || sampleCount == 0) {
        return std::nullopt;
    }
    const int channels = stereo ? 2 : 1;
    const size_t bytesPerSample = is16Bit ? 2 : 1;
    const size_t neededBytes = static_cast<size_t>(sampleCount) * static_cast<size_t>(channels) *
                                bytesPerSample;
    if (length < neededBytes) {
        LOG_WARN("AUDIO",
                  "decodeSwfUncompressedSound: need %zu bytes for sampleCount=%u channels=%d "
                  "is16Bit=%d, only have %zu -- refusing to decode",
                  neededBytes, static_cast<unsigned int>(sampleCount), channels, is16Bit ? 1 : 0,
                  length);
        return std::nullopt;
    }

    DecodedAudio result;
    result.sampleRate = static_cast<int>(sampleRateHz + 0.5);
    result.channels = channels;
    result.samples.resize(static_cast<size_t>(sampleCount) * static_cast<size_t>(channels));

    swf::SwfReader reader(data, length);
    for (size_t i = 0; i < result.samples.size(); ++i) {
        if (is16Bit) {
            result.samples[i] = reader.readS16();
        } else {
            // 8-bit UNSIGNED, centered at 128 (silence) per spec --
            // convert to signed 16-bit by centering at 0 and scaling into
            // the full 16-bit range.
            uint8_t raw = reader.readU8();
            result.samples[i] = static_cast<int16_t>((static_cast<int>(raw) - 128) << 8);
        }
    }
    return result;
}

std::optional<DecodedAudio> decodeSwfAdpcmSound(const uint8_t* data, size_t length,
                                                 double sampleRateHz, bool stereo,
                                                 uint32_t sampleCount) {
    if (!data || length < 1 || sampleCount == 0) {
        return std::nullopt;
    }
    const int channels = stereo ? 2 : 1;

    swf::SwfReader reader(data, length);
    const int bits = static_cast<int>(reader.readUBits(2)) + 2;  // ADPCMCodeSize -> 2..5
    const int8_t* indexTable = indexTableFor(bits);
    if (!indexTable) {
        // Unreachable in practice (a 2-bit field can only produce 2-5),
        // but defend against a corrupt/truncated read anyway.
        return std::nullopt;
    }

    DecodedAudio result;
    result.sampleRate = static_cast<int>(sampleRateHz + 0.5);
    result.channels = channels;
    result.samples.reserve(static_cast<size_t>(sampleCount) * static_cast<size_t>(channels));

    int32_t predictor[2] = {0, 0};
    int32_t stepIndex[2] = {0, 0};

    uint32_t samplesDecoded = 0;
    while (samplesDecoded < sampleCount && !reader.failed()) {
        const uint32_t blockSamples = std::min<uint32_t>(kAdpcmBlockSize, sampleCount - samplesDecoded);

        // Per-block header: for each channel, a 16-bit signed initial
        // predictor + 6-bit unsigned initial step-table index. The
        // initial predictor IS the block's first real output sample per
        // channel (not just seed state) -- pushed directly below.
        for (int ch = 0; ch < channels; ++ch) {
            predictor[ch] = reader.readSBits(16);
            stepIndex[ch] = std::clamp<int32_t>(static_cast<int32_t>(reader.readUBits(6)), 0, 88);
        }
        if (reader.failed()) break;
        for (int ch = 0; ch < channels; ++ch) {
            result.samples.push_back(static_cast<int16_t>(predictor[ch]));
        }
        ++samplesDecoded;

        // Remaining samples in this block: one `bits`-wide delta code per
        // channel, interleaved (L, R, L, R, ... for stereo) -- see this
        // file's header comment for why interleaved, not
        // channel-sequential.
        for (uint32_t i = 1; i < blockSamples && samplesDecoded < sampleCount; ++i) {
            for (int ch = 0; ch < channels; ++ch) {
                uint32_t code = reader.readUBits(bits);
                if (reader.failed()) break;

                const uint32_t signMask = 1u << (bits - 1);
                const uint32_t magnitude = code & (signMask - 1u);
                const bool negative = (code & signMask) != 0;

                const int32_t step = kStepTable[stepIndex[ch]];
                int32_t diff = decodeAdpcmDiff(step, magnitude, bits);
                if (negative) diff = -diff;

                predictor[ch] = std::clamp<int32_t>(predictor[ch] + diff, -32768, 32767);
                stepIndex[ch] = std::clamp<int32_t>(stepIndex[ch] + indexTable[magnitude], 0, 88);

                result.samples.push_back(static_cast<int16_t>(predictor[ch]));
            }
            if (reader.failed()) break;
            ++samplesDecoded;
        }
    }

    if (result.samples.empty()) {
        return std::nullopt;
    }
    return result;
}

}  // namespace flash3ds::audio
