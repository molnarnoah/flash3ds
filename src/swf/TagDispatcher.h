// TagDispatcher.h
//
// Phase 1 generic SWF tag reader.
//
// Reads the tag-header encoding from the public SWF spec: a 16-bit
// little-endian word where the top 10 bits are the tag type and the low 6
// bits are the body length, OR 0x3F (63) as an escape meaning "the real
// 32-bit length follows as a separate word".
//
// This matches the general SWF format (every SWF parser implements this
// same header, e.g. gameswf's stream::open_tag) — it is not proprietary to
// Shift-DX. See docs/shift-dx-behavior.md for the behavioral cross-check
// against our Ghidra findings (gameswf_stream_open_tag /
// gameswf_stream_close_tag).
//
// Phase 1 does not parse individual tag *bodies* (that starts in Phase 3+
// for shapes/sprites, Phase 4 for DoAction, etc). It only walks the tag
// stream, names each tag, and lets the caller skip over the body.

#pragma once

#include <cstdint>
#include <string>

#include "swf/SwfReader.h"

namespace flash3ds::swf {

struct TagRecord {
    uint16_t code = 0;
    std::string name;
    size_t bodyOffset = 0;   // absolute offset into the SWF byte buffer
    uint32_t bodyLength = 0;
    bool isLongHeader = false;
};

class TagDispatcher {
public:
    // Reads one tag header at `reader`'s current position and advances the
    // reader past the header (the body is NOT consumed — caller decides
    // whether to parse or skip it). Returns false if there is not enough
    // data left to read a full header (reader.failed() will also be set in
    // that case), or if the reader was already at/after the end.
    static bool readTagHeader(SwfReader& reader, TagRecord& outTag);

    // True for tags that indicate the presence of ActionScript bytecode
    // (AVM1 DoAction/DoInitAction, or AVM2/AS3 DoABC/DoABC2).
    static bool isActionScriptTag(uint16_t code);
};

}  // namespace flash3ds::swf
