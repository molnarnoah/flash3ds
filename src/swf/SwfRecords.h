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

// Composes a child's local transform with its parent's, producing the
// child's world transform: concatMatrix(parent, child) applies `child`
// first, then `parent` (standard nested-display-object convention — a
// nested sprite's placement matrix is relative to its parent's coordinate
// space). Equivalent to the usual 2x3 affine "parent * child" matrix
// multiply.
Matrix concatMatrix(const Matrix& parent, const Matrix& child);

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

// Composes a child's color transform with its parent's, producing the
// child's EFFECTIVE (world) color transform: concatColorTransform(parent,
// child) applies `child` first, then `parent` — same "parent-then-child"
// convention as concatMatrix, and for the same reason (a nested display
// object's authored/scripted color transform is relative to its parent's
// already-transformed color, exactly like its placement matrix is relative
// to its parent's coordinate space). Compose formula (standard affine
// "mult then add" composition): out.mult = parent.mult * child.mult;
// out.add = parent.mult * child.add + parent.add.
//
// Added 2026-08-18 (compatibility-audit phase, priority #1 fix): this
// function did not exist before — ColorTransform was parsed and stored
// per-MovieClipInstance (Phase 5+, e.g. `_alpha`) but never composed down
// the tree or applied to any rendered pixel. See docs/known-limitations.md
// and docs/compatibility-matrix.md for the audit finding, and
// SceneRenderer.cpp for where this is now actually used.
ColorTransform concatColorTransform(const ColorTransform& parent, const ColorTransform& child);

// Reads a MATRIX record. Must be called at a byte-aligned position (or
// right after another bit-packed record); leaves the reader byte-aligned.
Matrix readMatrix(SwfReader& reader);

// Reads a CXFORM (withAlpha == false) or CXFORMWITHALPHA (withAlpha ==
// true) record. Must be called at a byte-aligned position; leaves the
// reader byte-aligned.
ColorTransform readColorTransform(SwfReader& reader, bool withAlpha);

}  // namespace flash3ds::swf
