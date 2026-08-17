#include "SwfTestFixtures.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>

namespace flash3ds::test::fixtures {

namespace {

void writeU8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }

void writeU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void writeU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

// Minimal bit writer, mirroring SwfReader's bit reader (independently
// implemented for the test builder — not shared code).
class BitWriter {
public:
    explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}

    void writeBits(uint32_t value, int numBits) {
        for (int i = numBits - 1; i >= 0; --i) {
            uint8_t bit = (value >> i) & 1;
            current_ = static_cast<uint8_t>((current_ << 1) | bit);
            ++bitsFilled_;
            if (bitsFilled_ == 8) {
                out_.push_back(current_);
                current_ = 0;
                bitsFilled_ = 0;
            }
        }
    }

    void byteAlign() {
        if (bitsFilled_ > 0) {
            current_ = static_cast<uint8_t>(current_ << (8 - bitsFilled_));
            out_.push_back(current_);
            current_ = 0;
            bitsFilled_ = 0;
        }
    }

private:
    std::vector<uint8_t>& out_;
    uint8_t current_ = 0;
    int bitsFilled_ = 0;
};

int bitsNeededSigned(int32_t v) {
    // Smallest N such that v fits in a signed N-bit field.
    uint32_t magnitude = v < 0 ? static_cast<uint32_t>(-(v + 1)) : static_cast<uint32_t>(v);
    int bits = 1;  // sign bit
    while (magnitude != 0) {
        ++bits;
        magnitude >>= 1;
    }
    return bits;
}

void writeRect(std::vector<uint8_t>& out, int32_t xmin, int32_t xmax, int32_t ymin,
               int32_t ymax) {
    int nbits = 1;
    for (int32_t v : {xmin, xmax, ymin, ymax}) {
        nbits = std::max(nbits, bitsNeededSigned(v));
    }
    BitWriter bw(out);
    bw.writeBits(static_cast<uint32_t>(nbits), 5);
    bw.writeBits(static_cast<uint32_t>(xmin) & ((1u << nbits) - 1u), nbits);
    bw.writeBits(static_cast<uint32_t>(xmax) & ((1u << nbits) - 1u), nbits);
    bw.writeBits(static_cast<uint32_t>(ymin) & ((1u << nbits) - 1u), nbits);
    bw.writeBits(static_cast<uint32_t>(ymax) & ((1u << nbits) - 1u), nbits);
    bw.byteAlign();
}

void writeTagHeader(std::vector<uint8_t>& out, uint16_t code, uint32_t length) {
    if (length < 0x3F) {
        uint16_t header = static_cast<uint16_t>((code << 6) | length);
        writeU16(out, header);
    } else {
        uint16_t header = static_cast<uint16_t>((code << 6) | 0x3F);
        writeU16(out, header);
        writeU32(out, length);
    }
}

}  // namespace

std::vector<uint8_t> buildMovieBody(int32_t stageWidthTwips, int32_t stageHeightTwips,
                                     double frameRateFps, uint16_t frameCount,
                                     const std::vector<FixtureTag>& tags) {
    std::vector<uint8_t> body;
    writeRect(body, 0, stageWidthTwips, 0, stageHeightTwips);

    uint16_t frameRateFixed8 = static_cast<uint16_t>(std::lround(frameRateFps * 256.0));
    writeU16(body, frameRateFixed8);
    writeU16(body, frameCount);

    bool sawEnd = false;
    for (const auto& tag : tags) {
        writeTagHeader(body, tag.code, static_cast<uint32_t>(tag.body.size()));
        body.insert(body.end(), tag.body.begin(), tag.body.end());
        if (tag.code == 0) {
            sawEnd = true;
        }
    }
    if (!sawEnd) {
        writeTagHeader(body, 0 /* End */, 0);
    }
    return body;
}

std::vector<uint8_t> wrapFws(uint8_t version, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> out;
    uint32_t totalLength = static_cast<uint32_t>(8 + body.size());
    out.push_back('F');
    out.push_back('W');
    out.push_back('S');
    writeU8(out, version);
    writeU32(out, totalLength);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::vector<uint8_t> wrapCws(uint8_t version, const std::vector<uint8_t>& body) {
    uLongf compressedBound = compressBound(static_cast<uLong>(body.size()));
    std::vector<uint8_t> compressed(compressedBound);
    uLongf compressedSize = compressedBound;

    int ret = compress2(compressed.data(), &compressedSize,
                          body.empty() ? reinterpret_cast<const Bytef*>("") : body.data(),
                          static_cast<uLong>(body.size()), Z_BEST_COMPRESSION);
    compressed.resize(ret == Z_OK ? compressedSize : 0);

    std::vector<uint8_t> out;
    uint32_t totalLength = static_cast<uint32_t>(8 + body.size());  // uncompressed total, per spec
    out.push_back('C');
    out.push_back('W');
    out.push_back('S');
    writeU8(out, version);
    writeU32(out, totalLength);
    out.insert(out.end(), compressed.begin(), compressed.end());
    return out;
}

std::vector<uint8_t> minimalFwsMovie() {
    std::vector<FixtureTag> tags = {
        {1 /* ShowFrame */, {}},
    };
    auto body = buildMovieBody(550 * 20, 400 * 20, 24.0, 1, tags);
    return wrapFws(6, body);
}

std::vector<uint8_t> minimalCwsMovie() {
    std::vector<FixtureTag> tags = {
        {1 /* ShowFrame */, {}},
    };
    auto body = buildMovieBody(550 * 20, 400 * 20, 24.0, 1, tags);
    return wrapCws(6, body);
}

std::vector<uint8_t> movieWithActionScript() {
    std::vector<FixtureTag> tags = {
        {12 /* DoAction */, {0x00}},  // single ActionEnd opcode byte
        {1 /* ShowFrame */, {}},
    };
    auto body = buildMovieBody(200 * 20, 200 * 20, 25.0, 1, tags);
    return wrapFws(6, body);
}

// --- Phase 2 record/tag body builders --------------------------------

std::vector<uint8_t> buildMatrixBytes(int32_t translateXTwips, int32_t translateYTwips) {
    std::vector<uint8_t> out;
    BitWriter bw(out);
    bw.writeBits(0, 1);  // HasScale = 0
    bw.writeBits(0, 1);  // HasRotate = 0

    int nbits = std::max(bitsNeededSigned(translateXTwips), bitsNeededSigned(translateYTwips));
    bw.writeBits(static_cast<uint32_t>(nbits), 5);
    bw.writeBits(static_cast<uint32_t>(translateXTwips) & ((1u << nbits) - 1u), nbits);
    bw.writeBits(static_cast<uint32_t>(translateYTwips) & ((1u << nbits) - 1u), nbits);
    bw.byteAlign();
    return out;
}

std::vector<uint8_t> buildPlaceObjectV1Bytes(uint16_t characterId, uint16_t depth,
                                              const std::vector<uint8_t>& matrixBytes) {
    std::vector<uint8_t> out;
    writeU16(out, characterId);
    writeU16(out, depth);
    out.insert(out.end(), matrixBytes.begin(), matrixBytes.end());
    return out;
}

std::vector<uint8_t> buildPlaceObject2Bytes(uint16_t depth, bool move,
                                             std::optional<uint16_t> characterId,
                                             std::optional<std::vector<uint8_t>> matrixBytes,
                                             std::optional<std::string> name) {
    bool hasCharacter = characterId.has_value();
    bool hasMatrix = matrixBytes.has_value();
    bool hasName = name.has_value();

    uint8_t flags = static_cast<uint8_t>((hasName ? 0x20 : 0) | (hasMatrix ? 0x04 : 0) |
                                          (hasCharacter ? 0x02 : 0) | (move ? 0x01 : 0));

    std::vector<uint8_t> out;
    writeU8(out, flags);
    writeU16(out, depth);
    if (hasCharacter) {
        writeU16(out, *characterId);
    }
    if (hasMatrix) {
        out.insert(out.end(), matrixBytes->begin(), matrixBytes->end());
    }
    if (hasName) {
        out.insert(out.end(), name->begin(), name->end());
        out.push_back(0);
    }
    return out;
}

std::vector<uint8_t> buildRemoveObject2Bytes(uint16_t depth) {
    std::vector<uint8_t> out;
    writeU16(out, depth);
    return out;
}

std::vector<uint8_t> buildFrameLabelBytes(const std::string& name) {
    std::vector<uint8_t> out(name.begin(), name.end());
    out.push_back(0);
    return out;
}

}  // namespace flash3ds::test::fixtures
