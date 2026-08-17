#include "renderer/ShapeTessellator.h"

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

TessellatedShape tessellateShape(const swf::Shape& shape, int curveSubdivisions) {
    TessellatedShape result;

    const std::vector<swf::FillStyle>* activeFillStyles = &shape.fillStyles;
    const std::vector<swf::LineStyle>* activeLineStyles = &shape.lineStyles;

    std::optional<uint32_t> fillStyle0Index;
    std::optional<uint32_t> fillStyle1Index;
    std::optional<uint32_t> lineStyleIndex;

    int32_t currentX = 0;
    int32_t currentY = 0;

    // The subpath currently being built (points only — style is captured
    // separately so we know what to flush it as).
    std::vector<PointTwips> subpath;
    std::optional<uint32_t> subpathFillStyle0Index;
    std::optional<uint32_t> subpathFillStyle1Index;
    std::optional<uint32_t> subpathLineStyleIndex;

    auto flushSubpath = [&]() {
        if (subpath.size() >= 3) {
            // Prefer fillStyle1 (matches the common authoring-tool
            // convention for the "outer"/fill-forward edge direction),
            // falling back to fillStyle0 if only that side is styled.
            const swf::FillStyle* fill = resolveStyle(*activeFillStyles, subpathFillStyle1Index);
            if (!fill) {
                fill = resolveStyle(*activeFillStyles, subpathFillStyle0Index);
            }
            if (fill) {
                TessellatedPolygon poly;
                poly.color = toFlatColor(*fill);
                poly.points = subpath;
                result.polygons.push_back(std::move(poly));
            }
        }
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

    for (const swf::ShapeRecord& record : shape.records) {
        switch (record.type) {
            case swf::ShapeRecordType::kStyleChange: {
                if (record.hasNewStyles) {
                    // A new FILLSTYLEARRAY/LINESTYLEARRAY starts a fresh
                    // style scope (used by DefineShape's "one shape, many
                    // style groups" layering). Flush whatever subpath was
                    // open under the old styles first.
                    flushSubpath();
                    activeFillStyles = &record.newFillStyles;
                    activeLineStyles = &record.newLineStyles;
                    fillStyle0Index.reset();
                    fillStyle1Index.reset();
                    lineStyleIndex.reset();
                }

                if (record.fillStyle0) fillStyle0Index = record.fillStyle0;
                if (record.fillStyle1) fillStyle1Index = record.fillStyle1;
                if (record.lineStyleIndex) lineStyleIndex = record.lineStyleIndex;

                if (record.hasMoveTo) {
                    flushSubpath();
                    currentX = record.moveToXTwips;
                    currentY = record.moveToYTwips;
                    subpath.push_back(PointTwips{currentX, currentY});
                    subpathFillStyle0Index = fillStyle0Index;
                    subpathFillStyle1Index = fillStyle1Index;
                    subpathLineStyleIndex = lineStyleIndex;
                } else if (subpath.empty()) {
                    // A style-only change with no MoveTo yet and nothing
                    // open: start an implicit subpath at the current point
                    // so subsequent edges have somewhere to accumulate.
                    subpath.push_back(PointTwips{currentX, currentY});
                    subpathFillStyle0Index = fillStyle0Index;
                    subpathFillStyle1Index = fillStyle1Index;
                    subpathLineStyleIndex = lineStyleIndex;
                }
                break;
            }

            case swf::ShapeRecordType::kStraightEdge: {
                if (subpath.empty()) {
                    subpath.push_back(PointTwips{currentX, currentY});
                    subpathFillStyle0Index = fillStyle0Index;
                    subpathFillStyle1Index = fillStyle1Index;
                    subpathLineStyleIndex = lineStyleIndex;
                }
                currentX += record.deltaXTwips;
                currentY += record.deltaYTwips;
                subpath.push_back(PointTwips{currentX, currentY});
                break;
            }

            case swf::ShapeRecordType::kCurvedEdge: {
                if (subpath.empty()) {
                    subpath.push_back(PointTwips{currentX, currentY});
                    subpathFillStyle0Index = fillStyle0Index;
                    subpathFillStyle1Index = fillStyle1Index;
                    subpathLineStyleIndex = lineStyleIndex;
                }
                PointTwips start{currentX, currentY};
                PointTwips control{currentX + record.controlDeltaXTwips,
                                    currentY + record.controlDeltaYTwips};
                PointTwips anchor{control.x + record.anchorDeltaXTwips,
                                   control.y + record.anchorDeltaYTwips};
                flattenQuadraticBezier(subpath, start, control, anchor, curveSubdivisions);
                currentX = anchor.x;
                currentY = anchor.y;
                break;
            }

            case swf::ShapeRecordType::kEnd:
                break;
        }
    }

    flushSubpath();
    return result;
}

}  // namespace flash3ds::renderer
