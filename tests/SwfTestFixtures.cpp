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

void writeS16(std::vector<uint8_t>& out, int16_t v) { writeU16(out, static_cast<uint16_t>(v)); }

void writeCString(std::vector<uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
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

// ExportAssets (tag 56) body: Count (UI16), then Count x {Tag (UI16
// characterId), Name (STRING, null-terminated)} — see CharacterDictionary.
// cpp's parseExportAssets() for the reading side this mirrors.
std::vector<uint8_t> buildExportAssetsBytes(
    const std::vector<std::pair<uint16_t, std::string>>& entries) {
    std::vector<uint8_t> out;
    writeU16(out, static_cast<uint16_t>(entries.size()));
    for (const auto& [characterId, name] : entries) {
        writeU16(out, characterId);
        writeCString(out, name);
    }
    return out;
}

// --- Phase 3: shape / sprite body builders ----------------------------

std::vector<uint8_t> buildSolidFillStyleArrayBytes(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                                     int shapeVersion) {
    std::vector<uint8_t> out;
    writeU8(out, 1);     // count = 1
    writeU8(out, 0x00);  // FillStyleType::kSolid
    writeU8(out, r);
    writeU8(out, g);
    writeU8(out, b);
    if (shapeVersion >= 3) {
        writeU8(out, a);
    }
    return out;
}

std::vector<uint8_t> buildEmptyLineStyleArrayBytes() {
    std::vector<uint8_t> out;
    writeU8(out, 0);
    return out;
}

std::vector<uint8_t> buildSolidLineStyleArrayBytes(uint16_t widthTwips, uint8_t r, uint8_t g,
                                                     uint8_t b, uint8_t a, int shapeVersion) {
    std::vector<uint8_t> out;
    writeU8(out, 1);  // count = 1
    writeU16(out, widthTwips);
    writeU8(out, r);
    writeU8(out, g);
    writeU8(out, b);
    if (shapeVersion >= 3) {
        writeU8(out, a);
    }
    return out;
}

std::vector<uint8_t> buildRectShapeRecordsBytes(int32_t widthTwips, int32_t heightTwips,
                                                 bool withLine) {
    std::vector<uint8_t> out;
    BitWriter bw(out);

    int numFillBits = 1;  // fillStyle index 1 fits in 1 bit
    int numLineBits = withLine ? 1 : 0;
    bw.writeBits(static_cast<uint32_t>(numFillBits), 4);
    bw.writeBits(static_cast<uint32_t>(numLineBits), 4);

    // StyleChangeRecord: MoveTo(0,0), FillStyle1=1[, LineStyle=1]. Field
    // order matches ShapeRecords.cpp's read order: NewStyles, LineStyle,
    // FillStyle1, FillStyle0, MoveTo flag bits, then (if set) MoveTo data,
    // FillStyle0 index, FillStyle1 index, LineStyle index.
    bw.writeBits(0, 1);                  // TypeFlag = 0 (style-change, not edge)
    bw.writeBits(0, 1);                  // NewStyles
    bw.writeBits(withLine ? 1 : 0, 1);   // LineStyle
    bw.writeBits(1, 1);                  // FillStyle1
    bw.writeBits(0, 1);                  // FillStyle0
    bw.writeBits(1, 1);                  // MoveTo
    bw.writeBits(1, 5);                  // MoveBits = 1 (enough to encode 0)
    bw.writeBits(0, 1);                  // MoveToX = 0
    bw.writeBits(0, 1);                  // MoveToY = 0
    bw.writeBits(1u & ((1u << numFillBits) - 1u), numFillBits);  // FillStyle1 index = 1
    if (withLine) {
        bw.writeBits(1u & ((1u << numLineBits) - 1u), numLineBits);  // LineStyle index = 1
    }

    auto writeStraightEdge = [&](int32_t dx, int32_t dy, bool vertical) {
        int32_t magnitude = vertical ? dy : dx;
        int bits = std::max(2, bitsNeededSigned(magnitude));
        bw.writeBits(1, 1);                                 // TypeFlag = 1 (edge)
        bw.writeBits(1, 1);                                 // StraightFlag = 1
        bw.writeBits(static_cast<uint32_t>(bits - 2), 4);   // NumBits - 2
        bw.writeBits(0, 1);                                 // GeneralLineFlag = 0
        bw.writeBits(vertical ? 1 : 0, 1);                  // VertLineFlag
        bw.writeBits(static_cast<uint32_t>(magnitude) & ((1u << bits) - 1u), bits);
    };

    // Three explicit edges (right, down, left); the closing edge back to
    // the MoveTo origin is left implicit, same as ShapeTessellator's
    // implicit-closure convention.
    writeStraightEdge(widthTwips, 0, false);
    writeStraightEdge(0, heightTwips, true);
    writeStraightEdge(-widthTwips, 0, false);

    // EndShapeRecord: TypeFlag=0 followed by five zero flag bits.
    bw.writeBits(0, 1);
    bw.writeBits(0, 5);
    bw.byteAlign();

    return out;
}

std::vector<uint8_t> buildSolidRectShapeWithStyleBytes(int shapeVersion, uint8_t r, uint8_t g,
                                                         uint8_t b, uint8_t a,
                                                         int32_t widthTwips,
                                                         int32_t heightTwips) {
    std::vector<uint8_t> out = buildSolidFillStyleArrayBytes(r, g, b, a, shapeVersion);
    auto lineStyles = buildEmptyLineStyleArrayBytes();
    out.insert(out.end(), lineStyles.begin(), lineStyles.end());
    auto records = buildRectShapeRecordsBytes(widthTwips, heightTwips, false);
    out.insert(out.end(), records.begin(), records.end());
    return out;
}

std::vector<uint8_t> buildShapeWithMidStreamNewStylesBytes(int shapeVersion, uint8_t r1,
                                                             uint8_t g1, uint8_t b1, uint8_t r2,
                                                             uint8_t g2, uint8_t b2) {
    // Initial (pre-record-stream) FillStyleArray/LineStyleArray — both
    // byte-level structures, written directly rather than via BitWriter.
    std::vector<uint8_t> out = buildSolidFillStyleArrayBytes(r1, g1, b1, 255, shapeVersion);
    auto lineStyles = buildEmptyLineStyleArrayBytes();
    out.insert(out.end(), lineStyles.begin(), lineStyles.end());

    BitWriter bw(out);
    bw.writeBits(1, 4);  // NumFillBits = 1
    bw.writeBits(0, 4);  // NumLineBits = 0

    // Mid-stream StyleChangeRecord: only StateNewStyles set (no MoveTo/
    // FillStyle/LineStyle fields to read) — 6 bits total, landing at a
    // non-byte-aligned position (bit 6 of the current byte).
    bw.writeBits(0, 1);  // TypeFlag = 0 (style-change)
    bw.writeBits(1, 1);  // StateNewStyles = 1
    bw.writeBits(0, 1);  // StateLineStyle = 0
    bw.writeBits(0, 1);  // StateFillStyle1 = 0
    bw.writeBits(0, 1);  // StateFillStyle0 = 0
    bw.writeBits(0, 1);  // StateMoveTo = 0

    // Per spec, the new style arrays are byte-aligned — a real encoder
    // pads here before writing them. This is exactly the alignment the
    // Phase 9 bug (missing reader.byteAlign()) failed to resync to.
    bw.byteAlign();

    // New FillStyleArray: one solid fill, color 2.
    writeU8(out, 1);     // count = 1
    writeU8(out, 0x00);  // FillStyleType::kSolid
    writeU8(out, r2);
    writeU8(out, g2);
    writeU8(out, b2);
    if (shapeVersion >= 3) writeU8(out, 255);
    // New LineStyleArray: empty.
    writeU8(out, 0);

    // New NumFillBits/NumLineBits (bit-packed again, but the reader is
    // byte-aligned here so this restarts cleanly).
    bw.writeBits(1, 4);  // NumFillBits = 1
    bw.writeBits(0, 4);  // NumLineBits = 0

    // EndShapeRecord: TypeFlag=0 followed by five zero state bits.
    bw.writeBits(0, 1);
    bw.writeBits(0, 5);
    bw.byteAlign();

    return out;
}

std::vector<uint8_t> buildDefineShapeBytes(int shapeVersion, uint16_t characterId,
                                            int32_t widthTwips, int32_t heightTwips, uint8_t r,
                                            uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> out;
    writeU16(out, characterId);
    writeRect(out, 0, widthTwips, 0, heightTwips);
    auto shapeBytes =
        buildSolidRectShapeWithStyleBytes(shapeVersion, r, g, b, a, widthTwips, heightTwips);
    out.insert(out.end(), shapeBytes.begin(), shapeBytes.end());
    return out;
}

std::vector<uint8_t> buildLinearGradientFillStyleArrayBytes(
    const std::vector<uint8_t>& gradientMatrixBytes, const std::vector<GradientStopFixture>& stops,
    int shapeVersion) {
    std::vector<uint8_t> out;
    writeU8(out, 1);     // count = 1
    writeU8(out, 0x10);  // FillStyleType::kLinearGradient
    out.insert(out.end(), gradientMatrixBytes.begin(), gradientMatrixBytes.end());

    // GRADIENT record: one flags byte (SpreadMode=00/kPad in bits 7-6,
    // InterpolationMode=00/kNormal in bits 5-4, NumGradients in bits 3-0),
    // then each stop's Ratio(UI8)+Color. Byte-aligned already (matrix bytes
    // above end byte-aligned — buildMatrixBytes()/readMatrix() both
    // byteAlign()), matching readGradient()'s plain readU8() reads.
    writeU8(out, static_cast<uint8_t>(stops.size() & 0xF));
    for (const auto& s : stops) {
        writeU8(out, s.ratio);
        writeU8(out, s.r);
        writeU8(out, s.g);
        writeU8(out, s.b);
        if (shapeVersion >= 3) {
            writeU8(out, s.a);
        }
    }
    return out;
}

std::vector<uint8_t> buildDefineShapeWithLinearGradientBytes(
    int shapeVersion, uint16_t characterId, int32_t widthTwips, int32_t heightTwips,
    const std::vector<uint8_t>& gradientMatrixBytes, const std::vector<GradientStopFixture>& stops) {
    std::vector<uint8_t> out;
    writeU16(out, characterId);
    writeRect(out, 0, widthTwips, 0, heightTwips);

    auto fillStyles = buildLinearGradientFillStyleArrayBytes(gradientMatrixBytes, stops, shapeVersion);
    out.insert(out.end(), fillStyles.begin(), fillStyles.end());
    auto lineStyles = buildEmptyLineStyleArrayBytes();
    out.insert(out.end(), lineStyles.begin(), lineStyles.end());
    auto records = buildRectShapeRecordsBytes(widthTwips, heightTwips, false);
    out.insert(out.end(), records.begin(), records.end());
    return out;
}

std::vector<uint8_t> buildDefineSpriteBytes(uint16_t characterId, uint16_t frameCount,
                                             const std::vector<FixtureTag>& nestedTags) {
    std::vector<uint8_t> out;
    writeU16(out, characterId);
    writeU16(out, frameCount);

    bool sawEnd = false;
    for (const auto& tag : nestedTags) {
        writeTagHeader(out, tag.code, static_cast<uint32_t>(tag.body.size()));
        out.insert(out.end(), tag.body.begin(), tag.body.end());
        if (tag.code == 0) {
            sawEnd = true;
        }
    }
    if (!sawEnd) {
        writeTagHeader(out, 0 /* End */, 0);
    }
    return out;
}

// --- Phase 6: sound / clip-action builders ------------------------------

std::vector<uint8_t> buildDefineSoundBytes(uint16_t soundId, uint8_t format, uint8_t rate,
                                            bool is16Bit, bool stereo, uint32_t sampleCount,
                                            const std::vector<uint8_t>& sampleDataBytes) {
    std::vector<uint8_t> out;
    writeU16(out, soundId);
    uint8_t flags = static_cast<uint8_t>(((format & 0x0F) << 4) | ((rate & 0x03) << 2) |
                                          (is16Bit ? 0x02 : 0) | (stereo ? 0x01 : 0));
    writeU8(out, flags);
    writeU32(out, sampleCount);
    out.insert(out.end(), sampleDataBytes.begin(), sampleDataBytes.end());
    return out;
}

std::vector<uint8_t> buildSoundInfoBytes(bool syncStop, bool syncNoMultiple,
                                          std::optional<uint32_t> inPointSamples,
                                          std::optional<uint32_t> outPointSamples,
                                          std::optional<uint16_t> loopCount) {
    bool hasInPoint = inPointSamples.has_value();
    bool hasOutPoint = outPointSamples.has_value();
    bool hasLoops = loopCount.has_value();

    std::vector<uint8_t> out;
    uint8_t flags = static_cast<uint8_t>((syncStop ? 0x20 : 0) | (syncNoMultiple ? 0x10 : 0) |
                                          (hasLoops ? 0x04 : 0) | (hasOutPoint ? 0x02 : 0) |
                                          (hasInPoint ? 0x01 : 0));
    writeU8(out, flags);
    if (hasInPoint) writeU32(out, *inPointSamples);
    if (hasOutPoint) writeU32(out, *outPointSamples);
    if (hasLoops) writeU16(out, *loopCount);
    // HasEnvelope always 0 here — no fixture currently needs envelope
    // points; add a parameter if a future test does.
    return out;
}

std::vector<uint8_t> buildStartSoundBytes(uint16_t soundId,
                                           const std::vector<uint8_t>& soundInfoBytes) {
    std::vector<uint8_t> out;
    writeU16(out, soundId);
    out.insert(out.end(), soundInfoBytes.begin(), soundInfoBytes.end());
    return out;
}

const std::vector<uint8_t>& sampleMp3AudioBytes() {
    // Generated 2026-08-21 via:
    //   ffmpeg -f lavfi -i "sine=frequency=440:duration=0.05:sample_rate=44100" \
    //       -ac 1 -codec:a libmp3lame -b:a 32k -write_id3v2 0 -id3v2_version 0 \
    //       test_tone.mp3
    // 495 bytes: a real MPEG-1 Layer III stream (mono, 44100 Hz, ~50ms of a
    // 440Hz sine tone), no ID3/container framing -- frame sync starts at
    // byte 0. See this file's declaration in SwfTestFixtures.h for why an
    // external encoder's output is used here rather than hand-built bytes.
    static const std::vector<uint8_t> kBytes = {
        0xff, 0xfb, 0x40, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x6e, 0x66, 0x6f, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x01, 0xef, 0x00, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93,
        0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93,
        0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca,
        0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca,
        0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xca, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x4c, 0x61, 0x76,
        0x63, 0x36, 0x30, 0x2e, 0x33, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x24, 0x02, 0xa3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xef, 0x28, 0xb6, 0xd4,
        0xc3, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xfb, 0x10, 0xc4, 0x00, 0x00, 0x04, 0x74, 0x13, 0x55,
        0x54, 0x90, 0x80, 0x30, 0xa6, 0x09, 0xaf, 0x37, 0x1a, 0x20, 0x02, 0x00, 0x01, 0xad, 0x39, 0x40,
        0x00, 0x01, 0x59, 0x3a, 0x3d, 0x50, 0x50, 0x08, 0x06, 0x09, 0x01, 0xf0, 0x7c, 0x1f, 0x07, 0xca,
        0x02, 0x00, 0x80, 0x61, 0x10, 0x7c, 0x1f, 0xd4, 0x08, 0x3b, 0x13, 0x87, 0xf8, 0x83, 0x70, 0x04,
        0x93, 0xf6, 0xc0, 0x60, 0x38, 0x1c, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x89, 0x2a, 0x99,
        0x14, 0x64, 0x08, 0xe9, 0x02, 0x48, 0x16, 0xa3, 0xf7, 0x85, 0x01, 0xf0, 0x13, 0x1b, 0xf0, 0x22,
        0x94, 0x2f, 0xa8, 0x1a, 0x12, 0xfc, 0x24, 0x0d, 0x2a, 0x0a, 0x00, 0x18, 0x30, 0x00, 0xff, 0xfb,
        0x12, 0xc4, 0x02, 0x83, 0xc5, 0x58, 0x1d, 0x20, 0x1d, 0xe0, 0x00, 0x28, 0xa4, 0x83, 0xa4, 0x82,
        0xbc, 0x00, 0x05, 0xcc, 0x09, 0x00, 0xbc, 0x40, 0x04, 0x86, 0x00, 0xe0, 0x78, 0x67, 0xee, 0xf6,
        0xa6, 0x63, 0x03, 0x96, 0x61, 0xc4, 0x11, 0x26, 0x0c, 0x00, 0x7e, 0x60, 0x42, 0x06, 0x06, 0x05,
        0x20, 0x4c, 0x60, 0x5e, 0x03, 0xc5, 0x9a, 0xb4, 0x95, 0xf9, 0x48, 0xf3, 0x01, 0x30, 0x11, 0x30,
        0x00, 0x03, 0x63, 0x03, 0x60, 0x84, 0x33, 0x6e, 0x50, 0xd3, 0x2e, 0xb1, 0x77, 0x30, 0xbf, 0x07,
        0xd3, 0x05, 0x90, 0x1d, 0x30, 0x0b, 0x02, 0xd3, 0x02, 0x50, 0x1f, 0x30, 0x23, 0x01, 0xb4, 0x4f,
        0x9f, 0x49, 0x03, 0x92, 0x48, 0x00, 0x0a, 0xff, 0xfb, 0x10, 0xc4, 0x02, 0x80, 0x04, 0xb4, 0x43,
        0x52, 0xb9, 0x92, 0x80, 0x10, 0x97, 0x06, 0xa6, 0xeb, 0x98, 0x30, 0x04, 0x61, 0x11, 0xd2, 0xa1,
        0x4c, 0x16, 0xe9, 0x9a, 0xd1, 0x5c, 0xfa, 0x22, 0xaa, 0xf9, 0x12, 0xcc, 0xbb, 0xbf, 0x7f, 0x37,
        0x96, 0x4f, 0xe0, 0x61, 0x5f, 0xc7, 0x8b, 0x17, 0xc0, 0xc7, 0x7e, 0x15, 0x50, 0x0c, 0x5d, 0x85,
        0xc0, 0x00, 0x00, 0x26, 0x12, 0x84, 0x62, 0x73, 0xcc, 0x92, 0x41, 0xa8, 0x1d, 0x5e, 0x49, 0x12,
        0x42, 0x95, 0x2d, 0x3c, 0x94, 0x49, 0x14, 0x14, 0x05, 0x63, 0x18, 0x53, 0xbc, 0x4b, 0x74, 0xa8,
        0x2d, 0xc4, 0xaa, 0x4c, 0x41, 0x4d, 0x45, 0x33, 0x2e, 0x31, 0x30, 0x30, 0xaa, 0xaa, 0xaa,
    };
    return kBytes;
}

std::vector<uint8_t> buildPlaceObject2WithClipActionsBytes(
    uint16_t depth, std::optional<uint16_t> characterId,
    std::optional<std::vector<uint8_t>> matrixBytes, std::optional<std::string> name,
    const std::vector<ClipActionFixture>& clipActions) {
    bool hasCharacter = characterId.has_value();
    bool hasMatrix = matrixBytes.has_value();
    bool hasName = name.has_value();

    // Move=0: this helper is for a brand-new placement (the common case for
    // a fixture exercising onClipEvent(load) etc.), not an in-place update.
    uint8_t flags = static_cast<uint8_t>(0x80 /* HasClipActions */ | (hasName ? 0x20 : 0) |
                                          (hasMatrix ? 0x04 : 0) | (hasCharacter ? 0x02 : 0));

    std::vector<uint8_t> out;
    writeU8(out, flags);
    writeU16(out, depth);
    if (hasCharacter) writeU16(out, *characterId);
    if (hasMatrix) out.insert(out.end(), matrixBytes->begin(), matrixBytes->end());
    if (hasName) {
        out.insert(out.end(), name->begin(), name->end());
        out.push_back(0);
    }

    // CLIPACTIONS, SWF6+ (32-bit CLIPEVENTFLAGS) encoding.
    writeU16(out, 0);  // Reserved
    uint32_t allFlags = 0;
    for (const auto& rec : clipActions) allFlags |= rec.eventFlags;
    writeU32(out, allFlags);
    for (const auto& rec : clipActions) {
        writeU32(out, rec.eventFlags);
        uint32_t recordSize = static_cast<uint32_t>((rec.keyCode ? 1 : 0) + rec.actionBytes.size());
        writeU32(out, recordSize);
        if (rec.keyCode) writeU8(out, *rec.keyCode);
        out.insert(out.end(), rec.actionBytes.begin(), rec.actionBytes.end());
    }
    writeU32(out, 0);  // terminator record (zero EventFlags)

    return out;
}

// --- Phase 8: font / text / button / edit-text builders -----------------

std::vector<uint8_t> buildGlyphShapeBytes(int32_t widthUnits, int32_t heightUnits) {
    // A bare SHAPE (NumFillBits/NumLineBits + records, no style arrays) is
    // exactly what buildRectShapeRecordsBytes already produces — see its
    // own doc comment (Phase 3) and this function's doc comment (Phase 8)
    // in SwfTestFixtures.h.
    return buildRectShapeRecordsBytes(widthUnits, heightUnits, false);
}

std::vector<uint8_t> buildDefineFontV1Bytes(
    uint16_t fontId, const std::vector<std::vector<uint8_t>>& glyphShapeBytes) {
    std::vector<uint8_t> out;
    writeU16(out, fontId);

    uint16_t numGlyphs = static_cast<uint16_t>(glyphShapeBytes.size());
    if (numGlyphs == 0) {
        // The v1 encoding derives numGlyphs from the FIRST offset table
        // entry (numGlyphs = firstOffset / 2) — so even a zero-glyph font
        // still needs that one sentinel UI16 (value 0) present, or a
        // reader can't tell "empty font" apart from "truncated stream".
        writeU16(out, 0);
        return out;
    }
    std::vector<uint16_t> offsets(numGlyphs);
    uint16_t running = static_cast<uint16_t>(numGlyphs * 2);  // offset table's own byte size
    for (uint16_t i = 0; i < numGlyphs; ++i) {
        offsets[i] = running;
        running = static_cast<uint16_t>(running + glyphShapeBytes[i].size());
    }
    for (uint16_t off : offsets) writeU16(out, off);
    for (const auto& glyph : glyphShapeBytes) out.insert(out.end(), glyph.begin(), glyph.end());
    return out;
}

std::vector<uint8_t> buildDefineFont2Bytes(
    uint16_t fontId, const std::string& fontName,
    const std::vector<std::vector<uint8_t>>& glyphShapeBytes, const std::vector<uint16_t>& codes,
    bool wideCodes, int16_t ascent, int16_t descent, int16_t leading,
    const std::vector<GlyphLayoutFixture>& layout) {
    std::vector<uint8_t> out;
    writeU16(out, fontId);

    bool hasLayout = !layout.empty();
    uint8_t flags = static_cast<uint8_t>((hasLayout ? 0x80 : 0) | (wideCodes ? 0x04 : 0));
    writeU8(out, flags);
    writeU8(out, 0);  // LanguageCode

    writeU8(out, static_cast<uint8_t>(fontName.size()));
    out.insert(out.end(), fontName.begin(), fontName.end());

    uint16_t numGlyphs = static_cast<uint16_t>(glyphShapeBytes.size());
    writeU16(out, numGlyphs);

    std::vector<uint16_t> offsets(static_cast<size_t>(numGlyphs) + 1);
    uint16_t running = static_cast<uint16_t>((numGlyphs + 1) * 2);  // offset table's own byte size
    for (uint16_t i = 0; i < numGlyphs; ++i) {
        offsets[i] = running;
        running = static_cast<uint16_t>(running + glyphShapeBytes[i].size());
    }
    offsets[numGlyphs] = running;  // CodeTableOffset

    for (uint16_t off : offsets) writeU16(out, off);
    for (const auto& glyph : glyphShapeBytes) out.insert(out.end(), glyph.begin(), glyph.end());

    for (uint16_t i = 0; i < numGlyphs; ++i) {
        uint16_t code = i < codes.size() ? codes[i] : 0;
        if (wideCodes) {
            writeU16(out, code);
        } else {
            writeU8(out, static_cast<uint8_t>(code));
        }
    }

    if (hasLayout) {
        writeS16(out, ascent);
        writeS16(out, descent);
        writeS16(out, leading);
        for (uint16_t i = 0; i < numGlyphs; ++i) {
            int16_t adv = i < layout.size() ? layout[i].advance : 0;
            writeS16(out, adv);
        }
        for (uint16_t i = 0; i < numGlyphs; ++i) {
            if (i < layout.size()) {
                const auto& l = layout[i];
                writeRect(out, l.boundsXMin, l.boundsXMax, l.boundsYMin, l.boundsYMax);
            } else {
                writeRect(out, 0, 0, 0, 0);
            }
        }
        writeU16(out, 0);  // KerningCount = 0
    }

    return out;
}

std::vector<uint8_t> buildDefineTextBytes(uint16_t characterId,
                                           const std::vector<uint8_t>& matrixBytes,
                                           uint8_t glyphBits, uint8_t advanceBits,
                                           const std::vector<TextRecordFixture>& records,
                                           bool withAlpha, int32_t boundsWidthTwips,
                                           int32_t boundsHeightTwips) {
    std::vector<uint8_t> out;
    writeU16(out, characterId);
    writeRect(out, 0, boundsWidthTwips, 0, boundsHeightTwips);
    out.insert(out.end(), matrixBytes.begin(), matrixBytes.end());
    writeU8(out, glyphBits);
    writeU8(out, advanceBits);

    for (const auto& rec : records) {
        bool hasFont = rec.fontId.has_value();
        bool hasColor = rec.colorRgba.has_value();
        bool hasYOffset = rec.yOffsetTwips.has_value();
        bool hasXOffset = rec.xOffsetTwips.has_value();
        uint8_t flags = static_cast<uint8_t>(0x80 /* TextRecordType */ | (hasFont ? 0x08 : 0) |
                                              (hasColor ? 0x04 : 0) | (hasYOffset ? 0x02 : 0) |
                                              (hasXOffset ? 0x01 : 0));
        writeU8(out, flags);
        if (hasFont) writeU16(out, *rec.fontId);
        if (hasColor) {
            const auto& c = *rec.colorRgba;
            writeU8(out, c[0]);
            writeU8(out, c[1]);
            writeU8(out, c[2]);
            if (withAlpha) writeU8(out, c[3]);
        }
        if (hasXOffset) writeS16(out, *rec.xOffsetTwips);
        if (hasYOffset) writeS16(out, *rec.yOffsetTwips);
        if (hasFont) writeU16(out, rec.textHeightTwips.value_or(0));

        writeU8(out, static_cast<uint8_t>(rec.glyphs.size()));
        BitWriter bw(out);
        uint32_t advanceMask = advanceBits >= 32 ? 0xFFFFFFFFu : ((1u << advanceBits) - 1u);
        for (const auto& g : rec.glyphs) {
            bw.writeBits(g.first, glyphBits);
            bw.writeBits(static_cast<uint32_t>(g.second) & advanceMask, advanceBits);
        }
        bw.byteAlign();
    }
    writeU8(out, 0);  // terminator
    return out;
}

namespace {

void writeButtonRecordV1Fields(std::vector<uint8_t>& out, const ButtonRecordV1Fixture& r) {
    uint16_t characterId = r.characterId;
    uint16_t depth = r.depth;
    writeU16(out, characterId);
    writeU16(out, depth);
    out.insert(out.end(), r.matrixBytes.begin(), r.matrixBytes.end());
}

void writeIdentityColorTransformWithAlpha(std::vector<uint8_t>& out) {
    BitWriter bw(out);
    bw.writeBits(0, 1);  // HasAddTerms
    bw.writeBits(0, 1);  // HasMultTerms
    bw.writeBits(0, 4);  // NBits
    bw.byteAlign();
}

}  // namespace

std::vector<uint8_t> buildDefineButtonV1Bytes(uint16_t characterId,
                                               const std::vector<ButtonRecordV1Fixture>& records,
                                               const std::vector<uint8_t>& actionBytes) {
    std::vector<uint8_t> out;
    writeU16(out, characterId);
    for (const auto& r : records) {
        uint8_t flags = static_cast<uint8_t>((r.hitTest ? 0x08 : 0) | (r.down ? 0x04 : 0) |
                                              (r.over ? 0x02 : 0) | (r.up ? 0x01 : 0));
        writeU8(out, flags);
        writeButtonRecordV1Fields(out, r);
    }
    writeU8(out, 0);  // end-of-records terminator
    out.insert(out.end(), actionBytes.begin(), actionBytes.end());
    return out;
}

std::vector<uint8_t> buildDefineButtonV2Bytes(uint16_t characterId,
                                               const std::vector<ButtonRecordV1Fixture>& records,
                                               uint16_t conditions, std::optional<uint8_t> keyCode,
                                               const std::vector<uint8_t>& actionBytes) {
    std::vector<uint8_t> out;
    writeU16(out, characterId);
    writeU8(out, 0);  // flags: TrackAsMenu = 0

    size_t actionOffsetFieldPos = out.size();
    writeU16(out, 0);  // ButtonActionOffset placeholder, patched below

    for (const auto& r : records) {
        uint8_t flags = static_cast<uint8_t>((r.hitTest ? 0x08 : 0) | (r.down ? 0x04 : 0) |
                                              (r.over ? 0x02 : 0) | (r.up ? 0x01 : 0));
        writeU8(out, flags);
        writeButtonRecordV1Fields(out, r);
        writeIdentityColorTransformWithAlpha(out);
    }
    writeU8(out, 0);  // end-of-records terminator

    if (!actionBytes.empty()) {
        size_t recordStart = out.size();
        uint16_t actionOffset = static_cast<uint16_t>(recordStart - actionOffsetFieldPos);
        out[actionOffsetFieldPos] = static_cast<uint8_t>(actionOffset & 0xFF);
        out[actionOffsetFieldPos + 1] = static_cast<uint8_t>((actionOffset >> 8) & 0xFF);

        writeU16(out, 0);  // CondActionSize = 0 (last/only record — runs to end of tag)

        // `conditions` uses swf::ButtonCondition's bit values (kIdleToOverDown = 1<<0, ...,
        // kOverDownToIdle = 1<<8) — re-map each to its CONDACTION wire position, mirroring
        // parseDefineButton2's read side (independently re-derived here, not shared code).
        uint16_t rawConditions = 0;
        auto mapBit = [&](uint16_t prodBit, uint16_t wireBit) {
            if (conditions & prodBit) rawConditions = static_cast<uint16_t>(rawConditions | wireBit);
        };
        mapBit(1u << 7, 0x8000);  // kIdleToOverDown
        mapBit(1u << 6, 0x4000);  // kOutDownToIdle
        mapBit(1u << 5, 0x2000);  // kOutDownToOverDown
        mapBit(1u << 4, 0x1000);  // kOverDownToOutDown
        mapBit(1u << 3, 0x0800);  // kOverDownToOverUp
        mapBit(1u << 2, 0x0400);  // kOverUpToOverDown
        mapBit(1u << 1, 0x0200);  // kOverUpToIdle
        mapBit(1u << 0, 0x0100);  // kIdleToOverUp
        mapBit(1u << 8, 0x0001);  // kOverDownToIdle
        if (keyCode) rawConditions = static_cast<uint16_t>(rawConditions | ((*keyCode & 0x7F) << 1));

        writeU16(out, rawConditions);
        out.insert(out.end(), actionBytes.begin(), actionBytes.end());
    }

    return out;
}

std::vector<uint8_t> buildDefineEditTextBytes(uint16_t characterId, int32_t boundsWidthTwips,
                                               int32_t boundsHeightTwips,
                                               std::optional<uint16_t> fontId,
                                               std::optional<uint16_t> fontHeightTwips,
                                               std::optional<std::array<uint8_t, 4>> textColorRgba,
                                               const std::string& variableName,
                                               std::optional<std::string> initialText) {
    std::vector<uint8_t> out;
    writeU16(out, characterId);
    writeRect(out, 0, boundsWidthTwips, 0, boundsHeightTwips);

    bool hasFont = fontId.has_value();
    bool hasTextColor = textColorRgba.has_value();
    bool hasText = initialText.has_value();

    uint8_t flags1 = static_cast<uint8_t>((hasText ? 0x80 : 0) | (hasTextColor ? 0x04 : 0) |
                                           (hasFont ? 0x01 : 0));
    uint8_t flags2 = 0;
    writeU8(out, flags1);
    writeU8(out, flags2);

    if (hasFont) writeU16(out, *fontId);
    if (hasFont) writeU16(out, fontHeightTwips.value_or(0));
    if (hasTextColor) {
        const auto& c = *textColorRgba;
        writeU8(out, c[0]);
        writeU8(out, c[1]);
        writeU8(out, c[2]);
        writeU8(out, c[3]);
    }
    writeCString(out, variableName);
    if (hasText) writeCString(out, *initialText);

    return out;
}

// --- Phase 9: DefineMorphShape body builder ----------------------------

std::vector<uint8_t> buildDefineMorphShapeBytes(uint16_t characterId, int32_t startWidthTwips,
                                                 int32_t startHeightTwips, int32_t endWidthTwips,
                                                 int32_t endHeightTwips, uint8_t r1, uint8_t g1,
                                                 uint8_t b1, uint8_t a1, uint8_t r2, uint8_t g2,
                                                 uint8_t b2, uint8_t a2) {
    std::vector<uint8_t> out;
    writeU16(out, characterId);
    writeRect(out, 0, startWidthTwips, 0, startHeightTwips);
    writeRect(out, 0, endWidthTwips, 0, endHeightTwips);
    writeU32(out, 0);  // Offset — not consulted by parseDefineMorphShape.

    // MorphFillStyleArray: one solid fill, start+end RGBA.
    writeU8(out, 1);     // count = 1
    writeU8(out, 0x00);  // FillStyleType::kSolid
    writeU8(out, r1);
    writeU8(out, g1);
    writeU8(out, b1);
    writeU8(out, a1);
    writeU8(out, r2);
    writeU8(out, g2);
    writeU8(out, b2);
    writeU8(out, a2);

    // MorphLineStyleArray: empty.
    writeU8(out, 0);

    // StartEdges / EndEdges: each a bare SHAPE (own NumFillBits/NumLineBits
    // + records), reusing the same rectangle-tracing bit-packer regular
    // DefineShape fixtures use — fillStyle1=1 matches the single fill style
    // registered above.
    auto startEdges = buildRectShapeRecordsBytes(startWidthTwips, startHeightTwips, false);
    out.insert(out.end(), startEdges.begin(), startEdges.end());
    auto endEdges = buildRectShapeRecordsBytes(endWidthTwips, endHeightTwips, false);
    out.insert(out.end(), endEdges.begin(), endEdges.end());

    return out;
}

}  // namespace flash3ds::test::fixtures
