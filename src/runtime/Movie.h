// Movie.h
//
// Phase 1 Movie model: the result of loading and header/tag-scanning a SWF
// file. This is intentionally "flat" for Phase 1 (no Timeline/DisplayList
// yet — that's Phase 2) but is the structure later phases build on.

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

    double frameRateFps() const { return frameRateFixed8 / 256.0; }

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
