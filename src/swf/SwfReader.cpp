#include "swf/SwfReader.h"

#include <algorithm>
#include <cstring>

namespace flash3ds::swf {

void SwfReader::fail() { failed_ = true; }

uint8_t SwfReader::readU8() {
    if (pos_ + 1 > size_) {
        fail();
        return 0;
    }
    return data_[pos_++];
}

uint16_t SwfReader::readU16() {
    uint16_t lo = readU8();
    uint16_t hi = readU8();
    return static_cast<uint16_t>(lo | (hi << 8));
}

uint32_t SwfReader::readU32() {
    uint32_t b0 = readU8();
    uint32_t b1 = readU8();
    uint32_t b2 = readU8();
    uint32_t b3 = readU8();
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

int16_t SwfReader::readS16() { return static_cast<int16_t>(readU16()); }
int32_t SwfReader::readS32() { return static_cast<int32_t>(readU32()); }
uint16_t SwfReader::readFixed8() { return readU16(); }

std::vector<uint8_t> SwfReader::readBytes(size_t count) {
    size_t avail = bytesRemaining();
    size_t n = std::min(count, avail);
    std::vector<uint8_t> out(data_ + pos_, data_ + pos_ + n);
    pos_ += n;
    if (n < count) {
        fail();
    }
    return out;
}

std::string SwfReader::readCString() {
    std::string s;
    while (!atEnd()) {
        uint8_t c = readU8();
        if (c == 0) {
            return s;
        }
        s.push_back(static_cast<char>(c));
    }
    // Ran off the end without a terminator: malformed input. Return what we
    // have and let the sticky failed() flag (set by the readU8 that hit EOF
    // on the *next* attempted read) propagate if the caller checks it.
    return s;
}

void SwfReader::skip(size_t count) {
    size_t avail = bytesRemaining();
    if (count > avail) {
        pos_ = size_;
        fail();
        return;
    }
    pos_ += count;
}

void SwfReader::seek(size_t absolutePos) {
    if (absolutePos > size_) {
        pos_ = size_;
        fail();
        return;
    }
    pos_ = absolutePos;
    bitsLeftInBuffer_ = 0;
    bitBuffer_ = 0;
}

uint32_t SwfReader::readUBits(int numBits) {
    if (numBits <= 0) return 0;
    uint32_t result = 0;
    int bitsNeeded = numBits;
    while (bitsNeeded > 0) {
        if (bitsLeftInBuffer_ == 0) {
            if (atEnd()) {
                fail();
                return result << bitsNeeded;  // pad with zeros
            }
            bitBuffer_ = readU8();
            bitsLeftInBuffer_ = 8;
        }
        int take = std::min(bitsNeeded, bitsLeftInBuffer_);
        int shift = bitsLeftInBuffer_ - take;
        uint32_t bits = (bitBuffer_ >> shift) & ((1u << take) - 1u);
        result = (result << take) | bits;
        bitsLeftInBuffer_ -= take;
        bitsNeeded -= take;
    }
    return result;
}

int32_t SwfReader::readSBits(int numBits) {
    if (numBits <= 0) return 0;
    uint32_t raw = readUBits(numBits);
    // Sign-extend from numBits to 32 bits.
    if (raw & (1u << (numBits - 1))) {
        raw |= ~0u << numBits;
    }
    return static_cast<int32_t>(raw);
}

void SwfReader::byteAlign() {
    bitsLeftInBuffer_ = 0;
    bitBuffer_ = 0;
}

Rect SwfReader::readRect() {
    Rect r;
    int nBits = static_cast<int>(readUBits(5));
    r.xMin = readSBits(nBits);
    r.xMax = readSBits(nBits);
    r.yMin = readSBits(nBits);
    r.yMax = readSBits(nBits);
    byteAlign();
    return r;
}

}  // namespace flash3ds::swf
