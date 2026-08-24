// test_mp3_decoder.cpp
//
// Roadmap Phase 3 (2026-08-21, MP3 audio decode — see
// docs/known-limitations.md L1). Unit tests for audio::decodeMp3()/
// decodeSwfMp3Sound() (src/audio/Mp3Decoder.h) against a real MP3 payload
// (SwfTestFixtures::sampleMp3AudioBytes() — see that function's own
// comment for why a real ffmpeg-encoded stream is used here instead of
// hand-built bytes, unlike the rest of this test suite's SWF fixtures).
//
// Integration-level coverage (a real DefineSound/StartSound tag carrying
// this same MP3 payload actually reaching ScriptEnvironment::
// playSoundById() and IAudioBackend::loadSound()) lives in
// test_movieclip_instance.cpp, not here — this file is decoder-only, with
// no SWF/runtime dependency at all.

#include <cmath>
#include <cstdint>

#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "audio/Mp3Decoder.h"

namespace swf_fixtures = flash3ds::test::fixtures;
using flash3ds::audio::decodeMp3;
using flash3ds::audio::decodeSwfMp3Sound;

namespace {

// Root-mean-square of a PCM16 buffer — a simple, deterministic "is this
// actually audio, not silence" checksum. Real digital silence (or an
// all-zero/failed-decode buffer) has rms == 0 exactly; any real decoded
// audio has rms well above 0.
double rms(const std::vector<int16_t>& samples) {
    if (samples.empty()) return 0.0;
    int64_t sumSquares = 0;
    for (int16_t s : samples) {
        sumSquares += static_cast<int64_t>(s) * static_cast<int64_t>(s);
    }
    return std::sqrt(static_cast<double>(sumSquares) / static_cast<double>(samples.size()));
}

}  // namespace

TEST_CASE(DecodeMp3_RealPayload_ProducesNonSilentPcm) {
    const auto& mp3 = swf_fixtures::sampleMp3AudioBytes();
    auto decoded = decodeMp3(mp3.data(), mp3.size());
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->sampleRate, 44100);
    CHECK_EQ(decoded->channels, 1);
    // A real ~50ms mono MP3 at 44100Hz decodes to on the order of a few
    // thousand samples (MPEG1 Layer III's 1152-samples-per-frame granule
    // size means even a handful of frames adds up quickly) — this is a
    // loose sanity bound, not an exact expected count (exact sample count
    // depends on encoder framing details this test doesn't need to pin
    // down), so >= 1000 is enough to confirm real decode happened rather
    // than e.g. a single empty/degenerate frame.
    CHECK(decoded->samples.size() >= 1000);
    CHECK_EQ(decoded->frameCount(), decoded->samples.size());  // mono: frames == samples

    // The actual "is this audio, not silence" check, per this file's own
    // header comment.
    CHECK(rms(decoded->samples) > 0.0);
}

TEST_CASE(DecodeMp3_EmptyInput_ReturnsNullopt) {
    auto decoded = decodeMp3(nullptr, 0);
    CHECK(!decoded.has_value());
}

TEST_CASE(DecodeMp3_GarbageInput_ReturnsNullopt) {
    // Bytes that are never a valid MPEG frame sync (0xFF followed by a
    // byte whose top 3 bits aren't all set) anywhere in the buffer.
    std::vector<uint8_t> garbage(64, 0x00);
    auto decoded = decodeMp3(garbage.data(), garbage.size());
    CHECK(!decoded.has_value());
}

TEST_CASE(DecodeMp3_TruncatedToFirstByte_ReturnsNullopt) {
    const auto& mp3 = swf_fixtures::sampleMp3AudioBytes();
    // Just the frame-sync byte, no header/body — not enough to decode
    // anything, but also shouldn't crash or hang.
    auto decoded = decodeMp3(mp3.data(), 1);
    CHECK(!decoded.has_value());
}

TEST_CASE(DecodeSwfMp3Sound_SkipsSeekSamplesPrefix_MatchesDecodeMp3OnRemainder) {
    const auto& mp3 = swf_fixtures::sampleMp3AudioBytes();
    // Build a real MP3SOUNDDATA-shaped buffer: 2-byte SeekSamples (value
    // doesn't matter — this runtime never seeks mid-stream, see
    // Mp3Decoder.h) followed by the same real MP3 frame data.
    std::vector<uint8_t> soundData;
    soundData.push_back(0x34);  // arbitrary SeekSamples low byte
    soundData.push_back(0x12);  // arbitrary SeekSamples high byte
    soundData.insert(soundData.end(), mp3.begin(), mp3.end());

    auto viaSwfEntryPoint = decodeSwfMp3Sound(soundData.data(), soundData.size());
    auto viaDirectDecode = decodeMp3(mp3.data(), mp3.size());

    CHECK(viaSwfEntryPoint.has_value());
    CHECK(viaDirectDecode.has_value());
    CHECK_EQ(viaSwfEntryPoint->samples.size(), viaDirectDecode->samples.size());
    CHECK_EQ(viaSwfEntryPoint->sampleRate, viaDirectDecode->sampleRate);
    CHECK_EQ(viaSwfEntryPoint->channels, viaDirectDecode->channels);
    CHECK(viaSwfEntryPoint->samples == viaDirectDecode->samples);
}

TEST_CASE(DecodeSwfMp3Sound_TooShortForSeekSamplesField_ReturnsNullopt) {
    std::vector<uint8_t> oneByte = {0xFF};
    auto decoded = decodeSwfMp3Sound(oneByte.data(), oneByte.size());
    CHECK(!decoded.has_value());

    auto decodedEmpty = decodeSwfMp3Sound(nullptr, 0);
    CHECK(!decodedEmpty.has_value());
}

TEST_CASE(DecodeMp3_LeadingJunkBeforeFrameSync_StillFindsFirstFrame) {
    // Real-world MP3 files often carry leading junk (ID3 tags, in
    // practice) before the first actual frame sync — decodeMp3() must
    // scan for it rather than assume byte 0 is always a frame header.
    const auto& mp3 = swf_fixtures::sampleMp3AudioBytes();
    std::vector<uint8_t> withJunk(32, 0xAB);  // never a valid frame sync
    withJunk.insert(withJunk.end(), mp3.begin(), mp3.end());

    auto decoded = decodeMp3(withJunk.data(), withJunk.size());
    CHECK(decoded.has_value());
    CHECK(rms(decoded->samples) > 0.0);
}
