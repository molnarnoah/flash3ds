#include "swf/DefineTextTag.h"

#include "swf/TagCode.h"

namespace flash3ds::swf {

namespace {

RgbaColor readRgb(SwfReader& reader) {
    RgbaColor c;
    c.r = reader.readU8();
    c.g = reader.readU8();
    c.b = reader.readU8();
    c.a = 255;
    return c;
}

RgbaColor readRgba(SwfReader& reader) {
    RgbaColor c;
    c.r = reader.readU8();
    c.g = reader.readU8();
    c.b = reader.readU8();
    c.a = reader.readU8();
    return c;
}

}  // namespace

std::optional<TextDef> parseDefineText(SwfReader& reader, uint16_t tagCode) {
    bool withAlpha;
    switch (static_cast<TagCode>(tagCode)) {
        case TagCode::DefineText: withAlpha = false; break;
        case TagCode::DefineText2: withAlpha = true; break;
        default: return std::nullopt;
    }

    TextDef def;
    def.characterId = reader.readU16();
    if (reader.failed()) return std::nullopt;

    def.bounds = reader.readRect();
    def.matrix = readMatrix(reader);
    if (reader.failed()) return std::nullopt;

    uint8_t glyphBits = reader.readU8();
    uint8_t advanceBits = reader.readU8();
    if (reader.failed()) return std::nullopt;

    while (!reader.failed()) {
        uint8_t flags = reader.readU8();
        if (reader.failed() || flags == 0) break;  // terminator (or truncated stream)

        bool hasFont = (flags & 0x08) != 0;
        bool hasColor = (flags & 0x04) != 0;
        bool hasYOffset = (flags & 0x02) != 0;
        bool hasXOffset = (flags & 0x01) != 0;

        TextRecord rec;
        if (hasFont) rec.fontId = reader.readU16();
        if (hasColor) rec.color = withAlpha ? readRgba(reader) : readRgb(reader);
        if (hasXOffset) rec.xOffsetTwips = reader.readS16();
        if (hasYOffset) rec.yOffsetTwips = reader.readS16();
        if (hasFont) rec.textHeightTwips = reader.readU16();

        uint8_t glyphCount = reader.readU8();
        rec.glyphs.reserve(glyphCount);
        for (uint8_t i = 0; i < glyphCount && !reader.failed(); ++i) {
            GlyphEntry g;
            g.glyphIndex = reader.readUBits(glyphBits);
            g.advance = reader.readSBits(advanceBits);
            rec.glyphs.push_back(g);
        }
        reader.byteAlign();

        def.records.push_back(std::move(rec));
    }

    return def;
}

}  // namespace flash3ds::swf
