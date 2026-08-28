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

    for (int y = 0; y < blitH; ++y) {
        for (int x = 0; x < blitW; ++x) {
            const swf::RgbaColor px = software_.pixelAt(x, y);
            // Documented rotated/column-major indexing formula (see header
            // comment and the fbWidth/fbHeight note above) — physical row
            // index within the destination column runs bottom-to-top
            // relative to our logical top-to-bottom y.
            const int physIndex =
                (x * static_cast<int>(fbWidth) + (static_cast<int>(fbWidth) - 1 - y)) *
                kBytesPerPixel;
            fb[physIndex + 0] = px.b;
            fb[physIndex + 1] = px.g;
            fb[physIndex + 2] = px.r;
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
