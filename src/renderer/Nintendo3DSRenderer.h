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
// source-cross-checked convention. It has NOT been verified against real
// hardware or an emulator
// in this session — the from-source toolchain bootstrap (docs/3ds-
// toolchain.md) produces a linkable, packageable .3dsx, but no 3DS
// hardware/emulator was available to actually run it and visually confirm
// the framebuffer orientation/byte order. Treat this as "implemented per
// public documentation" rather than "hardware-confirmed" until someone runs
// it on a Citra/real-3DS and checks.

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
// a narrower touch-enabled bottom screen; flash3ds-runtime's Phase 10 scope
// targets the top screen only (bottom-screen use — e.g. a virtual keyboard
// or debug overlay — is an explicit follow-up, not implemented here).
class Nintendo3DSRenderer : public IRenderer {
public:
    // widthPixels/heightPixels describe the LOGICAL (SWF stage) render
    // surface, in the usual non-rotated width-by-height sense — e.g. 400x240
    // for the 3DS top screen's native resolution. The SoftwareRenderer used
    // internally is sized to exactly this; endFrame() handles the
    // rotated-framebuffer transposition when blitting to the real screen.
    Nintendo3DSRenderer(int widthPixels, int heightPixels, gfxScreen_t screen = GFX_TOP);

    void beginFrame(swf::RgbaColor backgroundColor) override;
    void endFrame() override;

    void fillPolygon(const std::vector<PointTwips>& devicePoints, swf::RgbaColor color) override;
    void strokePolyline(const std::vector<PointTwips>& devicePoints, swf::RgbaColor color,
                         int widthPixels) override;

    int width() const { return software_.width(); }
    int height() const { return software_.height(); }

private:
    SoftwareRenderer software_;
    gfxScreen_t screen_;
};

}  // namespace flash3ds::renderer
