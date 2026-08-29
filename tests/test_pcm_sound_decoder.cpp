// test_pcm_sound_decoder.cpp
//
// Fidelity-audit TASK 1, divergence #6 (docs/flash-fidelity-audit.md,
// docs/audio.md — 2026-08-29): unit tests for
// audio::decodeSwfUncompressedSound() / audio::decodeSwfAdpcmSound()
// (src/audio/PcmSoundDecoder.h/.cpp).
//
// As PcmSoundDecoder.h's own top-of-file scope note explains, the real
// corpus (all 30 SWF files across Hobo 1-8 + Extreme Pamplona + its 21
// sub-SWFs) is 100% MP3 DefineSound content — there is no real
// uncompressed/ADPCM SWF audio anywhere to test against, unlike
// test_mp3_decoder.cpp's real ffmpeg-encoded fixture. So this file takes
// the same approach PcmSoundDecoder.h documents: build synthetic
// SWF-format bitstreams by hand (a small MSB-first bit writer below,
// mirroring SwfReader's own bit order, confirmed against SwfReader.cpp
// directly) and, for ADPCM, cross-check the decoder's output against an
// INDEPENDENTLY-transcribed reference implementation of the same
// step/index tables and shift-and-add diff algorithm (not a call into
// PcmSoundDecoder.cpp's own helpers) — this catches transcription bugs
// (table typos, off-by-one loop bounds, wrong clamp bounds, wrong
// interleave order, wrong block-reset boundary) that a test merely
// re-deriving expected values from the same production code could not.

#include <cstdint>
#include <vector>

#include "TestFramework.h"
#include "audio/PcmSoundDecoder.h"

using flash3ds::audio::decodeSwfAdpcmSound;
using flash3ds::audio::decodeSwfUncompressedSound;

namespace {

// --- MSB-first bit writer, mirroring SwfReader::readUBits()'s bit order
// (confirmed directly against SwfReader.cpp: each byte's bits are
// consumed most-significant-bit-first). Test-only -- not part of the
// production decoder.
class BitWriter {
public:
    void writeUBits(uint32_t value, int numBits) {
        for (int i = numBits - 1; i >= 0; --i) {
            uint32_t bit = (value >> i) & 1u;
            currentByte_ = static_cast<uint8_t>((currentByte_ << 1) | bit);
            ++bitsInCurrentByte_;
            if (bitsInCurrentByte_ == 8) {
                bytes_.push_back(currentByte_);
                currentByte_ = 0;
                bitsInCurrentByte_ = 0;
            }
        }
    }

    void writeSBits(int32_t value, int numBits) {
        writeUBits(static_cast<uint32_t>(value) & ((numBits >= 32) ? 0xFFFFFFFFu : ((1u << numBits) - 1u)),
                   numBits);
    }

    // Pads the final partial byte with zero bits (matching byteAlign()'s
    // read-side convention of discarding leftover bits) and returns the
    // finished buffer.
    std::vector<uint8_t> finish() {
        if (bitsInCurrentByte_ > 0) {
            currentByte_ = static_cast<uint8_t>(currentByte_ << (8 - bitsInCurrentByte_));
            bytes_.push_back(currentByte_);
            currentByte_ = 0;
            bitsInCurrentByte_ = 0;
        }
        return bytes_;
    }

private:
    std::vector<uint8_t> bytes_;
    uint8_t currentByte_ = 0;
    int bitsInCurrentByte_ = 0;
};

// --- Independently-transcribed reference ADPCM decode, used only to
// compute expected values in this test file -- deliberately NOT sharing
// code with src/audio/PcmSoundDecoder.cpp, so a bug transcribed into both
// places identically is the only way this cross-check could pass
// spuriously.
constexpr int16_t kRefStepTable[89] = {
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

const int8_t* refIndexTable(int bits) {
    static constexpr int8_t t2[2] = {-1, 2};
    static constexpr int8_t t3[4] = {-1, -1, 2, 4};
    static constexpr int8_t t4[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
    static constexpr int8_t t5[16] = {-1, -1, -1, -1, -1, -1, -1, -1, 1, 2, 4, 6, 8, 10, 13, 16};
    switch (bits) {
        case 2: return t2;
        case 3: return t3;
        case 4: return t4;
        case 5: return t5;
    }
    return nullptr;
}

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Applies one ADPCM delta code to a running (predictor, stepIndex) pair,
// returning the new predictor value (the decoded sample) and updating
// both by reference -- same job as PcmSoundDecoder.cpp's
// decodeAdpcmDiff()+surrounding update logic, but written from scratch
// against the classic bit-by-bit IMA form rather than transcribed from
// that file.
int16_t refApplyAdpcmCode(uint32_t code, int bits, int32_t& predictor, int32_t& stepIndex) {
    const uint32_t signMask = 1u << (bits - 1);
    const uint32_t magnitude = code & (signMask - 1u);
    const bool negative = (code & signMask) != 0;

    const int32_t step = kRefStepTable[stepIndex];
    int32_t diff = 0;
    int32_t stepShift = step;
    for (int bit = bits - 2; bit >= 0; --bit) {
        if (magnitude & (1u << bit)) diff += stepShift;
        stepShift >>= 1;
    }
    diff += stepShift;  // final term: step >> (bits - 1)
    if (negative) diff = -diff;

    predictor = clampInt(predictor + diff, -32768, 32767);
    stepIndex = clampInt(stepIndex + refIndexTable(bits)[magnitude], 0, 88);
    return static_cast<int16_t>(predictor);
}

}  // namespace

// ---------------------------------------------------------------------
// Uncompressed (SoundFormat 0 / 3)
// ---------------------------------------------------------------------

TEST_CASE(DecodeSwfUncompressedSound_16BitMono_ExactSampleMatch) {
    const int16_t expected[] = {0, 1234, -1234, 32767, -32768, -1};
    const uint32_t sampleCount = 6;

    std::vector<uint8_t> bytes;
    for (int16_t s : expected) {
        bytes.push_back(static_cast<uint8_t>(s & 0xFF));
        bytes.push_back(static_cast<uint8_t>((s >> 8) & 0xFF));
    }

    auto decoded = decodeSwfUncompressedSound(bytes.data(), bytes.size(), 44100.0,
                                               /*is16Bit=*/true, /*stereo=*/false, sampleCount);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->sampleRate, 44100);
    CHECK_EQ(decoded->channels, 1);
    CHECK_EQ(decoded->samples.size(), static_cast<size_t>(sampleCount));
    for (size_t i = 0; i < sampleCount; ++i) {
        CHECK_EQ(decoded->samples[i], expected[i]);
    }
}

TEST_CASE(DecodeSwfUncompressedSound_8BitStereo_CentersAt128AndInterleaves) {
    // Raw 8-bit unsigned samples: L0=128 (silence), R0=255 (max), L1=0
    // (min), R1=64.
    const std::vector<uint8_t> bytes = {128, 255, 0, 64};
    auto decoded = decodeSwfUncompressedSound(bytes.data(), bytes.size(), 22050.0,
                                               /*is16Bit=*/false, /*stereo=*/true,
                                               /*sampleCount=*/2);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->channels, 2);
    CHECK_EQ(decoded->samples.size(), static_cast<size_t>(4));
    // (raw - 128) << 8
    CHECK_EQ(decoded->samples[0], static_cast<int16_t>((128 - 128) << 8));  // L0 = 0
    CHECK_EQ(decoded->samples[1], static_cast<int16_t>((255 - 128) << 8));  // R0 = 32512
    CHECK_EQ(decoded->samples[2], static_cast<int16_t>((0 - 128) << 8));    // L1 = -32768
    CHECK_EQ(decoded->samples[3], static_cast<int16_t>((64 - 128) << 8));   // R1 = -16384
}

TEST_CASE(DecodeSwfUncompressedSound_TruncatedData_ReturnsNullopt) {
    // Claims 10 16-bit mono samples (20 bytes) but only supplies 4.
    const std::vector<uint8_t> bytes = {1, 2, 3, 4};
    auto decoded = decodeSwfUncompressedSound(bytes.data(), bytes.size(), 44100.0, true, false, 10);
    CHECK(!decoded.has_value());
}

TEST_CASE(DecodeSwfUncompressedSound_NullDataOrZeroSampleCount_ReturnsNullopt) {
    const std::vector<uint8_t> bytes = {1, 2, 3, 4};
    CHECK(!decodeSwfUncompressedSound(nullptr, 4, 44100.0, true, false, 4).has_value());
    CHECK(!decodeSwfUncompressedSound(bytes.data(), bytes.size(), 44100.0, true, false, 0).has_value());
}

// ---------------------------------------------------------------------
// ADPCM (SoundFormat 1)
// ---------------------------------------------------------------------

TEST_CASE(DecodeSwfAdpcmSound_MonoSingleBlockFourBitCodes_MatchesReferenceAlgorithm) {
    const int bits = 4;  // ADPCMCodeSize field value 2 -> bits = 2+2 = 4
    const int16_t initialPredictor = 1000;
    const int32_t initialStepIndex = 10;
    const uint32_t codes[] = {0x3, 0xB, 0x0, 0xF, 0x5};  // arbitrary 4-bit codes
    const uint32_t sampleCount = 1 + (sizeof(codes) / sizeof(codes[0]));

    BitWriter writer;
    writer.writeUBits(static_cast<uint32_t>(bits - 2), 2);  // ADPCMCodeSize
    writer.writeSBits(initialPredictor, 16);
    writer.writeUBits(static_cast<uint32_t>(initialStepIndex), 6);
    for (uint32_t code : codes) writer.writeUBits(code, bits);
    std::vector<uint8_t> bytes = writer.finish();

    auto decoded = decodeSwfAdpcmSound(bytes.data(), bytes.size(), 11025.0, /*stereo=*/false, sampleCount);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->sampleRate, 11025);
    CHECK_EQ(decoded->channels, 1);
    CHECK_EQ(decoded->samples.size(), static_cast<size_t>(sampleCount));

    CHECK_EQ(decoded->samples[0], initialPredictor);

    int32_t predictor = initialPredictor;
    int32_t stepIndex = initialStepIndex;
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        int16_t expected = refApplyAdpcmCode(codes[i], bits, predictor, stepIndex);
        CHECK_EQ(decoded->samples[i + 1], expected);
    }
}

TEST_CASE(DecodeSwfAdpcmSound_StereoInterleavesLRPerSample) {
    const int bits = 3;  // ADPCMCodeSize field value 1 -> bits = 3
    const int16_t initialPredictorL = -500;
    const int16_t initialPredictorR = 2000;
    const int32_t initialStepIndexL = 5;
    const int32_t initialStepIndexR = 20;
    // Interleaved L,R code pairs.
    const uint32_t codesL[] = {0x1, 0x6, 0x3};
    const uint32_t codesR[] = {0x7, 0x0, 0x2};
    const uint32_t sampleCount = 1 + (sizeof(codesL) / sizeof(codesL[0]));

    BitWriter writer;
    writer.writeUBits(static_cast<uint32_t>(bits - 2), 2);
    writer.writeSBits(initialPredictorL, 16);
    writer.writeUBits(static_cast<uint32_t>(initialStepIndexL), 6);
    writer.writeSBits(initialPredictorR, 16);
    writer.writeUBits(static_cast<uint32_t>(initialStepIndexR), 6);
    for (size_t i = 0; i < sizeof(codesL) / sizeof(codesL[0]); ++i) {
        writer.writeUBits(codesL[i], bits);
        writer.writeUBits(codesR[i], bits);
    }
    std::vector<uint8_t> bytes = writer.finish();

    auto decoded = decodeSwfAdpcmSound(bytes.data(), bytes.size(), 44100.0, /*stereo=*/true, sampleCount);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->channels, 2);
    CHECK_EQ(decoded->samples.size(), static_cast<size_t>(sampleCount) * 2);

    CHECK_EQ(decoded->samples[0], initialPredictorL);
    CHECK_EQ(decoded->samples[1], initialPredictorR);

    int32_t predictorL = initialPredictorL, predictorR = initialPredictorR;
    int32_t stepIndexL = initialStepIndexL, stepIndexR = initialStepIndexR;
    for (size_t i = 0; i < sizeof(codesL) / sizeof(codesL[0]); ++i) {
        int16_t expectedL = refApplyAdpcmCode(codesL[i], bits, predictorL, stepIndexL);
        int16_t expectedR = refApplyAdpcmCode(codesR[i], bits, predictorR, stepIndexR);
        CHECK_EQ(decoded->samples[2 * (i + 1) + 0], expectedL);
        CHECK_EQ(decoded->samples[2 * (i + 1) + 1], expectedR);
    }
}

TEST_CASE(DecodeSwfAdpcmSound_AllFourCodeSizes_DecodeCorrectSampleCount) {
    for (int bits = 2; bits <= 5; ++bits) {
        BitWriter writer;
        writer.writeUBits(static_cast<uint32_t>(bits - 2), 2);
        writer.writeSBits(0, 16);
        writer.writeUBits(0, 6);
        const uint32_t sampleCount = 4;
        for (uint32_t i = 0; i < sampleCount - 1; ++i) {
            writer.writeUBits(1u, bits);  // smallest positive nonzero code each width supports
        }
        std::vector<uint8_t> bytes = writer.finish();

        auto decoded = decodeSwfAdpcmSound(bytes.data(), bytes.size(), 8000.0, false, sampleCount);
        CHECK(decoded.has_value());
        CHECK_EQ(decoded->samples.size(), static_cast<size_t>(sampleCount));
    }
}

TEST_CASE(DecodeSwfAdpcmSound_SpansMultipleBlocks_SecondBlockResetsFromOwnHeader) {
    // Force exactly two 4096-sample blocks by requesting 4096 + 3 samples,
    // so byte 4097 (0-indexed sample 4096) must come from a SECOND
    // per-channel header, not a continuation of the first block's
    // predictor/step state.
    const int bits = 4;
    const uint32_t sampleCount = 4096 + 3;

    BitWriter writer;
    writer.writeUBits(static_cast<uint32_t>(bits - 2), 2);

    // Block 1 header + 4095 arbitrary codes (fills the rest of the block).
    writer.writeSBits(100, 16);
    writer.writeUBits(0, 6);
    for (uint32_t i = 0; i < 4095; ++i) writer.writeUBits(static_cast<uint32_t>(i % 16), bits);

    // Block 2 header (fresh predictor/step -- deliberately far from where
    // block 1 left off) + 2 more codes.
    const int16_t block2Predictor = -12345;
    const int32_t block2StepIndex = 42;
    const uint32_t block2Codes[] = {0x9, 0x2};
    writer.writeSBits(block2Predictor, 16);
    writer.writeUBits(static_cast<uint32_t>(block2StepIndex), 6);
    for (uint32_t code : block2Codes) writer.writeUBits(code, bits);

    std::vector<uint8_t> bytes = writer.finish();
    auto decoded = decodeSwfAdpcmSound(bytes.data(), bytes.size(), 44100.0, false, sampleCount);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->samples.size(), static_cast<size_t>(sampleCount));

    // Sample index 4096 (the first sample of block 2) must be exactly the
    // block's own header predictor, not a value derived from block 1's
    // running state.
    CHECK_EQ(decoded->samples[4096], block2Predictor);

    int32_t predictor = block2Predictor;
    int32_t stepIndex = block2StepIndex;
    for (size_t i = 0; i < sizeof(block2Codes) / sizeof(block2Codes[0]); ++i) {
        int16_t expected = refApplyAdpcmCode(block2Codes[i], bits, predictor, stepIndex);
        CHECK_EQ(decoded->samples[4097 + i], expected);
    }
}

TEST_CASE(DecodeSwfAdpcmSound_TooShortForEvenTheCodeSizeByte_ReturnsNullopt) {
    CHECK(!decodeSwfAdpcmSound(nullptr, 0, 44100.0, false, 10).has_value());
    const std::vector<uint8_t> empty;
    CHECK(!decodeSwfAdpcmSound(empty.data(), empty.size(), 44100.0, false, 10).has_value());
}

TEST_CASE(DecodeSwfAdpcmSound_ZeroSampleCount_ReturnsNullopt) {
    const std::vector<uint8_t> bytes = {0x00, 0x00};
    CHECK(!decodeSwfAdpcmSound(bytes.data(), bytes.size(), 44100.0, false, 0).has_value());
}
