#include "TestFramework.h"
#include "swf/SwfReader.h"

using flash3ds::swf::Rect;
using flash3ds::swf::SwfReader;

TEST_CASE(SwfReader_U16_U32_LittleEndian) {
    const uint8_t data[] = {0x34, 0x12, 0x78, 0x56, 0x34, 0x12};
    SwfReader r(data, sizeof(data));
    CHECK_EQ(r.readU16(), 0x1234);
    CHECK_EQ(r.readU32(), 0x12345678);
}

TEST_CASE(SwfReader_ReadsPastEnd_SetsFailedWithoutCrashing) {
    const uint8_t data[] = {0x01};
    SwfReader r(data, sizeof(data));
    CHECK_EQ(r.readU8(), 0x01);
    CHECK(!r.failed());

    uint8_t v = r.readU8();  // past end
    CHECK_EQ(v, 0);
    CHECK(r.failed());

    // Further reads keep returning 0, never crash.
    CHECK_EQ(r.readU32(), 0u);
    CHECK(r.failed());
}

TEST_CASE(SwfReader_BitReads_RoundTrip) {
    // 0b101_11001 packed into 2 bytes: 3-bit value 5, 5-bit value 25.
    // 101 11001 -> byte0 = 10111001 = 0xB9
    const uint8_t data[] = {0xB9};
    SwfReader r(data, sizeof(data));
    CHECK_EQ(r.readUBits(3), 0b101u);
    CHECK_EQ(r.readUBits(5), 0b11001u);
}

TEST_CASE(SwfReader_SignedBits_SignExtend) {
    // 5-bit field, value -1 => all-ones pattern 11111.
    const uint8_t data[] = {0b11111000};
    SwfReader r(data, sizeof(data));
    int32_t v = r.readSBits(5);
    CHECK_EQ(v, -1);
}

TEST_CASE(SwfReader_ReadRect_MatchesKnownEncoding) {
    // nbits=8 (5 bits) followed by 4 signed 8-bit fields: 0, 100, 0, 80.
    // Build manually using the bit layout: 01000 | 00000000 | 01100100 | 00000000 | 01010000
    // then byte-align (pad). Easiest to just construct via a small local bit writer here.
    std::vector<uint8_t> bytes;
    uint32_t acc = 0;
    int accBits = 0;
    auto push = [&](uint32_t value, int bits) {
        for (int i = bits - 1; i >= 0; --i) {
            acc = (acc << 1) | ((value >> i) & 1);
            ++accBits;
            if (accBits == 8) {
                bytes.push_back(static_cast<uint8_t>(acc));
                acc = 0;
                accBits = 0;
            }
        }
    };
    push(8, 5);        // nbits = 8
    push(0, 8);         // xmin
    push(100, 8);        // xmax
    push(0, 8);          // ymin
    push(80, 8);          // ymax
    if (accBits > 0) {
        acc <<= (8 - accBits);
        bytes.push_back(static_cast<uint8_t>(acc));
    }

    SwfReader r(bytes.data(), bytes.size());
    Rect rect = r.readRect();
    CHECK(!r.failed());
    CHECK_EQ(rect.xMin, 0);
    CHECK_EQ(rect.xMax, 100);
    CHECK_EQ(rect.yMin, 0);
    CHECK_EQ(rect.yMax, 80);
}

TEST_CASE(SwfReader_Skip_And_BytesRemaining) {
    const uint8_t data[] = {1, 2, 3, 4, 5};
    SwfReader r(data, sizeof(data));
    CHECK_EQ(r.bytesRemaining(), 5u);
    r.skip(3);
    CHECK(!r.failed());
    CHECK_EQ(r.bytesRemaining(), 2u);
    CHECK_EQ(r.readU8(), 4);

    r.skip(10);  // way past end
    CHECK(r.failed());
}

TEST_CASE(SwfReader_CString_Bounded) {
    const uint8_t data[] = {'h', 'i', 0, 'X'};
    SwfReader r(data, sizeof(data));
    std::string s = r.readCString();
    CHECK_EQ(s, std::string("hi"));
    CHECK(!r.failed());
}
