#include "swf/DefineMorphShapeTag.h"

#include "swf/TagCode.h"

namespace flash3ds::swf {

namespace {

// MORPHFILLSTYLE/MORPHLINESTYLE colors are always RGBA (32-bit), regardless
// of which DefineShape version's colors an embedded StyleChangeRecord's
// "new styles" sub-array might otherwise use -- see the SWF spec's
// MORPHFILLSTYLE/MORPHLINESTYLE definitions, both of which read RGBA
// unconditionally rather than taking a shape-version-dependent COLOR/
// RGBA choice the way regular FILLSTYLE/LINESTYLE do.
RgbaColor readRgba(SwfReader& reader) {
    RgbaColor c;
    c.r = reader.readU8();
    c.g = reader.readU8();
    c.b = reader.readU8();
    c.a = reader.readU8();
    return c;
}

// Mirrors ShapeRecords.cpp's anonymous-namespace readGradient(), but reads
// MORPHGRADRECORD entries (start+end ratio/color pairs) instead of plain
// GRADRECORDs. Per spec, MORPHGRADIENT has no FocalPoint field even for a
// focal-radial fill style (unlike regular FOCALGRADIENT) -- so there is no
// `isFocal` parameter here.
MorphGradient readMorphGradient(SwfReader& reader) {
    MorphGradient g;
    uint8_t flags = reader.readU8();
    uint8_t spread = (flags >> 6) & 0x3;
    uint8_t interp = (flags >> 4) & 0x3;
    uint8_t numGradients = flags & 0xF;

    g.spreadMode = spread == 0 ? GradientSpreadMode::kPad
                                 : (spread == 1 ? GradientSpreadMode::kReflect
                                                 : GradientSpreadMode::kRepeat);
    g.interpolationMode =
        interp == 1 ? GradientInterpolationMode::kLinear : GradientInterpolationMode::kNormal;

    g.records.reserve(numGradients);
    for (uint8_t i = 0; i < numGradients; ++i) {
        MorphGradientRecord gr;
        gr.startRatio = reader.readU8();
        gr.startColor = readRgba(reader);
        gr.endRatio = reader.readU8();
        gr.endColor = readRgba(reader);
        g.records.push_back(gr);
    }
    return g;
}

std::vector<MorphFillStyle> readMorphFillStyleArray(SwfReader& reader) {
    uint32_t count = reader.readU8();
    if (count == 0xFF) {
        count = reader.readU16();
    }

    std::vector<MorphFillStyle> styles;
    styles.reserve(count);
    for (uint32_t i = 0; i < count && !reader.failed(); ++i) {
        MorphFillStyle fs;
        fs.type = static_cast<FillStyleType>(reader.readU8());

        switch (fs.type) {
            case FillStyleType::kSolid:
                fs.startColor = readRgba(reader);
                fs.endColor = readRgba(reader);
                break;
            case FillStyleType::kLinearGradient:
            case FillStyleType::kRadialGradient:
            case FillStyleType::kFocalRadialGradient:
                fs.startMatrix = readMatrix(reader);
                fs.endMatrix = readMatrix(reader);
                fs.gradient = readMorphGradient(reader);
                break;
            case FillStyleType::kRepeatingBitmap:
            case FillStyleType::kClippedBitmap:
            case FillStyleType::kNonSmoothedRepeatingBitmap:
            case FillStyleType::kNonSmoothedClippedBitmap:
                fs.bitmapCharacterId = reader.readU16();
                fs.startMatrix = readMatrix(reader);
                fs.endMatrix = readMatrix(reader);
                break;
            default:
                // Unknown fill style type -- leave as default-constructed
                // (solid, transparent black start/end) and keep parsing the
                // remaining styles, matching readFillStyleArray's tolerance
                // of unrecognized types in ShapeRecords.cpp.
                break;
        }
        styles.push_back(std::move(fs));
    }
    return styles;
}

std::vector<MorphLineStyle> readMorphLineStyleArray(SwfReader& reader) {
    uint32_t count = reader.readU8();
    if (count == 0xFF) {
        count = reader.readU16();
    }

    std::vector<MorphLineStyle> styles;
    styles.reserve(count);
    for (uint32_t i = 0; i < count && !reader.failed(); ++i) {
        MorphLineStyle ls;
        ls.startWidthTwips = reader.readU16();
        ls.endWidthTwips = reader.readU16();
        ls.startColor = readRgba(reader);
        ls.endColor = readRgba(reader);
        styles.push_back(ls);
    }
    return styles;
}

// Reads a bare morph SHAPE (own leading NumFillBits(4)/NumLineBits(4)
// nibbles + SHAPERECORD stream, no FillStyleArray/LineStyleArray of its
// own -- those live once, up front, in MorphFillStyles/MorphLineStyles).
// shapeVersion=3 is passed to readShapeRecordStream so that if a
// StyleChangeRecord's (spec-legal but not expected in real content --
// same rarity note as regular DefineShape) "new styles" sub-arrays appear
// inside the edge stream, they parse as RGBA -- matching this tag's
// colors-are-always-RGBA convention rather than guessing a version.
std::vector<ShapeRecord> readMorphEdges(SwfReader& reader) {
    uint32_t numFillBits = reader.readUBits(4);
    uint32_t numLineBits = reader.readUBits(4);
    return readShapeRecordStream(reader, numFillBits, numLineBits, /*shapeVersion=*/3);
}

}  // namespace

std::optional<MorphShapeDef> parseDefineMorphShape(SwfReader& reader, uint16_t tagCode) {
    if (static_cast<TagCode>(tagCode) != TagCode::DefineMorphShape) {
        // Includes DefineMorphShape2 (84) -- deliberately not supported,
        // see the file-level comment in DefineMorphShapeTag.h.
        return std::nullopt;
    }

    MorphShapeDef def;
    def.characterId = reader.readU16();
    if (reader.failed()) return std::nullopt;

    def.startBounds = reader.readRect();
    def.endBounds = reader.readRect();
    if (reader.failed()) return std::nullopt;

    reader.readU32();  // Offset -- not needed; StartEdges' SHAPE self-terminates.
    if (reader.failed()) return std::nullopt;

    def.fillStyles = readMorphFillStyleArray(reader);
    def.lineStyles = readMorphLineStyleArray(reader);
    if (reader.failed()) return std::nullopt;

    def.startEdges = readMorphEdges(reader);
    def.endEdges = readMorphEdges(reader);

    return def;
}

}  // namespace flash3ds::swf
