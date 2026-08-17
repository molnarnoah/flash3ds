// SceneRenderer.h
//
// Walks a Timeline's current DisplayList and draws it into an IRenderer:
// resolves each entry's characterId via CharacterDictionary, composes world
// transforms (concatMatrix, parent-then-child per SWF's nested coordinate
// space convention), tessellates shape characters on the fly, and recurses
// into DefineSprite characters (nested display lists) at their placement
// transform.
//
// KNOWN PHASE 3 LIMITATION: sprite (MovieClip) instances do not yet have an
// independent playhead — Phase 3 has no AVM1/frame-advance model per
// instance, only per top-level Movie. Every placed instance of a given
// sprite character currently renders that character's Timeline at whatever
// frame it's on (frame 1 initially, and shared across all instances of the
// same character). Proper per-instance MovieClip state arrives with the
// AVM1 VM and MovieClip API (Phase 4/5) — see docs/renderer.md.

#pragma once

#include <memory>
#include <unordered_map>

#include "renderer/IRenderer.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/DisplayList.h"
#include "runtime/Movie.h"
#include "runtime/Timeline.h"

namespace flash3ds::renderer {

class SceneRenderer {
public:
    SceneRenderer(const runtime::Movie& movie, const runtime::CharacterDictionary& characters);

    // Renders `timeline`'s current frame (timeline.displayList()) into
    // `target`, mapping the movie's stage bounds (Movie::frameSize, in
    // twips) onto a `outputWidthPixels` x `outputHeightPixels` device
    // viewport. Calls target.beginFrame()/endFrame() itself — the caller
    // should not.
    void render(const runtime::Timeline& timeline, IRenderer& target, int outputWidthPixels,
                int outputHeightPixels);

private:
    void renderDisplayList(const runtime::DisplayList& displayList,
                            const swf::Matrix& parentWorldMatrix, IRenderer& target,
                            double pixelsPerTwipX, double pixelsPerTwipY, int depth);

    void renderCharacterInstance(uint16_t characterId, const swf::Matrix& worldMatrix,
                                  IRenderer& target, double pixelsPerTwipX,
                                  double pixelsPerTwipY, int depth);

    const runtime::Movie* movie_;
    const runtime::CharacterDictionary* characters_;

    // Lazily-built, cached Timelines for DefineSprite characters — see the
    // per-instance-playhead limitation documented above.
    std::unordered_map<uint16_t, std::unique_ptr<runtime::Timeline>> spriteTimelines_;

    // Guards against a malformed/cyclic sprite reference (a sprite that
    // directly or indirectly places itself) recursing forever.
    static constexpr int kMaxRecursionDepth = 64;
};

}  // namespace flash3ds::renderer
