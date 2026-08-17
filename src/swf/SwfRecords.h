// SwfRecords.h
//
// Clean-room readers for the small bit-packed records the public SWF spec
// reuses across many tags: MATRIX (2D affine transform) and CXFORM /
// CXFORMWITHALPHA (color transform). Needed starting with PlaceObject /
// PlaceObject2 (Phase 2) and reused again for later tags (PlaceObject3,
// button records, etc).

#pragma once

#include <cstdint>

#include "swf/SwfReader.h"

namespace flash3ds::swf {

// A 2D affine transform: [scaleX rotateSkew1; rotateSkew0 scaleY] + translate.
// scale/rotateSkew fields are stored in the SWF as 16.16 fixed-point; here
// they're already converted to double. Translate fields are in twips
// (1/20 px), matching Rect's convention.
struct Matrix {
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotateSkew0 = 0.0;
    double rotateSkew1 = 0.0;
    int32_t translateXTwips = 0;
    int32_t translateYTwips = 0;

    double translateXPixels() const { return translateXTwips / 20.0; }
    double translateYPixels() const { return translateYTwips / 20.0; }

    static Matrix identity() { return Matrix{}; }
};

// RGBA color transform: output = input * mult + add, applied per channel.
// Mult terms are stored as 8.8 fixed point in the SWF; here already
// converted to double (default 1.0). Add terms are plain signed integers in
// [-255, 255] (default 0). `hasAlpha` records whether this came from a
// CXFORMWITHALPHA (PlaceObject2+) vs a plain CXFORM (legacy PlaceObject) —
// for a plain CXFORM, alpha mult/add stay at their identity defaults.
struct ColorTransform {
    double redMult = 1.0;
    double greenMult = 1.0;
    double blueMult = 1.0;
    double alphaMult = 1.0;
    int32_t redAdd = 0;
    int32_t greenAdd = 0;
    int32_t blueAdd = 0;
    int32_t alphaAdd = 0;

    static ColorTransform identity() { return ColorTransform{}; }
};

// Reads a MATRIX record. Must be called at a byte-aligned position (or
// right after another bit-packed record); leaves the reader byte-aligned.
Matrix readMatrix(SwfReader& reader);

// Reads a CXFORM (withAlpha == false) or CXFORMWITHALPHA (withAlpha ==
// true) record. Must be called at a byte-aligned position; leaves the
// reader byte-aligned.
ColorTransform readColorTransform(SwfReader& reader, bool withAlpha);

}  // namespace flash3ds::swf
