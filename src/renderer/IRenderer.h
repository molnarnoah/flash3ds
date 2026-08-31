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

// A device-pixel-space-ready bitmap fill (2026-08-31, Priority Fix List
// item #2 — see renderer/ShapeTessellator.h's BitmapPaint doc comment and
// swf/DefineBitsTag.h for the full design). Mirrors DeviceGradientFill's
// own shape closely: everything here is already resolved by the caller
// (SceneRenderer) so the implementation doesn't need to know about
// FillStyle/CharacterDictionary at all — an affine transform mapping a
// DEVICE pixel coordinate back into the bitmap's own PIXEL space (not the
// "20 twips per source pixel" space BitmapPaint's own matrix uses —
// SceneRenderer folds that /20 scale into this transform, so the
// implementation just does `px = a*devX + c*devY + tx` and reads pixel
// (round(px), round(py)) directly, no further scaling), plus a non-owning
// pointer to the RAW (NOT ColorTransform-applied — see `colorTransform`
// below) RGBA8 pixel data owned by the CharacterDictionary's cached
// BitmapDef (which outlives any single render() call), and its width/
// height for bounds-checking/wrapping. `repeat` selects whether an
// out-of-[0,width)x[0,height) sample coordinate wraps
// (kRepeatingBitmap/kNonSmoothedRepeatingBitmap) or clamps to the nearest
// edge pixel (k*ClippedBitmap) — never "paints nothing", matching every
// real player's behavior for a clipped bitmap fill (the *shape*, not the
// bitmap, is what bounds the visible fill region; a clipped bitmap still
// covers its whole shape, just by stretching its edge pixels rather than
// tiling). `smoothed` is currently unused by every IRenderer implementation
// (see SoftwareRenderer.cpp's fillPolygonBitmap() for why: this renderer
// has no texture-filtering precedent anywhere else either — flat and
// gradient fills are already sampled at exact/interpolated-but-not-
// filtered precision — so bilinear sampling for the "smoothed" variants is
// left as a documented future refinement rather than implemented without
// an established convention to match); it's still carried through from
// BitmapPaint in case a future pass adds it.
//
// `colorTransform`, unlike DeviceGradientFill's own ramp (which has
// ColorTransform baked into all 256 entries up front, a fixed cost), is
// applied by the implementation PER SAMPLED pixel instead — a source
// bitmap can have far more distinct colors than a 256-stop ramp (a real
// photo easily has tens of thousands), so pre-transforming the whole
// source buffer up front would cost proportional to the BITMAP's size
// every time it's resolved, where applying it lazily at sample time costs
// proportional to how many PIXELS actually get PAINTED — the same cost
// class every other paint kind already pays (one applyColorTransform()
// call per output pixel, not per source-data byte).
struct DeviceBitmapFill {
    const swf::RgbaColor* pixels = nullptr;  // row-major, top-to-bottom, width*height entries, raw
    int width = 0;
    int height = 0;
    bool repeat = true;
    bool smoothed = true;
    swf::ColorTransform colorTransform;  // default-constructed == identity; see above
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

    // Combined even-odd fill across MULTIPLE closed contours that share one
    // logical fill region (2026-08-31, Priority Fix List item #1 — hole/
    // counter rendering; see ShapeTessellator.h's TessellatedPolygon::
    // fillGroupId doc comment and docs/renderer.md for the full writeup).
    // SWF shapes can describe a single fill style as several disjoint
    // MoveTo-bounded pen runs within one shape (e.g. the letter "O": an
    // outer boundary contour plus an inner counter contour, both under the
    // same FillStyle) — filling each contour independently and fully (what
    // repeated fillPolygon() calls would do) renders the counter as another
    // solid patch instead of a transparent cutout. This entry point fills
    // every contour's edges TOGETHER in one combined even-odd scanline
    // pass, so a point covered by an odd number of contours is filled and a
    // point covered by an even number (e.g. inside both the outer boundary
    // and the inner counter) is left as background — producing the correct
    // hole. Every `contours` entry is itself in the same device pixel-space
    // as fillPolygon()'s `devicePoints`.
    virtual void fillPolygonGroup(const std::vector<std::vector<PointTwips>>& contours,
                                   swf::RgbaColor color) = 0;

    // Gradient counterpart to fillPolygonGroup() — same combined-even-odd
    // contract, sampling per-pixel from `fill` instead of a flat color. A
    // deliberately separate entry point (not a `color OR gradient` variant),
    // matching the existing fillPolygon()/fillPolygonGradient() split and
    // for the same reason: never risk the tuned flat-fill hot path.
    virtual void fillPolygonGradientGroup(const std::vector<std::vector<PointTwips>>& contours,
                                           const DeviceGradientFill& fill) = 0;

    // Bitmap counterpart to fillPolygon()/fillPolygonGradient() (2026-08-31,
    // Priority Fix List item #2) — same device pixel-space contract as
    // fillPolygon(), sampling a per-pixel color from `fill`'s decoded
    // bitmap instead of a flat color or gradient ramp. See
    // DeviceBitmapFill's own doc comment above for what the caller has
    // already resolved. A deliberately separate entry point (not folded
    // into fillPolygonGradient() as a third paint variant), matching the
    // existing flat/gradient split and for the same reason: never risk the
    // tuned flat-fill hot path, and keep each paint kind's implementation
    // independently reasoned-about.
    virtual void fillPolygonBitmap(const std::vector<PointTwips>& devicePoints,
                                    const DeviceBitmapFill& fill) = 0;

    // Bitmap counterpart to fillPolygonGroup()/fillPolygonGradientGroup() —
    // same combined-contour, combined-even-odd contract as those two, for
    // a bitmap-filled shape whose fill forms multiple disjoint contours
    // (e.g. a hole/counter under a bitmap fill instead of a flat/gradient
    // one).
    virtual void fillPolygonBitmapGroup(const std::vector<std::vector<PointTwips>>& contours,
                                         const DeviceBitmapFill& fill) = 0;
};

}  // namespace flash3ds::renderer
