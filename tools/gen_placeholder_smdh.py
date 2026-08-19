#!/usr/bin/env python3
"""gen_placeholder_smdh.py

Virtual Console resource layer helper (NOT part of any shipped game
content) -- generates a minimal, well-formed, placeholder SMDH (the
homebrew menu icon/title-metadata format) at build time.

WHY THIS EXISTS: 3dsxtool's own --romfs= flag (used to embed this
project's romfs/ directory -- config.ini + game.swf -- into flash3ds_3ds.3dsx,
see CMakeLists.txt and docs/virtual-console.md) unconditionally requires an
--smdh= file to ALSO be given whenever either flag is used (confirmed by
reading 3dstools/src/3dsxtool.cpp's own WriteExtHeader(): it calls
fopen(smdhFile, "rb") with no null check at all). 3dsxtool does NOT parse
or validate the SMDH's actual contents -- it copies the file's bytes
verbatim into the .3dsx and computes the RomFS section's offset from the
SMDH's byte SIZE only -- so what matters here is producing a correctly
SIZED, well-formed blob; the icon/title text are cosmetic placeholders,
not a real game banner/icon (that's a SEPARATE, still out-of-scope concern
-- see docs/virtual-console.md's CIA packaging boundary section; CIAToolsR's
own banner/icon requirements for actual CIA packaging are untouched by
this file).

Format: the public, widely-documented SMDH structure (8-byte header + 16
UTF-16LE application-title blocks + a 0x30-byte settings block + 8 bytes
reserved + a 24x24 "small" icon + a 48x48 "large" icon, both RGB565).
Written directly against that public format -- shares no code with
Shift-DX/gameswf/code.bin.

Usage:
    python3 tools/gen_placeholder_smdh.py <output.smdh>

(Invoked automatically by CMakeLists.txt's FLASH3DS_BUILD_3DS block --
not meant to be run by hand, though nothing stops you.)
"""
import struct
import sys

TITLE_COUNT = 16
SHORT_DESC_UNITS = 0x40   # 0x80 bytes
LONG_DESC_UNITS = 0x80    # 0x100 bytes
PUBLISHER_UNITS = 0x40    # 0x80 bytes

SMALL_ICON_DIM = 24
LARGE_ICON_DIM = 48

# A flat, dark blue-grey placeholder color (RGB565: 5 bits R, 6 bits G, 5
# bits B) -- solid, so the 3DS's tiled/Z-order icon pixel layout doesn't
# matter (every pixel is the same value regardless of ordering).
PLACEHOLDER_COLOR_RGB565 = (0x08 << 11) | (0x10 << 5) | 0x14


def utf16_field(text, unit_count):
    """UTF-16LE-encodes `text`, NUL-padded/truncated to exactly unit_count
    code units (unit_count * 2 bytes)."""
    encoded = text.encode('utf-16-le')
    max_bytes = unit_count * 2
    encoded = encoded[:max_bytes]
    return encoded + b'\x00' * (max_bytes - len(encoded))


def build_smdh():
    out = bytearray()

    # --- header (8 bytes) ---
    out += b'SMDH'
    out += struct.pack('<H', 0)  # version
    out += struct.pack('<H', 0)  # reserved

    # --- 16 application titles (0x200 bytes each) ---
    short_desc = "flash3ds (dev)"
    long_desc = "flash3ds Virtual Console runtime -- placeholder SMDH, see docs/virtual-console.md"
    publisher = "flash3ds project"
    title_block = (
        utf16_field(short_desc, SHORT_DESC_UNITS) +
        utf16_field(long_desc, LONG_DESC_UNITS) +
        utf16_field(publisher, PUBLISHER_UNITS)
    )
    assert len(title_block) == 0x200
    for _ in range(TITLE_COUNT):
        out += title_block

    # --- settings block (0x30 bytes) ---
    out += bytes([0x00] * 16)              # game ratings (all "no rating info")
    out += struct.pack('<I', 0x7FFFFFFF)   # region lockout: all regions
    out += struct.pack('<I', 0)            # matchmaker id
    out += struct.pack('<Q', 0)            # matchmaker bit id
    out += struct.pack('<I', 0x00000001)   # flags: Visible
    out += struct.pack('<H', 0)            # EULA version
    out += bytes([0, 0])                   # reserved
    out += struct.pack('<I', 0)            # optimal animation default frame
    out += struct.pack('<I', 0)            # StreetPass ID
    assert len(out) == 8 + TITLE_COUNT * 0x200 + 0x30

    # --- reserved (8 bytes) ---
    out += bytes(8)
    assert len(out) == 0x2040

    # --- icon data: solid placeholder color, RGB565LE, no per-pixel
    # swizzle needed since every pixel is identical ---
    pixel = struct.pack('<H', PLACEHOLDER_COLOR_RGB565)
    out += pixel * (SMALL_ICON_DIM * SMALL_ICON_DIM)
    out += pixel * (LARGE_ICON_DIM * LARGE_ICON_DIM)

    assert len(out) == 0x36C0, f"unexpected SMDH size: {len(out):#x}"
    return bytes(out)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <output.smdh>", file=sys.stderr)
        sys.exit(1)

    with open(sys.argv[1], 'wb') as f:
        f.write(build_smdh())
