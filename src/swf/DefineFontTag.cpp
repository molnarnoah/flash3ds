#include "swf/DefineFontTag.h"

#include "platform/Log.h"
#include "swf/TagCode.h"

namespace flash3ds::swf {

namespace {

// Reads a bare glyph SHAPE (NumFillBits/NumLineBits + SHAPERECORD stream,
// no FillStyleArray/LineStyleArray of its own — see DefineFontTag.h's file
// header). `shapeVersion` only matters if a malformed glyph illegally sets
// a StyleChangeRecord's "new styles" flag; passing 1 (RGB, not RGBA) is a
// reasonable default since real font glyphs never legitimately hit that
// path at all.
Shape readGlyphShape(SwfReader& reader) {
    Shape shape;
    uint32_t numFillBits = reader.readUBits(4);
    uint32_t numLineBits = reader.readUBits(4);
    shape.records = readShapeRecordStream(reader, numFillBits, numLineBits, /*shapeVersion=*/1);
    return shape;
}

}  // namespace

int FontDef::glyphIndexForCode(uint16_t code) const {
    for (size_t i = 0; i < codeTable.size(); ++i) {
        if (codeTable[i] == code) return static_cast<int>(i);
    }
    return -1;
}

std::optional<FontDef> parseDefineFont(SwfReader& reader) {
    FontDef def;
    def.fontId = reader.readU16();
    if (reader.failed()) return std::nullopt;

    size_t offsetTableStart = reader.position();
    uint16_t firstOffset = reader.readU16();
    if (reader.failed()) return std::nullopt;

    if (firstOffset == 0 || firstOffset % 2 != 0) {
        // A well-formed v1 font's first offset is always a positive even
        // byte count (numGlyphs * 2) — 0 legitimately means "no glyphs" in
        // some minimal/degenerate fonts; anything odd is malformed.
        if (firstOffset == 0) return def;  // valid, just empty
        LOG_WARN("FONT", "DefineFont %u: malformed offset table (first offset=%u)", def.fontId,
                  firstOffset);
        return std::nullopt;
    }

    uint16_t numGlyphs = firstOffset / 2;
    std::vector<uint16_t> offsets;
    offsets.reserve(numGlyphs);
    offsets.push_back(firstOffset);
    for (uint16_t i = 1; i < numGlyphs && !reader.failed(); ++i) {
        offsets.push_back(reader.readU16());
    }
    if (reader.failed()) return std::nullopt;

    def.glyphShapes.reserve(numGlyphs);
    for (uint16_t i = 0; i < numGlyphs; ++i) {
        reader.seek(offsetTableStart + offsets[i]);
        def.glyphShapes.push_back(readGlyphShape(reader));
        if (reader.failed()) {
            LOG_WARN("FONT", "DefineFont %u: glyph %u shape truncated/malformed", def.fontId, i);
            return std::nullopt;
        }
    }

    return def;
}

std::optional<FontDef> parseDefineFont2(SwfReader& reader, uint16_t tagCode) {
    if (static_cast<TagCode>(tagCode) != TagCode::DefineFont2) {
        // DefineFont3 (75) uses a 20x finer em-square for its glyph
        // coordinates — see DefineFontTag.h's file header for why mixing
        // that in unscaled would be a silent correctness bug rather than a
        // missing feature. Explicitly rejected, not guessed at.
        return std::nullopt;
    }

    FontDef def;
    def.fontId = reader.readU16();
    if (reader.failed()) return std::nullopt;

    uint8_t flags = reader.readU8();
    bool hasLayout = (flags & 0x80) != 0;
    bool wideOffsets = (flags & 0x08) != 0;
    bool wideCodes = (flags & 0x04) != 0;
    def.italic = (flags & 0x02) != 0;
    def.bold = (flags & 0x01) != 0;

    reader.readU8();  // LanguageCode — not modeled (no locale-specific text handling)

    uint8_t fontNameLen = reader.readU8();
    auto nameBytes = reader.readBytes(fontNameLen);
    def.fontName.assign(nameBytes.begin(), nameBytes.end());

    uint16_t numGlyphs = reader.readU16();
    if (reader.failed()) return std::nullopt;

    size_t offsetTableStart = reader.position();
    std::vector<uint32_t> offsets;
    offsets.reserve(static_cast<size_t>(numGlyphs) + 1);
    for (uint16_t i = 0; i <= numGlyphs && !reader.failed(); ++i) {
        offsets.push_back(wideOffsets ? reader.readU32() : reader.readU16());
    }
    if (reader.failed()) return std::nullopt;

    def.glyphShapes.reserve(numGlyphs);
    for (uint16_t i = 0; i < numGlyphs; ++i) {
        reader.seek(offsetTableStart + offsets[i]);
        def.glyphShapes.push_back(readGlyphShape(reader));
        if (reader.failed()) {
            LOG_WARN("FONT", "DefineFont2 %u: glyph %u shape truncated/malformed", def.fontId, i);
            return std::nullopt;
        }
    }

    // CodeTableOffset (offsets[numGlyphs]) is where the spec says the code
    // table starts — seek explicitly rather than trusting the reader's
    // position fell out exactly right after the last glyph's SHAPE (robust
    // against any padding/reordering a real-world encoder might emit).
    reader.seek(offsetTableStart + offsets[numGlyphs]);
    def.codeTable.reserve(numGlyphs);
    for (uint16_t i = 0; i < numGlyphs && !reader.failed(); ++i) {
        def.codeTable.push_back(wideCodes ? reader.readU16() : reader.readU8());
    }
    if (reader.failed()) return std::nullopt;

    def.hasLayout = hasLayout;
    if (hasLayout) {
        def.ascent = reader.readS16();
        def.descent = reader.readS16();
        def.leading = reader.readS16();

        def.glyphAdvances.reserve(numGlyphs);
        for (uint16_t i = 0; i < numGlyphs && !reader.failed(); ++i) {
            def.glyphAdvances.push_back(reader.readS16());
        }
        def.glyphBounds.reserve(numGlyphs);
        for (uint16_t i = 0; i < numGlyphs && !reader.failed(); ++i) {
            def.glyphBounds.push_back(reader.readRect());
        }
        if (reader.failed()) return std::nullopt;

        // KerningTable: parsed-and-discarded (kerning isn't applied by this
        // runtime's glyph-run renderer — see docs/avm1-support.md). Reading
        // it (rather than blindly skipping a byte count) keeps the reader
        // correctly positioned regardless of KerningCount, since each
        // record's width itself depends on `wideCodes`.
        uint16_t kerningCount = reader.readU16();
        for (uint16_t i = 0; i < kerningCount && !reader.failed(); ++i) {
            if (wideCodes) {
                reader.readU16();  // FontKerningCode1
                reader.readU16();  // FontKerningCode2
            } else {
                reader.readU8();  // FontKerningCode1
                reader.readU8();  // FontKerningCode2
            }
            reader.readS16();  // FontKerningAdjustment
        }
    }

    return def;
}

}  // namespace flash3ds::swf
