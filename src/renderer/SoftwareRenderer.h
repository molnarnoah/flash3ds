// SoftwareRenderer.h
//
// A plain CPU rasterizer: an RGBA8 framebuffer, even-odd scanline polygon
// fill, a naive line-stroke rasterizer (Bresenham-ish, no anti-aliasing or
// caps/joins), and a PPM (P6) file writer for inspecting output without a
// display. This is the desktop/testing IRenderer implementation; the future
// Nintendo3DSRenderer (Phase 10) is a separate, unrelated implementation of
// the same interface.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "renderer/IRenderer.h"

namespace flash3ds::renderer {

class SoftwareRenderer : public IRenderer {
public:
    SoftwareRenderer(int widthPixels, int heightPixels);

    void beginFrame(swf::RgbaColor backgroundColor) override;
    void endFrame() override;

    void fillPolygon(const std::vector<PointTwips>& devicePoints, swf::RgbaColor color) override;

    // Graphics/gradients task (2026-08-28) — see IRenderer.h's
    // DeviceGradientFill doc comment. Deliberately a full, independent
    // copy of fillPolygon()'s active-edge-table scanline loop (see
    // fillPolygonGradient()'s own comment in the .cpp for why), not a
    // shared-code refactor — the flat-fill path below is this project's
    // measured, tuned hot path (see its own performance-fix comments) and
    // this addition must not risk it.
    void fillPolygonGradient(const std::vector<PointTwips>& devicePoints,
                              const DeviceGradientFill& fill) override;

    void strokePolyline(const std::vector<PointTwips>& devicePoints, swf::RgbaColor color,
                         int widthPixels) override;

    int width() const { return width_; }
    int height() const { return height_; }

    // Raw RGBA8 pixel access (row-major, top-to-bottom), e.g. for unit
    // tests that want to sample specific pixels without going through a
    // file round-trip.
    swf::RgbaColor pixelAt(int x, int y) const;

    // Performance fix (2026-08-28, "resolve the 7-12 FPS pacing" task,
    // continuing after the span-fill fix brought raster's share of the
    // frame down to ~17% and made `blit` -- Nintendo3DSRenderer::
    // endFrame()'s per-pixel copy into the real LCD framebuffer -- one of
    // the two largest remaining costs, see docs/performance-pacing.md):
    // same pattern as fillSpan() -- a bounds-check-free read for a caller
    // that has ALREADY established x/y are in range by construction, not
    // per-call. endFrame()'s blit loop computes its iteration bounds as
    // `std::min(srcW, ...)`/`std::min(srcH, ...)`, so every (x, y) it
    // passes here is provably within [0, width_) x [0, height_) before
    // the loop even starts -- pixelAt()'s bounds check on every one of
    // up to 96,000 pixels/frame was therefore pure waste for that call
    // site. Returns a reference (not pixelAt()'s by-value copy) since the
    // caller only reads it once per pixel; UB if x/y are actually
    // out-of-bounds, which is exactly why this is a separate, clearly
    // documented method rather than pixelAt() itself relaxing its
    // contract.
    const swf::RgbaColor& pixelAtUnchecked(int x, int y) const {
        return pixels_[static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)];
    }

    // Writes the current framebuffer as a binary PPM (P6 — RGB, alpha
    // dropped) to `path`. Returns false if the file couldn't be opened for
    // writing.
    bool writePpm(const std::string& path) const;

    // Diagnostic instrumentation (2026-08-28, "resolve the 7-12 FPS
    // pacing" task -- see docs/performance-pacing.md): after the
    // active-edge-table fix to fillPolygon() only produced a modest
    // improvement (raster's share of the frame dropped ~46%->~41%, not
    // the dramatic win expected), the next hypothesis is that
    // blendPixel()'s per-pixel COST -- not the edge-testing that fix
    // targeted -- is the larger remaining factor: every alpha<255 pixel
    // write does three integer divisions (see blendChannel()), against a
    // single direct write for a fully opaque one. These counters let a
    // caller find out, for the MOST RECENTLY COMPLETED frame (reset in
    // beginFrame(), same lifecycle as Nintendo3DSRenderer's
    // lastRasterMs()/lastBlitMs()), how many actual pixel writes went
    // through each path -- measuring the composition of the workload
    // before guessing at a fix for it, the same discipline every fix in
    // this task has followed. A fully-transparent write (alpha==0, a
    // real early-out in blendPixel()) counts as neither -- it never
    // touches the framebuffer at all.
    size_t lastOpaquePixelWrites() const { return opaquePixelWrites_; }
    size_t lastBlendedPixelWrites() const { return blendedPixelWrites_; }

private:
    void setPixel(int x, int y, swf::RgbaColor color);
    // Alpha-blends `color` over whatever is currently at (x, y). Used by
    // both fillPolygon and strokePolyline so partially-transparent fills/
    // strokes (alpha < 255) composite instead of clobbering.
    void blendPixel(int x, int y, swf::RgbaColor color);

    // Performance fix (2026-08-28, "resolve the 7-12 FPS pacing" task --
    // see docs/performance-pacing.md's pixel-write-counter measurement:
    // ~146-148K opaque writes per frame, 0.4% blended): fillPolygon()'s
    // scanline loop already computes and clamps a whole [xStart, xEnd]
    // span before filling it, but was then calling blendPixel() once per
    // pixel in that span -- which re-checks bounds itself, then (for the
    // opaque case) calls setPixel(), which checks bounds AGAIN -- two
    // fully redundant bounds checks per pixel for a range already known
    // to be in-bounds. fillSpan() fills a whole pre-clamped span at once:
    // a single std::fill() for a fully-opaque color (no per-pixel branch
    // at all), or a bounds-check-free per-pixel blend loop otherwise.
    // Callers MUST guarantee xStart/xEnd are already clamped to
    // [0, width_-1] and y to [0, height_-1] -- exactly what
    // fillPolygon()'s scanline loop already establishes once per row/span
    // before calling this. NOT used by strokePolyline(), whose
    // stamped-square points aren't pre-clamped the same way -- it keeps
    // calling blendPixel() per point, unchanged.
    void fillSpan(int y, int xStart, int xEnd, swf::RgbaColor color);

    // Gradient counterpart to fillSpan() — same pre-clamped-bounds contract
    // (caller guarantees xStart/xEnd in [0, width_-1], y in [0, height_-1]),
    // but samples a per-pixel color from `fill`'s ramp instead of writing
    // one flat color, so there's no bulk std::fill() fast path available.
    // See fillPolygonGradient()'s .cpp comment for the incremental
    // gradient-space stepping this uses.
    void fillSpanGradient(int y, int xStart, int xEnd, const DeviceGradientFill& fill);

    int width_ = 0;
    int height_ = 0;
    std::vector<swf::RgbaColor> pixels_;  // size width_ * height_

    // See lastOpaquePixelWrites()/lastBlendedPixelWrites() above.
    size_t opaquePixelWrites_ = 0;
    size_t blendedPixelWrites_ = 0;
};

}  // namespace flash3ds::renderer
