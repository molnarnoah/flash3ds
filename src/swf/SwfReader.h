// SwfReader.h
//
// Clean-room SWF byte/bit-stream reader.
//
// This implements the *publicly documented* SWF file format primitives
// (Adobe's "SWF File Format Specification"): little-endian fixed-width
// integers, the bit-packed RECT record, and the tag-header encoding
// (10-bit tag code + 6-bit short length, with a 32-bit extended length
// when the short length field is 0x3F).
//
// It is an independent implementation written from the public spec, not
// copied from Shift-DX or gameswf. Where our Ghidra reverse-engineering of
// Shift-DX confirmed that a particular encoding is used (e.g. the tag
// header layout, the RECT bit-packing), that is documented in
// docs/shift-dx-behavior.md as a behavioral cross-check, not as a source
// of implementation code.
//
// The reader never throws and never reads out of bounds: once the cursor
// would run past the end of the buffer, every subsequent read returns 0
// (or an empty value) and sets a sticky `failed()` flag. Callers (notably
// SwfLoader) must check `failed()` after parsing untrusted input and bail
// out cleanly instead of trusting partially-read structures.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace flash3ds::swf {

// Stage bounding box, in twips (1/20th of a pixel), as read from the SWF
// header's RECT record.
struct Rect {
    int32_t xMin = 0;
    int32_t xMax = 0;
    int32_t yMin = 0;
    int32_t yMax = 0;

    int32_t widthTwips() const { return xMax - xMin; }
    int32_t heightTwips() const { return yMax - yMin; }
    double widthPixels() const { return widthTwips() / 20.0; }
    double heightPixels() const { return heightTwips() / 20.0; }
};

class SwfReader {
public:
    SwfReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    // --- byte-aligned reads -------------------------------------------------
    uint8_t readU8();
    uint16_t readU16();   // little-endian
    uint32_t readU32();   // little-endian
    int16_t readS16();
    int32_t readS32();
    uint16_t readFixed8();  // 8.8 fixed point, returned as raw u16 (caller divides by 256.0)

    // Reads `count` raw bytes. Returns fewer than `count` (possibly zero) if
    // the stream does not have enough data; never reads past the end.
    std::vector<uint8_t> readBytes(size_t count);

    // Reads a NUL-terminated string (as used by e.g. DefineFont/ExportAssets
    // records in later phases). Bounded by the remaining buffer.
    std::string readCString();

    void skip(size_t count);

    // --- bit-aligned reads (must byteAlign() before resuming byte reads) ---
    uint32_t readUBits(int numBits);
    int32_t readSBits(int numBits);
    void byteAlign();

    // SWF RECT record: 5-bit Nbits field, then 4 signed Nbits fields
    // (xmin, xmax, ymin, ymax), byte-aligned afterwards.
    Rect readRect();

    // --- position / status ---------------------------------------------------
    size_t position() const { return pos_; }
    size_t size() const { return size_; }
    size_t bytesRemaining() const { return pos_ < size_ ? size_ - pos_ : 0; }
    bool atEnd() const { return pos_ >= size_; }
    bool failed() const { return failed_; }

    void seek(size_t absolutePos);

    const uint8_t* dataPtr() const { return data_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    bool failed_ = false;

    // bit-reader state
    uint8_t bitBuffer_ = 0;
    int bitsLeftInBuffer_ = 0;

    void fail();
};

}  // namespace flash3ds::swf
