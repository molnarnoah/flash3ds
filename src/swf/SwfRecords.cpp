#include "swf/SwfRecords.h"

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
