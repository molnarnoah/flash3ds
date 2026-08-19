#!/usr/bin/env python3
"""gen_3ds_demo_swf.py

Phase 10 helper (NOT part of the CMake build) — generates a small,
hand-authored, clean-room demonstration SWF file (a red square that slides
across the stage over a few frames) and a C++ header embedding its bytes as
a byte array, for the Nintendo 3DS entry point (src/platform/nintendo3ds_main.cpp)
to play without needing SD-card file I/O (which is explicitly out of scope
for Phase 10 -- see docs/3ds-toolchain.md).

Written directly against the public SWF File Format Specification, the same
source of truth the rest of flash3ds-runtime is implemented from. Shares no
code with tests/SwfTestFixtures.cpp (which is test-only and not linked into
any shipped binary) or with Shift-DX/gameswf/code.bin.

Usage:
    python3 tools/gen_3ds_demo_swf.py > src/platform/EmbeddedDemoSwf.h
    python3 tools/gen_3ds_demo_swf.py --swf-out romfs/game.swf

(The generated header is checked into the repo -- see that file's own
top comment -- so the 3DS build does not need Python at build time. As of
the Virtual Console resource layer (2026-08-19), the C++ entry point no
longer includes/uses EmbeddedDemoSwf.h at all -- see nintendo3ds_main.cpp
-- but the SAME clean-room demo content is reused, via --swf-out, as the
default committed romfs/game.swf so the RomFS dev workflow has SOME
default content out of the box without needing any third-party SWF. See
docs/virtual-console.md's "Replacing game.swf" section.)
"""
import argparse
import struct
import sys


def ubits(bits_list):
    """Packs a list of (value, nbits) tuples MSB-first into bytes."""
    acc = 0
    nbits = 0
    out = bytearray()
    for value, n in bits_list:
        acc = (acc << n) | (value & ((1 << n) - 1))
        nbits += n
        while nbits >= 8:
            nbits -= 8
            out.append((acc >> nbits) & 0xFF)
    if nbits > 0:
        out.append((acc << (8 - nbits)) & 0xFF)
    return bytes(out)


def sb(value, nbits):
    """Signed value packed into nbits (two's complement)."""
    if value < 0:
        value = (1 << nbits) + value
    return (value, nbits)


def rect(xmin, xmax, ymin, ymax):
    # RECT: Nbits(5) then 4 signed fields of Nbits each.
    vals = [xmin, xmax, ymin, ymax]
    maxabs = max(abs(v) for v in vals) if vals else 0
    nbits = 1
    while (1 << (nbits - 1)) <= maxabs:
        nbits += 1
    nbits = max(nbits, 1)
    fields = [(nbits, 5)] + [sb(v, nbits) for v in vals]
    return ubits(fields)


def tag_header(tag_type, body_len):
    if body_len < 0x3F:
        return struct.pack('<H', (tag_type << 6) | body_len)
    return struct.pack('<H', (tag_type << 6) | 0x3F) + struct.pack('<I', body_len)


def tag(tag_type, body):
    return tag_header(tag_type, len(body)) + body


def matrix_translate(tx_twips, ty_twips):
    # MATRIX: HasScale(1)=0, HasRotate(1)=0, then (unconditionally --
    # translate is NOT flag-gated, unlike scale/rotate; cross-checked
    # against this project's own src/swf/SwfRecords.cpp readMatrix())
    # NTranslateBits(5), TranslateX, TranslateY.
    maxabs = max(abs(tx_twips), abs(ty_twips), 1)
    nbits = 1
    while (1 << (nbits - 1)) <= maxabs:
        nbits += 1
    fields = [
        (0, 1),  # HasScale
        (0, 1),  # HasRotate
        (nbits, 5),
        sb(tx_twips, nbits),
        sb(ty_twips, nbits),
    ]
    return ubits(fields)


def define_shape3(character_id, r, g, b, a, size_twips):
    # FILLSTYLEARRAY: FillStyleCount(u8), then one solid FILLSTYLE (type=0x00, RGBA).
    fillstyles = bytes([1, 0x00, r, g, b, a])
    linestyles = bytes([0])  # LineStyleCount = 0
    half = size_twips // 2
    shape_bounds = rect(-half, half, -half, half)

    # SHAPEWITHSTYLE: FillStyles, LineStyles, NumFillBits(4)/NumLineBits(4),
    # then SHAPERECORDs.
    num_fill_bits = 1  # enough for fill style index 1
    num_line_bits = 0
    header_bits = ubits([(num_fill_bits, 4), (num_line_bits, 4)])

    # First SHAPERECORD: StyleChangeRecord that sets a MoveTo + fill style 1.
    # TypeFlag(1)=0, StateNewStyles=0, StateLineStyle=0, StateFillStyle1=1,
    # StateFillStyle0=0, StateMoveTo=1, MoveBits(5), MoveDeltaX, MoveDeltaY,
    # FillStyle1(numFillBits).
    move_val = half
    move_bits = 1
    while (1 << (move_bits - 1)) <= abs(move_val):
        move_bits += 1
    # StyleChangeRecord: move to the shape's top-left corner and select
    # fill style 1 (already defined in SHAPEWITHSTYLE's leading
    # FILLSTYLEARRAY above) -- StateNewStyles is deliberately left 0 since
    # no new fill/line styles are introduced mid-stream here.
    style_change = ubits([
        (0, 1),   # TypeFlag = 0
        (0, 1),   # StateNewStyles = 0
        (0, 1),   # StateLineStyle = 0
        (1, 1),   # StateFillStyle1 = 1
        (0, 1),   # StateFillStyle0 = 0
        (1, 1),   # StateMoveTo = 1
        (move_bits, 5),
        sb(-half, move_bits),
        sb(-half, move_bits),
        (1, num_fill_bits),  # FillStyle1 = 1
    ])

    def straight_edge(dx, dy):
        # StraightEdgeRecord: TypeFlag=1, StraightFlag=1, NumBits(4)-2,
        # GeneralLineFlag(1)=1, DeltaX, DeltaY.
        maxabs = max(abs(dx), abs(dy), 1)
        nbits = 2
        while (1 << (nbits - 1)) <= maxabs:
            nbits += 1
        return ubits([
            (1, 1),        # TypeFlag = 1 (edge record)
            (1, 1),        # StraightFlag = 1
            (nbits - 2, 4),
            (1, 1),        # GeneralLineFlag = 1 (both X and Y present)
            sb(dx, nbits),
            sb(dy, nbits),
        ])

    side = size_twips
    edges = (
        straight_edge(side, 0) +
        straight_edge(0, side) +
        straight_edge(-side, 0) +
        straight_edge(0, -side)
    )
    end_record = ubits([(0, 1), (0, 6)])  # EndShapeRecord: TypeFlag=0, EndOfShape(6)=0

    # Concatenate the whole bit-packed record stream by re-packing through
    # a single bit accumulator so records don't get byte-padded between
    # each other (matches SWF's actual continuous bitstream).
    class BitAccum:
        def __init__(self):
            self.acc = 0
            self.nbits = 0
            self.out = bytearray()

        def add_bytes_as_bits(self, data_bytes, total_bits):
            # Feed a byte string produced by ubits() back in as a bit
            # stream of exactly total_bits (ubits() right-pads the final
            # byte with zero bits, so we must only consume total_bits).
            bitpos = 0
            for byte in data_bytes:
                for i in range(8):
                    if bitpos >= total_bits:
                        return
                    bit = (byte >> (7 - i)) & 1
                    self.acc = (self.acc << 1) | bit
                    self.nbits += 1
                    bitpos += 1
                    if self.nbits == 8:
                        self.out.append(self.acc & 0xFF)
                        self.acc = 0
                        self.nbits = 0

        def finish(self):
            if self.nbits > 0:
                self.out.append((self.acc << (8 - self.nbits)) & 0xFF)
            return bytes(self.out)

    def bitlen(*parts):
        return sum(p[1] for p in parts)

    accum = BitAccum()
    style_change_bits = 1 + 1 + 1 + 1 + 1 + 1 + 5 + move_bits + move_bits + num_fill_bits
    accum.add_bytes_as_bits(style_change, style_change_bits)
    for dx, dy in [(side, 0), (0, side), (-side, 0), (0, -side)]:
        maxabs = max(abs(dx), abs(dy), 1)
        nbits = 2
        while (1 << (nbits - 1)) <= maxabs:
            nbits += 1
        edge_bits = 1 + 1 + 4 + 1 + nbits + nbits
        accum.add_bytes_as_bits(straight_edge(dx, dy), edge_bits)
    accum.add_bytes_as_bits(end_record, 7)
    shape_records = accum.finish()

    body = (
        struct.pack('<H', character_id) +
        shape_bounds +
        fillstyles +
        linestyles +
        header_bits +
        shape_records
    )
    return tag(32, body)  # DefineShape3


def place_object2(character_id, depth, tx_twips, ty_twips, move):
    # PlaceObject2 flags byte (bit 0 = LSB; cross-checked against this
    # project's own src/swf/PlaceObjectTag.cpp parser, not assumed):
    # bit0=Move, bit1=HasCharacter, bit2=HasMatrix, bit3=HasColorTransform,
    # bit4=HasRatio, bit5=HasName, bit6=HasClipDepth, bit7=HasClipActions.
    flags = 0
    flags |= 0x04  # HasMatrix
    if not move:
        flags |= 0x02  # HasCharacter (only on the first, non-move placement)
    if move:
        flags |= 0x01  # Move flag
    body = bytes([flags]) + struct.pack('<H', depth)
    if not move:
        body += struct.pack('<H', character_id)
    body += matrix_translate(tx_twips, ty_twips)
    return tag(26, body)  # PlaceObject2


def build_demo_swf():
    STAGE_W = 400 * 20   # twips (400px)
    STAGE_H = 240 * 20   # twips (240px)
    SQUARE_SIZE = 40 * 20  # 40px square, in twips
    FRAME_COUNT = 8

    frame_size = rect(0, STAGE_W, 0, STAGE_H)
    frame_rate = struct.pack('<H', 12 * 256)  # 12 fps, 8.8 fixed point
    frame_count = struct.pack('<H', FRAME_COUNT)

    body = bytearray()
    body += frame_size
    body += frame_rate
    body += frame_count

    body += tag(9, bytes([0x33, 0x66, 0xCC]))  # SetBackgroundColor (RGB)
    body += define_shape3(1, 0xE0, 0x30, 0x30, 0xFF, SQUARE_SIZE)

    step_x = (STAGE_W - SQUARE_SIZE) // (FRAME_COUNT - 1)
    for i in range(FRAME_COUNT):
        tx = step_x * i + SQUARE_SIZE // 2
        ty = STAGE_H // 2
        body += place_object2(1, 1, tx, ty, move=(i > 0))
        body += tag(1, b'')  # ShowFrame

    body += tag(0, b'')  # End

    header_rest = bytes(body)
    total_len = 8 + len(header_rest)  # 8 = "FWS" + version + u32 file length
    header = b'FWS' + bytes([6]) + struct.pack('<I', total_len)
    return header + header_rest


def emit_cpp_header(swf_bytes, out=sys.stdout):
    out.write("// EmbeddedDemoSwf.h\n")
    out.write("//\n")
    out.write("// GENERATED FILE -- do not hand-edit. Produced by tools/gen_3ds_demo_swf.py\n")
    out.write("// from a hand-authored, clean-room SWF (written directly against the public\n")
    out.write("// SWF File Format Specification -- shares no code or content with\n")
    out.write("// Shift-DX/gameswf/code.bin, or even with this project's own\n")
    out.write("// tests/SwfTestFixtures.cpp, which is test-only and not linked into any\n")
    out.write("// shipped binary). It is a solid red square that slides across the stage\n")
    out.write("// over 8 frames at 12fps against a blue background -- just enough content\n")
    out.write("// to exercise DefineShape3/PlaceObject2/Timeline/SceneRenderer end-to-end on\n")
    out.write("// real 3DS hardware without needing SD-card file I/O (explicitly out of\n")
    out.write("// scope for Phase 10 -- see docs/3ds-toolchain.md). Regenerate with:\n")
    out.write("//   python3 tools/gen_3ds_demo_swf.py > src/platform/EmbeddedDemoSwf.h\n")
    out.write("\n#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n")
    out.write("namespace flash3ds::platform {\n\n")
    out.write(f"inline constexpr uint8_t kEmbeddedDemoSwf[] = {{\n")
    for i in range(0, len(swf_bytes), 16):
        chunk = swf_bytes[i:i + 16]
        out.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
    out.write("};\n\n")
    out.write(f"inline constexpr size_t kEmbeddedDemoSwfSize = sizeof(kEmbeddedDemoSwf);\n\n")
    out.write("}  // namespace flash3ds::platform\n")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--swf-out', metavar='PATH',
                         help='Write the raw .swf bytes to PATH instead of emitting the C++ '
                              'header to stdout (used to (re)generate romfs/game.swf).')
    args = parser.parse_args()

    swf = build_demo_swf()
    if args.swf_out:
        with open(args.swf_out, 'wb') as f:
            f.write(swf)
    else:
        emit_cpp_header(swf)
