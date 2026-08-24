// memory_breakdown.cpp
//
// Phase 2 of docs/implementation-roadmap.md ("Memory: byte-level
// breakdown, then a fix design"). Turns docs/memory-audit.md's INFERRED
// "shapes are probably the dominant sub-cost within
// CharacterDictionary::build()" into a PROVEN, byte-counted breakdown by
// CharacterDef variant arm.
//
// This is diagnostic-only, read-only code -- it does NOT change any
// runtime behavior or data structure. It walks the same public
// CharacterDictionary::find()/size() API real code already uses (same
// approach as tools/real_game_harness/button_scan.cpp), summing struct
// sizes and heap-container element counts to estimate real allocated
// bytes per character kind. This is an ESTIMATE (sizeof()-based, doesn't
// account for allocator/malloc bookkeeping overhead per allocation, which
// is real but not modeled here -- see the printed caveat), not a
// byte-exact accounting -- but it's the first tool in this project to
// break the ~145MB CharacterDictionary::build() cost down by WHAT kind of
// character is responsible, rather than treating it as one opaque number.
//
// Usage: memory_breakdown <path.swf>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "runtime/CharacterDictionary.h"
#include "runtime/Movie.h"
#include "swf/DefineButtonTag.h"
#include "swf/DefineEditTextTag.h"
#include "swf/DefineFontTag.h"
#include "swf/DefineShapeTag.h"
#include "swf/DefineSoundTag.h"
#include "swf/DefineTextTag.h"
#include "swf/ShapeRecords.h"
#include "swf/SwfLoader.h"

using namespace flash3ds;
using runtime::CharacterDictionary;

namespace {

struct ShapeCounts {
    size_t shapeCount = 0;
    size_t recordCount = 0;
    size_t fillStyleCount = 0;
    size_t lineStyleCount = 0;
    size_t gradientRecordCount = 0;
};

void walkFillStyles(const std::vector<swf::FillStyle>& styles, ShapeCounts& c) {
    c.fillStyleCount += styles.size();
    for (const auto& fs : styles) {
        c.gradientRecordCount += fs.gradient.records.size();
    }
}

// Walks one Shape (top-level shape OR a font glyph outline -- same type)
// and folds its cost into `c`. Recurses into StyleChangeRecords' own
// newFillStyles/newLineStyles (and THEIR gradients), since a mid-shape
// style change re-embeds a full style array per the SWF spec -- see
// swf::ShapeRecord's own doc comment in ShapeRecords.h.
void walkShape(const swf::Shape& shape, ShapeCounts& c) {
    ++c.shapeCount;
    c.recordCount += shape.records.size();
    walkFillStyles(shape.fillStyles, c);
    c.lineStyleCount += shape.lineStyles.size();
    for (const auto& rec : shape.records) {
        if (rec.type == swf::ShapeRecordType::kStyleChange && rec.styleChange &&
            rec.styleChange->hasNewStyles) {
            walkFillStyles(rec.styleChange->newFillStyles, c);
            c.lineStyleCount += rec.styleChange->newLineStyles.size();
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path.swf>\n", argv[0]);
        return 1;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "could not open %s\n", argv[1]);
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();

    auto movie = swf::SwfLoader::loadSwf(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
    if (!movie || !movie->valid) {
        std::fprintf(stderr, "load failed\n");
        return 1;
    }
    CharacterDictionary characters = CharacterDictionary::build(*movie);

    ShapeCounts topLevelShapes;   // DefineShape/2/3 characters
    ShapeCounts glyphShapes;      // font glyph outlines (same Shape type)

    size_t spriteCount = 0, spriteTagRecordCount = 0;
    size_t soundCount = 0;
    size_t fontCount = 0, fontAdvanceCount = 0, fontBoundsCount = 0, fontCodeTableCount = 0;
    size_t textCount = 0, textRecordCount = 0, textGlyphEntryCount = 0;
    size_t buttonCount = 0, buttonRecordCount = 0, buttonCondActionCount = 0, buttonActionBytes = 0;
    size_t editTextCount = 0;
    size_t totalCharacters = 0;

    for (uint32_t id = 1; id < 65536; ++id) {
        const auto* def = characters.find(static_cast<uint16_t>(id));
        if (!def) continue;
        ++totalCharacters;

        if (const auto* s = std::get_if<swf::ShapeDef>(def)) {
            walkShape(s->shape, topLevelShapes);
        } else if (const auto* sp = std::get_if<runtime::SpriteDef>(def)) {
            ++spriteCount;
            spriteTagRecordCount += sp->tags.size();
        } else if (std::get_if<swf::SoundDef>(def)) {
            ++soundCount;
        } else if (const auto* f = std::get_if<swf::FontDef>(def)) {
            ++fontCount;
            for (const auto& glyph : f->glyphShapes) walkShape(glyph, glyphShapes);
            fontAdvanceCount += f->glyphAdvances.size();
            fontBoundsCount += f->glyphBounds.size();
            fontCodeTableCount += f->codeTable.size();
        } else if (const auto* t = std::get_if<swf::TextDef>(def)) {
            ++textCount;
            textRecordCount += t->records.size();
            for (const auto& r : t->records) textGlyphEntryCount += r.glyphs.size();
        } else if (const auto* b = std::get_if<swf::ButtonDef>(def)) {
            ++buttonCount;
            buttonRecordCount += b->records.size();
            buttonCondActionCount += b->condActionsV2.size();
            for (const auto& ca : b->condActionsV2) buttonActionBytes += ca.actionBytes.size();
        } else if (std::get_if<swf::EditTextDef>(def)) {
            ++editTextCount;
        }
    }

    // --- byte estimates -----------------------------------------------
    auto shapeBytes = [](const ShapeCounts& c) {
        return c.shapeCount * sizeof(swf::ShapeDef) + c.recordCount * sizeof(swf::ShapeRecord) +
               c.fillStyleCount * sizeof(swf::FillStyle) + c.lineStyleCount * sizeof(swf::LineStyle) +
               c.gradientRecordCount * sizeof(swf::GradientRecord);
    };
    // Glyphs live inside FontDef::glyphShapes (a std::vector<Shape>, not
    // ShapeDef) -- charge sizeof(Shape), not sizeof(ShapeDef), for those.
    size_t glyphShapeContainerBytes = glyphShapes.shapeCount * sizeof(swf::Shape) +
                                       glyphShapes.recordCount * sizeof(swf::ShapeRecord) +
                                       glyphShapes.fillStyleCount * sizeof(swf::FillStyle) +
                                       glyphShapes.lineStyleCount * sizeof(swf::LineStyle) +
                                       glyphShapes.gradientRecordCount * sizeof(swf::GradientRecord);

    size_t shapeTotal = shapeBytes(topLevelShapes);
    size_t spriteTotal = spriteCount * sizeof(runtime::SpriteDef) + spriteTagRecordCount * sizeof(swf::TagRecord);
    size_t soundTotal = soundCount * sizeof(swf::SoundDef);
    size_t fontTotal = fontCount * sizeof(swf::FontDef) + glyphShapeContainerBytes +
                        fontAdvanceCount * sizeof(int16_t) + fontBoundsCount * sizeof(swf::Rect) +
                        fontCodeTableCount * sizeof(uint16_t);
    size_t textTotal = textCount * sizeof(swf::TextDef) + textRecordCount * sizeof(swf::TextRecord) +
                        textGlyphEntryCount * sizeof(swf::GlyphEntry);
    size_t buttonTotal = buttonCount * sizeof(swf::ButtonDef) +
                          buttonRecordCount * sizeof(swf::ButtonRecordDef) +
                          buttonCondActionCount * sizeof(swf::ButtonCondAction) + buttonActionBytes;
    size_t editTextTotal = editTextCount * sizeof(swf::EditTextDef);
    size_t variantOverhead = totalCharacters * sizeof(runtime::CharacterDef);

    size_t grandTotal = shapeTotal + spriteTotal + soundTotal + fontTotal + textTotal + buttonTotal +
                         editTextTotal + variantOverhead;

    auto mb = [](size_t bytes) { return bytes / (1024.0 * 1024.0); };

    std::printf("=== %s ===\n", argv[1]);
    std::printf("total characters resolved: %zu\n\n", totalCharacters);

    std::printf("SHAPES: %zu characters, %zu records, %zu fill styles, %zu line styles, %zu gradient records\n",
                topLevelShapes.shapeCount, topLevelShapes.recordCount, topLevelShapes.fillStyleCount,
                topLevelShapes.lineStyleCount, topLevelShapes.gradientRecordCount);
    std::printf("  estimated bytes: %.2f MB (%.1f%% of total)\n\n", mb(shapeTotal), 100.0 * shapeTotal / grandTotal);

    std::printf("SPRITES: %zu characters, %zu nested tag records (offsets into Movie::data, no byte copies)\n",
                spriteCount, spriteTagRecordCount);
    std::printf("  estimated bytes: %.2f MB (%.1f%%)\n\n", mb(spriteTotal), 100.0 * spriteTotal / grandTotal);

    std::printf("SOUNDS: %zu characters (header fields + offset/length into Movie::data -- NO raw-byte copy)\n",
                soundCount);
    std::printf("  estimated bytes: %.2f MB (%.1f%%)\n\n", mb(soundTotal), 100.0 * soundTotal / grandTotal);

    std::printf("FONTS: %zu characters, %zu glyph outlines (%zu glyph records, %zu fill/%zu line styles within them)\n",
                fontCount, glyphShapes.shapeCount, glyphShapes.recordCount, glyphShapes.fillStyleCount,
                glyphShapes.lineStyleCount);
    std::printf("  estimated bytes: %.2f MB (%.1f%%)\n\n", mb(fontTotal), 100.0 * fontTotal / grandTotal);

    std::printf("TEXT: %zu characters, %zu text records, %zu glyph entries\n", textCount, textRecordCount,
                textGlyphEntryCount);
    std::printf("  estimated bytes: %.2f MB (%.1f%%)\n\n", mb(textTotal), 100.0 * textTotal / grandTotal);

    std::printf("BUTTONS: %zu characters, %zu button records, %zu condActionsV2 (%zu action bytes total)\n",
                buttonCount, buttonRecordCount, buttonCondActionCount, buttonActionBytes);
    std::printf("  estimated bytes: %.2f MB (%.1f%%)\n\n", mb(buttonTotal), 100.0 * buttonTotal / grandTotal);

    std::printf("EDIT TEXT: %zu characters\n", editTextCount);
    std::printf("  estimated bytes: %.2f MB (%.1f%%)\n\n", mb(editTextTotal), 100.0 * editTextTotal / grandTotal);

    std::printf("CharacterDef variant/std::variant inline storage overhead (%zu chars x %zu bytes):\n",
                totalCharacters, sizeof(runtime::CharacterDef));
    std::printf("  estimated bytes: %.2f MB (%.1f%%)\n\n", mb(variantOverhead), 100.0 * variantOverhead / grandTotal);

    std::printf("ESTIMATED GRAND TOTAL: %.2f MB\n", mb(grandTotal));
    std::printf("(NOTE: this is a sizeof()-based estimate of live payload bytes. It does NOT include per-\n"
                " allocation malloc bookkeeping overhead (typically 16-32 bytes per heap allocation on\n"
                " glibc), unordered_map bucket-array/node overhead, or std::string/std::vector's own\n"
                " capacity-vs-size slack from growth reallocation -- all of which are real and additive,\n"
                " not already included above. Compare against the measured RSS delta in\n"
                " docs/memory-audit.md to see how much of the real number this estimate accounts for.)\n");

    return 0;
}
