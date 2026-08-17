// SwfTestFixtures.h
//
// Programmatically builds minimal, spec-conformant SWF byte buffers for
// tests, instead of shipping opaque binary .swf fixture files. Every byte
// written here is produced from the public SWF spec fields (rect bit
// packing, tag header encoding, etc.) — nothing is copied from Shift-DX or
// any other existing SWF.

#pragma once

#include <cstdint>
#include <vector>

namespace flash3ds::test::fixtures {

// A single synthetic tag to place in a generated movie body.
struct FixtureTag {
    uint16_t code;
    std::vector<uint8_t> body;
};

// Builds the *uncompressed* body of a SWF (everything after the 8-byte
// FWS/CWS/ZWS header): FrameSize RECT, FrameRate, FrameCount, then each tag
// in `tags` (an End tag is appended automatically if the last entry isn't
// already code 0).
std::vector<uint8_t> buildMovieBody(int32_t stageWidthTwips, int32_t stageHeightTwips,
                                     double frameRateFps, uint16_t frameCount,
                                     const std::vector<FixtureTag>& tags);

// Wraps `body` in an uncompressed "FWS" SWF file.
std::vector<uint8_t> wrapFws(uint8_t version, const std::vector<uint8_t>& body);

// Wraps `body` in a zlib-compressed "CWS" SWF file.
std::vector<uint8_t> wrapCws(uint8_t version, const std::vector<uint8_t>& body);

// A ready-made minimal movie: 550x400px stage, 24fps, 1 frame,
// ShowFrame + End. No ActionScript.
std::vector<uint8_t> minimalFwsMovie();
std::vector<uint8_t> minimalCwsMovie();

// A movie containing a DoAction tag (single ActionEnd byte, 0x00) so
// hasActionScript should be detected.
std::vector<uint8_t> movieWithActionScript();

}  // namespace flash3ds::test::fixtures
