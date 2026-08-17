// IRenderer.h
//
// Abstract pixel-output interface. Deliberately minimal and NOT coupled to
// any particular graphics API (no OpenGL/OpenGL ES/citro3d types appear
// here) so the same SceneRenderer walk can drive a desktop SoftwareRenderer
// today and a Nintendo3DSRenderer later (Phase 10) without changes to
// scene-graph code. See docs/renderer.md.

#pragma once

#include <cstdint>

#include "renderer/ShapeTessellator.h"
#include "swf/SwfRecords.h"

namespace flash3ds::renderer {

class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Called once per rendered frame, before any draw calls, with the
    // background color to clear to (from the movie's background color, or
    // white if unknown).
    virtual void beginFrame(swf::RgbaColor backgroundColor) = 0;

    // Called once per rendered frame, after all draw calls.
    virtual void endFrame() = 0;

    // Fills a closed polygon given in DEVICE pixel-space coordinates
    // (already transformed and twips->pixels converted by the caller) with
    // a flat color, using a simple even-odd/nonzero scanline rule
    // (implementation-defined winding — shapes produced by ShapeTessellator
    // are simple, non-self-intersecting single contours, so the distinction
    // doesn't matter for current content).
    virtual void fillPolygon(const std::vector<PointTwips>& devicePoints,
                              swf::RgbaColor color) = 0;

    // Draws an open polyline in device pixel-space with the given color and
    // approximate width (in device pixels).
    virtual void strokePolyline(const std::vector<PointTwips>& devicePoints,
                                 swf::RgbaColor color, int widthPixels) = 0;
};

}  // namespace flash3ds::renderer
