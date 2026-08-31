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

// Performance fix (2026-08-28, continuing the "solid 25fps" task after the
// blit-loop fix -- see docs/performance-pacing.md): profiled fillPolygon()
// against real hobo.swf content with valgrind/callgrind (desktop x86, but
// instruction-count composition -- not wall-clock -- is what's being read
// off it, which carries over) after the blit fix made `raster` the largest
// real-work phase again. Finding: std::lround() -- called twice per filled
// span, computing xStart/xEnd -- accounted for over 20% of TOTAL PROGRAM
// INSTRUCTIONS, dwarfing every other cost in the renderer including the
// actual pixel writes. glibc's lround()/llround() is a real (non-inlined)
// libm call that handles the full IEEE-754 contract: NaN/Inf, values
// outside the representable range, and the current floating-point
// rounding-mode environment -- none of which this call site can ever
// actually hit (x is always a scanline/edge-intersection x-coordinate in
// screen-pixel units, always finite and always small in magnitude for any
// SWF this runtime renders).
//
// roundToInt() replaces it with the same round-half-away-from-zero
// tie-breaking std::lround() uses (lround(2.5)==3, lround(-2.5)==-3), just
// inlined and specialized for exactly the value range this renderer
// produces, with no libm call, no rounding-mode/exception-flag handling,
// and no NaN/Inf/overflow path. `x + 0.5` (or `x - 0.5`) then truncated is
// only NOT bit-identical to true round-half-away-from-zero for magnitudes
// large enough that a double's ~52 bits of mantissa can't represent both
// the integer part and the 0.5 tie-breaker exactly -- many orders of
// magnitude past any coordinate this renderer ever computes (twip-derived
// pixel coordinates, realistically within a few thousand of zero).
// Verified, not just reasoned about: rendered hobo.swf frames 1-5 before/
// after are byte-identical MD5s (same standard as every prior raster fix
// this task), and the full existing SoftwareRenderer test suite (including
// the concave-polygon active-edge-table test, which exercises plenty of
// non-integer intersection x-values) passes unmodified.
inline int roundToInt(double x) {
    return x >= 0.0 ? static_cast<int>(x + 0.5) : static_cast<int>(x - 0.5);
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

void SoftwareRenderer::blendPixelCoverage(int x, int y, swf::RgbaColor color, double coverage) {
    if (coverage <= 0.0) return;
    if (coverage >= 1.0) {
        // Fully covered -- identical cost/output to the pre-AA code path.
        blendPixel(x, y, color);
        return;
    }
    swf::RgbaColor scaled = color;
    int alpha = static_cast<int>(color.a * coverage + 0.5);  // round-half-up, same convention as roundToInt()
    alpha = std::max(0, std::min(255, alpha));
    if (alpha == 0) return;  // no-op, same convention as blendPixel()'s own alpha==0 early-out
    scaled.a = static_cast<uint8_t>(alpha);
    blendPixel(x, y, scaled);
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

namespace {

// Maps a gradient-space t (from DeviceGradientFill's linear formula,
// t = (gx + 16384) / 32768, computed by the caller below) into [0, 1] per
// GradientSpreadMode — kPad clamps, kReflect triangle-waves, kRepeat
// sawtooths. Matches the public SWF spec's three spread modes.
double applySpreadMode(double t, swf::GradientSpreadMode mode) {
    switch (mode) {
        case swf::GradientSpreadMode::kPad:
            return std::min(1.0, std::max(0.0, t));
        case swf::GradientSpreadMode::kRepeat: {
            double m = std::fmod(t, 1.0);
            if (m < 0.0) m += 1.0;
            return m;
        }
        case swf::GradientSpreadMode::kReflect: {
            double m = std::fmod(t, 2.0);
            if (m < 0.0) m += 2.0;
            return m > 1.0 ? 2.0 - m : m;
        }
    }
    return std::min(1.0, std::max(0.0, t));
}

}  // namespace

void SoftwareRenderer::fillSpanGradient(int y, int xStart, int xEnd, const DeviceGradientFill& fill) {
    if (xStart > xEnd) return;  // empty span, same convention as fillSpan()
    const size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(width_);

    // gx = a*px + c*py + tx, evaluated at pixel centers (px = x+0.5,
    // py = y+0.5, matching fillPolygon()'s own scanY = y+0.5 sampling
    // convention) — y is fixed for this whole span, so only the x term
    // varies; step gx by `a` (and, unused for linear gradients but kept
    // for a future radial implementation, gy by `b`) each pixel instead of
    // recomputing the full affine expression from scratch every iteration.
    double py = y + 0.5;
    double gx = fill.a * (xStart + 0.5) + fill.c * py + fill.tx;

    for (int x = xStart; x <= xEnd; ++x, gx += fill.a) {
        double t = (gx + 16384.0) / 32768.0;
        t = applySpreadMode(t, fill.spreadMode);
        int index = static_cast<int>(t * 255.0 + 0.5);
        index = std::min(255, std::max(0, index));
        const swf::RgbaColor& color = fill.ramp[static_cast<size_t>(index)];

        if (color.a == 0) continue;  // no-op, same as blendPixel()/fillSpan()
        size_t idx = rowOffset + static_cast<size_t>(x);
        if (color.a == 255) {
            pixels_[idx] = color;
            ++opaquePixelWrites_;
            continue;
        }
        swf::RgbaColor& dst = pixels_[idx];
        dst.r = blendChannel(dst.r, color.r, color.a);
        dst.g = blendChannel(dst.g, color.g, color.a);
        dst.b = blendChannel(dst.b, color.b, color.a);
        dst.a = static_cast<uint8_t>(std::min(255, static_cast<int>(dst.a) + color.a));
        ++blendedPixelWrites_;
    }
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
        //
        // Anti-aliasing fix (2026-08-30, Fidelity-audit TASK 3 divergence
        // #1 -- see docs/flash-fidelity-audit.md): this used to round
        // BOTH span edges to the nearest pixel (roundToInt()) and fillSpan()
        // the whole inclusive [xStart, xEnd] range at full alpha -- every
        // filled pixel either fully colored or fully background, hence the
        // hard "jaggies" the audit doc's divergence #1 describes, and (a
        // separate, incidentally-discovered bug) an inclusive-both-ends
        // range over-fills by one pixel column whenever a span happens to
        // land on an exact integer boundary (e.g. xLeft=2.0, xRight=12.0
        // -- geometrically 10 pixels wide, [2,11], but the old code filled
        // [2,12], 11 pixels).
        //
        // Fix: real coverage-based AA, scoped to edge pixels only (the
        // audit doc's own recommended "cheaper middle ground" given this
        // renderer's tight measured FPS budget, docs/performance-pacing.md
        // -- ~27fps achievable vs. hobo.swf's declared 25fps at the time of
        // that measurement, not much headroom for a full-supersampling
        // approach). For each span [xLeft, xRight):
        //   - the one or two boundary pixel COLUMNS (X direction) get a
        //     coverage-scaled alpha blend via blendPixelCoverage(), where
        //     coverage is the exact fractional overlap between the pixel's
        //     [x, x+1) column and the span -- verified against a brute-
        //     force per-pixel overlap integration over 200,000 random
        //     spans (session notes) before writing this loop, same
        //     Python-simulate-first discipline as every other numerically-
        //     sensitive fix in this project;
        //   - the interior (pixels strictly between the two boundary
        //     columns, always fully covered once the span is more than a
        //     pixel wide) stays on the unchanged, full-cost fillSpan()
        //     bulk path -- this is precisely what keeps this "edge pixels
        //     only" AA cheap: a wide fill pays for exactly 2 extra
        //     blendPixelCoverage() calls total, not 2 per scanline row's
        //     worth of edge complexity, and not a single-pixel-wider
        //     interior loop.
        //
        // Scope limitation, stated plainly (matches this project's
        // established honest-scoping convention, e.g. MP3-only audio,
        // Math-only globals, DefineMorphShape-v1-only): this smooths
        // near-vertical/diagonal silhouette edges via X-direction
        // sub-pixel coverage at each scanline's crossings, but does NOT
        // anti-alias purely horizontal top/bottom edges of a shape --
        // rendering still samples exactly one scanline row per integer y,
        // with no Y-direction coverage/supersampling. A shape's top and
        // bottom edges remain hard-edged. See docs/flash-fidelity-audit.md
        // and docs/renderer.md for the same statement in the project's
        // permanent documentation.
        for (size_t i = 0; i + 1 < intersections.size(); i += 2) {
            double xLeft = intersections[i];
            double xRight = intersections[i + 1];
            if (xRight <= xLeft) continue;  // degenerate/empty span

            int leftPixel = static_cast<int>(std::floor(xLeft));
            int rightPixel = static_cast<int>(std::ceil(xRight)) - 1;

            if (rightPixel <= leftPixel) {
                // Span narrower than one pixel column (or exactly one
                // column wide): a single boundary pixel carries the whole
                // span's coverage, no interior fill.
                double coverage = xRight - xLeft;
                if (leftPixel >= 0 && leftPixel < width_) {
                    blendPixelCoverage(leftPixel, y, color, coverage);
                }
                continue;
            }

            double leftCoverage = (leftPixel + 1) - xLeft;
            if (leftPixel >= 0 && leftPixel < width_) {
                blendPixelCoverage(leftPixel, y, color, leftCoverage);
            }

            double rightCoverage = xRight - rightPixel;
            if (rightPixel >= 0 && rightPixel < width_) {
                blendPixelCoverage(rightPixel, y, color, rightCoverage);
            }

            int interiorStart = std::max(leftPixel + 1, 0);
            int interiorEnd = std::min(rightPixel - 1, width_ - 1);
            if (interiorStart <= interiorEnd) {
                fillSpan(y, interiorStart, interiorEnd, color);
            }
        }
    }
}

// Graphics/gradients task (2026-08-28) — see IRenderer.h's
// DeviceGradientFill doc comment and this class's header comment for why
// this is a full, independent copy of fillPolygon()'s active-edge-table
// scanline loop rather than a shared refactor: fillPolygon() above is this
// project's measured, tuned hot path (the roundToInt()/active-edge-table/
// fillSpan() fixes documented in this file's own history), and gradient
// fills are rare in real content (161 of ~13,415 total fills across all of
// hobo.swf's shapes, /tmp/gradient_scan.cpp) — not worth risking that path
// for. The only difference from fillPolygon() below is the final call
// (fillSpanGradient() instead of fillSpan()); the edge-building/active-list/
// intersection math is intentionally identical so this produces the same
// polygon coverage a flat fill of the same shape would.
void SoftwareRenderer::fillPolygonGradient(const std::vector<PointTwips>& devicePoints,
                                            const DeviceGradientFill& fill) {
    if (devicePoints.size() < 3) return;

    int minY = static_cast<int>(devicePoints[0].y);
    int maxY = static_cast<int>(devicePoints[0].y);
    for (const auto& p : devicePoints) {
        minY = std::min(minY, static_cast<int>(p.y));
        maxY = std::max(maxY, static_cast<int>(p.y));
    }
    minY = std::max(minY, 0);
    maxY = std::min(maxY, height_ - 1);

    size_t n = devicePoints.size();

    struct Edge {
        double ax, ay, bx, by;
        double yLo, yHi;
    };
    std::vector<Edge> edges;
    edges.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const PointTwips& a = devicePoints[i];
        const PointTwips& b = devicePoints[(i + 1) % n];
        double ay = a.y, by = b.y;
        if (ay == by) continue;
        edges.push_back(Edge{static_cast<double>(a.x), ay, static_cast<double>(b.x), by,
                              std::min(ay, by), std::max(ay, by)});
    }
    std::sort(edges.begin(), edges.end(),
              [](const Edge& l, const Edge& r) { return l.yLo < r.yLo; });

    std::vector<double> intersections;
    std::vector<const Edge*> active;
    size_t nextEdge = 0;

    for (int y = minY; y <= maxY; ++y) {
        double scanY = y + 0.5;

        while (nextEdge < edges.size() && edges[nextEdge].yLo <= scanY) {
            active.push_back(&edges[nextEdge]);
            ++nextEdge;
        }
        active.erase(std::remove_if(active.begin(), active.end(),
                                     [scanY](const Edge* e) { return e->yHi <= scanY; }),
                     active.end());

        intersections.clear();
        for (const Edge* e : active) {
            if ((scanY >= e->ay && scanY < e->by) || (scanY >= e->by && scanY < e->ay)) {
                double t = (scanY - e->ay) / (e->by - e->ay);
                double x = e->ax + t * (e->bx - e->ax);
                intersections.push_back(x);
            }
        }

        if (intersections.empty()) continue;
        std::sort(intersections.begin(), intersections.end());

        for (size_t i = 0; i + 1 < intersections.size(); i += 2) {
            int xStart = roundToInt(intersections[i]);
            int xEnd = roundToInt(intersections[i + 1]);
            xStart = std::max(xStart, 0);
            xEnd = std::min(xEnd, width_ - 1);
            fillSpanGradient(y, xStart, xEnd, fill);
        }
    }
}

// Hole/counter rendering (2026-08-31, Priority Fix List item #1) — see
// IRenderer.h's fillPolygonGroup() doc comment and ShapeTessellator.h's
// TessellatedPolygon::fillGroupId doc comment for the full design. Same
// active-edge-table algorithm as fillPolygon() above, generalized to build
// its edge list from MULTIPLE closed contours instead of one: every
// contour contributes its own closed-loop edges (vertex i -> vertex
// (i+1)%n, including the implicit closing edge back to point 0) into ONE
// combined, y-sorted edge list, and everything after that — the active-
// edge sweep, per-scanline intersection test, sort, and even-odd pairwise
// span fill (including the same coverage-based anti-aliasing) — is
// IDENTICAL to fillPolygon()'s own loop, copied verbatim. This is what
// makes it correct with no further changes: even-odd fill already means
// "a point is inside iff it's covered by an ODD number of contour
// crossings on its scanline", which is exactly the rule that turns a
// second, disjoint contour sharing one fill style (e.g. the letter O's
// inner counter) into a hole instead of another solid patch, PROVIDED its
// edges are tested together with the outer contour's on the same scanline
// sweep — which combining them into one edge list before sorting achieves
// directly, with no edge welding or endpoint matching involved.
void SoftwareRenderer::fillPolygonGroup(const std::vector<std::vector<PointTwips>>& contours,
                                         swf::RgbaColor color) {
    bool haveBounds = false;
    int minY = 0, maxY = 0;
    for (const auto& contour : contours) {
        if (contour.size() < 3) continue;
        for (const auto& p : contour) {
            int y = static_cast<int>(p.y);
            if (!haveBounds) {
                minY = maxY = y;
                haveBounds = true;
            } else {
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (!haveBounds) return;
    minY = std::max(minY, 0);
    maxY = std::min(maxY, height_ - 1);

    struct Edge {
        double ax, ay, bx, by;
        double yLo, yHi;
    };
    std::vector<Edge> edges;
    for (const auto& contour : contours) {
        size_t n = contour.size();
        if (n < 3) continue;
        for (size_t i = 0; i < n; ++i) {
            const PointTwips& a = contour[i];
            const PointTwips& b = contour[(i + 1) % n];
            double ay = a.y, by = b.y;
            if (ay == by) continue;
            edges.push_back(Edge{static_cast<double>(a.x), ay, static_cast<double>(b.x), by,
                                  std::min(ay, by), std::max(ay, by)});
        }
    }
    if (edges.empty()) return;
    std::sort(edges.begin(), edges.end(),
              [](const Edge& l, const Edge& r) { return l.yLo < r.yLo; });

    std::vector<double> intersections;
    std::vector<const Edge*> active;
    size_t nextEdge = 0;

    for (int y = minY; y <= maxY; ++y) {
        double scanY = y + 0.5;

        while (nextEdge < edges.size() && edges[nextEdge].yLo <= scanY) {
            active.push_back(&edges[nextEdge]);
            ++nextEdge;
        }
        active.erase(std::remove_if(active.begin(), active.end(),
                                     [scanY](const Edge* e) { return e->yHi <= scanY; }),
                     active.end());

        intersections.clear();
        for (const Edge* e : active) {
            if ((scanY >= e->ay && scanY < e->by) || (scanY >= e->by && scanY < e->ay)) {
                double t = (scanY - e->ay) / (e->by - e->ay);
                double x = e->ax + t * (e->bx - e->ax);
                intersections.push_back(x);
            }
        }

        if (intersections.empty()) continue;
        std::sort(intersections.begin(), intersections.end());

        // Same even-odd pairwise span fill + edge-coverage AA as
        // fillPolygon() — see that function's own comment for the full
        // rationale; copied verbatim so this combined-contour path renders
        // identically (same AA behavior) to the single-contour path.
        for (size_t i = 0; i + 1 < intersections.size(); i += 2) {
            double xLeft = intersections[i];
            double xRight = intersections[i + 1];
            if (xRight <= xLeft) continue;

            int leftPixel = static_cast<int>(std::floor(xLeft));
            int rightPixel = static_cast<int>(std::ceil(xRight)) - 1;

            if (rightPixel <= leftPixel) {
                double coverage = xRight - xLeft;
                if (leftPixel >= 0 && leftPixel < width_) {
                    blendPixelCoverage(leftPixel, y, color, coverage);
                }
                continue;
            }

            double leftCoverage = (leftPixel + 1) - xLeft;
            if (leftPixel >= 0 && leftPixel < width_) {
                blendPixelCoverage(leftPixel, y, color, leftCoverage);
            }

            double rightCoverage = xRight - rightPixel;
            if (rightPixel >= 0 && rightPixel < width_) {
                blendPixelCoverage(rightPixel, y, color, rightCoverage);
            }

            int interiorStart = std::max(leftPixel + 1, 0);
            int interiorEnd = std::min(rightPixel - 1, width_ - 1);
            if (interiorStart <= interiorEnd) {
                fillSpan(y, interiorStart, interiorEnd, color);
            }
        }
    }
}

// Gradient counterpart to fillPolygonGroup() above — same combined-
// multi-contour edge list, same rationale for staying a separate copy
// as fillPolygonGradient() vs. fillPolygon(). The only difference from
// fillPolygonGroup() is the final call (fillSpanGradient() instead of
// fillSpan(), and roundToInt()-based span bounds instead of the AA
// coverage split — matching fillPolygonGradient()'s own, not
// fillPolygon()'s, span-fill convention).
void SoftwareRenderer::fillPolygonGradientGroup(const std::vector<std::vector<PointTwips>>& contours,
                                                 const DeviceGradientFill& fill) {
    bool haveBounds = false;
    int minY = 0, maxY = 0;
    for (const auto& contour : contours) {
        if (contour.size() < 3) continue;
        for (const auto& p : contour) {
            int y = static_cast<int>(p.y);
            if (!haveBounds) {
                minY = maxY = y;
                haveBounds = true;
            } else {
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (!haveBounds) return;
    minY = std::max(minY, 0);
    maxY = std::min(maxY, height_ - 1);

    struct Edge {
        double ax, ay, bx, by;
        double yLo, yHi;
    };
    std::vector<Edge> edges;
    for (const auto& contour : contours) {
        size_t n = contour.size();
        if (n < 3) continue;
        for (size_t i = 0; i < n; ++i) {
            const PointTwips& a = contour[i];
            const PointTwips& b = contour[(i + 1) % n];
            double ay = a.y, by = b.y;
            if (ay == by) continue;
            edges.push_back(Edge{static_cast<double>(a.x), ay, static_cast<double>(b.x), by,
                                  std::min(ay, by), std::max(ay, by)});
        }
    }
    if (edges.empty()) return;
    std::sort(edges.begin(), edges.end(),
              [](const Edge& l, const Edge& r) { return l.yLo < r.yLo; });

    std::vector<double> intersections;
    std::vector<const Edge*> active;
    size_t nextEdge = 0;

    for (int y = minY; y <= maxY; ++y) {
        double scanY = y + 0.5;

        while (nextEdge < edges.size() && edges[nextEdge].yLo <= scanY) {
            active.push_back(&edges[nextEdge]);
            ++nextEdge;
        }
        active.erase(std::remove_if(active.begin(), active.end(),
                                     [scanY](const Edge* e) { return e->yHi <= scanY; }),
                     active.end());

        intersections.clear();
        for (const Edge* e : active) {
            if ((scanY >= e->ay && scanY < e->by) || (scanY >= e->by && scanY < e->ay)) {
                double t = (scanY - e->ay) / (e->by - e->ay);
                double x = e->ax + t * (e->bx - e->ax);
                intersections.push_back(x);
            }
        }

        if (intersections.empty()) continue;
        std::sort(intersections.begin(), intersections.end());

        for (size_t i = 0; i + 1 < intersections.size(); i += 2) {
            int xStart = roundToInt(intersections[i]);
            int xEnd = roundToInt(intersections[i + 1]);
            xStart = std::max(xStart, 0);
            xEnd = std::min(xEnd, width_ - 1);
            fillSpanGradient(y, xStart, xEnd, fill);
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
