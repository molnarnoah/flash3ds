#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/SwfRecords.h"

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
