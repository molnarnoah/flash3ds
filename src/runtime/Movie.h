// Movie.h
//
// The result of loading and header/tag-scanning a SWF file.
//
// Movie owns the fully-decompressed tag-stream bytes (`data`) for the
// lifetime of the object: SwfLoader hands FWS bytes over unchanged and CWS
// bytes post-zlib-inflate. Every TagRecord's `bodyOffset`/`bodyLength` are
// offsets into this buffer, so any later pass (Timeline construction in
// Phase 2, shape/sprite parsing in Phase 3, ...) can come back and parse a
// specific tag's body on demand via tagBodyReader() without SwfLoader
// having had to eagerly parse every tag type up front.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "swf/SwfReader.h"
#include "swf/TagDispatcher.h"

namespace flash3ds::runtime {

enum class SwfCompression {
    kNone,        // "FWS"
    kZlib,        // "CWS"
    kLzma,        // "ZWS" — recognized but NOT supported in Phase 1
};

class Movie {
public:
    // --- populated by SwfLoader on success ---
    bool valid = false;
    std::string errorMessage;  // set when valid == false

    uint8_t version = 0;
    SwfCompression compression = SwfCompression::kNone;
    uint32_t declaredFileLength = 0;  // as stated in the SWF header (may lie)

    swf::Rect frameSize;         // stage bounds, in twips
    uint16_t frameRateFixed8 = 0;  // raw 8.8 fixed-point, see frameRateFps()
    uint16_t frameCount = 0;

    std::vector<swf::TagRecord> tags;
    bool hasActionScript = false;

    // Fully-decompressed tag-stream bytes (everything after the 8-byte
    // FWS/CWS/ZWS header: RECT, frame rate, frame count, then all tags).
    // TagRecord::bodyOffset/bodyLength index into this buffer.
    std::vector<uint8_t> data;

    double frameRateFps() const { return frameRateFixed8 / 256.0; }

    // Returns a reader scoped exactly to `tag`'s body bytes (within `data`),
    // ready for a tag-specific parser (e.g. parsePlaceObject). If `tag`'s
    // offsets somehow fall outside `data` (shouldn't happen for tags that
    // came from this same Movie), returns a reader over an empty span —
    // callers get graceful "failed()" behavior, never out-of-bounds access.
    swf::SwfReader tagBodyReader(const swf::TagRecord& tag) const {
        if (tag.bodyOffset > data.size() ||
            tag.bodyOffset + tag.bodyLength > data.size()) {
            return swf::SwfReader(nullptr, 0);
        }
        return swf::SwfReader(data.data() + tag.bodyOffset, tag.bodyLength);
    }

    const char* compressionName() const {
        switch (compression) {
            case SwfCompression::kNone: return "FWS (uncompressed)";
            case SwfCompression::kZlib: return "CWS (zlib)";
            case SwfCompression::kLzma: return "ZWS (LZMA)";
        }
        return "?";
    }

    // Number of tags matching a given SWF tag code (e.g. count DefineSprite
    // tags). Used by the CLI inspector and by tests.
    size_t countTagsWithCode(uint16_t code) const;
};

}  // namespace flash3ds::runtime
