// DefineTextTag.h
//
// Parses DefineText (tag 11) and DefineText2 (tag 33) bodies: CharacterId,
// TextBounds (RECT), TextMatrix (MATRIX), then a byte-aligned sequence of
// TEXTRECORDs (each optionally changing font/color/x-offset/y-offset,
// followed by a run of GLYPHENTRYs — glyph index + advance, both bit-packed
// at widths given by the tag's own GlyphBits/AdvanceBits fields) terminated
// by a single zero byte.
//
// TEXTRECORD's bit layout (the flags byte starting each record) is per the
// commonly-referenced public SWF spec structure — believed correct, but
// (like swf::ClipEventFlag's bit table and ActionPush's DOUBLE encoding —
// see docs/avm1-support.md's confidence notes) not independently verified
// against a real Flash-authored file. Getting this wrong would show up
// immediately and completely (a garbled/empty text run), not subtly.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "swf/ShapeRecords.h"
#include "swf/SwfRecords.h"
#include "swf/SwfReader.h"

namespace flash3ds::swf {

struct GlyphEntry {
    uint32_t glyphIndex = 0;
    int32_t advance = 0;  // in the text's em-square units (1024/em) — see DefineFontTag.h
};

// One TEXTRECORD: font/color/position fields are std::nullopt when the
// record didn't set the corresponding HasFont/HasColor/HasXOffset/
// HasYOffset flag (meaning "unchanged from the previous record" per the
// SWF spec's incremental-state text model — a renderer must carry the
// last-set values forward across records that don't set them).
struct TextRecord {
    std::optional<uint16_t> fontId;
    std::optional<RgbaColor> color;
    std::optional<int32_t> xOffsetTwips;
    std::optional<int32_t> yOffsetTwips;
    std::optional<uint16_t> textHeightTwips;  // only ever set alongside fontId (HasFont)
    std::vector<GlyphEntry> glyphs;
};

struct TextDef {
    uint16_t characterId = 0;
    Rect bounds;  // twips
    Matrix matrix;
    std::vector<TextRecord> records;
};

// `tagCode` must be TagCode::DefineText (11) or TagCode::DefineText2 (33) —
// the only difference between them is RGB vs RGBA TextRecord colors.
std::optional<TextDef> parseDefineText(SwfReader& reader, uint16_t tagCode);

}  // namespace flash3ds::swf
