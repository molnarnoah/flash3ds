// Nintendo3DSRenderer.cpp
//
// See Nintendo3DSRenderer.h for the framebuffer-orientation confidence note
// (documented libctru convention, not hardware/emulator-verified in this
// session).

#include "renderer/Nintendo3DSRenderer.h"

#include <algorithm>

namespace flash3ds::renderer {

Nintendo3DSRenderer::Nintendo3DSRenderer(int widthPixels, int heightPixels, gfxScreen_t screen)
    : software_(widthPixels, heightPixels), screen_(screen) {}

void Nintendo3DSRenderer::beginFrame(swf::RgbaColor backgroundColor) {
    // Reset the rasterization accumulator here, not in the constructor --
    // SceneRenderer::render() calls beginFrame() itself once per real
    // frame (see this class's header), so this is exactly "start of a new
    // frame's fillPolygon()/strokePolyline() cost tally." blitMs_ isn't
    // reset here since endFrame() always overwrites it fresh below rather
    // than accumulating.
    rasterMs_ = 0.0;
    software_.beginFrame(backgroundColor);
}

void Nintendo3DSRenderer::fillPolygon(const std::vector<PointTwips>& devicePoints,
                                       swf::RgbaColor color) {
    TickCounter t;
    osTickCounterStart(&t);
    software_.fillPolygon(devicePoints, color);
    osTickCounterUpdate(&t);
    rasterMs_ += osTickCounterRead(&t);
}

void Nintendo3DSRenderer::fillPolygonGradient(const std::vector<PointTwips>& devicePoints,
                                               const DeviceGradientFill& fill) {
    // Same TickCounter/rasterMs_ accumulation pattern as fillPolygon() above
    // — a gradient fill is still raster work for pacing-measurement
    // purposes, and this project's PhaseTimingWindow HUD bars (see
    // docs/performance-pacing.md) shouldn't silently miss it.
    TickCounter t;
    osTickCounterStart(&t);
    software_.fillPolygonGradient(devicePoints, fill);
    osTickCounterUpdate(&t);
    rasterMs_ += osTickCounterRead(&t);
}

void Nintendo3DSRenderer::strokePolyline(const std::vector<PointTwips>& devicePoints,
                                          swf::RgbaColor color, int widthPixels) {
    TickCounter t;
    osTickCounterStart(&t);
    software_.strokePolyline(devicePoints, color, widthPixels);
    osTickCounterUpdate(&t);
    rasterMs_ += osTickCounterRead(&t);
}

void Nintendo3DSRenderer::endFrame() {
    // Pull the finished CPU-rasterized frame out of the SoftwareRenderer and
    // blit it into the real LCD framebuffer. gfxGetFramebuffer gives us the
    // CURRENT (non-visible, being-drawn-into) buffer for the requested
    // screen/side; gfxSwapBuffers() at the end presents it.
    u16 fbWidth = 0, fbHeight = 0;
    u8* fb = gfxGetFramebuffer(screen_, GFX_LEFT, &fbWidth, &fbHeight);
    if (!fb) {
        // No framebuffer available (e.g. gfx not initialized by the caller).
        // Nothing sane to do here; skip the blit rather than crash.
        return;
    }

    const int srcW = software_.width();
    const int srcH = software_.height();
    // libctru's gfxGetFramebuffer width/height out-params are in PHYSICAL
    // (rotated, column-major) terms, NOT logical screen terms: confirmed by
    // reading libctru's own gfx.c — for the top screen it reports
    // width=GSP_SCREEN_WIDTH=240 (the short/physical dimension, used below
    // as the per-column byte stride) and height=GSP_SCREEN_HEIGHT_TOP=400
    // (the long/physical dimension, which bounds our LOGICAL x). That is:
    // our logical x (0..~400) is bounded by fbHeight, and our logical y
    // (0..~240) is bounded by fbWidth — this is intentional, not swapped.
    const int blitW = std::min(srcW, static_cast<int>(fbHeight));
    const int blitH = std::min(srcH, static_cast<int>(fbWidth));

    constexpr int kBytesPerPixel = 3;  // default GSP_BGR8_OES format

    TickCounter blitTimer;
    osTickCounterStart(&blitTimer);

    // Performance fix (2026-08-28, "resolve the 7-12 FPS pacing" task,
    // continuing after the span-fill fix -- see docs/performance-pacing.md):
    // two changes to this loop, both reasoned from the indexing formula
    // itself (this file is 3DS-only, so unlike every prior fix in this
    // task there is no desktop MD5 check available -- correctness here
    // rests on this reasoning plus the next on-device recording, not on
    // an automated test).
    //
    // 1. pixelAt() -> pixelAtUnchecked(): blitW/blitH are already
    //    std::min()'d against srcW/srcH above, so every (x, y) this loop
    //    visits is provably in [0, srcW) x [0, srcH) before the loop
    //    starts -- pixelAt()'s own bounds check was pure redundant work
    //    on every one of up to 96,000 pixels/frame (same class of fix as
    //    fillSpan() in SoftwareRenderer.cpp).
    //
    // 2. Loop nesting swapped from (y outer, x inner) to (x outer, y
    //    inner). `physIndex = (x*fbWidth + (fbWidth-1-y)) * 3` means: for
    //    FIXED y, stepping x writes `fb` in fbWidth*3-byte STRIDES (the
    //    original nesting) -- but for FIXED x, stepping y writes `fb` at
    //    physIndex DECREASING by exactly 3 each step, i.e. sequentially.
    //    Writing the destination framebuffer contiguously matters more
    //    than reading the source contiguously: `fb` is display memory
    //    (likely write-combined/uncached, where a strided write pattern
    //    is much more expensive than a strided read from ordinary cached
    //    system RAM), and it's `fb`, not `pixels_`, whose access pattern
    //    this loop actually controls via its own nesting -- `pixels_` is
    //    read-only here regardless of which loop is outer. The pixel
    //    VALUES written are identical either way (each (x, y) still maps
    //    to the exact same physIndex via the exact same formula); only
    //    the order they're computed in changes, and every iteration
    //    writes a distinct location with no cross-iteration dependency,
    //    so reordering cannot change the final framebuffer contents.
    for (int x = 0; x < blitW; ++x) {
        int physIndex = (x * static_cast<int>(fbWidth) + (static_cast<int>(fbWidth) - 1)) * kBytesPerPixel;
        for (int y = 0; y < blitH; ++y) {
            const swf::RgbaColor& px = software_.pixelAtUnchecked(x, y);
            fb[physIndex + 0] = px.b;
            fb[physIndex + 1] = px.g;
            fb[physIndex + 2] = px.r;
            physIndex -= kBytesPerPixel;
        }
    }

    osTickCounterUpdate(&blitTimer);
    blitMs_ = osTickCounterRead(&blitTimer);

    // NOTE: deliberately does NOT call gfxFlushBuffers()/gfxSwapBuffers()
    // here — see presentFrame()'s comment below for why (both are GLOBAL,
    // both-screens operations in libctru, not per-screen; calling them once
    // per Nintendo3DSRenderer::endFrame() was correct back when only the
    // top screen was in use, one screen == one real frame, but breaks once
    // a second screen is active — confirmed by reading libctru's own
    // source/gfx.c while adding bottom-screen support).
}

void Nintendo3DSRenderer::presentFrame() {
    // gfxFlushBuffers() flushes the CPU data cache for BOTH screens'
    // current framebuffers (harmless/idempotent for a screen that wasn't
    // actually touched this frame — it just flushes unchanged cache
    // lines), and gfxSwapBuffers() calls gfxScreenSwapBuffers() for BOTH
    // GFX_TOP and GFX_BOTTOM unconditionally (verified directly in
    // libctru's source/gfx.c, not assumed). Calling either once per screen
    // per frame — i.e. once per Nintendo3DSRenderer::endFrame() — would
    // double-flip each screen's current/back buffer index every real
    // frame once two screens are both drawn per frame, which shows the
    // PREVIOUS frame's content (or worse, an interleaved mix) instead of
    // the one just rendered. Call this exactly once per real frame, after
    // every active Nintendo3DSRenderer's endFrame() has already run.
    gfxFlushBuffers();
    gfxSwapBuffers();
}

}  // namespace flash3ds::renderer
