// ShapeRecords.h
//
// Clean-room readers for the SWF shape sub-format: FILLSTYLE(ARRAY),
// LINESTYLE(ARRAY), GRADIENT, and SHAPERECORD (the StyleChangeRecord /
// StraightEdgeRecord / CurvedEdgeRecord bit-packed stream used by
// DefineShape/DefineShape2/DefineShape3 — DefineShape4's LineStyle2 and
// FocalRadialGradient nuances are NOT implemented; see docs/swf-support.md).
//
// Implemented from the public SWF File Format Specification.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "swf/SwfRecords.h"
#include "swf/SwfReader.h"

namespace flash3ds::swf {

struct RgbaColor {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct GradientRecord {
    uint8_t ratio = 0;
    RgbaColor color;
};

enum class GradientSpreadMode { kPad, kReflect, kRepeat };
enum class GradientInterpolationMode { kNormal, kLinear };

struct Gradient {
    GradientSpreadMode spreadMode = GradientSpreadMode::kPad;
    GradientInterpolationMode interpolationMode = GradientInterpolationMode::kNormal;
    std::vector<GradientRecord> records;
};

enum class FillStyleType : uint8_t {
    kSolid = 0x00,
    kLinearGradient = 0x10,
    kRadialGradient = 0x12,
    kFocalRadialGradient = 0x13,
    kRepeatingBitmap = 0x40,
    kClippedBitmap = 0x41,
    kNonSmoothedRepeatingBitmap = 0x42,
    kNonSmoothedClippedBitmap = 0x43,
};

struct FillStyle {
    FillStyleType type = FillStyleType::kSolid;
    RgbaColor solidColor;         // valid iff type == kSolid
    Matrix gradientMatrix;         // valid iff type is a gradient or bitmap fill
    Gradient gradient;              // valid iff type is a gradient fill
    uint16_t bitmapCharacterId = 0;  // valid iff type is a bitmap fill (not decoded/rendered — see docs)

    bool isSolid() const { return type == FillStyleType::kSolid; }
    bool isGradient() const {
        return type == FillStyleType::kLinearGradient || type == FillStyleType::kRadialGradient ||
               type == FillStyleType::kFocalRadialGradient;
    }
    bool isBitmap() const {
        return type == FillStyleType::kRepeatingBitmap || type == FillStyleType::kClippedBitmap ||
               type == FillStyleType::kNonSmoothedRepeatingBitmap ||
               type == FillStyleType::kNonSmoothedClippedBitmap;
    }
};

struct LineStyle {
    uint16_t widthTwips = 0;
    RgbaColor color;
};

enum class ShapeRecordType { kStyleChange, kStraightEdge, kCurvedEdge, kEnd };

// A single SHAPERECORD. Only the fields relevant to `type` are meaningful;
// see the public SWF spec for the full StyleChangeRecord/EdgeRecord layout.
struct ShapeRecord {
    ShapeRecordType type = ShapeRecordType::kEnd;

    // --- kStyleChange ---
    bool hasMoveTo = false;
    int32_t moveToXTwips = 0;
    int32_t moveToYTwips = 0;
    // Style indices are 1-based in the SWF format (0 == "no style"); we
    // keep that convention here (0 == none) rather than re-basing to make
    // cross-referencing against the spec/hex dumps easier. std::nullopt
    // means "not specified by this record" (i.e. keep whatever the current
    // sub-path's style was).
    std::optional<uint32_t> fillStyle0;
    std::optional<uint32_t> fillStyle1;
    std::optional<uint32_t> lineStyleIndex;
    bool hasNewStyles = false;
    std::vector<FillStyle> newFillStyles;
    std::vector<LineStyle> newLineStyles;

    // --- kStraightEdge ---
    int32_t deltaXTwips = 0;
    int32_t deltaYTwips = 0;

    // --- kCurvedEdge (quadratic bezier, deltas relative to current point) ---
    int32_t controlDeltaXTwips = 0;
    int32_t controlDeltaYTwips = 0;
    int32_t anchorDeltaXTwips = 0;
    int32_t anchorDeltaYTwips = 0;
};

struct Shape {
    std::vector<FillStyle> fillStyles;
    std::vector<LineStyle> lineStyles;
    std::vector<ShapeRecord> records;
};

// Reads FILLSTYLEARRAY (count byte, or 0xFF + extended UI16 count, then that
// many FILLSTYLE records). `shapeVersion` (1/2/3) governs RGB vs RGBA solid
// colors (DefineShape/2 use RGB, DefineShape3 uses RGBA).
std::vector<FillStyle> readFillStyleArray(SwfReader& reader, int shapeVersion);

// Reads LINESTYLEARRAY (LineStyle1 only — DefineShape4's LineStyle2 is not
// supported).
std::vector<LineStyle> readLineStyleArray(SwfReader& reader, int shapeVersion);

// Reads a full ShapeWithStyle: FillStyleArray, LineStyleArray, NumFillBits/
// NumLineBits, then SHAPERECORDs up to (and including, as an
// end-of-vector marker rather than a stored record) the EndShapeRecord.
Shape readShapeWithStyle(SwfReader& reader, int shapeVersion);

// Reads just the SHAPERECORD stream itself (StyleChangeRecord/
// StraightEdgeRecord/CurvedEdgeRecord, up to and including the
// EndShapeRecord, which is consumed but not represented as a stored
// record) — the part of a ShapeWithStyle that comes AFTER its
// FillStyleArray/LineStyleArray/NumFillBits/NumLineBits header. Factored
// out of readShapeWithStyle (Phase 3) so Phase 8's font-glyph SHAPE
// records — which have NO leading FillStyleArray/LineStyleArray, just a
// bare NumFillBits/NumLineBits + record stream — can reuse the exact same
// record-decoding logic without duplicating it. `shapeVersion` is only
// consulted if a StyleChangeRecord's (rare, and never legitimately present
// in a font glyph's SHAPE, per spec) "new styles" sub-arrays appear, to
// decide RGB vs RGBA fill/line colors.
std::vector<ShapeRecord> readShapeRecordStream(SwfReader& reader, uint32_t numFillBits,
                                                uint32_t numLineBits, int shapeVersion);

}  // namespace flash3ds::swf
