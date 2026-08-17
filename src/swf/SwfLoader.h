// SwfLoader.h
//
// Phase 1 SWF loader: signature validation, CWS (zlib) decompression, SWF
// header parsing (version / file length / stage rect / frame rate / frame
// count), and a generic tag scan (via TagDispatcher) that records every tag
// without yet interpreting tag bodies (that begins in later phases).
//
// Clean-room implementation against Adobe's public SWF File Format
// Specification. Behavior is cross-checked against our Shift-DX Ghidra
// findings (see docs/shift-dx-behavior.md) but no Shift-DX/gameswf code is
// copied.
//
// Never assumes the input is well-formed: malformed SWFs produce a Movie
// with valid == false and a human-readable errorMessage (or, for tag-level
// truncation deep in a stream, a logged warning) instead of crashing or
// invoking undefined behavior.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "runtime/Movie.h"

namespace flash3ds::swf {

class SwfLoader {
public:
    // Parses a SWF from an in-memory buffer. Always returns a non-null
    // Movie; check movie->valid before using header/tag fields.
    static std::unique_ptr<runtime::Movie> loadSwf(const uint8_t* data, size_t size);

    // Reads `filename` fully into memory and forwards to loadSwf(). If the
    // file cannot be opened/read, returns a Movie with valid == false.
    static std::unique_ptr<runtime::Movie> loadSwfFile(const std::string& filename);
};

}  // namespace flash3ds::swf
