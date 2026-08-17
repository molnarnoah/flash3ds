#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "swf/ShapeRecords.h"

using flash3ds::swf::FillStyleType;
using flash3ds::swf::readFillStyleArray;
using flash3ds::swf::readLineStyleArray;
using flash3ds::swf::readShapeWithStyle;
using flash3ds::swf::ShapeRecordType;
using flash3ds::swf::SwfReader;
namespace fixtures = flash3ds::test::fixtures;

TEST_CASE(FillStyleArray_SingleSolid_ParsesColorAndType) {
    auto bytes = fixtures::buildSolidFillStyleArrayBytes(0x11, 0x22, 0x33, 0xFF, /*shapeVersion=*/2);
    SwfReader r(bytes.data(), bytes.size());
    auto styles = readFillStyleArray(r, /*shapeVersion=*/2);
    CHECK(!r.failed());
    CHECK_EQ(styles.size(), static_cast<size_t>(1));
    CHECK(styles[0].isSolid());
    CHECK_EQ(styles[0].type, FillStyleType::kSolid);
    CHECK_EQ(styles[0].solidColor.r, 0x11);
    CHECK_EQ(styles[0].solidColor.g, 0x22);
    CHECK_EQ(styles[0].solidColor.b, 0x33);
    // shapeVersion 2 uses RGB (no alpha byte in the stream) -> default alpha.
    CHECK_EQ(styles[0].solidColor.a, 255);
}

TEST_CASE(FillStyleArray_SingleSolid_Version3ReadsAlpha) {
    auto bytes = fixtures::buildSolidFillStyleArrayBytes(0x44, 0x55, 0x66, 0x80, /*shapeVersion=*/3);
    SwfReader r(bytes.data(), bytes.size());
    auto styles = readFillStyleArray(r, /*shapeVersion=*/3);
    CHECK(!r.failed());
    CHECK_EQ(styles[0].solidColor.a, 0x80);
}

TEST_CASE(LineStyleArray_Empty_ParsesZeroStyles) {
    auto bytes = fixtures::buildEmptyLineStyleArrayBytes();
    SwfReader r(bytes.data(), bytes.size());
    auto styles = readLineStyleArray(r, /*shapeVersion=*/2);
    CHECK(!r.failed());
    CHECK_EQ(styles.size(), static_cast<size_t>(0));
}

TEST_CASE(LineStyleArray_SingleStyle_ParsesWidthAndColor) {
    auto bytes = fixtures::buildSolidLineStyleArrayBytes(20, 0x10, 0x20, 0x30, 0xFF, 2);
    SwfReader r(bytes.data(), bytes.size());
    auto styles = readLineStyleArray(r, 2);
    CHECK(!r.failed());
    CHECK_EQ(styles.size(), static_cast<size_t>(1));
    CHECK_EQ(styles[0].widthTwips, 20);
    CHECK_EQ(styles[0].color.r, 0x10);
}

TEST_CASE(ShapeWithStyle_Rectangle_ParsesFillStylesAndRecords) {
    auto bytes = fixtures::buildSolidRectShapeWithStyleBytes(2, 0xAA, 0xBB, 0xCC, 0xFF,
                                                                200 * 20, 100 * 20);
    SwfReader r(bytes.data(), bytes.size());
    auto shape = readShapeWithStyle(r, 2);
    CHECK(!r.failed());
    CHECK_EQ(shape.fillStyles.size(), static_cast<size_t>(1));
    CHECK_EQ(shape.lineStyles.size(), static_cast<size_t>(0));

    // Expect: one StyleChange (MoveTo) record, three StraightEdge records.
    CHECK_EQ(shape.records.size(), static_cast<size_t>(4));
    CHECK(shape.records[0].type == ShapeRecordType::kStyleChange);
    CHECK(shape.records[0].hasMoveTo);
    CHECK_EQ(shape.records[0].moveToXTwips, 0);
    CHECK_EQ(shape.records[0].moveToYTwips, 0);
    CHECK(shape.records[0].fillStyle1.has_value());
    CHECK_EQ(*shape.records[0].fillStyle1, static_cast<uint32_t>(1));

    CHECK(shape.records[1].type == ShapeRecordType::kStraightEdge);
    CHECK_EQ(shape.records[1].deltaXTwips, 200 * 20);
    CHECK_EQ(shape.records[1].deltaYTwips, 0);

    CHECK(shape.records[2].type == ShapeRecordType::kStraightEdge);
    CHECK_EQ(shape.records[2].deltaXTwips, 0);
    CHECK_EQ(shape.records[2].deltaYTwips, 100 * 20);

    CHECK(shape.records[3].type == ShapeRecordType::kStraightEdge);
    CHECK_EQ(shape.records[3].deltaXTwips, -200 * 20);
    CHECK_EQ(shape.records[3].deltaYTwips, 0);
}

TEST_CASE(ShapeWithStyle_WithLineStyle_ParsesLineStyleIndex) {
    auto fillBytes = fixtures::buildSolidFillStyleArrayBytes(0, 0, 0, 255, 2);
    auto lineBytes = fixtures::buildSolidLineStyleArrayBytes(10, 255, 0, 0, 255, 2);
    auto recordBytes = fixtures::buildRectShapeRecordsBytes(50 * 20, 50 * 20, /*withLine=*/true);

    std::vector<uint8_t> bytes = fillBytes;
    bytes.insert(bytes.end(), lineBytes.begin(), lineBytes.end());
    bytes.insert(bytes.end(), recordBytes.begin(), recordBytes.end());

    SwfReader r(bytes.data(), bytes.size());
    auto shape = readShapeWithStyle(r, 2);
    CHECK(!r.failed());
    CHECK_EQ(shape.lineStyles.size(), static_cast<size_t>(1));
    CHECK(shape.records[0].lineStyleIndex.has_value());
    CHECK_EQ(*shape.records[0].lineStyleIndex, static_cast<uint32_t>(1));
}
