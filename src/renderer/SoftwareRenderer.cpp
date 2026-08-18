#include "renderer/SoftwareRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace flash3ds::renderer {

namespace {

uint8_t blendChannel(uint8_t dst, uint8_t src, uint8_t alpha) {
    // Simple linear "over" compositing in integer math.
    return static_cast<uint8_t>((static_cast<int>(src) * alpha +
                                  static_cast<int>(dst) * (255 - alpha)) /
                                 255);
}

}  // namespace

SoftwareRenderer::SoftwareRenderer(int widthPixels, int heightPixels)
    : width_(std::max(0, widthPixels)),
      height_(std::max(0, heightPixels)),
      pixels_(static_cast<size_t>(width_) * static_cast<size_t>(height_)) {}

void SoftwareRenderer::beginFrame(swf::RgbaColor backgroundColor) {
    std::fill(pixels_.begin(), pixels_.end(), backgroundColor);
}

void SoftwareRenderer::endFrame() {
    // Nothing to do for a software framebuffer — present/swap is the
    // caller's concern (e.g. writePpm, or a future on-screen blit).
}

swf::RgbaColor SoftwareRenderer::pixelAt(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return swf::RgbaColor{0, 0, 0, 0};
    }
    return pixels_[static_cast<size_t>(y) * width_ + x];
}

void SoftwareRenderer::setPixel(int x, int y, swf::RgbaColor color) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    pixels_[static_cast<size_t>(y) * width_ + x] = color;
}

void SoftwareRenderer::blendPixel(int x, int y, swf::RgbaColor color) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    if (color.a == 255) {
        setPixel(x, y, color);
        return;
    }
    if (color.a == 0) return;
    swf::RgbaColor& dst = pixels_[static_cast<size_t>(y) * width_ + x];
    dst.r = blendChannel(dst.r, color.r, color.a);
    dst.g = blendChannel(dst.g, color.g, color.a);
    dst.b = blendChannel(dst.b, color.b, color.a);
    dst.a = static_cast<uint8_t>(std::min(255, static_cast<int>(dst.a) + color.a));
}

void SoftwareRenderer::fillPolygon(const std::vector<PointTwips>& devicePoints,
                                    swf::RgbaColor color) {
    if (devicePoints.size() < 3) return;

    int minY = static_cast<int>(devicePoints[0].y);
    int maxY = static_cast<int>(devicePoints[0].y);
    for (const auto& p : devicePoints) {
        // Explicit cast: PointTwips::y is int32_t, which is NOT guaranteed
        // to be the same type as `int` (they coincide on x86_64 desktop
        // builds but not on this project's ARM/newlib cross-compile
        // target -- see Timeline.cpp's gotoAndStop() for the same class of
        // portability issue with std::clamp).
        minY = std::min(minY, static_cast<int>(p.y));
        maxY = std::max(maxY, static_cast<int>(p.y));
    }
    minY = std::max(minY, 0);
    maxY = std::min(maxY, height_ - 1);

    size_t n = devicePoints.size();
    std::vector<double> intersections;

    for (int y = minY; y <= maxY; ++y) {
        double scanY = y + 0.5;  // sample at pixel center
        intersections.clear();

        for (size_t i = 0; i < n; ++i) {
            const PointTwips& a = devicePoints[i];
            const PointTwips& b = devicePoints[(i + 1) % n];  // edge back to points[0] is implicit
            double ay = a.y, by = b.y;
            if (ay == by) continue;  // horizontal edge contributes no crossing
            if ((scanY >= ay && scanY < by) || (scanY >= by && scanY < ay)) {
                double t = (scanY - ay) / (by - ay);
                double x = a.x + t * (b.x - a.x);
                intersections.push_back(x);
            }
        }

        if (intersections.empty()) continue;
        std::sort(intersections.begin(), intersections.end());

        // Even-odd fill: fill between each pair of consecutive crossings.
        for (size_t i = 0; i + 1 < intersections.size(); i += 2) {
            int xStart = static_cast<int>(std::lround(intersections[i]));
            int xEnd = static_cast<int>(std::lround(intersections[i + 1]));
            xStart = std::max(xStart, 0);
            xEnd = std::min(xEnd, width_ - 1);
            for (int x = xStart; x <= xEnd; ++x) {
                blendPixel(x, y, color);
            }
        }
    }
}

void SoftwareRenderer::strokePolyline(const std::vector<PointTwips>& devicePoints,
                                       swf::RgbaColor color, int widthPixels) {
    if (devicePoints.size() < 2) return;
    int halfWidth = std::max(0, widthPixels / 2);

    for (size_t i = 0; i + 1 < devicePoints.size(); ++i) {
        int x0 = devicePoints[i].x, y0 = devicePoints[i].y;
        int x1 = devicePoints[i + 1].x, y1 = devicePoints[i + 1].y;

        // Naive Bresenham; thickness is approximated by stamping a small
        // square at each plotted point rather than proper line-join
        // geometry. Sufficient for a "basic renderer" (Phase 3).
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;

        int x = x0, y = y0;
        while (true) {
            for (int oy = -halfWidth; oy <= halfWidth; ++oy) {
                for (int ox = -halfWidth; ox <= halfWidth; ++ox) {
                    blendPixel(x + ox, y + oy, color);
                }
            }
            if (x == x1 && y == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y += sy;
            }
        }
    }
}

bool SoftwareRenderer::writePpm(const std::string& path) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    std::fprintf(f, "P6\n%d %d\n255\n", width_, height_);
    std::vector<uint8_t> row(static_cast<size_t>(width_) * 3);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const swf::RgbaColor& p = pixels_[static_cast<size_t>(y) * width_ + x];
            row[static_cast<size_t>(x) * 3 + 0] = p.r;
            row[static_cast<size_t>(x) * 3 + 1] = p.g;
            row[static_cast<size_t>(x) * 3 + 2] = p.b;
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }

    std::fclose(f);
    return true;
}

}  // namespace flash3ds::renderer
