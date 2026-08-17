// SwfTestFixtures.h
//
// Programmatically builds minimal, spec-conformant SWF byte buffers for
// tests, instead of shipping opaque binary .swf fixture files. Every byte
// written here is produced from the public SWF spec fields (rect bit
// packing, tag header encoding, etc.) — nothing is copied from Shift-DX or
// any other existing SWF.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flash3ds::test::fixtures {

// A single synthetic tag to place in a generated movie body.
struct FixtureTag {
    uint16_t code;
    std::vector<uint8_t> body;
};

// Builds the *uncompressed* body of a SWF (everything after the 8-byte
// FWS/CWS/ZWS header): FrameSize RECT, FrameRate, FrameCount, then each tag
// in `tags` (an End tag is appended automatically if the last entry isn't
// already code 0).
std::vector<uint8_t> buildMovieBody(int32_t stageWidthTwips, int32_t stageHeightTwips,
                                     double frameRateFps, uint16_t frameCount,
                                     const std::vector<FixtureTag>& tags);

// Wraps `body` in an uncompressed "FWS" SWF file.
std::vector<uint8_t> wrapFws(uint8_t version, const std::vector<uint8_t>& body);

// Wraps `body` in a zlib-compressed "CWS" SWF file.
std::vector<uint8_t> wrapCws(uint8_t version, const std::vector<uint8_t>& body);

// A ready-made minimal movie: 550x400px stage, 24fps, 1 frame,
// ShowFrame + End. No ActionScript.
std::vector<uint8_t> minimalFwsMovie();
std::vector<uint8_t> minimalCwsMovie();

// A movie containing a DoAction tag (single ActionEnd byte, 0x00) so
// hasActionScript should be detected.
std::vector<uint8_t> movieWithActionScript();

// ---------------------------------------------------------------------
// Phase 2: record/tag body builders (MATRIX, PlaceObject/2,
// RemoveObject/2, FrameLabel), independently encoded from the same public
// SWF spec the production parsers (SwfRecords/PlaceObjectTag) implement —
// used to test those parsers round-trip correctly.
// ---------------------------------------------------------------------

// A translate-only MATRIX (no scale/rotate): HasScale=0, HasRotate=0.
std::vector<uint8_t> buildMatrixBytes(int32_t translateXTwips, int32_t translateYTwips);

// PlaceObject (tag 4) body: CharacterId, Depth, Matrix (no color transform).
std::vector<uint8_t> buildPlaceObjectV1Bytes(uint16_t characterId, uint16_t depth,
                                              const std::vector<uint8_t>& matrixBytes);

// PlaceObject2 (tag 26) body. Pass std::nullopt for characterId/matrixBytes/name
// to omit those optional fields (HasCharacter/HasMatrix/HasName left unset).
std::vector<uint8_t> buildPlaceObject2Bytes(uint16_t depth, bool move,
                                             std::optional<uint16_t> characterId,
                                             std::optional<std::vector<uint8_t>> matrixBytes,
                                             std::optional<std::string> name = std::nullopt);

// RemoveObject2 (tag 28) body: Depth only.
std::vector<uint8_t> buildRemoveObject2Bytes(uint16_t depth);

// FrameLabel (tag 43) body: NUL-terminated name.
std::vector<uint8_t> buildFrameLabelBytes(const std::string& name);

// ---------------------------------------------------------------------
// Phase 3: shape (FILLSTYLEARRAY/LINESTYLEARRAY/SHAPERECORD) and
// DefineSprite body builders, independently bit-packed from the same
// public SWF spec the production readers (ShapeRecords/DefineShapeTag)
// implement.
// ---------------------------------------------------------------------

// A FILLSTYLEARRAY with exactly one solid-color fill style (count=1, type
// 0x00, then RGB or RGBA depending on `shapeVersion` — DefineShape3+ uses
// RGBA).
std::vector<uint8_t> buildSolidFillStyleArrayBytes(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                                     int shapeVersion);

// An empty (count=0) LINESTYLEARRAY.
std::vector<uint8_t> buildEmptyLineStyleArrayBytes();

// A LINESTYLEARRAY with exactly one line style.
std::vector<uint8_t> buildSolidLineStyleArrayBytes(uint16_t widthTwips, uint8_t r, uint8_t g,
                                                     uint8_t b, uint8_t a, int shapeVersion);

// The SHAPERECORD stream (NumFillBits/NumLineBits + records +
// EndShapeRecord) for a simple axis-aligned rectangle: MoveTo(0,0) with
// fillStyle1=1 (and lineStyleIndex=1 if `withLine`), then three straight
// edges (right, down, left) — the fourth (closing) edge back to the origin
// is left implicit, matching how ShapeTessellator closes polygons.
std::vector<uint8_t> buildRectShapeRecordsBytes(int32_t widthTwips, int32_t heightTwips,
                                                 bool withLine = false);

// A full ShapeWithStyle body (fill styles + line styles + shape records)
// for a solid-filled rectangle, ready to follow a DefineShape tag's
// CharacterId+Bounds fields.
std::vector<uint8_t> buildSolidRectShapeWithStyleBytes(int shapeVersion, uint8_t r, uint8_t g,
                                                         uint8_t b, uint8_t a,
                                                         int32_t widthTwips, int32_t heightTwips);

// A full DefineShape/DefineShape2/DefineShape3 tag body: CharacterId,
// Bounds (RECT), then a solid-filled rectangle ShapeWithStyle.
std::vector<uint8_t> buildDefineShapeBytes(int shapeVersion, uint16_t characterId,
                                            int32_t widthTwips, int32_t heightTwips, uint8_t r,
                                            uint8_t g, uint8_t b, uint8_t a);

// A DefineSprite (tag 39) body: CharacterId, FrameCount, then each of
// `nestedTags` written as TagRecords (an End tag is appended automatically
// if the last entry isn't already code 0) — exactly the nested-tag-stream
// shape CharacterDictionary::build() expects to scan.
std::vector<uint8_t> buildDefineSpriteBytes(uint16_t characterId, uint16_t frameCount,
                                             const std::vector<FixtureTag>& nestedTags);

}  // namespace flash3ds::test::fixtures
