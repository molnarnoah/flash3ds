#include "swf/DefineBitsTag.h"

#include <zlib.h>

#include <algorithm>
#include <cstdlib>

#include "jpgd.h"
#include "platform/Log.h"
#include "swf/TagCode.h"

namespace flash3ds::swf {

namespace {

// Defensive cap on decoded pixel COUNT (not byte size — that's checked
// per-format below), shared by every variant this file parses. A real SWF6-
// 8 title's bitmap assets are, by construction, small enough to matter for
// a 3DS-era screen (real corpus content tops out at a few hundred pixels
// per side — see docs/renderer.md's "Bitmap rendering" section); this
// exists purely to reject a malformed/hostile width*height combination
// before attempting to allocate or decode anything, mirroring
// SwfLoader.cpp's own kMaxDecompressedSize safety-cap precedent for CWS
// bodies. 4096x4096 is enormously more generous than any real corpus
// asset while still bounding worst-case allocation to a sane ~64 MiB RGBA8
// buffer.
constexpr int kMaxReasonableDimension = 4096;
constexpr int64_t kMaxReasonablePixels = 16LL * 1024 * 1024;  // 4096 * 4096

bool dimensionsReasonable(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (width > kMaxReasonableDimension || height > kMaxReasonableDimension) return false;
    return static_cast<int64_t>(width) * static_cast<int64_t>(height) <= kMaxReasonablePixels;
}

// Inflates `compressed` (raw zlib stream, as used by ZlibBitmapData/the
// DefineBitsJpeg3 alpha record) into exactly `expectedSize` bytes.
// Deliberately a separate, bounded-by-known-target variant of
// SwfLoader.cpp's own file-local inflateZlib() rather than a shared
// utility: that one has to handle an a-priori-unknown output size (a whole
// CWS movie body) and so grows an open-ended buffer under a generous
// safety cap, where every call site here already knows the EXACT expected
// output size from the tag's own width/height/format fields before
// inflating anything, so the target itself doubles as a much tighter,
// per-call safety bound (no fixed constant needed at all). Returns false
// (leaving `out` unmodified) on any zlib error, or if the stream produced
// fewer than `expectedSize` bytes; extra trailing bytes beyond
// `expectedSize` are silently ignored (harmless — the two zlib-compressed
// records this parses are never followed by anything else meaningful
// within the same tag body).
bool inflateExact(const uint8_t* compressed, size_t compressedSize, size_t expectedSize,
                   std::vector<uint8_t>& out) {
    out.assign(expectedSize, 0);
    if (expectedSize == 0) return true;

    z_stream strm{};
    if (inflateInit(&strm) != Z_OK) return false;

    strm.next_in = const_cast<Bytef*>(compressed);
    strm.avail_in = static_cast<uInt>(compressedSize);
    strm.next_out = out.data();
    strm.avail_out = static_cast<uInt>(expectedSize);

    int ret;
    do {
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            return false;
        }
    } while (ret == Z_OK && strm.avail_out > 0 && strm.avail_in > 0);

    size_t produced = expectedSize - strm.avail_out;
    inflateEnd(&strm);
    return produced >= expectedSize;
}

// Byte stride of one decoded pixel row for a given colormapped/15-bit/
// 24-32-bit source format, BEFORE the per-spec "pad every scanline to a
// multiple of 4 bytes" rule (applied by the caller). `bytesPerPixel` is 1
// for an 8-bit colormap index, 2 for 15-bit RGB, 4 for 24-bit RGB (PIX24,
// one reserved byte + RGB) or 32-bit ARGB.
size_t paddedRowStride(int width, int bytesPerPixel) {
    size_t raw = static_cast<size_t>(width) * static_cast<size_t>(bytesPerPixel);
    return (raw + 3) & ~static_cast<size_t>(3);
}

// Un-premultiplies one ARGB32 DefineBitsLossless2 pixel (spec: format-5
// color data in the v2 tag is stored premultiplied by alpha) into straight
// RGBA — see BitmapDef's own doc comment for why every BitmapDef this file
// produces uses the same straight-alpha convention regardless of source
// format. alpha == 0 has no recoverable color (every premultiplied channel
// is necessarily 0 too), so it's left as transparent black — invisible
// either way once composited.
RgbaColor unpremultiply(uint8_t a, uint8_t rPremult, uint8_t gPremult, uint8_t bPremult) {
    if (a == 0) return RgbaColor{0, 0, 0, 0};
    auto unmul = [a](uint8_t c) -> uint8_t {
        int v = (static_cast<int>(c) * 255 + a / 2) / a;  // round-to-nearest
        return static_cast<uint8_t>(std::min(255, v));
    };
    return RgbaColor{unmul(rPremult), unmul(gPremult), unmul(bPremult), a};
}

// 5-bit -> 8-bit channel expansion for format-4 (15-bit RGB) pixels:
// replicates the top 3 bits into the low bits (the standard, bit-accurate
// 5->8 expansion) rather than a plain "<<3", so pure white (0x1F) maps to
// 255 exactly instead of 248.
uint8_t expand5(uint8_t v) { return static_cast<uint8_t>((v << 3) | (v >> 2)); }

std::optional<BitmapDef> parseLossless(SwfReader& reader, uint16_t tagCode) {
    bool isVersion2 = (static_cast<TagCode>(tagCode) == TagCode::DefineBitsLossless2);

    BitmapDef def;
    def.characterId = reader.readU16();
    uint8_t format = reader.readU8();
    def.width = reader.readU16();
    def.height = reader.readU16();
    if (reader.failed() || !dimensionsReasonable(def.width, def.height)) return std::nullopt;

    uint32_t colorTableCount = 0;
    if (format == 3) {
        colorTableCount = static_cast<uint32_t>(reader.readU8()) + 1;
        if (reader.failed()) return std::nullopt;
    } else if (format != 4 && format != 5) {
        LOG_WARN("SWF", "DefineBitsLossless%s: unknown BitmapFormat %u",
                  isVersion2 ? "2" : "", format);
        return std::nullopt;
    }
    if (format == 4 && isVersion2) {
        // Per spec, DefineBitsLossless2 only ever uses format 3 or 5 — 15-
        // bit RGB is a v1-only encoding. No real corpus evidence of this
        // combination either way; fail cleanly rather than guess.
        LOG_WARN("SWF", "DefineBitsLossless2: 15-bit format (4) is v1-only, rejecting");
        return std::nullopt;
    }

    int bytesPerPixel = (format == 3) ? 1 : (format == 4) ? 2 : 4;
    size_t rowStride = paddedRowStride(def.width, bytesPerPixel);
    size_t colorTableBytes = static_cast<size_t>(colorTableCount) * (isVersion2 ? 4 : 3);
    size_t expectedSize = colorTableBytes + rowStride * static_cast<size_t>(def.height);

    std::vector<uint8_t> compressed = reader.readBytes(reader.bytesRemaining());
    std::vector<uint8_t> pixelData;
    if (!inflateExact(compressed.data(), compressed.size(), expectedSize, pixelData)) {
        LOG_WARN("SWF", "DefineBitsLossless%s characterId=%u: zlib inflate failed/short (expected %zu bytes)",
                  isVersion2 ? "2" : "", def.characterId, expectedSize);
        return std::nullopt;
    }

    def.pixels.resize(static_cast<size_t>(def.width) * static_cast<size_t>(def.height));

    if (format == 3) {
        // Colormapped: color table first, then one index byte per pixel,
        // rows padded to a 4-byte stride.
        const uint8_t* table = pixelData.data();
        for (int y = 0; y < def.height; ++y) {
            const uint8_t* row = pixelData.data() + colorTableBytes + static_cast<size_t>(y) * rowStride;
            for (int x = 0; x < def.width; ++x) {
                uint8_t index = row[x];
                RgbaColor color{0, 0, 0, 0};
                if (index < colorTableCount) {
                    const uint8_t* entry = table + static_cast<size_t>(index) * (isVersion2 ? 4 : 3);
                    if (isVersion2) {
                        color = RgbaColor{entry[0], entry[1], entry[2], entry[3]};
                    } else {
                        color = RgbaColor{entry[0], entry[1], entry[2], 255};
                    }
                }
                def.pixels[static_cast<size_t>(y) * def.width + x] = color;
            }
        }
    } else if (format == 4) {
        // 15-bit RGB (v1 only, checked above): UI16 per pixel, 5/5/5 bits,
        // top bit reserved/unused. Little-endian per every other UI16 read
        // in this codebase.
        for (int y = 0; y < def.height; ++y) {
            const uint8_t* row = pixelData.data() + static_cast<size_t>(y) * rowStride;
            for (int x = 0; x < def.width; ++x) {
                uint16_t pixel = static_cast<uint16_t>(row[x * 2]) |
                                  (static_cast<uint16_t>(row[x * 2 + 1]) << 8);
                uint8_t r5 = (pixel >> 10) & 0x1F;
                uint8_t g5 = (pixel >> 5) & 0x1F;
                uint8_t b5 = pixel & 0x1F;
                def.pixels[static_cast<size_t>(y) * def.width + x] =
                    RgbaColor{expand5(r5), expand5(g5), expand5(b5), 255};
            }
        }
    } else {
        // format == 5: PIX24 (v1, opaque) or ARGB32 (v2, premultiplied).
        for (int y = 0; y < def.height; ++y) {
            const uint8_t* row = pixelData.data() + static_cast<size_t>(y) * rowStride;
            for (int x = 0; x < def.width; ++x) {
                const uint8_t* p = row + static_cast<size_t>(x) * 4;
                RgbaColor color;
                if (isVersion2) {
                    color = unpremultiply(p[0], p[1], p[2], p[3]);
                } else {
                    // PIX24: byte 0 reserved, then R, G, B.
                    color = RgbaColor{p[1], p[2], p[3], 255};
                }
                def.pixels[static_cast<size_t>(y) * def.width + x] = color;
            }
        }
    }

    return def;
}

// Real-corpus finding (Priority Fix List item #2, 2026-08-31): a real,
// well-known Flash/SWF authoring-tool JPEG encoding quirk, confirmed
// present in EVERY DefineBitsJpeg2/3 tag across the entire test corpus
// (Hobo2/3/6 and Extreme Pamplona) except one. Rather than embedding one
// self-contained JPEG stream (as the SWF spec describes and this file's
// header comment originally assumed for JPEG2/3, unlike DefineBits(6)'s
// external-JPEGTables split), several tags' raw bytes instead contain a
// SHARED-TABLES segment (SOI + DQT + DHT) immediately followed by an
// ERRONEOUS "EOI SOI" 4-byte marker pair (0xFF 0xD9 0xFF 0xD8) and then
// the ACTUAL per-image segment (its own APP0/SOF/SOS/
// entropy-coded scan/EOI) that references the FIRST segment's quant/Huffman
// tables by ID without redefining them. A strict single-stream JPEG decoder
// (jpgd included) can't parse that as one image: naively trimming to the
// LAST SOI marker (an earlier, simpler theory this project tried and
// disproved with the same corpus evidence) discards the shared tables the
// second segment actually depends on, failing with JPGD_UNDEFINED_HUFF_TABLE/
// JPGD_UNDEFINED_QUANT_TABLE-class errors instead. The correct fix —
// verified via a standalone jpgd probe against every failing corpus
// sample, 100% success, zero regressions on the one already-working
// sample (Extreme Pamplona characterId=131, which has no such marker
// pair at all) — is to splice out just the 4-byte erroneous "EOI SOI"
// pair itself (0xFF 0xD9 0xFF 0xD8) wherever it appears in the stream,
// producing one continuous, valid JPEG (tables segment's DQT/DHT directly
// followed by the image segment's APP0/SOF/SOS/scan/EOI, no intervening
// marker). See docs/renderer.md's "Bitmap rendering" section for the full
// evidence writeup (byte offsets per corpus file).
std::vector<uint8_t> stripErroneousEoiSoiMarkers(const uint8_t* data, size_t size) {
    std::vector<uint8_t> out;
    out.reserve(size);
    size_t i = 0;
    while (i < size) {
        if (i + 3 < size && data[i] == 0xFF && data[i + 1] == 0xD9 && data[i + 2] == 0xFF &&
            data[i + 3] == 0xD8) {
            i += 4;  // skip the erroneous EOI+SOI pair, splicing the surrounding bytes together
            continue;
        }
        out.push_back(data[i]);
        ++i;
    }
    return out;
}

// Decodes `jpegData` via the vendored jpgd decoder (third_party/jpgd — see
// its own README.md) into straight RGB, returned as one RgbaColor per
// pixel with alpha left at 255 (JPEG has no alpha channel of its own —
// DefineBitsJpeg3's separate alpha record, if present, is applied by the
// caller afterwards). Returns std::nullopt on any decode failure or if the
// decoded dimensions aren't reasonable (see dimensionsReasonable()).
std::optional<std::vector<RgbaColor>> decodeJpegRgb(const uint8_t* jpegDataIn, size_t jpegSizeIn,
                                                      int& outWidth, int& outHeight) {
    // Splicing is a no-op (returns an identical copy) for a normal,
    // single-segment JPEG — see stripErroneousEoiSoiMarkers()'s own
    // comment for why this must run unconditionally rather than only when
    // some other heuristic first detects the quirk.
    std::vector<uint8_t> spliced = stripErroneousEoiSoiMarkers(jpegDataIn, jpegSizeIn);
    const uint8_t* jpegData = spliced.data();
    size_t jpegSize = spliced.size();

    int width = 0, height = 0, actualComps = 0;
    unsigned char* decoded =
        jpgd::decompress_jpeg_image_from_memory(jpegData, static_cast<int>(jpegSize), &width,
                                                  &height, &actualComps, /*req_comps=*/3);
    if (!decoded) return std::nullopt;
    if (!dimensionsReasonable(width, height)) {
        std::free(decoded);
        return std::nullopt;
    }

    std::vector<RgbaColor> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (size_t i = 0; i < pixels.size(); ++i) {
        const unsigned char* p = decoded + i * 3;
        pixels[i] = RgbaColor{p[0], p[1], p[2], 255};
    }
    std::free(decoded);

    outWidth = width;
    outHeight = height;
    return pixels;
}

std::optional<BitmapDef> parseJpeg2(SwfReader& reader) {
    BitmapDef def;
    def.characterId = reader.readU16();
    if (reader.failed()) return std::nullopt;

    std::vector<uint8_t> jpegData = reader.readBytes(reader.bytesRemaining());
    int width = 0, height = 0;
    auto pixels = decodeJpegRgb(jpegData.data(), jpegData.size(), width, height);
    if (!pixels) {
        LOG_WARN("SWF", "DefineBitsJpeg2 characterId=%u: JPEG decode failed", def.characterId);
        return std::nullopt;
    }
    def.width = width;
    def.height = height;
    def.pixels = std::move(*pixels);
    return def;
}

std::optional<BitmapDef> parseJpeg3(SwfReader& reader) {
    BitmapDef def;
    def.characterId = reader.readU16();
    uint32_t jpegDataSize = reader.readU32();
    if (reader.failed()) return std::nullopt;

    std::vector<uint8_t> jpegData = reader.readBytes(jpegDataSize);
    if (jpegData.size() != jpegDataSize) {
        // Tag body truncated before the declared JPEG data even ends.
        return std::nullopt;
    }
    std::vector<uint8_t> alphaCompressed = reader.readBytes(reader.bytesRemaining());

    int width = 0, height = 0;
    auto pixels = decodeJpegRgb(jpegData.data(), jpegData.size(), width, height);
    if (!pixels) {
        LOG_WARN("SWF", "DefineBitsJpeg3 characterId=%u: JPEG decode failed", def.characterId);
        return std::nullopt;
    }
    def.width = width;
    def.height = height;
    def.pixels = std::move(*pixels);

    // The alpha record is optional in principle (some encoders emit an
    // opaque DefineBitsJpeg3 with a zero-length/absent alpha section,
    // relying on the same opaque-JPEG semantics as DefineBitsJpeg2) — if
    // it's missing or fails to inflate to exactly width*height bytes,
    // leave every pixel's alpha at the 255 decodeJpegRgb() already set
    // rather than failing the whole character over an optional field.
    size_t expectedAlphaSize = static_cast<size_t>(def.width) * static_cast<size_t>(def.height);
    std::vector<uint8_t> alpha;
    if (!alphaCompressed.empty() &&
        inflateExact(alphaCompressed.data(), alphaCompressed.size(), expectedAlphaSize, alpha)) {
        for (size_t i = 0; i < def.pixels.size(); ++i) {
            def.pixels[i].a = alpha[i];
        }
    } else if (!alphaCompressed.empty()) {
        LOG_WARN("SWF",
                  "DefineBitsJpeg3 characterId=%u: alpha record present but failed to inflate to "
                  "%zu bytes, rendering opaque",
                  def.characterId, expectedAlphaSize);
    }

    return def;
}

}  // namespace

std::optional<BitmapDef> parseDefineBits(SwfReader& reader, uint16_t tagCode) {
    switch (static_cast<TagCode>(tagCode)) {
        case TagCode::DefineBitsLossless:
        case TagCode::DefineBitsLossless2:
            return parseLossless(reader, tagCode);
        case TagCode::DefineBitsJpeg2:
            return parseJpeg2(reader);
        case TagCode::DefineBitsJpeg3:
            return parseJpeg3(reader);
        default:
            // DefineBits(6)/DefineBitsJpeg4(90) and anything else — see
            // this file's header comment for why those two specifically
            // are out of scope.
            return std::nullopt;
    }
}

}  // namespace flash3ds::swf
