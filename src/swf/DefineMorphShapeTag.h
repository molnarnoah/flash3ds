// DefineMorphShapeTag.h
//
// Roadmap Phase 9 (`docs/implementation-roadmap-2026-08-21-part2.md`).
//
// Parses DefineMorphShape (tag 46) bodies: CharacterId, StartBounds/
// EndBounds (RECT), an Offset field (UI32, read but not needed for a
// sequential parse -- StartEdges' own SHAPE structure self-terminates via
// its EndShapeRecord, same as a regular DefineShape), MorphFillStyles/
// MorphLineStyles (each style carries BOTH start and end values -- colors
// are always RGBA here regardless of SWF version, unlike FILLSTYLE/
// LINESTYLE), then StartEdges/EndEdges -- each a bare SHAPE (own leading
// NumFillBits(4)/NumLineBits(4) nibbles + SHAPERECORD stream, reusing
// ShapeRecords.h's readShapeRecordStream()).
//
// DefineMorphShape2 (tag 84) is NOT supported: real-corpus evidence
// (tools/real_game_harness/, tag histogram across all 8 corpus games) found
// zero DefineMorphShape2 tags anywhere -- only tag 46 appears -- so this
// pass is deliberately scoped to v1 only, mirroring the existing
// DefineShape4/LineStyle2 explicit-non-support precedent (see
// DefineShapeTag.h). DefineMorphShape2 adds EdgeBounds RECTs, a
// scaling-stroke flags byte, and MorphLineStyle2 (LineStyle2-style cap/
// join fields) -- none of which this parser reads.
//
// Rendering note (see SceneRenderer.cpp): the roadmap pre-approves
// rendering morph shapes using only their start-side geometry/styles
// (ratio=0) as an acceptable "simplest correct-enough first
// implementation" -- real-corpus evidence
// (tools/real_game_harness/morph_ratio_scan.cpp) confirms EVERY morph
// placement across all 7 Hobo files uses ratio=0 (explicit or absent), so
// this simplification loses zero real animated morphing for this corpus.
// EndEdges/end-side styles are still parsed and stored here (cheap, and
// useful if a future pass wants true interpolated rendering) even though
// the current renderer only consumes the start side.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "swf/ShapeRecords.h"
#include "swf/SwfRecords.h"
#include "swf/SwfReader.h"

namespace flash3ds::swf {

struct MorphGradientRecord {
    uint8_t startRatio = 0;
    RgbaColor startColor;
    uint8_t endRatio = 0;
    RgbaColor endColor;
};

struct MorphGradient {
    GradientSpreadMode spreadMode = GradientSpreadMode::kPad;
    GradientInterpolationMode interpolationMode = GradientInterpolationMode::kNormal;
    std::vector<MorphGradientRecord> records;
};

// Mirrors FillStyle (ShapeRecords.h), but every color/matrix field is
// doubled into start/end variants per the MORPHFILLSTYLE spec. `type`
// reuses FillStyleType since MORPHFILLSTYLE uses the identical type byte
// values as regular FILLSTYLE.
struct MorphFillStyle {
    FillStyleType type = FillStyleType::kSolid;

    // valid iff type == kSolid
    RgbaColor startColor;
    RgbaColor endColor;

    // valid iff type is a gradient or bitmap fill
    Matrix startMatrix;
    Matrix endMatrix;

    // valid iff type is a gradient fill
    MorphGradient gradient;

    // valid iff type is a bitmap fill (not decoded/rendered -- see docs,
    // same as regular FillStyle::bitmapCharacterId)
    uint16_t bitmapCharacterId = 0;

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

// Mirrors LineStyle (ShapeRecords.h) doubled into start/end. This is
// MORPHLINESTYLE (v1) only -- DefineMorphShape2's MorphLineStyle2 (cap/join
// styles, scaling-stroke flags) is not represented, matching the tag-84
// non-support noted above.
struct MorphLineStyle {
    uint16_t startWidthTwips = 0;
    uint16_t endWidthTwips = 0;
    RgbaColor startColor;
    RgbaColor endColor;
};

struct MorphShapeDef {
    uint16_t characterId = 0;
    Rect startBounds;  // in twips
    Rect endBounds;    // in twips
    std::vector<MorphFillStyle> fillStyles;
    std::vector<MorphLineStyle> lineStyles;
    // Each is a full SHAPERECORD stream (own NumFillBits/NumLineBits
    // consumed internally by readShapeRecordStream at parse time -- the
    // parser does not re-expose those nibbles here since callers only ever
    // need the resulting record stream).
    std::vector<ShapeRecord> startEdges;
    std::vector<ShapeRecord> endEdges;
};

// `tagCode` must be TagCode::DefineMorphShape (46). Returns std::nullopt
// for any other tag code, including DefineMorphShape2 (84) -- see the
// file-level comment above for why tag 84 is out of scope.
std::optional<MorphShapeDef> parseDefineMorphShape(SwfReader& reader, uint16_t tagCode);

}  // namespace flash3ds::swf
