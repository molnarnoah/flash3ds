#include "swf/SwfRecords.h"

#include <algorithm>
#include <cmath>

namespace flash3ds::swf {

Matrix concatMatrix(const Matrix& parent, const Matrix& child) {
    Matrix out;
    out.scaleX = parent.scaleX * child.scaleX + parent.rotateSkew1 * child.rotateSkew0;
    out.rotateSkew1 = parent.scaleX * child.rotateSkew1 + parent.rotateSkew1 * child.scaleY;
    out.rotateSkew0 = parent.rotateSkew0 * child.scaleX + parent.scaleY * child.rotateSkew0;
    out.scaleY = parent.rotateSkew0 * child.rotateSkew1 + parent.scaleY * child.scaleY;

    double tx = parent.scaleX * child.translateXTwips + parent.rotateSkew1 * child.translateYTwips +
                parent.translateXTwips;
    double ty = parent.rotateSkew0 * child.translateXTwips + parent.scaleY * child.translateYTwips +
                parent.translateYTwips;
    out.translateXTwips = static_cast<int32_t>(std::lround(tx));
    out.translateYTwips = static_cast<int32_t>(std::lround(ty));
    return out;
}

ColorTransform concatColorTransform(const ColorTransform& parent, const ColorTransform& child) {
    ColorTransform out;
    out.redMult = parent.redMult * child.redMult;
    out.greenMult = parent.greenMult * child.greenMult;
    out.blueMult = parent.blueMult * child.blueMult;
    out.alphaMult = parent.alphaMult * child.alphaMult;
    // Add terms compose as: applying child's add, THEN parent's mult+add —
    // i.e. parent.mult scales child's already-added contribution before
    // parent's own add is layered on top. Rounded to the nearest integer
    // per intermediate composition (ColorTransform::*Add is a plain int32_t,
    // matching the SWF spec's integer add-term encoding); final clamping to
    // a displayable [0,255] channel happens later, in applyColorTransform()
    // (swf/ShapeRecords.h) — NOT here, since a composed transform is itself
    // a valid (possibly out-of-[0,255]-effective-range) intermediate value
    // that may still be composed further down a deeper tree.
    out.redAdd = static_cast<int32_t>(std::lround(parent.redMult * child.redAdd)) + parent.redAdd;
    out.greenAdd =
        static_cast<int32_t>(std::lround(parent.greenMult * child.greenAdd)) + parent.greenAdd;
    out.blueAdd =
        static_cast<int32_t>(std::lround(parent.blueMult * child.blueAdd)) + parent.blueAdd;
    out.alphaAdd =
        static_cast<int32_t>(std::lround(parent.alphaMult * child.alphaAdd)) + parent.alphaAdd;
    return out;
}

Rect transformRect(const Matrix& m, const Rect& r) {
    auto transformPoint = [&](int32_t x, int32_t y) -> std::pair<double, double> {
        double nx = m.scaleX * x + m.rotateSkew1 * y + m.translateXTwips;
        double ny = m.rotateSkew0 * x + m.scaleY * y + m.translateYTwips;
        return {nx, ny};
    };
    auto [x0, y0] = transformPoint(r.xMin, r.yMin);
    auto [x1, y1] = transformPoint(r.xMax, r.yMin);
    auto [x2, y2] = transformPoint(r.xMin, r.yMax);
    auto [x3, y3] = transformPoint(r.xMax, r.yMax);
    double xMin = std::min({x0, x1, x2, x3});
    double xMax = std::max({x0, x1, x2, x3});
    double yMin = std::min({y0, y1, y2, y3});
    double yMax = std::max({y0, y1, y2, y3});
    return Rect{
        static_cast<int32_t>(std::lround(xMin)),
        static_cast<int32_t>(std::lround(xMax)),
        static_cast<int32_t>(std::lround(yMin)),
        static_cast<int32_t>(std::lround(yMax)),
    };
}

Point transformPoint(const Matrix& m, const Point& p) {
    return Point{
        m.scaleX * p.x + m.rotateSkew1 * p.y + m.translateXTwips,
        m.rotateSkew0 * p.x + m.scaleY * p.y + m.translateYTwips,
    };
}

bool invertMatrix(const Matrix& m, Matrix* out) {
    // Standard 2x3 affine inverse. Forward transform (see transformPoint()/
    // transformRect()):
    //   x' = scaleX*x + rotateSkew1*y + translateX
    //   y' = rotateSkew0*x + scaleY*y + translateY
    // i.e. the linear part is [[a c]; [b d]] with a=scaleX, b=rotateSkew0,
    // c=rotateSkew1, d=scaleY. Its inverse is (1/det)*[[d -c]; [-b a]],
    // det = a*d - b*c.
    const double a = m.scaleX;
    const double b = m.rotateSkew0;
    const double c = m.rotateSkew1;
    const double d = m.scaleY;
    const double det = a * d - b * c;
    // A tiny (not exactly-zero) epsilon — comparing a computed determinant
    // against exactly 0.0 would reject only the mathematically-exact
    // degenerate case and accept near-singular matrices that still blow up
    // numerically once divided through.
    constexpr double kEpsilon = 1e-9;
    if (std::abs(det) < kEpsilon) return false;

    const double invA = d / det;
    const double invB = -b / det;
    const double invC = -c / det;
    const double invD = a / det;
    const double tx = m.translateXTwips;
    const double ty = m.translateYTwips;

    out->scaleX = invA;
    out->scaleY = invD;
    out->rotateSkew0 = invB;
    out->rotateSkew1 = invC;
    out->translateXTwips = static_cast<int32_t>(std::lround(-(invA * tx + invC * ty)));
    out->translateYTwips = static_cast<int32_t>(std::lround(-(invB * tx + invD * ty)));
    return true;
}

bool rectContainsPoint(const Rect& r, const Point& p) {
    return p.x >= r.xMin && p.x <= r.xMax && p.y >= r.yMin && p.y <= r.yMax;
}

namespace {
// Converts a raw 16.16 fixed-point SB[nbits] value (already sign-extended
// by SwfReader::readSBits) into a double.
double fixed16_16ToDouble(int32_t raw) { return raw / 65536.0; }
}  // namespace

Matrix readMatrix(SwfReader& reader) {
    Matrix m;

    bool hasScale = reader.readUBits(1) != 0;
    if (hasScale) {
        int nScaleBits = static_cast<int>(reader.readUBits(5));
        m.scaleX = fixed16_16ToDouble(reader.readSBits(nScaleBits));
        m.scaleY = fixed16_16ToDouble(reader.readSBits(nScaleBits));
    }

    bool hasRotate = reader.readUBits(1) != 0;
    if (hasRotate) {
        int nRotateBits = static_cast<int>(reader.readUBits(5));
        m.rotateSkew0 = fixed16_16ToDouble(reader.readSBits(nRotateBits));
        m.rotateSkew1 = fixed16_16ToDouble(reader.readSBits(nRotateBits));
    }

    int nTranslateBits = static_cast<int>(reader.readUBits(5));
    m.translateXTwips = reader.readSBits(nTranslateBits);
    m.translateYTwips = reader.readSBits(nTranslateBits);

    reader.byteAlign();
    return m;
}

ColorTransform readColorTransform(SwfReader& reader, bool withAlpha) {
    ColorTransform ct;

    bool hasAddTerms = reader.readUBits(1) != 0;
    bool hasMultTerms = reader.readUBits(1) != 0;
    int nbits = static_cast<int>(reader.readUBits(4));

    if (hasMultTerms) {
        ct.redMult = reader.readSBits(nbits) / 256.0;
        ct.greenMult = reader.readSBits(nbits) / 256.0;
        ct.blueMult = reader.readSBits(nbits) / 256.0;
        if (withAlpha) {
            ct.alphaMult = reader.readSBits(nbits) / 256.0;
        }
    }
    if (hasAddTerms) {
        ct.redAdd = reader.readSBits(nbits);
        ct.greenAdd = reader.readSBits(nbits);
        ct.blueAdd = reader.readSBits(nbits);
        if (withAlpha) {
            ct.alphaAdd = reader.readSBits(nbits);
        }
    }

    reader.byteAlign();
    return ct;
}

}  // namespace flash3ds::swf
