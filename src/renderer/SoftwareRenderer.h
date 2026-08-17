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
    void strokePolyline(const std::vector<PointTwips>& devicePoints, swf::RgbaColor color,
                         int widthPixels) override;

    int width() const { return width_; }
    int height() const { return height_; }

    // Raw RGBA8 pixel access (row-major, top-to-bottom), e.g. for unit
    // tests that want to sample specific pixels without going through a
    // file round-trip.
    swf::RgbaColor pixelAt(int x, int y) const;

    // Writes the current framebuffer as a binary PPM (P6 — RGB, alpha
    // dropped) to `path`. Returns false if the file couldn't be opened for
    // writing.
    bool writePpm(const std::string& path) const;

private:
    void setPixel(int x, int y, swf::RgbaColor color);
    // Alpha-blends `color` over whatever is currently at (x, y). Used by
    // both fillPolygon and strokePolyline so partially-transparent fills/
    // strokes (alpha < 255) composite instead of clobbering.
    void blendPixel(int x, int y, swf::RgbaColor color);

    int width_ = 0;
    int height_ = 0;
    std::vector<swf::RgbaColor> pixels_;  // size width_ * height_
};

}  // namespace flash3ds::renderer
