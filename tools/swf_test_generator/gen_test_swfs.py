#!/usr/bin/env python3
"""gen_test_swfs.py

Compatibility-audit phase (2026-08-18) — generates a small set of minimal,
hand-authored, clean-room standalone .swf files under tests/swf/, one
isolated feature per file, for manual/CLI/3DS-visual verification
alongside the project's existing low-level C++ fixture tests (see
tests/SwfTestFixtures.cpp, which builds equivalent byte streams in-memory
for unit tests — these files are for END-TO-END file-loading + rendering
checks the C++ suite doesn't cover, e.g. `flash_runtime --render` /
manual visual inspection / future real-3DS-hardware SD-card loading once
that's implemented — see docs/3ds-limitations.md).

Written directly against the public SWF File Format Specification, same
source of truth as the rest of this project. Shares no code with
tests/SwfTestFixtures.cpp, tools/gen_3ds_demo_swf.py, or Shift-DX/gameswf/
code.bin. Deliberately duplicates a SMALL amount of bit-packing helper code
from tools/gen_3ds_demo_swf.py rather than importing it, to keep each
generator script independently runnable/auditable — this is boilerplate
(RECT/MATRIX/tag-header encoding), not SWF-parsing logic, so duplication
carries none of the "don't copy implementation" risk that matters for this
project's clean-room constraint.

Usage:
    python3 tools/swf_test_generator/gen_test_swfs.py
(writes into tests/swf/, relative to the repo root — run from repo root)

See tests/swf/README.md for the full planned numbered test matrix and
which entries this script actually produces vs. which remain planned.
"""
import os
import struct


def ubits(bits_list):
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
    if value < 0:
        value = (1 << nbits) + value
    return (value, nbits)


def rect(xmin, xmax, ymin, ymax):
    vals = [xmin, xmax, ymin, ymax]
    maxabs = max(abs(v) for v in vals) if vals else 0
    nbits = 1
    while (1 << (nbits - 1)) <= maxabs:
        nbits += 1
    fields = [(nbits, 5)] + [sb(v, nbits) for v in vals]
    return ubits(fields)


def tag_header(tag_type, body_len):
    if body_len < 0x3F:
        return struct.pack('<H', (tag_type << 6) | body_len)
    return struct.pack('<H', (tag_type << 6) | 0x3F) + struct.pack('<I', body_len)


def tag(tag_type, body):
    return tag_header(tag_type, len(body)) + body


def matrix_translate(tx_twips, ty_twips):
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


def cxform_with_alpha_identity_but_alpha(alpha_mult_x256):
    # CXFORMWITHALPHA: HasAddTerms(1), HasMultTerms(1), Nbits(4), then
    # (if HasMultTerms) RedMult/GreenMult/BlueMult/AlphaMult each Nbits
    # signed 8.8-fixed-point-as-integer (e.g. 256 == 1.0), byte-aligned at
    # the end. Cross-checked against this project's own
    # src/swf/SwfRecords.cpp readColorTransform().
    nbits = 10  # covers signed range needed for 256 (1.0 mult) and any 0-256 alpha value
    fields = [
        (0, 1),  # HasAddTerms
        (1, 1),  # HasMultTerms
        (nbits, 4),
        sb(256, nbits),               # RedMult = 1.0
        sb(256, nbits),               # GreenMult = 1.0
        sb(256, nbits),               # BlueMult = 1.0
        sb(alpha_mult_x256, nbits),   # AlphaMult
    ]
    return ubits(fields)


def define_shape3(character_id, r, g, b, a, size_twips):
    fillstyles = bytes([1, 0x00, r, g, b, a])
    linestyles = bytes([0])
    half = size_twips // 2
    shape_bounds = rect(-half, half, -half, half)
    num_fill_bits = 1
    num_line_bits = 0
    header_bits = ubits([(num_fill_bits, 4), (num_line_bits, 4)])

    move_val = half
    move_bits = 1
    while (1 << (move_bits - 1)) <= abs(move_val):
        move_bits += 1
    style_change = ubits([
        (0, 1), (0, 1), (0, 1), (1, 1), (0, 1), (1, 1),
        (move_bits, 5), sb(-half, move_bits), sb(-half, move_bits),
        (1, num_fill_bits),
    ])
    style_change_bits = 1 + 1 + 1 + 1 + 1 + 1 + 5 + move_bits + move_bits + num_fill_bits

    def straight_edge(dx, dy):
        maxabs = max(abs(dx), abs(dy), 1)
        nbits = 2
        while (1 << (nbits - 1)) <= maxabs:
            nbits += 1
        data = ubits([(1, 1), (1, 1), (nbits - 2, 4), (1, 1), sb(dx, nbits), sb(dy, nbits)])
        return data, 1 + 1 + 4 + 1 + nbits + nbits

    side = size_twips
    end_record = ubits([(0, 1), (0, 6)])

    class BitAccum:
        def __init__(self):
            self.acc, self.nbits, self.out = 0, 0, bytearray()

        def add(self, data_bytes, total_bits):
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
                        self.acc, self.nbits = 0, 0

        def finish(self):
            if self.nbits > 0:
                self.out.append((self.acc << (8 - self.nbits)) & 0xFF)
            return bytes(self.out)

    accum = BitAccum()
    accum.add(style_change, style_change_bits)
    for dx, dy in [(side, 0), (0, side), (-side, 0), (0, -side)]:
        data, bits = straight_edge(dx, dy)
        accum.add(data, bits)
    accum.add(end_record, 7)
    shape_records = accum.finish()

    body = (
        struct.pack('<H', character_id) + shape_bounds + fillstyles + linestyles +
        header_bits + shape_records
    )
    return tag(32, body)  # DefineShape3


def place_object2(character_id, depth, tx_twips, ty_twips, color_transform_bytes=None):
    flags = 0x04 | 0x02  # HasMatrix | HasCharacter (first placement, not a move)
    if color_transform_bytes is not None:
        flags |= 0x08  # HasColorTransform
    body = bytes([flags]) + struct.pack('<H', depth) + struct.pack('<H', character_id)
    body += matrix_translate(tx_twips, ty_twips)
    if color_transform_bytes is not None:
        body += color_transform_bytes
    return tag(26, body)


def wrap_fws(version, body_bytes):
    total_len = 8 + len(body_bytes)
    return b'FWS' + bytes([version]) + struct.pack('<I', total_len) + body_bytes


def movie_body(stage_w_twips, stage_h_twips, frame_rate_fps, frame_count, tag_bytes_list):
    body = bytearray()
    body += rect(0, stage_w_twips, 0, stage_h_twips)
    body += struct.pack('<H', int(frame_rate_fps * 256))
    body += struct.pack('<H', frame_count)
    for t in tag_bytes_list:
        body += t
    return bytes(body)


def gen_001_empty():
    body = movie_body(400 * 20, 240 * 20, 12.0, 1, [tag(1, b''), tag(0, b'')])
    return wrap_fws(6, body)


def gen_002_single_shape():
    shape = define_shape3(1, 0xE0, 0x30, 0x30, 0xFF, 40 * 20)
    place = place_object2(1, 1, 200 * 20 // 2, 120 * 20 // 2)
    body = movie_body(400 * 20, 240 * 20, 12.0, 1, [shape, place, tag(1, b''), tag(0, b'')])
    return wrap_fws(6, body)


def gen_003_alpha_colortransform():
    # Same 40px red square as 002, but placed with a PlaceObject2
    # CXFORMWITHALPHA of alphaMult=0.5 (128/256) — exercises the
    # compatibility-audit phase's priority #1 fix (ColorTransform/_alpha
    # actually applied to rendered pixels; see docs/known-limitations.md).
    # Rendered against the default white background, the square should
    # come out visibly LIGHTER/PINKER than fully-opaque red (0xE0,0x30,0x30),
    # not full-strength — a quick visual smoke test distinct from the
    # project's automated SceneRenderer_MovieClipInstanceAlpha_* unit tests
    # (which check a MovieClip's own _alpha, not a direct PlaceObject2
    # CXFORMWITHALPHA on a leaf shape placement).
    shape = define_shape3(1, 0xE0, 0x30, 0x30, 0xFF, 40 * 20)
    cxform = cxform_with_alpha_identity_but_alpha(128)
    place = place_object2(1, 1, 200 * 20 // 2, 120 * 20 // 2, color_transform_bytes=cxform)
    body = movie_body(400 * 20, 240 * 20, 12.0, 1, [shape, place, tag(1, b''), tag(0, b'')])
    return wrap_fws(6, body)


GENERATORS = {
    '001_empty.swf': gen_001_empty,
    '002_single_shape.swf': gen_002_single_shape,
    '003_placeobject2_colortransform_alpha50.swf': gen_003_alpha_colortransform,
}


def main():
    out_dir = os.path.join(os.path.dirname(__file__), '..', '..', 'tests', 'swf')
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    for name, fn in GENERATORS.items():
        data = fn()
        path = os.path.join(out_dir, name)
        with open(path, 'wb') as f:
            f.write(data)
        print(f'wrote {path} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
