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
        // Not a MovieClipInstance — either a Shape leaf character, or an
        // unresolved/unsupported character (bitmap/text/button/font —
        // Phase 8+), which renderShapeCharacter() silently ignores.
        swf::Matrix childWorld = swf::concatMatrix(worldMatrix, entry.matrix);
        renderShapeCharacter(entry.characterId, childWorld, target, pixelsPerTwipX,
                              pixelsPerTwipY);
    }
}

void SceneRenderer::renderShapeCharacter(uint16_t characterId, const swf::Matrix& worldMatrix,
                                          IRenderer& target, double pixelsPerTwipX,
                                          double pixelsPerTwipY) {
    const runtime::CharacterDef* def = characters_->find(characterId);
    if (!def) return;

    const auto* shapeDef = std::get_if<swf::ShapeDef>(def);
    if (!shapeDef) {
        // A SpriteDef here means syncChildren() hasn't (yet) created a
        // MovieClipInstance for this depth — shouldn't normally happen
        // (every sprite-resolving depth gets a child at sync time), but
        // fail safe rather than crash/recurse via a stale path.
        return;
    }

    TessellatedShape tess = tessellateShape(shapeDef->shape);

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

}  // namespace flash3ds::renderer
