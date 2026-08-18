#include <cmath>

#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/SwfRecords.h"

using flash3ds::swf::concatMatrix;
using flash3ds::swf::invertMatrix;
using flash3ds::swf::Matrix;
using flash3ds::swf::Point;
using flash3ds::swf::readColorTransform;
using flash3ds::swf::readMatrix;
using flash3ds::swf::Rect;
using flash3ds::swf::rectContainsPoint;
using flash3ds::swf::SwfReader;
using flash3ds::swf::transformPoint;
namespace fixtures = flash3ds::test::fixtures;

TEST_CASE(Matrix_TranslateOnly_RoundTrips) {
    auto bytes = fixtures::buildMatrixBytes(1000, -500);
    SwfReader r(bytes.data(), bytes.size());
    auto m = readMatrix(r);
    CHECK(!r.failed());
    CHECK_EQ(m.translateXTwips, 1000);
    CHECK_EQ(m.translateYTwips, -500);
    // No scale/rotate bits present -> identity defaults.
    CHECK(m.scaleX == 1.0 && m.scaleY == 1.0);
    CHECK(m.rotateSkew0 == 0.0 && m.rotateSkew1 == 0.0);
}

TEST_CASE(Matrix_ZeroTranslate) {
    auto bytes = fixtures::buildMatrixBytes(0, 0);
    SwfReader r(bytes.data(), bytes.size());
    auto m = readMatrix(r);
    CHECK(!r.failed());
    CHECK_EQ(m.translateXTwips, 0);
    CHECK_EQ(m.translateYTwips, 0);
}

TEST_CASE(ColorTransform_NoTermsPresent_IsIdentity) {
    // HasAddTerms=0, HasMultTerms=0, Nbits=0, byte-aligned -> single zero byte.
    const uint8_t data[] = {0x00};
    SwfReader r(data, sizeof(data));
    auto ct = readColorTransform(r, /*withAlpha=*/true);
    CHECK(!r.failed());
    CHECK(ct.redMult == 1.0 && ct.greenMult == 1.0 && ct.blueMult == 1.0 && ct.alphaMult == 1.0);
    CHECK_EQ(ct.redAdd, 0);
}

// --- Phase 3: concatMatrix ---------------------------------------------

TEST_CASE(ConcatMatrix_IdentityParent_ReturnsChildUnchanged) {
    Matrix child;
    child.scaleX = 2.0;
    child.scaleY = 3.0;
    child.translateXTwips = 100;
    child.translateYTwips = -50;

    Matrix world = concatMatrix(Matrix::identity(), child);
    CHECK(world.scaleX == 2.0 && world.scaleY == 3.0);
    CHECK_EQ(world.translateXTwips, 100);
    CHECK_EQ(world.translateYTwips, -50);
}

TEST_CASE(ConcatMatrix_TranslateOnly_AddsTranslations) {
    Matrix parent;
    parent.translateXTwips = 1000;
    parent.translateYTwips = 2000;

    Matrix child;
    child.translateXTwips = 100;
    child.translateYTwips = 50;

    // Pure translation composition: child's translate is carried through
    // parent's identity scale/rotate, then parent's own translate is added.
    Matrix world = concatMatrix(parent, child);
    CHECK_EQ(world.translateXTwips, 1100);
    CHECK_EQ(world.translateYTwips, 2050);
}

TEST_CASE(ConcatMatrix_ScaleComposition_Multiplies) {
    Matrix parent;
    parent.scaleX = 2.0;
    parent.scaleY = 4.0;

    Matrix child;
    child.scaleX = 3.0;
    child.scaleY = 0.5;
    child.translateXTwips = 10;
    child.translateYTwips = 10;

    Matrix world = concatMatrix(parent, child);
    CHECK(world.scaleX == 6.0);   // 2 * 3
    CHECK(world.scaleY == 2.0);   // 4 * 0.5
    // The child's local translate (10, 10) is first scaled by the parent's
    // scale (2x, 4x) before parent's own (zero) translate is added.
    CHECK_EQ(world.translateXTwips, 20);
    CHECK_EQ(world.translateYTwips, 40);
}

// --- hit-testing primitives (interactivity phase, 2026-08-19) --------------

TEST_CASE(InvertMatrix_Identity_IsItself) {
    Matrix inv;
    CHECK(invertMatrix(Matrix::identity(), &inv));
    CHECK(inv.scaleX == 1.0 && inv.scaleY == 1.0);
    CHECK(inv.rotateSkew0 == 0.0 && inv.rotateSkew1 == 0.0);
    CHECK_EQ(inv.translateXTwips, 0);
    CHECK_EQ(inv.translateYTwips, 0);
}

TEST_CASE(InvertMatrix_TranslateOnly_NegatesTranslation) {
    Matrix m;
    m.translateXTwips = 100;
    m.translateYTwips = -50;
    Matrix inv;
    CHECK(invertMatrix(m, &inv));
    CHECK_EQ(inv.translateXTwips, -100);
    CHECK_EQ(inv.translateYTwips, 50);
    // Forward-then-inverse should round-trip an arbitrary point exactly.
    Point p = transformPoint(inv, transformPoint(m, Point{123.0, 456.0}));
    CHECK(std::abs(p.x - 123.0) < 1e-6);
    CHECK(std::abs(p.y - 456.0) < 1e-6);
}

TEST_CASE(InvertMatrix_ScaleAndTranslate_RoundTripsPoint) {
    Matrix m;
    m.scaleX = 2.0;
    m.scaleY = 0.5;
    m.translateXTwips = 200;
    m.translateYTwips = -300;
    Matrix inv;
    CHECK(invertMatrix(m, &inv));
    Point original{37.0, -19.0};
    Point roundTripped = transformPoint(inv, transformPoint(m, original));
    CHECK(std::abs(roundTripped.x - original.x) < 1e-6);
    CHECK(std::abs(roundTripped.y - original.y) < 1e-6);
}

TEST_CASE(InvertMatrix_RotationAndSkew_RoundTripsPoint) {
    Matrix m;
    // A non-trivial rotation-like matrix (not axis-aligned) -- exercises
    // the rotateSkew0/rotateSkew1 cross terms in the inverse formula, not
    // just the diagonal scaleX/scaleY case the two tests above cover.
    m.scaleX = 0.7071;
    m.scaleY = 0.7071;
    m.rotateSkew0 = 0.7071;
    m.rotateSkew1 = -0.7071;
    m.translateXTwips = 50;
    m.translateYTwips = 75;
    Matrix inv;
    CHECK(invertMatrix(m, &inv));
    Point original{100.0, 200.0};
    Point roundTripped = transformPoint(inv, transformPoint(m, original));
    // Looser tolerance than the axis-aligned round-trip tests above: this
    // matrix's rotateSkew coefficients aren't integer-friendly (~0.7071),
    // so BOTH the forward matrix's own translateXTwips/translateYTwips AND
    // the computed inverse's are each independently rounded to the nearest
    // whole twip (Matrix stores translate as int32_t, matching
    // concatMatrix()'s existing, already-accepted rounding convention) --
    // up to ~1 twip of round-trip error is expected and correct here, not
    // a bug in invertMatrix() itself.
    CHECK(std::abs(roundTripped.x - original.x) < 1.0);
    CHECK(std::abs(roundTripped.y - original.y) < 1.0);
}

TEST_CASE(InvertMatrix_DegenerateZeroScale_ReturnsFalse) {
    // _xscale = 0 (or any determinant-zero matrix) -- per docs/hit-
    // testing.md's design, this must be reported as un-invertible, not
    // silently produce garbage or divide-by-zero infinities.
    Matrix m;
    m.scaleX = 0.0;
    m.scaleY = 1.0;
    m.rotateSkew0 = 0.0;
    m.rotateSkew1 = 0.0;
    Matrix inv;
    CHECK(!invertMatrix(m, &inv));
}

TEST_CASE(InvertMatrix_DegenerateRankDeficientSkew_ReturnsFalse) {
    // A non-obviously-degenerate matrix (all fields nonzero) but with
    // determinant == 0 anyway (rotateSkew0/rotateSkew1 chosen so
    // scaleX*scaleY == rotateSkew0*rotateSkew1) -- confirms the check is a
    // real determinant test, not just "is scaleX or scaleY exactly zero."
    Matrix m;
    m.scaleX = 2.0;
    m.scaleY = 2.0;
    m.rotateSkew0 = 1.0;
    m.rotateSkew1 = 4.0;  // det = 2*2 - 1*4 = 0
    Matrix inv;
    CHECK(!invertMatrix(m, &inv));
}

TEST_CASE(RectContainsPoint_InsideAndOnBoundary_True) {
    Rect r{0, 100, 0, 200};
    CHECK(rectContainsPoint(r, Point{50.0, 100.0}));   // strictly inside
    CHECK(rectContainsPoint(r, Point{0.0, 0.0}));      // top-left corner (inclusive)
    CHECK(rectContainsPoint(r, Point{100.0, 200.0}));  // bottom-right corner (inclusive)
}

TEST_CASE(RectContainsPoint_Outside_False) {
    Rect r{0, 100, 0, 200};
    CHECK(!rectContainsPoint(r, Point{-1.0, 100.0}));
    CHECK(!rectContainsPoint(r, Point{101.0, 100.0}));
    CHECK(!rectContainsPoint(r, Point{50.0, -1.0}));
    CHECK(!rectContainsPoint(r, Point{50.0, 201.0}));
}
