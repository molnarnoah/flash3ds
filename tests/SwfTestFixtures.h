// SwfTestFixtures.h
//
// Programmatically builds minimal, spec-conformant SWF byte buffers for
// tests, instead of shipping opaque binary .swf fixture files. Every byte
// written here is produced from the public SWF spec fields (rect bit
// packing, tag header encoding, etc.) — nothing is copied from Shift-DX or
// any other existing SWF.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
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

// ExportAssets (tag 56) body: Count, then Count x {characterId, NUL-
// terminated linkage name}.
std::vector<uint8_t> buildExportAssetsBytes(
    const std::vector<std::pair<uint16_t, std::string>>& entries);

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

// A ShapeWithStyle body whose SHAPERECORD stream contains exactly one
// mid-stream StyleChangeRecord with StateNewStyles set (all other state
// bits 0 — no MoveTo/FillStyle/LineStyle fields), followed by a byte-
// aligned new FillStyleArray (one solid fill, `r2/g2/b2`)/LineStyleArray
// (empty)/NumFillBits(1)/NumLineBits(0), then EndShapeRecord. The initial
// (pre-record-stream) FillStyleArray holds one solid fill (`r1/g1/b1`).
// Regression fixture for the Phase 9 byte-alignment bug: real hobo.swf
// shapes with more than one style region hit exactly this path.
std::vector<uint8_t> buildShapeWithMidStreamNewStylesBytes(int shapeVersion, uint8_t r1,
                                                             uint8_t g1, uint8_t b1, uint8_t r2,
                                                             uint8_t g2, uint8_t b2);

// A full DefineShape/DefineShape2/DefineShape3 tag body: CharacterId,
// Bounds (RECT), then a solid-filled rectangle ShapeWithStyle.
std::vector<uint8_t> buildDefineShapeBytes(int shapeVersion, uint16_t characterId,
                                            int32_t widthTwips, int32_t heightTwips, uint8_t r,
                                            uint8_t g, uint8_t b, uint8_t a);

// Graphics/gradients task (2026-08-28): one GRADIENTRECORD (ratio + color)
// for buildLinearGradientFillStyleArrayBytes below.
struct GradientStopFixture {
    uint8_t ratio = 0;
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

// A FILLSTYLEARRAY with exactly one linear-gradient fill style (count=1,
// type 0x10, `gradientMatrixBytes` — a byte-aligned MATRIX, e.g. from
// buildMatrixBytes — verbatim, then a GRADIENT record: SpreadMode=kPad/
// InterpolationMode=kNormal always, `stops.size()` GradientRecords in the
// order given). Mirrors ShapeRecords.cpp's readFillStyleArray/readGradient
// read order exactly.
std::vector<uint8_t> buildLinearGradientFillStyleArrayBytes(
    const std::vector<uint8_t>& gradientMatrixBytes, const std::vector<GradientStopFixture>& stops,
    int shapeVersion);

// A full DefineShape/DefineShape2/DefineShape3 tag body for a rectangle
// filled with a linear gradient instead of a solid color: CharacterId,
// Bounds (RECT), then a ShapeWithStyle built from
// buildLinearGradientFillStyleArrayBytes + an empty LineStyleArray +
// buildRectShapeRecordsBytes (same rectangle-outline records a solid-fill
// DefineShape uses — the fill style TYPE is what differs, not the shape's
// geometry).
std::vector<uint8_t> buildDefineShapeWithLinearGradientBytes(
    int shapeVersion, uint16_t characterId, int32_t widthTwips, int32_t heightTwips,
    const std::vector<uint8_t>& gradientMatrixBytes, const std::vector<GradientStopFixture>& stops);

// A DefineSprite (tag 39) body: CharacterId, FrameCount, then each of
// `nestedTags` written as TagRecords (an End tag is appended automatically
// if the last entry isn't already code 0) — exactly the nested-tag-stream
// shape CharacterDictionary::build() expects to scan.
std::vector<uint8_t> buildDefineSpriteBytes(uint16_t characterId, uint16_t frameCount,
                                             const std::vector<FixtureTag>& nestedTags);

// ---------------------------------------------------------------------
// Phase 6: sound tag / ClipActionRecord builders, independently encoded
// from the same public SWF spec the production parsers (DefineSoundTag/
// StartSoundTag/PlaceObjectTag) implement.
// ---------------------------------------------------------------------

// DefineSound (14) tag body: SoundId, the bit-packed format/rate/size/type
// byte, SampleCount, then `sampleDataBytes` verbatim (opaque — never
// decoded by production code either, see swf/DefineSoundTag.h).
std::vector<uint8_t> buildDefineSoundBytes(uint16_t soundId, uint8_t format, uint8_t rate,
                                            bool is16Bit, bool stereo, uint32_t sampleCount,
                                            const std::vector<uint8_t>& sampleDataBytes = {});

// A standalone SOUNDINFO record.
std::vector<uint8_t> buildSoundInfoBytes(bool syncStop, bool syncNoMultiple,
                                          std::optional<uint32_t> inPointSamples,
                                          std::optional<uint32_t> outPointSamples,
                                          std::optional<uint16_t> loopCount);

// StartSound (15) tag body: SoundId then a SOUNDINFO record.
std::vector<uint8_t> buildStartSoundBytes(uint16_t soundId,
                                           const std::vector<uint8_t>& soundInfoBytes);

// Roadmap Phase 3 (2026-08-21, MP3 audio decode): a real, short (~50ms,
// mono, 44100 Hz, 495 bytes) MPEG-1 Layer III audio stream, encoded via
// ffmpeg/libmp3lame from a synthesized 440Hz sine tone — see this
// function's definition in SwfTestFixtures.cpp for the exact command used
// to generate it. This is genuinely different
// from the rest of this file's philosophy (everything else here is
// hand-built directly from spec bit-layouts, not generated by an external
// encoder) because MP3 is a real perceptual codec — there's no reasonable
// way to hand-craft a valid MPEG frame's Huffman-coded audio data the way
// SWF's comparatively simple tag/record structures can be hand-built.
// ffmpeg is a legitimate, independent, general-purpose encoder — nothing
// here is copied from Shift-DX/gameswf or any other SWF/Flash-specific
// source, consistent with this project's clean-room requirement (see
// top-level CLAUDE.md).
//
// Returned bytes are the raw MP3 frame stream ONLY (no SeekSamples
// prefix, no ID3/container framing) — a caller building a DefineSound tag
// body for SoundFormat::kMp3 must prepend the 2-byte SeekSamples field
// itself (see swf::SoundFormat::kMp3's MP3SOUNDDATA framing, documented
// in audio/Mp3Decoder.h) before passing this as buildDefineSoundBytes()'s
// `sampleDataBytes`.
const std::vector<uint8_t>& sampleMp3AudioBytes();

// One CLIPACTIONRECORD for buildPlaceObject2WithClipActionsBytes below.
struct ClipActionFixture {
    uint32_t eventFlags = 0;
    std::optional<uint8_t> keyCode;
    std::vector<uint8_t> actionBytes;
};

// PlaceObject2 (tag 26) body with HasClipActions set, using the SWF6+
// (32-bit CLIPEVENTFLAGS) CLIPACTIONS encoding — matches parsePlaceObject's
// default `swfVersion` of 6. Pass std::nullopt for characterId/matrixBytes/
// name to omit those optional fields, same convention as
// buildPlaceObject2Bytes.
std::vector<uint8_t> buildPlaceObject2WithClipActionsBytes(
    uint16_t depth, std::optional<uint16_t> characterId,
    std::optional<std::vector<uint8_t>> matrixBytes, std::optional<std::string> name,
    const std::vector<ClipActionFixture>& clipActions);

// ---------------------------------------------------------------------
// Phase 8: font/text/button/edit-text tag body builders, independently
// encoded from the same public SWF spec the production parsers
// (DefineFontTag/DefineTextTag/DefineButtonTag/DefineEditTextTag)
// implement.
// ---------------------------------------------------------------------

// A single glyph's bare SHAPE (NumFillBits=1/NumLineBits=0 + records, no
// FillStyleArray/LineStyleArray) tracing an axis-aligned rectangle
// `widthUnits` x `heightUnits` in the font's 1024-units-per-em space, with
// fillStyle1=1 (matching the real-world "fill style 1 == inside the
// glyph" convention). Reuses the exact same bit-packing as
// buildRectShapeRecordsBytes (Phase 3) — that function already produces
// exactly a bare SHAPE (NumFillBits/NumLineBits + records + EndShape), no
// leading style arrays, so it doubles as a glyph-shape builder unchanged.
std::vector<uint8_t> buildGlyphShapeBytes(int32_t widthUnits, int32_t heightUnits);

// DefineFont (tag 10, v1) body: FontID, an offset table, then each of
// `glyphShapeBytes` concatenated (each entry normally built by
// buildGlyphShapeBytes).
std::vector<uint8_t> buildDefineFontV1Bytes(uint16_t fontId,
                                             const std::vector<std::vector<uint8_t>>& glyphShapeBytes);

// One glyph's layout metadata for buildDefineFont2Bytes's optional layout
// table (HasLayout).
struct GlyphLayoutFixture {
    int16_t advance = 0;
    int32_t boundsXMin = 0, boundsXMax = 0, boundsYMin = 0, boundsYMax = 0;
};

// DefineFont2 (tag 48) body: FontID, flags (HasLayout/WideCodes per the
// arguments below; WideOffsets/ShiftJIS/SmallText/ANSI always unset),
// LanguageCode=0, FontName, NumGlyphs, offset table, each glyph's SHAPE,
// CodeTable, and (iff `layout` is non-empty) FontAscent/Descent/Leading +
// FontAdvanceTable + FontBoundsTable + an empty KerningTable.
// `glyphShapeBytes`, `codes`, and `layout` (if non-empty) must all be the
// same length (one entry per glyph).
std::vector<uint8_t> buildDefineFont2Bytes(uint16_t fontId, const std::string& fontName,
                                            const std::vector<std::vector<uint8_t>>& glyphShapeBytes,
                                            const std::vector<uint16_t>& codes, bool wideCodes,
                                            int16_t ascent, int16_t descent, int16_t leading,
                                            const std::vector<GlyphLayoutFixture>& layout);

// One TEXTRECORD for buildDefineTextBytes.
struct TextRecordFixture {
    std::optional<uint16_t> fontId;
    std::optional<uint16_t> textHeightTwips;  // only meaningful alongside fontId
    std::optional<std::array<uint8_t, 4>> colorRgba;  // r,g,b,a — a ignored if !withAlpha
    std::optional<int16_t> xOffsetTwips;
    std::optional<int16_t> yOffsetTwips;
    std::vector<std::pair<uint32_t, int32_t>> glyphs;  // {glyphIndex, advance}
};

// DefineText (tag 11, withAlpha=false) / DefineText2 (tag 33,
// withAlpha=true) body: CharacterId, TextBounds, TextMatrix, GlyphBits,
// AdvanceBits, then each of `records` (terminated by the required trailing
// zero byte).
std::vector<uint8_t> buildDefineTextBytes(uint16_t characterId,
                                           const std::vector<uint8_t>& matrixBytes,
                                           uint8_t glyphBits, uint8_t advanceBits,
                                           const std::vector<TextRecordFixture>& records,
                                           bool withAlpha, int32_t boundsWidthTwips = 2000,
                                           int32_t boundsHeightTwips = 2000);

// One BUTTONRECORD (v1) for buildDefineButtonV1Bytes.
struct ButtonRecordV1Fixture {
    bool up = false, over = false, down = false, hitTest = false;
    uint16_t characterId = 0;
    uint16_t depth = 0;
    std::vector<uint8_t> matrixBytes;
};

// DefineButton (tag 7) body: ButtonId, each of `records`, then
// `actionBytes` verbatim (no length prefix — runs to the end of the tag).
std::vector<uint8_t> buildDefineButtonV1Bytes(uint16_t characterId,
                                               const std::vector<ButtonRecordV1Fixture>& records,
                                               const std::vector<uint8_t>& actionBytes);

// DefineButton2 (tag 34) body: ButtonId, flags (TrackAsMenu=false),
// ButtonActionOffset (0 if `actionBytes` is empty), each of `records`
// (reusing ButtonRecordV1Fixture — v2's extra ColorTransform is always the
// identity CXFORMWITHALPHA, and HasFilterList/HasBlendMode are always
// unset), then (iff `actionBytes` non-empty) a single terminal
// BUTTONCONDACTION (CondActionSize=0, meaning "runs to end of tag") with
// `conditions`/`keyCode` as given.
std::vector<uint8_t> buildDefineButtonV2Bytes(uint16_t characterId,
                                               const std::vector<ButtonRecordV1Fixture>& records,
                                               uint16_t conditions, std::optional<uint8_t> keyCode,
                                               const std::vector<uint8_t>& actionBytes);

// DefineEditText (tag 37) body. Pass std::nullopt for fontId/fontHeight/
// textColorRgba/initialText to leave the corresponding HasFont/
// HasTextColor/HasText flags (and dependent fields) unset.
std::vector<uint8_t> buildDefineEditTextBytes(
    uint16_t characterId, int32_t boundsWidthTwips, int32_t boundsHeightTwips,
    std::optional<uint16_t> fontId, std::optional<uint16_t> fontHeightTwips,
    std::optional<std::array<uint8_t, 4>> textColorRgba, const std::string& variableName,
    std::optional<std::string> initialText);

// ---------------------------------------------------------------------
// Roadmap Phase 9: DefineMorphShape (tag 46) body builder, independently
// encoded from the public SWF spec's MORPHFILLSTYLE(ARRAY)/
// MORPHLINESTYLE(ARRAY)/MORPHGRADIENT layout (see swf/DefineMorphShapeTag.h).
// ---------------------------------------------------------------------

// A full DefineMorphShape (tag 46) body: CharacterId, StartBounds=EndBounds
// (both `startWidthTwips` x `startHeightTwips`), a (unused-by-the-parser,
// written as 0) Offset field, one solid MorphFillStyle whose start color is
// (r1,g1,b1,a1) and end color is (r2,g2,b2,a2), an empty MorphLineStyle
// array, StartEdges tracing a `startWidthTwips` x `startHeightTwips`
// rectangle with fillStyle1=1, and EndEdges tracing a `endWidthTwips` x
// `endHeightTwips` rectangle the same way (reusing
// buildRectShapeRecordsBytes, exactly as a real StartEdges/EndEdges pair
// would independently trace two related but not-necessarily-identical
// outlines).
std::vector<uint8_t> buildDefineMorphShapeBytes(uint16_t characterId, int32_t startWidthTwips,
                                                 int32_t startHeightTwips, int32_t endWidthTwips,
                                                 int32_t endHeightTwips, uint8_t r1, uint8_t g1,
                                                 uint8_t b1, uint8_t a1, uint8_t r2, uint8_t g2,
                                                 uint8_t b2, uint8_t a2);

}  // namespace flash3ds::test::fixtures
