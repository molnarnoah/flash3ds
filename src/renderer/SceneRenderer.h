// SceneRenderer.h
//
// Walks a MovieClipInstance tree (see runtime/MovieClipInstance.h, Phase 5)
// and draws it into an IRenderer: resolves each display-list entry's
// characterId via CharacterDictionary, composes world transforms
// (concatMatrix, parent-then-child per SWF's nested coordinate space
// convention), tessellates shape characters on the fly, and recurses into
// child MovieClipInstances at THEIR OWN (possibly script-mutated)
// localMatrix()/colorTransform(), skipping any that are !visible().
//
// Phase 3 rendered sprites by walking a shared, per-CHARACTER cached
// Timeline — every placed instance of a given sprite character rendered
// identically, with no independent playhead. Phase 5 replaces that with the
// actual MovieClipInstance tree, so two instances of the same sprite
// character can be on different frames, at different _x/_y/_alpha, etc.,
// and the renderer reflects it correctly.

#pragma once

#include "renderer/IRenderer.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/DisplayList.h"
#include "runtime/MovieClipInstance.h"
#include "runtime/Movie.h"

namespace flash3ds::renderer {

class SceneRenderer {
public:
    SceneRenderer(const runtime::Movie& movie, const runtime::CharacterDictionary& characters);

    // Renders `root`'s current state into `target`, mapping the movie's
    // stage bounds (Movie::frameSize, in twips) onto a `outputWidthPixels`
    // x `outputHeightPixels` device viewport. Calls target.beginFrame()/
    // endFrame() itself — the caller should not.
    void render(const runtime::MovieClipInstance& root, IRenderer& target, int outputWidthPixels,
                int outputHeightPixels);

private:
    void renderClip(const runtime::MovieClipInstance& clip, const swf::Matrix& worldMatrix,
                     IRenderer& target, double pixelsPerTwipX, double pixelsPerTwipY, int depth);

    void renderShapeCharacter(uint16_t characterId, const swf::Matrix& worldMatrix,
                               IRenderer& target, double pixelsPerTwipX, double pixelsPerTwipY);

    const runtime::Movie* movie_;
    const runtime::CharacterDictionary* characters_;

    // Guards against a malformed/cyclic sprite reference (a sprite that
    // directly or indirectly places itself) recursing forever.
    static constexpr int kMaxRecursionDepth = 64;
};

}  // namespace flash3ds::renderer
