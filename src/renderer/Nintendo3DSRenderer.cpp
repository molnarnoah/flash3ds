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
    software_.beginFrame(backgroundColor);
}

void Nintendo3DSRenderer::fillPolygon(const std::vector<PointTwips>& devicePoints,
                                       swf::RgbaColor color) {
    software_.fillPolygon(devicePoints, color);
}

void Nintendo3DSRenderer::strokePolyline(const std::vector<PointTwips>& devicePoints,
                                          swf::RgbaColor color, int widthPixels) {
    software_.strokePolyline(devicePoints, color, widthPixels);
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

    gfxFlushBuffers();
    gfxSwapBuffers();
}

}  // namespace flash3ds::renderer
