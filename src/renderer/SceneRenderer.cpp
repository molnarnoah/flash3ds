#include "renderer/SceneRenderer.h"

#include <algorithm>
#include <cmath>
#include <variant>

#include "platform/Log.h"
#include "renderer/ShapeTessellator.h"

namespace flash3ds::renderer {

namespace {

// Applies a Matrix (twips-space affine transform) to a single twips-space
// point. Same formula as SwfRecords::concatMatrix's translate terms, kept
// local here since it operates on a point rather than another matrix.
PointTwips applyMatrix(const swf::Matrix& m, int32_t x, int32_t y) {
    double nx = m.scaleX * x + m.rotateSkew1 * y + m.translateXTwips;
    double ny = m.rotateSkew0 * x + m.scaleY * y + m.translateYTwips;
    return PointTwips{static_cast<int32_t>(std::lround(nx)), static_cast<int32_t>(std::lround(ny))};
}

PointTwips twipsToDevice(PointTwips worldTwips, double pixelsPerTwipX, double pixelsPerTwipY) {
    return PointTwips{static_cast<int32_t>(std::lround(worldTwips.x * pixelsPerTwipX)),
                       static_cast<int32_t>(std::lround(worldTwips.y * pixelsPerTwipY))};
}

std::vector<PointTwips> toDevicePolyline(const std::vector<PointTwips>& localTwipsPoints,
                                          const swf::Matrix& worldMatrix, double pixelsPerTwipX,
                                          double pixelsPerTwipY) {
    std::vector<PointTwips> out;
    out.reserve(localTwipsPoints.size());
    for (const auto& p : localTwipsPoints) {
        PointTwips world = applyMatrix(worldMatrix, p.x, p.y);
        out.push_back(twipsToDevice(world, pixelsPerTwipX, pixelsPerTwipY));
    }
    return out;
}

}  // namespace

SceneRenderer::SceneRenderer(const runtime::Movie& movie,
                              const runtime::CharacterDictionary& characters)
    : movie_(&movie), characters_(&characters) {}

void SceneRenderer::render(const runtime::MovieClipInstance& root, IRenderer& target,
                            int outputWidthPixels, int outputHeightPixels) {
    double stageWidthTwips = movie_->frameSize.widthTwips();
    double stageHeightTwips = movie_->frameSize.heightTwips();
    // Fall back to the standard 20-twips-per-pixel ratio if the stage rect
    // is degenerate (zero-size, e.g. a malformed or not-yet-loaded movie).
    double pixelsPerTwipX =
        stageWidthTwips > 0 ? outputWidthPixels / stageWidthTwips : 1.0 / 20.0;
    double pixelsPerTwipY =
        stageHeightTwips > 0 ? outputHeightPixels / stageHeightTwips : 1.0 / 20.0;

    // SetBackgroundColor isn't parsed yet (Phase 8+ tag coverage), so we
    // always clear to white — matches the default Flash Player stage color
    // when no such tag is present.
    target.beginFrame(swf::RgbaColor{255, 255, 255, 255});

    renderClip(root, root.localMatrix(), target, pixelsPerTwipX, pixelsPerTwipY, 0);

    target.endFrame();
}

void SceneRenderer::renderClip(const runtime::MovieClipInstance& clip,
                                const swf::Matrix& worldMatrix, IRenderer& target,
                                double pixelsPerTwipX, double pixelsPerTwipY, int depth) {
    if (depth > kMaxRecursionDepth) {
        LOG_WARN("RENDER",
                  "Recursion depth limit (%d) exceeded while walking the display list — "
                  "possible cyclic sprite reference; stopping this branch",
                  kMaxRecursionDepth);
        return;
    }
    if (!clip.visible()) return;

    // DisplayList::entries() is a std::map<int32_t, ...>, so this iterates
    // in ascending depth order — exactly the back-to-front paint order the
    // SWF display model requires (lower depth = painted first/underneath).
    for (const auto& [depthValue, entry] : clip.timeline().displayList().entries()) {
        auto childIt = clip.children().find(depthValue);
        if (childIt != clip.children().end() && childIt->second) {
            // A sprite/MovieClip child — render via ITS OWN (possibly
            // script-mutated) transform, not the placement entry's, and
            // recurse using its own display list/children.
            const runtime::MovieClipInstance& child = *childIt->second;
            swf::Matrix childWorld = swf::concatMatrix(worldMatrix, child.localMatrix());
            renderClip(child, childWorld, target, pixelsPerTwipX, pixelsPerTwipY, depth + 1);
            continue;
        }
        // Not a MovieClipInstance — either a leaf character (shape/text/
        // button/edit-text) or an unresolved/still-unsupported (bitmap)
        // character, which renderCharacter() silently ignores.
        swf::Matrix childWorld = swf::concatMatrix(worldMatrix, entry.matrix);
        renderCharacter(entry.characterId, childWorld, target, pixelsPerTwipX, pixelsPerTwipY,
                         depth);
    }
}

void SceneRenderer::renderCharacter(uint16_t characterId, const swf::Matrix& worldMatrix,
                                     IRenderer& target, double pixelsPerTwipX,
                                     double pixelsPerTwipY, int depth) {
    if (depth > kMaxRecursionDepth) {
        LOG_WARN("RENDER",
                  "Recursion depth limit (%d) exceeded resolving character %u — possible cyclic "
                  "button/sprite reference; stopping this branch",
                  kMaxRecursionDepth, characterId);
        return;
    }

    const runtime::CharacterDef* def = characters_->find(characterId);
    if (!def) return;

    if (const auto* shapeDef = std::get_if<swf::ShapeDef>(def)) {
        renderShapeCharacter(*shapeDef, worldMatrix, target, pixelsPerTwipX, pixelsPerTwipY);
    } else if (const auto* textDef = std::get_if<swf::TextDef>(def)) {
        renderTextCharacter(*textDef, worldMatrix, target, pixelsPerTwipX, pixelsPerTwipY);
    } else if (const auto* editTextDef = std::get_if<swf::EditTextDef>(def)) {
        renderEditTextCharacter(*editTextDef, worldMatrix, target, pixelsPerTwipX, pixelsPerTwipY);
    } else if (const auto* buttonDef = std::get_if<swf::ButtonDef>(def)) {
        // No mouse hit-testing/state machine yet (see docs/avm1-support.md's
        // Known Phase 8 limitations) — always draw the "Up" state, matching
        // how the button looks before any interaction in a real player.
        for (const auto& rec : buttonDef->records) {
            if (!rec.stateUp) continue;
            swf::Matrix recordWorld = swf::concatMatrix(worldMatrix, rec.matrix);
            renderCharacter(rec.characterId, recordWorld, target, pixelsPerTwipX, pixelsPerTwipY,
                             depth + 1);
        }
    }
    // A SpriteDef here means syncChildren() hasn't (yet) created a
    // MovieClipInstance for this depth — shouldn't normally happen (every
    // sprite-resolving depth gets a child at sync time), but fail safe
    // rather than crash/recurse via a stale path. FontDef/SoundDef aren't
    // directly renderable leaf characters (a font is only ever referenced
    // BY a TextDef/EditTextDef, never placed on stage itself).
}

void SceneRenderer::renderShapeCharacter(const swf::ShapeDef& shapeDef,
                                          const swf::Matrix& worldMatrix, IRenderer& target,
                                          double pixelsPerTwipX, double pixelsPerTwipY) {
    TessellatedShape tess = tessellateShape(shapeDef.shape);

    for (const auto& poly : tess.polygons) {
        auto devicePoints =
            toDevicePolyline(poly.points, worldMatrix, pixelsPerTwipX, pixelsPerTwipY);
        target.fillPolygon(devicePoints, poly.color);
    }
    for (const auto& stroke : tess.strokes) {
        auto devicePoints =
            toDevicePolyline(stroke.points, worldMatrix, pixelsPerTwipX, pixelsPerTwipY);
        double avgPixelsPerTwip = (pixelsPerTwipX + pixelsPerTwipY) / 2.0;
        int widthPixels =
            std::max(1, static_cast<int>(std::lround(stroke.widthTwips * avgPixelsPerTwip)));
        target.strokePolyline(devicePoints, stroke.color, widthPixels);
    }
}

void SceneRenderer::renderGlyph(const swf::Shape& glyphShape, const swf::RgbaColor& color,
                                 double scale, int32_t offsetXTwips, int32_t offsetYTwips,
                                 const swf::Matrix& worldMatrix, IRenderer& target,
                                 double pixelsPerTwipX, double pixelsPerTwipY) {
    // A font glyph's own SHAPE carries no FillStyleArray of its own (see
    // swf/DefineFontTag.h) — synthesize a one-entry array holding the
    // requested color. Per the common real-world convention (glyph
    // StyleChangeRecords set FillStyle1=1 to mean "inside the glyph"), a
    // single entry at index 1 is what real content resolves against;
    // ShapeTessellator's fillStyle1-preferred-fallback-to-fillStyle0 logic
    // means it also works if some encoder used index 0 instead, since both
    // indices resolve into this same one-entry array either way.
    swf::Shape scaled;
    swf::FillStyle fs;
    fs.solidColor = color;
    scaled.fillStyles.push_back(fs);

    scaled.records.reserve(glyphShape.records.size());
    for (const swf::ShapeRecord& r : glyphShape.records) {
        swf::ShapeRecord sr = r;
        sr.moveToXTwips = static_cast<int32_t>(std::lround(r.moveToXTwips * scale));
        sr.moveToYTwips = static_cast<int32_t>(std::lround(r.moveToYTwips * scale));
        sr.deltaXTwips = static_cast<int32_t>(std::lround(r.deltaXTwips * scale));
        sr.deltaYTwips = static_cast<int32_t>(std::lround(r.deltaYTwips * scale));
        sr.controlDeltaXTwips = static_cast<int32_t>(std::lround(r.controlDeltaXTwips * scale));
        sr.controlDeltaYTwips = static_cast<int32_t>(std::lround(r.controlDeltaYTwips * scale));
        sr.anchorDeltaXTwips = static_cast<int32_t>(std::lround(r.anchorDeltaXTwips * scale));
        sr.anchorDeltaYTwips = static_cast<int32_t>(std::lround(r.anchorDeltaYTwips * scale));
        scaled.records.push_back(sr);
    }

    TessellatedShape tess = tessellateShape(scaled);
    for (auto& poly : tess.polygons) {
        for (auto& p : poly.points) {
            p.x += offsetXTwips;
            p.y += offsetYTwips;
        }
        auto devicePoints =
            toDevicePolyline(poly.points, worldMatrix, pixelsPerTwipX, pixelsPerTwipY);
        target.fillPolygon(devicePoints, poly.color);
    }
    // Glyph shapes never carry line styles (scaled.lineStyles is always
    // empty), so tess.strokes is always empty here too — nothing to draw.
}

void SceneRenderer::renderTextCharacter(const swf::TextDef& textDef,
                                         const swf::Matrix& worldMatrix, IRenderer& target,
                                         double pixelsPerTwipX, double pixelsPerTwipY) {
    // TextMatrix maps the text's own private "text space" (where glyph
    // coordinates/offsets live) into the character's local space, exactly
    // like a MovieClip's own transform maps into its parent's — compose it
    // in now, once, rather than per glyph.
    swf::Matrix textWorld = swf::concatMatrix(worldMatrix, textDef.matrix);

    std::optional<uint16_t> currentFontId;
    swf::RgbaColor currentColor{0, 0, 0, 255};
    uint16_t currentHeightTwips = 0;
    int32_t cursorX = 0;
    int32_t cursorY = 0;

    for (const auto& rec : textDef.records) {
        if (rec.fontId) currentFontId = rec.fontId;
        if (rec.color) currentColor = *rec.color;
        if (rec.textHeightTwips) currentHeightTwips = *rec.textHeightTwips;
        // XOffset/YOffset SET the cursor (they don't accumulate) — per spec,
        // absent means "carry forward from wherever the previous record's
        // glyphs left off".
        if (rec.xOffsetTwips) cursorX = *rec.xOffsetTwips;
        if (rec.yOffsetTwips) cursorY = *rec.yOffsetTwips;

        if (!currentFontId) continue;  // no font set yet — can't resolve glyph outlines
        const runtime::CharacterDef* fontCharDef = characters_->find(*currentFontId);
        const auto* font = fontCharDef ? std::get_if<swf::FontDef>(fontCharDef) : nullptr;
        if (!font) continue;

        double scale = currentHeightTwips / 1024.0;
        for (const auto& glyph : rec.glyphs) {
            if (glyph.glyphIndex < font->glyphShapes.size()) {
                renderGlyph(font->glyphShapes[glyph.glyphIndex], currentColor, scale, cursorX,
                            cursorY, textWorld, target, pixelsPerTwipX, pixelsPerTwipY);
            }
            cursorX += static_cast<int32_t>(std::lround(glyph.advance * scale));
        }
    }
}

void SceneRenderer::renderEditTextCharacter(const swf::EditTextDef& editTextDef,
                                             const swf::Matrix& worldMatrix, IRenderer& target,
                                             double pixelsPerTwipX, double pixelsPerTwipY) {
    // Deliberately narrow: only renders when there's an embedded font (with
    // a code table — i.e. DefineFont2, not a legacy DefineFont/
    // DefineFontInfo pairing, which this runtime doesn't parse — see
    // swf/DefineFontTag.h) and literal initial text. No word-wrap,
    // scrolling, alignment, multi-byte/HTML text, or variable-binding —
    // see docs/avm1-support.md's Known Phase 8 limitations.
    if (!editTextDef.fontId || !editTextDef.fontHeightTwips || !editTextDef.initialText) return;

    const runtime::CharacterDef* fontCharDef = characters_->find(*editTextDef.fontId);
    const auto* font = fontCharDef ? std::get_if<swf::FontDef>(fontCharDef) : nullptr;
    if (!font || font->codeTable.empty()) return;

    swf::RgbaColor color = editTextDef.textColor.value_or(swf::RgbaColor{0, 0, 0, 255});
    double scale = *editTextDef.fontHeightTwips / 1024.0;

    // Text starts at the field's own bounds top-left; cursorY is set to one
    // line height down so the first line's BASELINE (not its top) lands
    // inside the box, matching how text is normally authored to sit within
    // its bounds — an approximation (no real font-metric-based baseline;
    // see FontDef::ascent, unused here), not exact typographic placement.
    int32_t cursorX = editTextDef.bounds.xMin;
    int32_t cursorY = editTextDef.bounds.yMin + *editTextDef.fontHeightTwips;
    int32_t lineStartX = cursorX;

    for (unsigned char ch : *editTextDef.initialText) {
        if (ch == '\n' || ch == '\r') {
            cursorX = lineStartX;
            cursorY += *editTextDef.fontHeightTwips;
            continue;
        }
        int glyphIndex = font->glyphIndexForCode(ch);
        if (glyphIndex < 0) continue;  // character not in this font — skipped, not substituted
        renderGlyph(font->glyphShapes[static_cast<size_t>(glyphIndex)], color, scale, cursorX,
                    cursorY, worldMatrix, target, pixelsPerTwipX, pixelsPerTwipY);
        double advance = (!font->glyphAdvances.empty() &&
                           static_cast<size_t>(glyphIndex) < font->glyphAdvances.size())
                              ? font->glyphAdvances[static_cast<size_t>(glyphIndex)] * scale
                              : 0.0;
        cursorX += static_cast<int32_t>(std::lround(advance));
    }
}

}  // namespace flash3ds::renderer
