// DefineBitsTag.h
//
// Priority Fix List item #2 (docs/known-limitations.md; see
// docs/renderer.md's "Bitmap rendering" section for the full design writeup
// and real-corpus evidence this scope was built from). Parses the
// `DefineBits*` tag family into fully-decoded RGBA8 pixel data — unlike
// every other Define* parser in this directory (which only reads
// structural fields and defers any heavy decode, e.g. DefineSoundTag.h's
// audio codec data), bitmap tags are decoded eagerly here, matching
// CharacterDictionary's existing "parse fully on first find()" contract for
// every other character kind (shapes/fonts/text/...) — see
// CharacterDictionary.h's own header comment for why that's already the
// natural "decode on first reference" gate, with no second-level cache
// needed the way DefineSound's MP3 decode uses one (see docs/audio.md for
// why audio's case is different: a sound may be defined but never played,
// where a bitmap referenced by a placed shape's fill style is essentially
// always about to be sampled).
//
// Scope, by real-corpus evidence (tools/swf_diagnostic tag histograms
// across all 8 Hobo titles' own SWFs plus Extreme Pamplona's loader and
// every one of its reachable content sub-SWFs — see docs/renderer.md):
//
//   - DefineBitsLossless   (tag 20) — supported.
//   - DefineBitsLossless2  (tag 36) — supported.
//   - DefineBitsJpeg2      (tag 21) — supported.
//   - DefineBitsJpeg3      (tag 35) — supported.
//   - DefineBits           (tag  6) — NOT supported. Needs an external
//     JPEGTables(8) tag (the encoding tables are shared across every
//     DefineBits-tag JPEG in the file rather than embedded per-tag, unlike
//     JPEG2/3) that this parser does not thread through. Zero occurrences
//     of either tag 6 or tag 8 anywhere in the corpus.
//   - DefineBitsJpeg4      (tag 90) — NOT supported. Adds an alpha channel
//     (like JPEG3) plus a deblocking-filter strength field on top of
//     JPEG3's own shape. Zero occurrences in the corpus.
//
// Both "NOT supported" cases are recognized by TagCode elsewhere (see
// swf/TagCode.h) but parseDefineBits() below returns std::nullopt for
// them, matching every other explicitly-out-of-scope tag in this project
// (DefineMorphShape2, DefineFont3, ...) — a real title that turns out to
// need either can add it later with real evidence, rather than this being
// implemented speculatively against zero corpus content.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "swf/ShapeRecords.h"  // RgbaColor
#include "swf/SwfReader.h"

namespace flash3ds::swf {

// A fully-decoded bitmap character: straight (non-premultiplied) RGBA8
// pixels, row-major, top-to-bottom, `width * height` entries. Every
// supported source format (see this file's header comment) is normalized
// into this one representation at parse time — DefineBitsLossless2's
// premultiplied-alpha ARGB32 pixels are un-premultiplied here (see
// DefineBitsTag.cpp), and a JPEG2 bitmap (which has no alpha channel of
// its own) gets alpha = 255 for every pixel — so nothing downstream
// (ShapeTessellator/SceneRenderer's bitmap-fill sampling) needs to know
// which tag variant a given BitmapDef actually came from.
struct BitmapDef {
    uint16_t characterId = 0;
    int width = 0;
    int height = 0;
    std::vector<RgbaColor> pixels;  // size == width * height, or empty on failure
};

// `tagCode` selects which DefineBits* variant to parse — must be one of
// TagCode::DefineBitsLossless/DefineBitsLossless2/DefineBitsJpeg2/
// DefineBitsJpeg3 (the four supported per this file's header comment).
// Returns std::nullopt for any other tag code (including the two
// explicitly-out-of-scope ones above) or on a real parse/decode failure
// (truncated tag body, corrupt zlib/JPEG stream, a width/height so large
// the decoded buffer would be unreasonable — see DefineBitsTag.cpp's
// kMaxReasonablePixels).
std::optional<BitmapDef> parseDefineBits(SwfReader& reader, uint16_t tagCode);

}  // namespace flash3ds::swf
