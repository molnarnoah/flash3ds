// IRenderer.h
//
// Abstract pixel-output interface. Deliberately minimal and NOT coupled to
// any particular graphics API (no OpenGL/OpenGL ES/citro3d types appear
// here) so the same SceneRenderer walk can drive a desktop SoftwareRenderer
// today and a Nintendo3DSRenderer later (Phase 10) without changes to
// scene-graph code. See docs/renderer.md.

#pragma once

#include <array>
#include <cstdint>

#include "renderer/ShapeTessellator.h"
#include "swf/ShapeRecords.h"
#include "swf/SwfRecords.h"

namespace flash3ds::renderer {

// A device-pixel-space-ready linear gradient fill (2026-08-28, graphics/
// gradients task — see docs/renderer.md's "Gradient rendering" section for
// the full design writeup). Everything here is already resolved by the
// caller (SceneRenderer) into a form fillPolygonGradient() can sample
// per-pixel with no further matrix/color-transform work: a 256-entry color
// ramp (already ColorTransform-applied, so the implementation doesn't need
// to know about ColorTransform at all — same division of responsibility as
// fillPolygon()'s already-transformed `color` parameter), and an affine
// transform mapping a DEVICE pixel coordinate directly back into the SWF
// gradient square's own -16384..16384 coordinate space:
//   gx = a*px + c*py + tx
//   gy = b*px + d*py + ty
// (linear gradients only vary along gx; gy is carried for a future radial
// implementation, see below). Only FillStyleType::kLinearGradient is
// resolved into one of these — real hobo.swf content (2990 shapes scanned,
// /tmp/gradient_scan.cpp, 2026-08-28) has 161 linear gradient fills and
// ZERO radial/focal-radial/bitmap fills, so — following this project's
// established evidence-driven-scope discipline (see CLAUDE.md's Roadmap
// Phase 8/9 entries: Math-only, DefineMorphShape-v1-only) — radial/focal-
// radial gradients and bitmap fills are deliberately left on the existing
// flat-averaged-color fallback rather than being implemented against zero
// corpus evidence.
struct DeviceGradientFill {
    std::array<swf::RgbaColor, 256> ramp{};
    swf::GradientSpreadMode spreadMode = swf::GradientSpreadMode::kPad;
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, tx = 0.0, ty = 0.0;
};

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

    // Fills a closed polygon (same device pixel-space contract as
    // fillPolygon()) with a real per-pixel linear gradient instead of a
    // flat color — see DeviceGradientFill's own doc comment above for what
    // the caller has already resolved. A deliberately separate entry point
    // (not a `color OR gradient` variant of fillPolygon()) so the existing,
    // already-tuned flat-fill hot path (see SoftwareRenderer.cpp's
    // fillPolygon()/fillSpan() performance-fix history) is never touched by
    // this addition.
    virtual void fillPolygonGradient(const std::vector<PointTwips>& devicePoints,
                                      const DeviceGradientFill& fill) = 0;

    // Draws an open polyline in device pixel-space with the given color and
    // approximate width (in device pixels).
    virtual void strokePolyline(const std::vector<PointTwips>& devicePoints,
                                 swf::RgbaColor color, int widthPixels) = 0;
};

}  // namespace flash3ds::renderer
