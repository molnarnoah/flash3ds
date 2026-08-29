// Nintendo3DSRenderer.h
//
// Phase 10 — Nintendo 3DS backend. IRenderer implementation for real 3DS
// hardware (or an emulator exposing the same libctru gfx API).
//
// Implementation strategy: wrap a SoftwareRenderer by composition and reuse
// its already-tested CPU rasterizer (fillPolygon/strokePolyline/beginFrame)
// unchanged, then in endFrame() blit the resulting RGBA8 framebuffer into
// the real 3DS LCD framebuffer obtained via libctru's gfxGetFramebuffer().
//
// This file is only compiled for the 3DS target (guarded by __3DS__, set by
// the ARM cross-compile invocation — see cmake/Toolchain-3DS.cmake) and is
// never linked into the desktop build. It is written against libctru's
// public <3ds.h> API only — no code from Shift-DX/gameswf/code.bin is used
// or consulted anywhere in this file (clean-room, per project CLAUDE.md).
//
// Framebuffer layout note (IMPORTANT — confidence-flagged per project
// convention): the 3DS LCD framebuffer is physically rotated 90 degrees and
// stored column-major. This is a well-documented libctru/homebrew
// convention; the exact indexing was cross-checked against libctru's own
// source (source/gfx.c) rather than assumed: gfxGetFramebuffer's width/
// height out-params are in PHYSICAL (rotated) terms — for the top screen,
// width=GSP_SCREEN_WIDTH=240 (used as the per-column byte stride) and
// height=GSP_SCREEN_HEIGHT_TOP=400 (bounds the logical x) — giving the
// indexing formula `(x * fbWidth + (fbWidth - 1 - y)) * bytesPerPixel`. The
// default pixel format (gfxInitDefault()) is GSP_BGR8_OES (3 bytes per
// pixel, B-G-R byte order). This implementation follows that
// source-cross-checked convention. The user confirmed the Phase 10 .3dsx
// boots and runs in Azahar (a Citra-based 3DS emulator) — the toolchain/
// link/package pipeline is hardware-emulator-confirmed working end to end.
// Pixel-exact confirmation of THIS formula specifically (right orientation,
// right colors, no off-by-one row/column) for the newer dual-screen/
// button/sound test content below has not been separately reported yet —
// treat that specific claim as "implemented per public documentation,
// booted successfully" rather than "visually pixel-confirmed" until
// explicitly checked against what's on screen.
//
// Multi-screen note: as of the dual-screen test app, more than one
// Nintendo3DSRenderer instance may be active per real frame (one per
// screen). gfxFlushBuffers()/gfxSwapBuffers() are GLOBAL, both-screens
// operations in libctru (confirmed via source/gfx.c) — endFrame() only
// blits pixels into the framebuffer; the caller MUST call the static
// presentFrame() exactly once per real frame, after every active
// renderer's endFrame() has run, or screens will flicker/show stale
// content (see presentFrame()'s own comment for the mechanism).

#pragma once

#ifndef __3DS__
#error "Nintendo3DSRenderer.h is only valid in a 3DS cross-compile (__3DS__ not defined)"
#endif

#include <3ds.h>

#include <memory>

#include "renderer/IRenderer.h"
#include "renderer/SoftwareRenderer.h"

namespace flash3ds::renderer {

// Which physical LCD screen to render to. The 3DS has a wide top screen and
// a narrower touch-enabled bottom screen; instantiate one Nintendo3DSRenderer
// per screen (GFX_TOP / GFX_BOTTOM) to drive both at once — see
// nintendo3ds_main.cpp for the dual-screen test app doing exactly this.
class Nintendo3DSRenderer : public IRenderer {
public:
    // widthPixels/heightPixels describe the LOGICAL (SWF stage) render
    // surface, in the usual non-rotated width-by-height sense — e.g. 400x240
    // for the 3DS top screen's native resolution, or 320x240 for the bottom
    // screen. The SoftwareRenderer used internally is sized to exactly
    // this; endFrame() handles the rotated-framebuffer transposition when
    // blitting to the real screen.
    Nintendo3DSRenderer(int widthPixels, int heightPixels, gfxScreen_t screen = GFX_TOP);

    void beginFrame(swf::RgbaColor backgroundColor) override;
    void endFrame() override;

    void fillPolygon(const std::vector<PointTwips>& devicePoints, swf::RgbaColor color) override;
    void fillPolygonGradient(const std::vector<PointTwips>& devicePoints,
                              const DeviceGradientFill& fill) override;
    void strokePolyline(const std::vector<PointTwips>& devicePoints, swf::RgbaColor color,
                         int widthPixels) override;

    int width() const { return software_.width(); }
    int height() const { return software_.height(); }

    // Sub-phase timing (2026-08-28, continuing the "resolve the 7-12 FPS
    // pacing" task after the tessellation-cache fix showed no on-device
    // improvement -- see docs/performance-pacing.md). The main loop's
    // single "renderTop" measurement wraps the ENTIRE
    // SceneRenderer::render() call, which per its own doc comment calls
    // beginFrame()/endFrame() on this renderer itself -- so that one
    // number bundles together three very different costs: the
    // MovieClipInstance tree walk + (now-cached) tessellation lookups,
    // SoftwareRenderer's CPU scanline-fill rasterization (every
    // fillPolygon()/strokePolyline() call SceneRenderer makes while
    // walking the tree), and this class's own endFrame() blit loop
    // (copying the finished software-rendered buffer into the real/
    // emulated 3DS LCD framebuffer, one function call + 3 byte writes per
    // pixel, up to 400x240 times). These two getters expose the latter
    // two directly so the caller can subtract them out of its own
    // "renderTop" total and see what's actually left for the tree walk.
    // Both reflect the MOST RECENTLY COMPLETED frame only (rasterMs_ is
    // reset and re-accumulated in beginFrame(); blitMs_ is overwritten
    // fresh every endFrame()) -- read them right after the render() call
    // they're timing, not later.
    double lastRasterMs() const { return rasterMs_; }
    double lastBlitMs() const { return blitMs_; }

    // Presents everything blitted into every active screen's framebuffer
    // since the last call. Call exactly ONCE per real frame, after every
    // active Nintendo3DSRenderer's endFrame() has already run this frame
    // — see the file header's "Multi-screen note" for why this is static/
    // global rather than a per-instance operation.
    static void presentFrame();

private:
    SoftwareRenderer software_;
    gfxScreen_t screen_;

    // See lastRasterMs()/lastBlitMs() above for what these measure and why.
    double rasterMs_ = 0.0;
    double blitMs_ = 0.0;
};

}  // namespace flash3ds::renderer
