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
    // Start of a new frame's pixel-write tally -- see lastOpaquePixelWrites()/
    // lastBlendedPixelWrites()'s doc comment in the header.
    opaquePixelWrites_ = 0;
    blendedPixelWrites_ = 0;
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
        ++opaquePixelWrites_;
        return;
    }
    if (color.a == 0) return;  // no-op: never touches the framebuffer, doesn't count as a write
    swf::RgbaColor& dst = pixels_[static_cast<size_t>(y) * width_ + x];
    dst.r = blendChannel(dst.r, color.r, color.a);
    dst.g = blendChannel(dst.g, color.g, color.a);
    dst.b = blendChannel(dst.b, color.b, color.a);
    dst.a = static_cast<uint8_t>(std::min(255, static_cast<int>(dst.a) + color.a));
    ++blendedPixelWrites_;
}

void SoftwareRenderer::fillSpan(int y, int xStart, int xEnd, swf::RgbaColor color) {
    if (xStart > xEnd) return;  // empty span, same as the per-pixel loop doing zero iterations
    const size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(width_);
    const size_t count = static_cast<size_t>(xEnd - xStart + 1);

    if (color.a == 255) {
        // Fully opaque: every pixel in the span becomes exactly `color`,
        // same as `blendPixel`'s `setPixel` fast path -- but as one bulk
        // write instead of `count` individual bounds-checked calls.
        auto begin = pixels_.begin() + static_cast<std::ptrdiff_t>(rowOffset + static_cast<size_t>(xStart));
        std::fill(begin, begin + static_cast<std::ptrdiff_t>(count), color);
        opaquePixelWrites_ += count;
        return;
    }
    if (color.a == 0) return;  // no-op, same as blendPixel() -- never touches the framebuffer

    // Partially transparent: same per-pixel blend math as blendPixel(),
    // just without re-checking bounds that are already guaranteed by the
    // caller (see this function's doc comment in the header).
    for (int x = xStart; x <= xEnd; ++x) {
        swf::RgbaColor& dst = pixels_[rowOffset + static_cast<size_t>(x)];
        dst.r = blendChannel(dst.r, color.r, color.a);
        dst.g = blendChannel(dst.g, color.g, color.a);
        dst.b = blendChannel(dst.b, color.b, color.a);
        dst.a = static_cast<uint8_t>(std::min(255, static_cast<int>(dst.a) + color.a));
    }
    blendedPixelWrites_ += count;
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

    // Performance fix (2026-08-28, "resolve the 7-12 FPS pacing" task --
    // see docs/performance-pacing.md for the on-device sub-phase timing
    // evidence this was built on): this used to test EVERY edge of the
    // polygon on EVERY scanline row of its bounding box -- an
    // O(rows * edges) loop -- which on-device measurement confirmed was
    // the single dominant real-frame cost by far (roughly 40-52% of every
    // frame, even a fully static one), well past tessellation (fixed
    // separately, commit 7dd6acb) or the framebuffer blit.
    //
    // Fix: build the edge list ONCE per fillPolygon() call, sorted by
    // each edge's lower y bound, then sweep scanlines top to bottom
    // maintaining an ACTIVE EDGE LIST -- only the edges whose y-range
    // actually covers the current row -- instead of re-testing every
    // edge on every row. This is the standard active-edge-table scanline
    // algorithm. Deliberately NOT touched: the actual crossing test and
    // x-intersection formula below (`t = (scanY - ay) / (by - ay); x = ...`)
    // is copied byte-for-byte from the original per-scanline loop, just
    // applied to a pre-filtered edge subset -- this keeps the floating-
    // point arithmetic (and therefore the rendered output) IDENTICAL to
    // before, verified via MD5-identical render output across hobo.swf
    // frames 1-5 before/after this change (see docs/performance-pacing.md).
    struct Edge {
        double ax, ay, bx, by;
        double yLo, yHi;  // min/max of ay,by -- the row range this edge can be active for
    };
    std::vector<Edge> edges;
    edges.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const PointTwips& a = devicePoints[i];
        const PointTwips& b = devicePoints[(i + 1) % n];  // edge back to points[0] is implicit
        double ay = a.y, by = b.y;
        if (ay == by) continue;  // horizontal edge contributes no crossing, same as before
        edges.push_back(Edge{static_cast<double>(a.x), ay, static_cast<double>(b.x), by,
                              std::min(ay, by), std::max(ay, by)});
    }
    std::sort(edges.begin(), edges.end(),
              [](const Edge& l, const Edge& r) { return l.yLo < r.yLo; });

    std::vector<double> intersections;
    std::vector<const Edge*> active;
    size_t nextEdge = 0;  // first not-yet-added index into the yLo-sorted `edges`

    for (int y = minY; y <= maxY; ++y) {
        double scanY = y + 0.5;  // sample at pixel center

        // Bring in edges that just became eligible. `edges` is sorted by
        // yLo and y only increases across this loop, so this is a single
        // forward sweep over the whole call, not O(n) per row.
        while (nextEdge < edges.size() && edges[nextEdge].yLo <= scanY) {
            active.push_back(&edges[nextEdge]);
            ++nextEdge;
        }
        // Drop edges whose range has already passed.
        active.erase(std::remove_if(active.begin(), active.end(),
                                     [scanY](const Edge* e) { return e->yHi <= scanY; }),
                     active.end());

        intersections.clear();
        for (const Edge* e : active) {
            // Same crossing test as the original (redundant given the
            // active-list membership above already guarantees it, but
            // kept so this is a provably direct transcription of the
            // original per-edge test, not a reasoned-about replacement).
            if ((scanY >= e->ay && scanY < e->by) || (scanY >= e->by && scanY < e->ay)) {
                double t = (scanY - e->ay) / (e->by - e->ay);
                double x = e->ax + t * (e->bx - e->ax);
                intersections.push_back(x);
            }
        }

        if (intersections.empty()) continue;
        std::sort(intersections.begin(), intersections.end());

        // Even-odd fill: fill between each pair of consecutive crossings.
        // fillSpan() (not a per-pixel blendPixel() loop) -- xStart/xEnd
        // are clamped right here, and y is already known in-bounds from
        // the minY/maxY clamp above, so this span-level call is exactly
        // the "bounds already established once" case fillSpan() requires
        // (see its doc comment in the header for why this matters: it was
        // measured to be ~146-148K redundant-bounds-checked pixel writes
        // per frame before this fix, docs/performance-pacing.md).
        for (size_t i = 0; i + 1 < intersections.size(); i += 2) {
            int xStart = static_cast<int>(std::lround(intersections[i]));
            int xEnd = static_cast<int>(std::lround(intersections[i + 1]));
            xStart = std::max(xStart, 0);
            xEnd = std::min(xEnd, width_ - 1);
            fillSpan(y, xStart, xEnd, color);
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
