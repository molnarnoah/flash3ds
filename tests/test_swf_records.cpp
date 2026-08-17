#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/SwfRecords.h"

using flash3ds::swf::concatMatrix;
using flash3ds::swf::Matrix;
using flash3ds::swf::readColorTransform;
using flash3ds::swf::readMatrix;
using flash3ds::swf::SwfReader;
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
