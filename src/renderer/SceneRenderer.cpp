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

void SceneRenderer::render(const runtime::Timeline& timeline, IRenderer& target,
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

    renderDisplayList(timeline.displayList(), swf::Matrix::identity(), target, pixelsPerTwipX,
                       pixelsPerTwipY, 0);

    target.endFrame();
}

void SceneRenderer::renderDisplayList(const runtime::DisplayList& displayList,
                                       const swf::Matrix& parentWorldMatrix, IRenderer& target,
                                       double pixelsPerTwipX, double pixelsPerTwipY, int depth) {
    if (depth > kMaxRecursionDepth) {
        LOG_WARN("RENDER",
                  "Recursion depth limit (%d) exceeded while walking the display list — "
                  "possible cyclic sprite reference; stopping this branch",
                  kMaxRecursionDepth);
        return;
    }

    // DisplayList::entries() is a std::map<int32_t, ...>, so this iterates
    // in ascending depth order — exactly the back-to-front paint order the
    // SWF display model requires (lower depth = painted first/underneath).
    for (const auto& [depthValue, entry] : displayList.entries()) {
        (void)depthValue;
        swf::Matrix worldMatrix = swf::concatMatrix(parentWorldMatrix, entry.matrix);
        renderCharacterInstance(entry.characterId, worldMatrix, target, pixelsPerTwipX,
                                 pixelsPerTwipY, depth);
    }
}

void SceneRenderer::renderCharacterInstance(uint16_t characterId, const swf::Matrix& worldMatrix,
                                             IRenderer& target, double pixelsPerTwipX,
                                             double pixelsPerTwipY, int depth) {
    const runtime::CharacterDef* def = characters_->find(characterId);
    if (!def) {
        // Unresolved reference: either a character type we don't parse yet
        // (bitmap/text/button/font — Phase 8+) or malformed input. Nothing
        // to draw; not an error worth logging per-instance (would spam for
        // every frame of every unsupported character).
        return;
    }

    if (const auto* shapeDef = std::get_if<swf::ShapeDef>(def)) {
        TessellatedShape tess = tessellateShape(shapeDef->shape);

        for (const auto& poly : tess.polygons) {
            auto devicePoints = toDevicePolyline(poly.points, worldMatrix, pixelsPerTwipX,
                                                  pixelsPerTwipY);
            target.fillPolygon(devicePoints, poly.color);
        }
        for (const auto& stroke : tess.strokes) {
            auto devicePoints = toDevicePolyline(stroke.points, worldMatrix, pixelsPerTwipX,
                                                  pixelsPerTwipY);
            double avgPixelsPerTwip = (pixelsPerTwipX + pixelsPerTwipY) / 2.0;
            int widthPixels =
                std::max(1, static_cast<int>(std::lround(stroke.widthTwips * avgPixelsPerTwip)));
            target.strokePolyline(devicePoints, stroke.color, widthPixels);
        }
        return;
    }

    if (const auto* spriteDef = std::get_if<runtime::SpriteDef>(def)) {
        auto it = spriteTimelines_.find(characterId);
        if (it == spriteTimelines_.end()) {
            auto spriteTimeline = runtime::Timeline::build(*movie_, spriteDef->tags);
            it = spriteTimelines_.emplace(characterId, std::move(spriteTimeline)).first;
        }
        if (it->second) {
            renderDisplayList(it->second->displayList(), worldMatrix, target, pixelsPerTwipX,
                               pixelsPerTwipY, depth + 1);
        }
        return;
    }
}

}  // namespace flash3ds::renderer
