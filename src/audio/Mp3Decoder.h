// Mp3Decoder.h
//
// Roadmap Phase 3 (2026-08-21): MP3 decode for DefineSound's embedded
// audio (see docs/known-limitations.md L1, docs/implementation-roadmap.md
// Phase 3). Wraps the vendored `third_party/minimp3/minimp3.h` (CC0/
// public domain — see that directory's README.md for why this library
// specifically, and exact provenance) behind a small, project-shaped API:
// raw MP3 bytes in, PCM16 samples out, no minimp3 types leak past this
// header (Mp3Decoder.cpp is the only translation unit that includes
// minimp3.h at all).
//
// This is decode-only: it does not know about SoundDef, Movie, or
// IAudioBackend. `runtime::ScriptEnvironment` (src/runtime/
// MovieClipInstance.h/.cpp) is what resolves a SWF soundId to raw bytes,
// calls decodeSwfMp3Sound() (decode-on-demand, cached after first
// reference — see that class's own comment for why, tying into the
// ongoing docs/memory-audit.md memory-cost findings), and hands the
// result to the current IAudioBackend.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace flash3ds::audio {

struct DecodedAudio {
    // Interleaved PCM16 samples (frame-major: for stereo, L0 R0 L1 R1 ...).
    std::vector<int16_t> samples;
    int sampleRate = 0;
    int channels = 0;  // 1 = mono, 2 = stereo

    // Number of samples PER CHANNEL (i.e. samples.size() / channels,
    // precomputed for callers that just want playback duration/frame
    // count without redoing the division).
    size_t frameCount() const { return channels > 0 ? samples.size() / static_cast<size_t>(channels) : 0; }
};

// Decodes a raw, container-less MP3 byte stream (back-to-back MPEG audio
// frames, no ID3/RIFF/etc. framing — real-world MP3 files often have
// leading ID3 tags or junk before the first frame sync; this scans for
// the first valid frame sync anywhere in `data` rather than assuming byte
// 0 is a frame header, matching real players' tolerance for that) into
// PCM16.
//
// All decodable frames are concatenated into one PCM buffer. The decoded
// sample rate/channel count are taken from the FIRST successfully decoded
// frame; if a later frame reports a different channel count (malformed/
// mixed-format input — not expected from a well-formed SWF, but SWF
// content is untrusted input), decoding stops at that point rather than
// silently mis-interleaving samples of two different channel counts into
// one buffer — the caller gets whatever was decoded before that frame.
//
// Returns std::nullopt only if NO frame anywhere in `data` decoded
// successfully (empty input, non-MP3 data, or a stream that's corrupt
// from the very first frame). A stream that decodes some frames then hits
// a bad one partway through still returns Some(partial result), not
// nullopt — matches how a real player degrades (plays what it can) rather
// than failing the whole sound over one bad frame.
std::optional<DecodedAudio> decodeMp3(const uint8_t* data, size_t length);

// SWF-specific entry point: per the public SWF File Format Specification,
// a DefineSound tag's SoundData for SoundFormat == MP3 (swf::SoundFormat::
// kMp3) is an MP3SOUNDDATA record — a 2-byte little-endian SI16
// "SeekSamples" field, THEN the actual MP3 frame data (not raw MP3 frames
// starting at byte 0, unlike a plain .mp3 file). This skips that field
// and calls decodeMp3() on what follows. `SeekSamples` itself (a
// stream-seeking hint for real Flash's player) is intentionally not
// surfaced here — this runtime always decodes/plays a DefineSound from
// its start, it never seeks mid-stream.
//
// Returns std::nullopt if `length < 2` (too short to even contain
// SeekSamples) or if decodeMp3() on the remainder returns nullopt.
std::optional<DecodedAudio> decodeSwfMp3Sound(const uint8_t* data, size_t length);

}  // namespace flash3ds::audio
