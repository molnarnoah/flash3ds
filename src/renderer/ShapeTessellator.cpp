#include "renderer/ShapeTessellator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace flash3ds::renderer {

namespace {

// Appends `count` line segments approximating the quadratic bezier
// start -> control -> anchor (start itself is assumed already the last
// point pushed onto `out`; it is not re-pushed here).
void flattenQuadraticBezier(std::vector<PointTwips>& out, PointTwips start, PointTwips control,
                             PointTwips anchor, int subdivisions) {
    if (subdivisions < 1) subdivisions = 1;
    for (int step = 1; step <= subdivisions; ++step) {
        double t = static_cast<double>(step) / static_cast<double>(subdivisions);
        double oneMinusT = 1.0 - t;
        double x = oneMinusT * oneMinusT * start.x + 2.0 * oneMinusT * t * control.x +
                   t * t * anchor.x;
        double y = oneMinusT * oneMinusT * start.y + 2.0 * oneMinusT * t * control.y +
                   t * t * anchor.y;
        out.push_back(PointTwips{static_cast<int32_t>(std::lround(x)),
                                  static_cast<int32_t>(std::lround(y))});
    }
}

// Resolves a 1-based (0 == none) style index against `styles`, returning
// nullptr if the index is absent or out of range.
template <typename T>
const T* resolveStyle(const std::vector<T>& styles, std::optional<uint32_t> index) {
    if (!index || *index == 0) return nullptr;
    size_t i = *index - 1;
    if (i >= styles.size()) return nullptr;
    return &styles[i];
}

}  // namespace

swf::RgbaColor toFlatColor(const swf::FillStyle& fill) {
    if (fill.isSolid()) {
        return fill.solidColor;
    }
    if (fill.isGradient()) {
        if (fill.gradient.records.empty()) {
            return swf::RgbaColor{128, 128, 128, 255};
        }
        uint32_t r = 0, g = 0, b = 0, a = 0;
        for (const auto& stop : fill.gradient.records) {
            r += stop.color.r;
            g += stop.color.g;
            b += stop.color.b;
            a += stop.color.a;
        }
        size_t n = fill.gradient.records.size();
        return swf::RgbaColor{static_cast<uint8_t>(r / n), static_cast<uint8_t>(g / n),
                               static_cast<uint8_t>(b / n), static_cast<uint8_t>(a / n)};
    }
    // Bitmap fill (or anything unrecognized): neutral gray placeholder —
    // bitmap decoding isn't implemented yet.
    return swf::RgbaColor{160, 160, 160, 255};
}

namespace {

uint8_t lerpChannel(uint8_t a, uint8_t b, double t) {
    return static_cast<uint8_t>(std::lround(a + (static_cast<double>(b) - a) * t));
}

swf::RgbaColor lerpColor(const swf::RgbaColor& a, const swf::RgbaColor& b, double t) {
    return swf::RgbaColor{lerpChannel(a.r, b.r, t), lerpChannel(a.g, b.g, t),
                           lerpChannel(a.b, b.b, t), lerpChannel(a.a, b.a, t)};
}

}  // namespace

std::array<swf::RgbaColor, 256> buildGradientRamp(const swf::Gradient& gradient) {
    std::array<swf::RgbaColor, 256> ramp{};
    if (gradient.records.empty()) {
        ramp.fill(swf::RgbaColor{128, 128, 128, 255});
        return ramp;
    }
    if (gradient.records.size() == 1) {
        ramp.fill(gradient.records[0].color);
        return ramp;
    }
    // GradientRecords are required by spec to be stored in non-decreasing
    // ratio order, but real-world encoders aren't always trustworthy — sort
    // a local copy defensively rather than assume it (cheap: at most a
    // handful of stops).
    std::vector<swf::GradientRecord> stops = gradient.records;
    std::sort(stops.begin(), stops.end(),
              [](const swf::GradientRecord& l, const swf::GradientRecord& r) {
                  return l.ratio < r.ratio;
              });

    for (int i = 0; i < 256; ++i) {
        uint8_t ratio = static_cast<uint8_t>(i);
        if (ratio <= stops.front().ratio) {
            ramp[i] = stops.front().color;
            continue;
        }
        if (ratio >= stops.back().ratio) {
            ramp[i] = stops.back().color;
            continue;
        }
        // Find the bracketing pair (stops.size() >= 2 here, and ratio is
        // strictly between the first and last stop's ratio from the checks
        // above, so this loop always finds a bracketing pair and never
        // falls through unset).
        for (size_t s = 0; s + 1 < stops.size(); ++s) {
            const swf::GradientRecord& lo = stops[s];
            const swf::GradientRecord& hi = stops[s + 1];
            if (ratio >= lo.ratio && ratio <= hi.ratio) {
                double t = hi.ratio == lo.ratio
                               ? 0.0
                               : static_cast<double>(ratio - lo.ratio) / (hi.ratio - lo.ratio);
                ramp[i] = lerpColor(lo.color, hi.color, t);
                break;
            }
        }
    }
    return ramp;
}

TessellatedShape tessellateShape(const swf::Shape& shape, int curveSubdivisions) {
    TessellatedShape result;

    const std::vector<swf::FillStyle>* activeFillStyles = &shape.fillStyles;
    const std::vector<swf::LineStyle>* activeLineStyles = &shape.lineStyles;

    std::optional<uint32_t> fillStyle0Index;
    std::optional<uint32_t> fillStyle1Index;
    std::optional<uint32_t> lineStyleIndex;

    int32_t currentX = 0;
    int32_t currentY = 0;

    // --- Stroke bookkeeping (unchanged from the original tessellator):
    // a stroke is the literal pen-drawn path, in authoring order, and a
    // fill-only style change (no MoveTo) must NOT interrupt it — a line
    // can legitimately cross from one fill region into another without
    // lifting the pen. ---
    std::vector<PointTwips> subpath;
    std::optional<uint32_t> subpathLineStyleIndex;

    auto flushStroke = [&]() {
        if (subpath.size() >= 2) {
            const swf::LineStyle* line = resolveStyle(*activeLineStyles, subpathLineStyleIndex);
            if (line) {
                TessellatedStroke stroke;
                stroke.color = line->color;
                stroke.widthTwips = line->widthTwips;
                stroke.points = subpath;
                result.strokes.push_back(std::move(stroke));
            }
        }
        subpath.clear();
    };

    // --- Fill reconstruction (2026-08-30, Phase 3 investigation --
    // real-content evidence: hobo.swf's title/menu vector artwork uses
    // multiple StyleChangeRecords sharing one continuous pen-drawn run
    // (everything between one MoveTo and the next MoveTo/hasNewStyles/
    // end of the shape), WITHOUT a MoveTo between the style switches, to
    // describe adjacent differently-colored regions -- e.g. multi-color
    // glyph fills for "break apart" text). The OLD tessellator treated
    // each such run as ONE polygon using whichever style was active when
    // the run STARTED, so a style switch with no MoveTo silently kept
    // using the stale style for the rest of that run's edges -- meaning
    // any content authored this way lost every fill region after the
    // first ("only base colors layer shows", per the user's own report).
    //
    // The fix keeps a fill sub-chain exactly parallel to `subpath` above,
    // except it also splits (flushes the accumulated chain as one
    // polygon, starts a new one) whenever the CURRENTLY RESOLVED style
    // differs from whatever the chain is already accumulating under --
    // not just at MoveTo/hasNewStyles boundaries. Since edges are pushed
    // in the exact order they're drawn, this needs no endpoint-matching
    // or searching at all: a straight append-or-split walk always
    // reproduces the shape's own authored point order.
    //
    // An earlier version of this fix instead tried real SWF-style edge
    // GRAPH reconstruction: record every edge with its resolved style,
    // then afterwards group ALL same-style edges (even from unrelated,
    // separately-MoveTo'd regions, or from both fillStyle0 and fillStyle1
    // sides of an edge) and chain them by matching endpoints into closed
    // loops. That is the textbook-correct SWF model, and it does handle
    // shapes with genuinely shared boundaries between regions -- but
    // real corpus content broke under it in two different ways, both
    // confirmed via direct before/after render diffs during this fix's
    // own verification pass (see docs/renderer.md for the visual
    // comparisons, and for why this scoped-down version was chosen
    // instead): a same-colored multi-letter caption ("Armor Games") and
    // a compact icon (a mute-button glyph) both have separate, unrelated
    // regions whose edges happen to touch the same coordinate somewhere
    // without being logically connected, which greedy endpoint-matching
    // wove together into garbled polygons; and contributing an edge to
    // BOTH fillStyle0's and fillStyle1's regions (needed for genuinely
    // shared boundaries) produced a second, spurious solid-colored loop
    // for content that, in this corpus, already describes every real
    // region as its own complete, separately-closed pen path. Since
    // nothing found in the corpus so far actually NEEDS true cross-region
    // edge merging, this stays scoped to "split one run by style,
    // preserving authored order" -- revisit only with real evidence a
    // corpus title needs the fuller edge-graph model.
    std::vector<PointTwips> fillSubpath;
    const swf::FillStyle* fillSubpathStyle = nullptr;

    // Hole/counter rendering (2026-08-31) — see this file's header comment.
    // Assigns a stable-within-this-call integer id to each distinct
    // resolved FillStyle pointer, in first-seen order, so the caller can
    // later tell which of this shape's emitted polygons are different
    // contours of the SAME original fill region (same pointer => same
    // style slot in `activeFillStyles`/`sc.newFillStyles` at the time it
    // was resolved) versus genuinely different fill styles. A linear scan
    // is deliberately fine here: a shape's distinct fill style count is
    // always small (single digits to low tens even for complex real
    // content), so this never approaches being a real cost next to the
    // rest of tessellation.
    std::vector<const swf::FillStyle*> fillGroupOrder;
    auto fillGroupIdFor = [&](const swf::FillStyle* style) -> int {
        for (size_t i = 0; i < fillGroupOrder.size(); ++i) {
            if (fillGroupOrder[i] == style) return static_cast<int>(i);
        }
        fillGroupOrder.push_back(style);
        return static_cast<int>(fillGroupOrder.size() - 1);
    };

    auto flushFill = [&]() {
        if (fillSubpath.size() >= 3 && fillSubpathStyle) {
            TessellatedPolygon poly;
            poly.color = toFlatColor(*fillSubpathStyle);
            poly.fillGroupId = fillGroupIdFor(fillSubpathStyle);
            poly.points = fillSubpath;
            // Real gradient rendering, scoped to kLinearGradient only —
            // see this file's header comment and IRenderer.h's
            // DeviceGradientFill doc comment for why radial/focal-radial
            // stay on the toFlatColor() fallback above.
            if (fillSubpathStyle->type == swf::FillStyleType::kLinearGradient) {
                poly.paintKind = PaintKind::kLinearGradient;
                poly.gradient.matrix = fillSubpathStyle->gradientMatrix;
                poly.gradient.spreadMode = fillSubpathStyle->gradient.spreadMode;
                poly.gradient.ramp = buildGradientRamp(fillSubpathStyle->gradient);
            } else if (fillSubpathStyle->isBitmap()) {
                // Priority Fix List item #2 (2026-08-31) — see this file's
                // header comment and IRenderer.h's DeviceBitmapFill doc
                // comment. Only the fill's own matrix/characterId/repeat/
                // smoothed flags are captured here; the actual decoded
                // pixel buffer is looked up later by SceneRenderer (see
                // BitmapPaint's own doc comment for why this file can't do
                // that lookup itself).
                poly.paintKind = PaintKind::kBitmap;
                poly.bitmap.matrix = fillSubpathStyle->gradientMatrix;
                poly.bitmap.bitmapCharacterId = fillSubpathStyle->bitmapCharacterId;
                poly.bitmap.repeat =
                    fillSubpathStyle->type == swf::FillStyleType::kRepeatingBitmap ||
                    fillSubpathStyle->type == swf::FillStyleType::kNonSmoothedRepeatingBitmap;
                poly.bitmap.smoothed =
                    fillSubpathStyle->type == swf::FillStyleType::kRepeatingBitmap ||
                    fillSubpathStyle->type == swf::FillStyleType::kClippedBitmap;
            }
            result.polygons.push_back(std::move(poly));
        }
        fillSubpath.clear();
        fillSubpathStyle = nullptr;
    };

    auto pushFillEdge = [&](PointTwips a, PointTwips b) {
        const swf::FillStyle* f = resolveStyle(*activeFillStyles, fillStyle1Index);
        if (!f) f = resolveStyle(*activeFillStyles, fillStyle0Index);
        if (!f) {
            // No fill active for this edge (e.g. a stroke-only segment) --
            // whatever was accumulating is done; don't touch it further.
            flushFill();
            return;
        }
        if (fillSubpath.empty()) {
            fillSubpathStyle = f;
            fillSubpath.push_back(a);
        } else if (f != fillSubpathStyle) {
            // Style changed mid-run (with or without an intervening
            // MoveTo — MoveTo already flushes via flushFill() at its own
            // call site below, so reaching here specifically means NO
            // MoveTo occurred): close off the region built so far and
            // start a new one continuing from the same pen position, per
            // this fix's own header comment above.
            flushFill();
            fillSubpathStyle = f;
            fillSubpath.push_back(a);
        }
        fillSubpath.push_back(b);
    };

    for (const swf::ShapeRecord& record : shape.records) {
        switch (record.type) {
            case swf::ShapeRecordType::kStyleChange: {
                // Invariant established by readShapeRecordStream(): styleChange
                // is always non-null when type == kStyleChange.
                const swf::ShapeStyleChange& sc = *record.styleChange;
                if (sc.hasNewStyles) {
                    // A new FILLSTYLEARRAY/LINESTYLEARRAY starts a fresh
                    // style scope (used by DefineShape's "one shape, many
                    // style groups" layering) -- ends the current run on
                    // both the stroke and fill sides.
                    flushStroke();
                    flushFill();
                    activeFillStyles = &sc.newFillStyles;
                    activeLineStyles = &sc.newLineStyles;
                    fillStyle0Index.reset();
                    fillStyle1Index.reset();
                    lineStyleIndex.reset();
                }

                if (sc.fillStyle0) fillStyle0Index = sc.fillStyle0;
                if (sc.fillStyle1) fillStyle1Index = sc.fillStyle1;
                if (sc.lineStyleIndex) lineStyleIndex = sc.lineStyleIndex;

                if (sc.hasMoveTo) {
                    flushStroke();
                    flushFill();
                    currentX = sc.moveToXTwips;
                    currentY = sc.moveToYTwips;
                    subpath.push_back(PointTwips{currentX, currentY});
                    subpathLineStyleIndex = lineStyleIndex;
                } else if (subpath.empty()) {
                    // A style-only change with no MoveTo yet and nothing
                    // open: start an implicit subpath at the current point
                    // so subsequent edges have somewhere to accumulate.
                    subpath.push_back(PointTwips{currentX, currentY});
                    subpathLineStyleIndex = lineStyleIndex;
                }
                break;
            }

            case swf::ShapeRecordType::kStraightEdge: {
                if (subpath.empty()) {
                    subpath.push_back(PointTwips{currentX, currentY});
                    subpathLineStyleIndex = lineStyleIndex;
                }
                PointTwips start{currentX, currentY};
                currentX += record.edge.straightEdge.deltaXTwips;
                currentY += record.edge.straightEdge.deltaYTwips;
                PointTwips end{currentX, currentY};
                subpath.push_back(end);
                pushFillEdge(start, end);
                break;
            }

            case swf::ShapeRecordType::kCurvedEdge: {
                if (subpath.empty()) {
                    subpath.push_back(PointTwips{currentX, currentY});
                    subpathLineStyleIndex = lineStyleIndex;
                }
                PointTwips start{currentX, currentY};
                PointTwips control{currentX + record.edge.curvedEdge.controlDeltaXTwips,
                                    currentY + record.edge.curvedEdge.controlDeltaYTwips};
                PointTwips anchor{control.x + record.edge.curvedEdge.anchorDeltaXTwips,
                                   control.y + record.edge.curvedEdge.anchorDeltaYTwips};
                std::vector<PointTwips> curvePoints{start};
                flattenQuadraticBezier(curvePoints, start, control, anchor, curveSubdivisions);
                for (size_t i = 1; i < curvePoints.size(); ++i) {
                    subpath.push_back(curvePoints[i]);
                    pushFillEdge(curvePoints[i - 1], curvePoints[i]);
                }
                currentX = anchor.x;
                currentY = anchor.y;
                break;
            }

            case swf::ShapeRecordType::kEnd:
                break;
        }
    }
    flushStroke();
    flushFill();

    return result;
}

}  // namespace flash3ds::renderer
