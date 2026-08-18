#include "swf/ShapeRecords.h"

#include "platform/Log.h"

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

RgbaColor readColor(SwfReader& reader, int shapeVersion) {
    return shapeVersion >= 3 ? readRgba(reader) : readRgb(reader);
}

Gradient readGradient(SwfReader& reader, int shapeVersion, bool isFocal) {
    Gradient g;
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
        GradientRecord gr;
        gr.ratio = reader.readU8();
        gr.color = readColor(reader, shapeVersion);
        g.records.push_back(gr);
    }

    if (isFocal) {
        reader.readU16();  // FocalPoint (8.8 fixed) — not used by Phase 3's flat-color renderer.
    }
    return g;
}

}  // namespace

std::vector<FillStyle> readFillStyleArray(SwfReader& reader, int shapeVersion) {
    uint32_t count = reader.readU8();
    if (count == 0xFF) {
        count = reader.readU16();
    }

    std::vector<FillStyle> styles;
    styles.reserve(count);
    for (uint32_t i = 0; i < count && !reader.failed(); ++i) {
        FillStyle fs;
        fs.type = static_cast<FillStyleType>(reader.readU8());

        switch (fs.type) {
            case FillStyleType::kSolid:
                fs.solidColor = readColor(reader, shapeVersion);
                break;
            case FillStyleType::kLinearGradient:
            case FillStyleType::kRadialGradient:
            case FillStyleType::kFocalRadialGradient:
                fs.gradientMatrix = readMatrix(reader);
                fs.gradient =
                    readGradient(reader, shapeVersion, fs.type == FillStyleType::kFocalRadialGradient);
                break;
            case FillStyleType::kRepeatingBitmap:
            case FillStyleType::kClippedBitmap:
            case FillStyleType::kNonSmoothedRepeatingBitmap:
            case FillStyleType::kNonSmoothedClippedBitmap:
                fs.bitmapCharacterId = reader.readU16();
                fs.gradientMatrix = readMatrix(reader);
                break;
            default:
                LOG_WARN("SHAPE", "Unknown fill style type 0x%02X", static_cast<int>(fs.type));
                break;
        }
        styles.push_back(std::move(fs));
    }
    return styles;
}

std::vector<LineStyle> readLineStyleArray(SwfReader& reader, int shapeVersion) {
    uint32_t count = reader.readU8();
    if (count == 0xFF) {
        count = reader.readU16();
    }

    std::vector<LineStyle> styles;
    styles.reserve(count);
    for (uint32_t i = 0; i < count && !reader.failed(); ++i) {
        LineStyle ls;
        ls.widthTwips = reader.readU16();
        ls.color = readColor(reader, shapeVersion);
        styles.push_back(ls);
    }
    return styles;
}

std::vector<ShapeRecord> readShapeRecordStream(SwfReader& reader, uint32_t numFillBits,
                                                uint32_t numLineBits, int shapeVersion) {
    std::vector<ShapeRecord> records;

    constexpr size_t kMaxRecords = 500000;  // defense-in-depth against pathological input
    while (records.size() < kMaxRecords) {
        if (reader.failed()) break;

        uint32_t typeFlag = reader.readUBits(1);
        if (reader.failed()) break;

        if (typeFlag == 0) {
            uint32_t stateNewStyles = reader.readUBits(1);
            uint32_t stateLineStyle = reader.readUBits(1);
            uint32_t stateFillStyle1 = reader.readUBits(1);
            uint32_t stateFillStyle0 = reader.readUBits(1);
            uint32_t stateMoveTo = reader.readUBits(1);

            if (!stateNewStyles && !stateLineStyle && !stateFillStyle1 && !stateFillStyle0 &&
                !stateMoveTo) {
                reader.byteAlign();  // EndShapeRecord
                break;
            }

            ShapeRecord rec;
            rec.type = ShapeRecordType::kStyleChange;

            if (stateMoveTo) {
                uint32_t moveBits = reader.readUBits(5);
                rec.hasMoveTo = true;
                rec.moveToXTwips = reader.readSBits(static_cast<int>(moveBits));
                rec.moveToYTwips = reader.readSBits(static_cast<int>(moveBits));
            }
            if (stateFillStyle0) rec.fillStyle0 = reader.readUBits(static_cast<int>(numFillBits));
            if (stateFillStyle1) rec.fillStyle1 = reader.readUBits(static_cast<int>(numFillBits));
            if (stateLineStyle) rec.lineStyleIndex = reader.readUBits(static_cast<int>(numLineBits));
            if (stateNewStyles) {
                rec.hasNewStyles = true;
                rec.newFillStyles = readFillStyleArray(reader, shapeVersion);
                rec.newLineStyles = readLineStyleArray(reader, shapeVersion);
                numFillBits = reader.readUBits(4);
                numLineBits = reader.readUBits(4);
            }
            records.push_back(std::move(rec));
        } else {
            uint32_t straightFlag = reader.readUBits(1);
            uint32_t numBitsField = reader.readUBits(4);
            int numBits = static_cast<int>(numBitsField) + 2;

            ShapeRecord rec;
            if (straightFlag) {
                rec.type = ShapeRecordType::kStraightEdge;
                uint32_t generalLineFlag = reader.readUBits(1);
                if (generalLineFlag) {
                    rec.deltaXTwips = reader.readSBits(numBits);
                    rec.deltaYTwips = reader.readSBits(numBits);
                } else {
                    uint32_t vertLineFlag = reader.readUBits(1);
                    if (vertLineFlag) {
                        rec.deltaYTwips = reader.readSBits(numBits);
                    } else {
                        rec.deltaXTwips = reader.readSBits(numBits);
                    }
                }
            } else {
                rec.type = ShapeRecordType::kCurvedEdge;
                rec.controlDeltaXTwips = reader.readSBits(numBits);
                rec.controlDeltaYTwips = reader.readSBits(numBits);
                rec.anchorDeltaXTwips = reader.readSBits(numBits);
                rec.anchorDeltaYTwips = reader.readSBits(numBits);
            }
            records.push_back(std::move(rec));
        }
    }

    return records;
}

Shape readShapeWithStyle(SwfReader& reader, int shapeVersion) {
    Shape shape;
    shape.fillStyles = readFillStyleArray(reader, shapeVersion);
    shape.lineStyles = readLineStyleArray(reader, shapeVersion);

    uint32_t numFillBits = reader.readUBits(4);
    uint32_t numLineBits = reader.readUBits(4);

    shape.records = readShapeRecordStream(reader, numFillBits, numLineBits, shapeVersion);
    return shape;
}

}  // namespace flash3ds::swf
