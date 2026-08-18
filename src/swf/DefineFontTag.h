// DefineFontTag.h
//
// Parses DefineFont (tag 10, "v1") and DefineFont2 (tag 48, "v2") bodies
// into a normalized FontDef: a list of per-glyph outlines (SHAPE records —
// NOT ShapeWithStyle; a font glyph has no fill/line style arrays of its
// own, see swf/ShapeRecords.h's readShapeRecordStream) plus (v2 only, and
// only when HasLayout is set) per-glyph advance widths/bounds and font
// metrics.
//
// DefineFont3 (tag 75) is NOT supported: it reuses DefineFont2's header
// layout but its glyph coordinates are defined in a 20x finer em-square
// (20480 units/em, for sub-pixel text hinting) rather than DefineFont2's
// 1024 units/em — mixing the two without accounting for the scale would
// silently mis-render text, so DefineFont3 is explicitly rejected (returns
// std::nullopt) rather than guessed at. DefineFontInfo/DefineFontInfo2 (tags
// 13/62, which attach a code table to an existing DefineFont v1 — the v1
// format has no code table of its own) are also NOT implemented; a v1 font
// with no accompanying DefineFontInfo has an empty FontDef::codeTable, which
// only matters for DefineEditText's initial-text rendering (DefineText/
// DefineText2 reference glyphs by index directly and never need codes at
// all — see swf/DefineTextTag.h).
//
// Coordinate convention: exactly like DefineShape's ShapeDef, glyph shape
// coordinates are left in the SWF's raw units (1024 units per em square,
// per the public spec) — converting to actual twips for rendering at a
// specific text height is the renderer's job (see docs/avm1-support.md's
// "Text/Font" section for the scale-factor convention used).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "swf/ShapeRecords.h"
#include "swf/SwfReader.h"

namespace flash3ds::swf {

struct FontDef {
    uint16_t fontId = 0;

    // v2 only (empty/false/0 for a v1 DefineFont).
    std::string fontName;
    bool bold = false;
    bool italic = false;

    // Present (non-empty/populated) only if the font has HasLayout set
    // (always false for v1, optional for v2). Renderer/EditText advance-
    // width computation needs these; a font without them can still render
    // (glyph outlines are always present) but callers can't compute glyph
    // spacing from font data alone — see docs/avm1-support.md.
    bool hasLayout = false;
    int16_t ascent = 0;
    int16_t descent = 0;
    int16_t leading = 0;

    // Parallel arrays, one entry per glyph, indexed by glyph index
    // (matching TEXTRECORD's GlyphIndex / DefineFontInfo's per-glyph code
    // convention). `glyphShapes[i].fillStyles`/`lineStyles` are always
    // empty (glyphs carry no style of their own — see the file header);
    // only `.records` is meaningful.
    std::vector<Shape> glyphShapes;
    std::vector<int16_t> glyphAdvances;  // empty unless hasLayout
    std::vector<Rect> glyphBounds;       // empty unless hasLayout

    // v2 only: character code -> glyph index, in glyph-index order (empty
    // for v1 — see the file header's DefineFontInfo note).
    std::vector<uint16_t> codeTable;

    size_t glyphCount() const { return glyphShapes.size(); }

    // Linear search (fonts are typically tens to a few hundred glyphs —
    // fine for this runtime's scale) for the glyph index whose codeTable
    // entry equals `code`. Returns -1 if not found or codeTable is empty
    // (v1 fonts, or a v2 font this parser still resolved without one,
    // which shouldn't normally happen).
    int glyphIndexForCode(uint16_t code) const;
};

// `tagCode` must be TagCode::DefineFont (10). Returns std::nullopt on
// malformed input.
std::optional<FontDef> parseDefineFont(SwfReader& reader);

// `tagCode` must be TagCode::DefineFont2 (48). DefineFont3 (75) is
// explicitly rejected — see the file header. Returns std::nullopt on
// malformed input or an unsupported tag code.
std::optional<FontDef> parseDefineFont2(SwfReader& reader, uint16_t tagCode);

}  // namespace flash3ds::swf
