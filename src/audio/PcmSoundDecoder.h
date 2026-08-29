// PcmSoundDecoder.h
//
// Fidelity-audit TASK 1, divergence #6 (docs/flash-fidelity-audit.md,
// 2026-08-29): decode support for DefineSound's two "uncompressed" SWF
// SoundFormat values (0 = platform-native-endian, 3 = little-endian) and
// SoundFormat 1 (ADPCM — SWF's own variable-bit-width variant of IMA
// ADPCM). Mirrors Mp3Decoder.h's own shape and division of labor: this is
// decode-only (raw bytes in, PCM16 out), knows nothing about SoundDef/
// Movie/IAudioBackend — runtime::ScriptEnvironment::ensureSoundDecoded()
// is what resolves a SWF soundId to raw bytes and calls into here.
//
// Scope note (explicit user decision, 2026-08-29): Nellymoser (SoundFormat
// 4/5/6) and Speex (11) are DELIBERATELY NOT covered here. Nellymoser has
// no public Adobe specification at all — every existing decoder (FFmpeg
// included) is reverse-engineered, not built from documentation, which
// conflicts with this project's CLAUDE.md "implement against the public
// SWF File Format Specification" rule; a clean-room attempt would be
// genuinely hard to get bit-exact without a reference. Speex IS a real
// open/publicly-specified codec (Xiph), but was scoped out this pass —
// revisit (vendoring libspeex, same pattern as minimp3 for MP3) only if a
// target title's corpus is shown to actually need it, same "don't build
// against a hypothetical" reasoning docs/audio.md already documents for
// items #5/#6's other half (SoundStreamHead/Block streaming audio,
// deferred entirely). Real corpus evidence (docs/audio.md): 100% of all
// 430 real DefineSound tags across the whole 30-file corpus are MP3 —
// zero ADPCM/uncompressed/Nellymoser/Speex content exists anywhere, so
// none of this (including what IS implemented here) has been exercised
// against real content; see this header's decode functions' own doc
// comments for how each was verified instead (self-consistent round-trip
// tests against a hand-written test-only encoder, not real SWF audio).

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "audio/Mp3Decoder.h"  // for DecodedAudio

namespace flash3ds::audio {

// Decodes SWF SoundFormat 0 (uncompressed, platform-native-endian) or 3
// (uncompressed, little-endian) sample data. Per the public SWF File
// Format Specification, DefineSound's SoundData for these formats is RAW
// sample data with NO framing/header of its own — exactly `sampleCount`
// samples per channel, each either 8-bit UNSIGNED (centered at 128 —
// value 128 is silence, per spec) or 16-bit SIGNED, interleaved L/R if
// stereo (L0 R0 L1 R1 ...). Since this project's own build targets
// (x86_64 desktop, ARM11 3DS) are BOTH little-endian, format 0
// ("native-endian") and format 3 ("little-endian") are byte-identical to
// decode here — this one function handles both uniformly, matching this
// project's other "no exotic-endian target" assumptions (see e.g.
// SwfReader's own little-endian-only integer reads).
//
// `is16Bit`/`stereo`/`sampleCount` come directly from the owning
// DefineSound's own header fields (swf::SoundDef) — unlike MP3, this
// format has no self-describing frame headers to read format from.
//
// Returns std::nullopt if `data` is null, `sampleCount` is 0, or `length`
// is too short to contain `sampleCount` frames at the given bit depth/
// channel count (a malformed/truncated SWF) — this format has no partial-
// decode concept the way MP3's "decode what's there, stop at a bad frame"
// does, since there's no per-frame structure to stop cleanly between.
std::optional<DecodedAudio> decodeSwfUncompressedSound(const uint8_t* data, size_t length,
                                                         double sampleRateHz, bool is16Bit,
                                                         bool stereo, uint32_t sampleCount);

// Decodes SWF SoundFormat 1 (ADPCM) sample data — SWF's own
// variable-bit-width (2/3/4/5-bit) variant of IMA ADPCM, per the public
// SWF File Format Specification's ADPCMSOUNDDATA/ADPCMPACKET records.
// Implemented clean-room from the bit-layout/tables documented across
// multiple independent public sources (fad.sourceforge.net's SWF
// reference, wiki.multimedia.cx's "Flash IMA ADPCM" page, and the general
// IMA ADPCM bit-by-bit diff-accumulation algorithm these both describe —
// cross-checked, not copied, against a real shipping decoder's documented
// behavior at the one point the primary prose sources were genuinely
// ambiguous: whether stereo delta codes interleave per-sample or run
// channel-sequential after the per-channel header — interleaved is what
// this implementation does, per that cross-check).
//
// Algorithm summary (see PcmSoundDecoder.cpp for the literal step/index
// tables and the exact per-sample formula, both with their own
// citations): a leading 2-bit ADPCMCodeSize field (mapped to 2/3/4/5 bits
// via `bits = ADPCMCodeSize + 2`) is read ONCE for the whole stream, then
// the stream is a sequence of 4096-sample blocks. Each block starts with,
// per channel, a 16-bit signed initial predictor + 6-bit unsigned initial
// step-table index (both are real output — the block's very first sample
// per channel IS that initial predictor value, not just seed state); every
// subsequent sample is a `bits`-wide delta code, decoded via the standard
// IMA-style shift-and-add diff computation against a running
// predictor/step-index pair, updated per sample.
//
// `stereo`/`sampleCount` come from the owning DefineSound's own header
// fields, same as decodeSwfUncompressedSound(). Unlike that function,
// `is16Bit` is NOT a parameter — ADPCM always decodes to 16-bit PCM
// regardless of DefineSound's own SoundSize field, which the spec does
// not apply to compressed formats (SoundSize describes RAW sample width,
// meaningless once a codec is involved — MP3 doesn't take an is16Bit
// parameter here either, for the same reason).
//
// Returns std::nullopt if `data` is null, `length < 1` (too short to even
// contain the leading ADPCMCodeSize byte), or `sampleCount` is 0.
// Otherwise decodes as many full samples as the stream actually contains
// before running out of bits (SwfReader's own "read past end returns 0,
// sets failed()" contract — see that class) — matching MP3's own "return
// what decoded successfully, not nullopt, on a truncated/malformed
// stream" degradation, since a real player would do the same rather than
// discard everything over a corrupt tail.
std::optional<DecodedAudio> decodeSwfAdpcmSound(const uint8_t* data, size_t length,
                                                 double sampleRateHz, bool stereo,
                                                 uint32_t sampleCount);

}  // namespace flash3ds::audio
