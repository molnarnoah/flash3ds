#include "renderer/SceneRenderer.h"

#include <algorithm>
#include <cmath>
#include <variant>

#include "platform/Log.h"
#include "renderer/ShapeTessellator.h"

namespace flash3ds::renderer {

namespace {

// Performance fix (continuing the FPS task after SoftwareRenderer.cpp's
// fillPolygon() roundToInt() fix -- see docs/performance-pacing.md): that
// earlier fix only addressed lround() calls inside the rasterizer's own
// span-fill inner loop. This file's calls are a separate, previously-
// unaddressed cost site the doc explicitly flagged as "now a clearly
// evidenced, separate target" -- the tree-walk/tessellation-to-device side
// of the pipeline, not the rasterizer. Unlike ShapeTessellator.cpp's own
// lround() calls (cold: tessellateShape() runs once per shape, cached in
// SceneRenderer::shapeTessellationCache_, and only recomputed on a cache
// miss), every call site in THIS file runs every single frame: toDevicePolyline()
// converts each already-tessellated vertex to device pixels via the current
// world transform once per vertex per visible shape (the transform can
// change frame-to-frame from animation, so this can't be cached the way
// tessellation is), and renderGlyph() re-tessellates and re-scales its
// synthesized glyph shape from scratch on every call with no cache at all --
// so its edge-scaling lround() calls are, if anything, hotter per-glyph than
// toDevicePolyline()'s per-vertex calls.
//
// Same reasoning and same safety argument as SoftwareRenderer.cpp's
// roundToInt(): every value reaching these call sites is a finite,
// small-magnitude twip or device-pixel coordinate (stage/glyph dimensions,
// never NaN/Inf/near-overflow), so std::lround()'s full IEEE-754 contract
// handling is pure overhead here, and `x + 0.5`/`x - 0.5` truncated is
// bit-identical to it (round-half-away-from-zero) across this renderer's
// entire coordinate range.
inline int32_t roundToInt(double x) {
    return x >= 0.0 ? static_cast<int32_t>(x + 0.5) : static_cast<int32_t>(x - 0.5);
}

// Applies a Matrix (twips-space affine transform) to a single twips-space
// point. Same formula as SwfRecords::concatMatrix's translate terms, kept
// local here since it operates on a point rather than another matrix.
PointTwips applyMatrix(const swf::Matrix& m, int32_t x, int32_t y) {
    double nx = m.scaleX * x + m.rotateSkew1 * y + m.translateXTwips;
    double ny = m.rotateSkew0 * x + m.scaleY * y + m.translateYTwips;
    return PointTwips{roundToInt(nx), roundToInt(ny)};
}

PointTwips twipsToDevice(PointTwips worldTwips, double pixelsPerTwipX, double pixelsPerTwipY) {
    return PointTwips{roundToInt(worldTwips.x * pixelsPerTwipX),
                       roundToInt(worldTwips.y * pixelsPerTwipY)};
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

// Resolves a TessellatedPolygon's GradientPaint (still in the shape's own
// local twips space) into a device-pixel-ready DeviceGradientFill (see
// IRenderer.h's doc comment on that struct) — the affine-matrix-inversion
// half of the graphics/gradients task (2026-08-28).
//
// The forward chain from the SWF gradient square's own -16384..16384 space
// to a device pixel is: gradientMatrix (gradient square -> shape-local
// twips) -> worldMatrix (shape-local twips -> world twips) ->
// pixelsPerTwipX/Y (world twips -> device pixels). concatMatrix(parent,
// child) composes "child first, then parent" (see swf/SwfRecords.h), so
// combinedLocal = concatMatrix(worldMatrix, poly.gradient.matrix) is
// exactly "apply gradientMatrix, then worldMatrix" — gradient square ->
// world twips in one Matrix. Folding in the final twips->pixels scale
// gives the full forward affine transform's six coefficients (A, B, C, D,
// TX, TY below); this function inverts that 2x2 (see the worked-out algebra
// in this project's session notes) to get the device-pixel -> gradient-
// space transform DeviceGradientFill actually needs. Returns false (leaving
// `out` unmodified) for a degenerate (near-singular, |det| ~ 0) transform —
// callers fall back to the polygon's flat toFlatColor() color instead,
// exactly like a real player would for a gradient that's been scaled to
// nothing.
bool resolveGradientFill(const GradientPaint& paint, const swf::Matrix& worldMatrix,
                          const swf::ColorTransform& worldColorTransform, double pixelsPerTwipX,
                          double pixelsPerTwipY, DeviceGradientFill& out) {
    swf::Matrix combinedLocal = swf::concatMatrix(worldMatrix, paint.matrix);

    double A = pixelsPerTwipX * combinedLocal.scaleX;
    double C = pixelsPerTwipX * combinedLocal.rotateSkew1;
    double TX = pixelsPerTwipX * combinedLocal.translateXTwips;
    double B = pixelsPerTwipY * combinedLocal.rotateSkew0;
    double D = pixelsPerTwipY * combinedLocal.scaleY;
    double TY = pixelsPerTwipY * combinedLocal.translateYTwips;

    double det = A * D - B * C;
    if (std::fabs(det) < 1e-9) return false;

    double invA = D / det;
    double invC = -C / det;
    double invB = -B / det;
    double invD = A / det;

    out.a = invA;
    out.c = invC;
    out.tx = -invA * TX - invC * TY;
    out.b = invB;
    out.d = invD;
    out.ty = -invB * TX - invD * TY;
    out.spreadMode = paint.spreadMode;
    for (size_t i = 0; i < paint.ramp.size(); ++i) {
        out.ramp[i] = swf::applyColorTransform(paint.ramp[i], worldColorTransform);
    }
    return true;
}

// Resolves a TessellatedPolygon's BitmapPaint into a device-pixel-ready
// DeviceBitmapFill (2026-08-31, Priority Fix List item #2 — see
// IRenderer.h's DeviceBitmapFill doc comment and ShapeTessellator.h's
// BitmapPaint doc comment). Two things this does that
// resolveGradientFill() above doesn't need to: (1) actually look up
// `paint.bitmapCharacterId` in `characters` — ShapeTessellator has no
// CharacterDictionary access, so this is the first point in the pipeline
// that CAN resolve it, and (2) fold an extra /20 scale into the inverted
// matrix, since BitmapPaint::matrix maps "bitmap space" at 20 twips per
// source pixel (matching the SWF spec's own bitmap-fill convention) into
// shape-local twips, where DeviceGradientFill's gradient square is already
// unscaled. Returns false (leaving `out` unmodified) if the character
// doesn't resolve to a decoded BitmapDef (an out-of-scope DefineBits(6)/
// DefineBitsJpeg4(90) tag, a dangling characterId, or a BitmapDef whose own
// parse failed and left `pixels` empty) or the transform is degenerate —
// callers fall back to the polygon's flat toFlatColor() color in either
// case, exactly like a gradient fill that hits the same degenerate-matrix
// case.
bool resolveBitmapFill(const BitmapPaint& paint, const swf::Matrix& worldMatrix,
                        const swf::ColorTransform& worldColorTransform,
                        const runtime::CharacterDictionary& characters, double pixelsPerTwipX,
                        double pixelsPerTwipY, DeviceBitmapFill& out) {
    const runtime::CharacterDef* def = characters.find(paint.bitmapCharacterId);
    if (!def) return false;
    const auto* bitmapDef = std::get_if<swf::BitmapDef>(def);
    if (!bitmapDef || bitmapDef->pixels.empty()) return false;

    swf::Matrix combinedLocal = swf::concatMatrix(worldMatrix, paint.matrix);

    double A = pixelsPerTwipX * combinedLocal.scaleX;
    double C = pixelsPerTwipX * combinedLocal.rotateSkew1;
    double TX = pixelsPerTwipX * combinedLocal.translateXTwips;
    double B = pixelsPerTwipY * combinedLocal.rotateSkew0;
    double D = pixelsPerTwipY * combinedLocal.scaleY;
    double TY = pixelsPerTwipY * combinedLocal.translateYTwips;

    double det = A * D - B * C;
    if (std::fabs(det) < 1e-9) return false;

    // Inverting the UN-scaled 2x2 first (exactly resolveGradientFill()'s
    // own invA..invD formulas) and dividing by 20 afterwards gives device-
    // pixel -> bitmap-space-in-TWIPS -> bitmap PIXEL index in one step:
    // if (u, v) = device-pixel -> bitmap-space-in-twips via the UN-divided
    // inverse (u = invA_full*px + invC_full*py + invTX_full, etc.), then
    // the actual pixel index is (u/20, v/20) — and since division by 20 is
    // linear, (invA_full/20, invC_full/20, invTX_full/20) computed from
    // the ALREADY-divided invA/invC below is exactly invTX_full/20 too
    // (distributing the /20 over the whole affine expression), so no
    // separate translation-term scaling is needed beyond just using the
    // pre-divided invA/invB/invC/invD in the same -invA*TX-invC*TY formula
    // resolveGradientFill() uses for its own (unscaled) invTX/invTY.
    double invA = (D / det) / 20.0;
    double invC = (-C / det) / 20.0;
    double invB = (-B / det) / 20.0;
    double invD = (A / det) / 20.0;

    out.a = invA;
    out.c = invC;
    out.tx = -invA * TX - invC * TY;
    out.b = invB;
    out.d = invD;
    out.ty = -invB * TX - invD * TY;
    out.width = bitmapDef->width;
    out.height = bitmapDef->height;
    out.repeat = paint.repeat;
    out.smoothed = paint.smoothed;
    out.colorTransform = worldColorTransform;
    out.pixels = bitmapDef->pixels.data();
    return true;
}

// Shared by renderShapeCharacter()/renderMorphShapeCharacter(): fills one
// already-tessellated polygon, taking the gradient/bitmap branch when the
// polygon resolved to one and the device-space matrix inversion above
// didn't hit the degenerate case, falling back to the existing flat-fill
// call otherwise (identical to this function's pre-gradient-task
// behavior). `characters` is only actually used by the kBitmap branch —
// threaded through unconditionally so every call site has one obvious
// signature regardless of which paint kinds a given polygon set happens
// to use.
void fillTessellatedPolygon(const TessellatedPolygon& poly, const swf::Matrix& worldMatrix,
                             const swf::ColorTransform& worldColorTransform,
                             const runtime::CharacterDictionary& characters, IRenderer& target,
                             double pixelsPerTwipX, double pixelsPerTwipY) {
    auto devicePoints = toDevicePolyline(poly.points, worldMatrix, pixelsPerTwipX, pixelsPerTwipY);
    if (poly.paintKind == PaintKind::kLinearGradient) {
        DeviceGradientFill fill;
        if (resolveGradientFill(poly.gradient, worldMatrix, worldColorTransform, pixelsPerTwipX,
                                 pixelsPerTwipY, fill)) {
            target.fillPolygonGradient(devicePoints, fill);
            return;
        }
    } else if (poly.paintKind == PaintKind::kBitmap) {
        DeviceBitmapFill fill;
        if (resolveBitmapFill(poly.bitmap, worldMatrix, worldColorTransform, characters,
                               pixelsPerTwipX, pixelsPerTwipY, fill)) {
            target.fillPolygonBitmap(devicePoints, fill);
            return;
        }
    }
    target.fillPolygon(devicePoints, swf::applyColorTransform(poly.color, worldColorTransform));
}

// Hole/counter rendering (2026-08-31, Priority Fix List item #1 — see
// ShapeTessellator.h's TessellatedPolygon::fillGroupId doc comment and
// IRenderer.h's fillPolygonGroup() doc comment for the full design).
// Shared by every call site that used to loop over `tess.polygons` calling
// fillTessellatedPolygon() once per contour independently
// (renderShapeCharacter/renderMorphShapeCharacter/renderGlyph): groups
// `polygons` by fillGroupId (same group id => different contours of the
// SAME original fill style, e.g. an outer boundary and an inner counter)
// and paints each group with exactly ONE call — fillTessellatedPolygon()
// unchanged for a lone-contour group (the overwhelming common case, so
// this reduces to the exact prior behavior/cost for it), or a combined
// even-odd fillPolygonGroup()/fillPolygonGradientGroup() call for a real
// multi-contour group.
//
// IMPORTANT (2026-08-31, real-corpus regression found + fixed against
// hobo.swf's characterId=89 "mute button" icon — see git history/commit
// message for the full before/after evidence): a group is combined ONLY
// when its same-fillGroupId members are CONTIGUOUS in `polygons` — i.e.
// no polygon of a DIFFERENT fillGroupId sits between them in the
// tessellator's authored-order emission. An earlier version of this
// function scanned the ENTIRE remaining list for same-fillGroupId matches
// regardless of what sat between them, which silently reordered painter's-
// algorithm z-order whenever a same-style contour was meant to be
// repainted LATER, on top of intervening differently-styled contours,
// rather than combined as a true nested hole of an EARLIER same-style
// contour. Concretely: character 89 emits polygons in the order
// {white outer circle, black detail x3, white highlight, black detail} —
// the two white contours share a fillGroupId but are NOT a boundary+hole
// pair; the second white contour is a deliberate overpaint that must stay
// AFTER the three black contours, not get pulled forward and combined
// with the first white contour (which produced a spurious even-odd hole
// and, more importantly, lost the "repaint white on top of black" step
// entirely). Restricting combination to contiguous runs preserves that
// z-order exactly — non-contiguous same-fillGroupId contours simply paint
// independently, in their own original position, same as before this
// whole fillGroupId mechanism existed. Genuine boundary+hole pairs (the
// actual target of this fix, e.g. a glyph's outer contour immediately
// followed by its counter contour, or vice versa) are still combined
// correctly, since nothing of a different fill style is ever tessellated
// between two contours of the very same fill run in that case.
void fillTessellatedPolygons(const std::vector<TessellatedPolygon>& polygons,
                              const swf::Matrix& worldMatrix,
                              const swf::ColorTransform& worldColorTransform,
                              const runtime::CharacterDictionary& characters, IRenderer& target,
                              double pixelsPerTwipX, double pixelsPerTwipY) {
    for (size_t i = 0; i < polygons.size();) {
        const TessellatedPolygon& first = polygons[i];

        size_t end = i + 1;
        while (end < polygons.size() && polygons[end].fillGroupId == first.fillGroupId) {
            ++end;
        }
        size_t count = end - i;

        if (count == 1) {
            fillTessellatedPolygon(first, worldMatrix, worldColorTransform, characters, target,
                                    pixelsPerTwipX, pixelsPerTwipY);
            i = end;
            continue;
        }

        std::vector<std::vector<PointTwips>> contours;
        contours.reserve(count);
        for (size_t m = i; m < end; ++m) {
            contours.push_back(toDevicePolyline(polygons[m].points, worldMatrix, pixelsPerTwipX,
                                                 pixelsPerTwipY));
        }
        if (first.paintKind == PaintKind::kLinearGradient) {
            DeviceGradientFill fill;
            if (resolveGradientFill(first.gradient, worldMatrix, worldColorTransform,
                                     pixelsPerTwipX, pixelsPerTwipY, fill)) {
                target.fillPolygonGradientGroup(contours, fill);
                i = end;
                continue;
            }
        } else if (first.paintKind == PaintKind::kBitmap) {
            DeviceBitmapFill fill;
            if (resolveBitmapFill(first.bitmap, worldMatrix, worldColorTransform, characters,
                                   pixelsPerTwipX, pixelsPerTwipY, fill)) {
                target.fillPolygonBitmapGroup(contours, fill);
                i = end;
                continue;
            }
        }
        target.fillPolygonGroup(contours,
                                 swf::applyColorTransform(first.color, worldColorTransform));
        i = end;
    }
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

    renderClip(root, root.localMatrix(), root.colorTransform(), target, pixelsPerTwipX,
               pixelsPerTwipY, 0);

    target.endFrame();
}

void SceneRenderer::renderClip(const runtime::MovieClipInstance& clip,
                                const swf::Matrix& worldMatrix,
                                const swf::ColorTransform& worldColorTransform, IRenderer& target,
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
            // script-mutated) transform/color transform, not the placement
            // entry's, and recurse using its own display list/children.
            const runtime::MovieClipInstance& child = *childIt->second;
            swf::Matrix childWorld = swf::concatMatrix(worldMatrix, child.localMatrix());
            swf::ColorTransform childColor =
                swf::concatColorTransform(worldColorTransform, child.colorTransform());
            renderClip(child, childWorld, childColor, target, pixelsPerTwipX, pixelsPerTwipY,
                       depth + 1);
            continue;
        }
        // Not a MovieClipInstance — either a leaf character (shape/text/
        // button/edit-text) or an unresolved/still-unsupported (bitmap)
        // character, which renderCharacter() silently ignores.
        swf::Matrix childWorld = swf::concatMatrix(worldMatrix, entry.matrix);
        swf::ColorTransform childColor =
            swf::concatColorTransform(worldColorTransform, entry.colorTransform);
        renderCharacter(entry.characterId, childWorld, childColor, target, pixelsPerTwipX,
                         pixelsPerTwipY, depth);
    }
}

void SceneRenderer::renderCharacter(uint16_t characterId, const swf::Matrix& worldMatrix,
                                     const swf::ColorTransform& worldColorTransform,
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
        renderShapeCharacter(*shapeDef, worldMatrix, worldColorTransform, target, pixelsPerTwipX,
                              pixelsPerTwipY);
    } else if (const auto* morphDef = std::get_if<swf::MorphShapeDef>(def)) {
        renderMorphShapeCharacter(*morphDef, worldMatrix, worldColorTransform, target, pixelsPerTwipX,
                                   pixelsPerTwipY);
    } else if (const auto* textDef = std::get_if<swf::TextDef>(def)) {
        renderTextCharacter(*textDef, worldMatrix, worldColorTransform, target, pixelsPerTwipX,
                             pixelsPerTwipY);
    } else if (const auto* editTextDef = std::get_if<swf::EditTextDef>(def)) {
        renderEditTextCharacter(*editTextDef, worldMatrix, worldColorTransform, target,
                                 pixelsPerTwipX, pixelsPerTwipY);
    } else if (const auto* buttonDef = std::get_if<swf::ButtonDef>(def)) {
        // No mouse hit-testing/state machine yet (see docs/avm1-support.md's
        // Known Phase 8 limitations) — always draw the "Up" state, matching
        // how the button looks before any interaction in a real player.
        for (const auto& rec : buttonDef->records) {
            if (!rec.stateUp) continue;
            swf::Matrix recordWorld = swf::concatMatrix(worldMatrix, rec.matrix);
            swf::ColorTransform recordColor = swf::concatColorTransform(
                worldColorTransform, rec.colorTransform.value_or(swf::ColorTransform::identity()));
            renderCharacter(rec.characterId, recordWorld, recordColor, target, pixelsPerTwipX,
                             pixelsPerTwipY, depth + 1);
        }
    }
    // A SpriteDef here means syncChildren() hasn't (yet) created a
    // MovieClipInstance for this depth — shouldn't normally happen (every
    // sprite-resolving depth gets a child at sync time), but fail safe
    // rather than crash/recurse via a stale path. FontDef/SoundDef aren't
    // directly renderable leaf characters (a font is only ever referenced
    // BY a TextDef/EditTextDef, never placed on stage itself). Likewise
    // BitmapDef (2026-08-31, Priority Fix List item #2) is never placed
    // directly on the display list by a real authoring tool — a bitmap
    // dragged onto the stage is always wrapped in an auto-generated shape
    // whose fill style references it by bitmapCharacterId (see
    // ShapeTessellator.h's BitmapPaint/SceneRenderer.cpp's
    // resolveBitmapFill()); a PlaceObject2 that somehow DID target a bare
    // bitmap character ID directly falls through every branch above and is
    // silently skipped, same as any other character kind with no leaf-
    // rendering branch here.
}

void SceneRenderer::renderShapeCharacter(const swf::ShapeDef& shapeDef,
                                          const swf::Matrix& worldMatrix,
                                          const swf::ColorTransform& worldColorTransform,
                                          IRenderer& target, double pixelsPerTwipX,
                                          double pixelsPerTwipY) {
    // Tessellation cache -- see shapeTessellationCache_'s own doc comment
    // in SceneRenderer.h for why keying by the swf::Shape's address is
    // safe. `emplace` only actually constructs/tessellates on a real miss
    // (its second argument is only evaluated if the key is absent), so a
    // cache hit costs one hash lookup, not a wasted tessellateShape() call
    // followed by a discard.
    const swf::Shape* key = &shapeDef.shape;
    auto [it, inserted] = shapeTessellationCache_.try_emplace(key);
    if (inserted) {
        it->second = tessellateShape(shapeDef.shape);
    }
    const TessellatedShape& tess = it->second;

    fillTessellatedPolygons(tess.polygons, worldMatrix, worldColorTransform, *characters_, target,
                             pixelsPerTwipX, pixelsPerTwipY);
    for (const auto& stroke : tess.strokes) {
        auto devicePoints =
            toDevicePolyline(stroke.points, worldMatrix, pixelsPerTwipX, pixelsPerTwipY);
        double avgPixelsPerTwip = (pixelsPerTwipX + pixelsPerTwipY) / 2.0;
        int widthPixels =
            std::max(1, static_cast<int>(roundToInt(stroke.widthTwips * avgPixelsPerTwip)));
        target.strokePolyline(devicePoints, swf::applyColorTransform(stroke.color, worldColorTransform),
                               widthPixels);
    }
}

void SceneRenderer::renderMorphShapeCharacter(const swf::MorphShapeDef& morphDef,
                                               const swf::Matrix& worldMatrix,
                                               const swf::ColorTransform& worldColorTransform,
                                               IRenderer& target, double pixelsPerTwipX,
                                               double pixelsPerTwipY) {
    // Synthesize a plain swf::Shape from the morph's START-side fill/line
    // styles and START edges only (ratio=0 simplification — see this
    // function's doc comment in SceneRenderer.h and
    // swf/DefineMorphShapeTag.h). `startEdges` is already a full
    // SHAPERECORD stream whose StyleChangeRecord fillStyle0/fillStyle1/
    // lineStyleIndex values were parsed against MorphFillStyles/
    // MorphLineStyles in the same order the loop below rebuilds them in,
    // so index correspondence is preserved without needing to touch the
    // edge records themselves — unlike renderGlyph()'s synthesis, which
    // does need to rewrite edge deltas (for scaling); no such transform is
    // needed here, so startEdges is reused directly.
    swf::Shape shape;
    shape.fillStyles.reserve(morphDef.fillStyles.size());
    for (const auto& mfs : morphDef.fillStyles) {
        swf::FillStyle fs;
        fs.type = mfs.type;
        fs.solidColor = mfs.startColor;
        fs.gradientMatrix = mfs.startMatrix;
        fs.bitmapCharacterId = mfs.bitmapCharacterId;
        if (mfs.isGradient()) {
            fs.gradient.spreadMode = mfs.gradient.spreadMode;
            fs.gradient.interpolationMode = mfs.gradient.interpolationMode;
            fs.gradient.records.reserve(mfs.gradient.records.size());
            for (const auto& mgr : mfs.gradient.records) {
                fs.gradient.records.push_back(
                    swf::GradientRecord{mgr.startRatio, mgr.startColor});
            }
        }
        shape.fillStyles.push_back(std::move(fs));
    }
    shape.lineStyles.reserve(morphDef.lineStyles.size());
    for (const auto& mls : morphDef.lineStyles) {
        shape.lineStyles.push_back(swf::LineStyle{mls.startWidthTwips, mls.startColor});
    }
    shape.records = morphDef.startEdges;

    TessellatedShape tess = tessellateShape(shape);

    fillTessellatedPolygons(tess.polygons, worldMatrix, worldColorTransform, *characters_, target,
                             pixelsPerTwipX, pixelsPerTwipY);
    for (const auto& stroke : tess.strokes) {
        auto devicePoints =
            toDevicePolyline(stroke.points, worldMatrix, pixelsPerTwipX, pixelsPerTwipY);
        double avgPixelsPerTwip = (pixelsPerTwipX + pixelsPerTwipY) / 2.0;
        int widthPixels =
            std::max(1, static_cast<int>(roundToInt(stroke.widthTwips * avgPixelsPerTwip)));
        target.strokePolyline(devicePoints, swf::applyColorTransform(stroke.color, worldColorTransform),
                               widthPixels);
    }
}

void SceneRenderer::renderGlyph(const swf::Shape& glyphShape, const swf::RgbaColor& color,
                                 double scale, int32_t offsetXTwips, int32_t offsetYTwips,
                                 const swf::Matrix& worldMatrix,
                                 const swf::ColorTransform& worldColorTransform, IRenderer& target,
                                 double pixelsPerTwipX, double pixelsPerTwipY) {
    // A font glyph's own SHAPE carries no FillStyleArray of its own (see
    // swf/DefineFontTag.h) — synthesize a one-entry array holding the
    // requested color (already color-transform-applied — the caller passes
    // the RAW record/field color; ShapeTessellator's output color is
    // transformed uniformly below, same as renderShapeCharacter, so the
    // color is applied post-tessellation, not pre-synthesis). Per the
    // common real-world convention (glyph StyleChangeRecords set
    // FillStyle1=1 to mean "inside the glyph"), a single entry at index 1
    // is what real content resolves against; ShapeTessellator's
    // fillStyle1-preferred-fallback-to-fillStyle0 logic means it also works
    // if some encoder used index 0 instead, since both indices resolve into
    // this same one-entry array either way.
    swf::Shape scaled;
    swf::FillStyle fs;
    fs.solidColor = color;
    scaled.fillStyles.push_back(fs);

    scaled.records.reserve(glyphShape.records.size());
    for (const swf::ShapeRecord& r : glyphShape.records) {
        // Type-aware copy (Roadmap Phase 2 "Compact ShapeRecord" fix,
        // 2026-08-21): ShapeRecord's kStraightEdge/kCurvedEdge fields now
        // share storage via a union, and kStyleChange's fields live behind
        // a possibly-null styleChange pointer, so a blind field-by-field
        // copy across all sub-type fields (the old approach, harmless only
        // because the fields used to always coexist) is no longer safe —
        // it would dereference a null styleChange for edge records. Switch
        // on r.type and only touch the fields that are actually live.
        swf::ShapeRecord sr;
        sr.type = r.type;
        switch (r.type) {
            case swf::ShapeRecordType::kStyleChange: {
                // r.styleChange is non-null (invariant from the parser).
                // Deep-copy it (not just share the pointer) so we can scale
                // moveTo without mutating the original glyph's own record.
                sr.styleChange = std::make_shared<swf::ShapeStyleChange>(*r.styleChange);
                sr.styleChange->moveToXTwips =
                    roundToInt(r.styleChange->moveToXTwips * scale);
                sr.styleChange->moveToYTwips =
                    roundToInt(r.styleChange->moveToYTwips * scale);
                break;
            }
            case swf::ShapeRecordType::kStraightEdge:
                sr.edge.straightEdge.deltaXTwips =
                    roundToInt(r.edge.straightEdge.deltaXTwips * scale);
                sr.edge.straightEdge.deltaYTwips =
                    roundToInt(r.edge.straightEdge.deltaYTwips * scale);
                break;
            case swf::ShapeRecordType::kCurvedEdge:
                sr.edge.curvedEdge.controlDeltaXTwips =
                    roundToInt(r.edge.curvedEdge.controlDeltaXTwips * scale);
                sr.edge.curvedEdge.controlDeltaYTwips =
                    roundToInt(r.edge.curvedEdge.controlDeltaYTwips * scale);
                sr.edge.curvedEdge.anchorDeltaXTwips =
                    roundToInt(r.edge.curvedEdge.anchorDeltaXTwips * scale);
                sr.edge.curvedEdge.anchorDeltaYTwips =
                    roundToInt(r.edge.curvedEdge.anchorDeltaYTwips * scale);
                break;
            case swf::ShapeRecordType::kEnd:
                break;
        }
        scaled.records.push_back(std::move(sr));
    }

    TessellatedShape tess = tessellateShape(scaled);
    for (auto& poly : tess.polygons) {
        for (auto& p : poly.points) {
            p.x += offsetXTwips;
            p.y += offsetYTwips;
        }
    }
    // A glyph synthesizes exactly ONE FillStyle (fs above), so every
    // polygon tessellateShape() emits here shares the same fillGroupId by
    // construction — meaning any letterform with a counter (O, A, B, D, P,
    // Q, R, ...) naturally becomes a single multi-contour group. Routing
    // through the shared helper (instead of the old per-polygon
    // fillPolygon() loop) is what actually closes Priority Fix List item
    // #1 for glyph rendering specifically: the outer boundary and inner
    // counter contours are now combined-filled with one even-odd pass
    // instead of each being painted as its own solid patch.
    fillTessellatedPolygons(tess.polygons, worldMatrix, worldColorTransform, *characters_, target,
                             pixelsPerTwipX, pixelsPerTwipY);
    // Glyph shapes never carry line styles (scaled.lineStyles is always
    // empty), so tess.strokes is always empty here too — nothing to draw.
}

void SceneRenderer::renderTextCharacter(const swf::TextDef& textDef,
                                         const swf::Matrix& worldMatrix,
                                         const swf::ColorTransform& worldColorTransform,
                                         IRenderer& target, double pixelsPerTwipX,
                                         double pixelsPerTwipY) {
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
                            cursorY, textWorld, worldColorTransform, target, pixelsPerTwipX,
                            pixelsPerTwipY);
            }
            cursorX += roundToInt(glyph.advance * scale);
        }
    }
}

void SceneRenderer::renderEditTextCharacter(const swf::EditTextDef& editTextDef,
                                             const swf::Matrix& worldMatrix,
                                             const swf::ColorTransform& worldColorTransform,
                                             IRenderer& target, double pixelsPerTwipX,
                                             double pixelsPerTwipY) {
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
                    cursorY, worldMatrix, worldColorTransform, target, pixelsPerTwipX,
                    pixelsPerTwipY);
        double advance = (!font->glyphAdvances.empty() &&
                           static_cast<size_t>(glyphIndex) < font->glyphAdvances.size())
                              ? font->glyphAdvances[static_cast<size_t>(glyphIndex)] * scale
                              : 0.0;
        cursorX += roundToInt(advance);
    }
}

}  // namespace flash3ds::renderer
