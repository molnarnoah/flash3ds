// bitmap_ram_probe.cpp
//
// Priority Fix List item #2 (2026-08-31, docs/known-limitations.md):
// measures the actual decoded-RGBA8 RAM cost of every DefineBits* bitmap
// character in a real movie (including nested DefineSprite tag streams),
// rather than assuming it's negligible -- swf::BitmapDef.h's own header
// comment documents that CharacterDictionary decodes bitmaps EAGERLY on
// first find() (unlike audio, which caches lazily), so this is real,
// resident memory for any bitmap character a placed shape ever references,
// not a hypothetical upper bound.
//
// Method: walk every top-level and nested tag, and for each of the 4
// supported DefineBits* tag codes, call swf::parseDefineBits() directly on
// the raw tag body (the exact same parse CharacterDictionary::
// parseOneCharacter() performs) and report width*height*4 bytes (one
// RgbaColor per pixel, see swf/DefineBitsTag.h). Read-only, standalone
// (matching this directory's other ad-hoc evidence tools, e.g.
// morph_ratio_scan.cpp) -- registered in CMakeLists.txt as a small,
// reusable diagnostic, same precedent as memory_breakdown/morph_ratio_scan.
//
// Usage: bitmap_ram_probe <path.swf> [path2.swf ...]

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "runtime/Movie.h"
#include "swf/DefineBitsTag.h"
#include "swf/SwfLoader.h"
#include "swf/TagCode.h"
#include "swf/TagDispatcher.h"

using namespace flash3ds;

namespace {

struct BitmapStat {
    uint16_t characterId = 0;
    uint16_t tagCode = 0;
    int width = 0;
    int height = 0;
    size_t bytes = 0;
    bool decodeFailed = false;
};

const char* tagName(uint16_t code) {
    switch (static_cast<swf::TagCode>(code)) {
        case swf::TagCode::DefineBitsLossless:
            return "DefineBitsLossless";
        case swf::TagCode::DefineBitsLossless2:
            return "DefineBitsLossless2";
        case swf::TagCode::DefineBitsJpeg2:
            return "DefineBitsJpeg2";
        case swf::TagCode::DefineBitsJpeg3:
            return "DefineBitsJpeg3";
        default:
            return "?";
    }
}

// Mirrors CharacterDictionary::scanTagsForCharacters()'s own recursion
// into DefineSprite tag streams -- a bitmap can legally be defined inside
// a sprite's nested stream, same as any other character-defining tag.
void walkTagsRecursive(const runtime::Movie& movie, const std::vector<swf::TagRecord>& tags,
                        std::vector<BitmapStat>& out) {
    for (const auto& tag : tags) {
        auto code = static_cast<swf::TagCode>(tag.code);
        if (code == swf::TagCode::DefineBitsLossless || code == swf::TagCode::DefineBitsLossless2 ||
            code == swf::TagCode::DefineBitsJpeg2 || code == swf::TagCode::DefineBitsJpeg3) {
            swf::SwfReader r = movie.tagBodyReader(tag);
            // Peek CharacterID without disturbing the reader parseDefineBits gets.
            swf::SwfReader idReader = movie.tagBodyReader(tag);
            uint16_t characterId = idReader.readU16();

            BitmapStat stat;
            stat.characterId = characterId;
            stat.tagCode = tag.code;
            auto def = swf::parseDefineBits(r, tag.code);
            if (def) {
                stat.width = def->width;
                stat.height = def->height;
                stat.bytes = def->pixels.size() * sizeof(swf::RgbaColor);
            } else {
                stat.decodeFailed = true;
            }
            out.push_back(stat);
        } else if (code == swf::TagCode::DefineSprite) {
            if (tag.bodyLength < 4) continue;
            swf::SwfReader header = movie.tagBodyReader(tag);
            header.readU16();  // characterId, unused here
            header.readU16();  // frameCount, unused here
            if (header.failed()) continue;

            std::vector<swf::TagRecord> nested;
            swf::SwfReader full(movie.data.data(), movie.data.size());
            full.seek(tag.bodyOffset + 4);
            size_t endOffset = tag.bodyOffset + tag.bodyLength;
            while (full.position() < endOffset && !full.failed()) {
                swf::TagRecord nestedTag;
                if (!swf::TagDispatcher::readTagHeader(full, nestedTag)) break;
                nested.push_back(nestedTag);
                if (static_cast<swf::TagCode>(nestedTag.code) == swf::TagCode::End) break;
                full.skip(nestedTag.bodyLength);
            }
            walkTagsRecursive(movie, nested, out);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path.swf> [path2.swf ...]\n", argv[0]);
        return 1;
    }

    size_t grandTotalBytes = 0;
    for (int i = 1; i < argc; ++i) {
        std::string path = argv[i];
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            printf("=== %s === FAILED TO OPEN\n", path.c_str());
            continue;
        }
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
        auto movie = swf::SwfLoader::loadSwf(data.data(), data.size());
        if (!movie || !movie->valid) {
            printf("=== %s === INVALID SWF\n", path.c_str());
            continue;
        }

        std::vector<BitmapStat> stats;
        walkTagsRecursive(*movie, movie->tags, stats);

        printf("=== %s ===\n", path.c_str());
        size_t fileTotal = 0;
        for (const auto& s : stats) {
            if (s.decodeFailed) {
                printf("  id=%-5u %-22s DECODE FAILED\n", s.characterId, tagName(s.tagCode));
                continue;
            }
            printf("  id=%-5u %-22s %4dx%-4d -> %8zu bytes (%.1f KB)\n", s.characterId,
                   tagName(s.tagCode), s.width, s.height, s.bytes, s.bytes / 1024.0);
            fileTotal += s.bytes;
        }
        printf("  -- file total: %zu bytes (%.1f KB), %zu bitmap character(s)\n", fileTotal,
               fileTotal / 1024.0, stats.size());
        grandTotalBytes += fileTotal;
    }

    printf("=== grand total across %d file(s): %zu bytes (%.1f KB / %.2f MB) ===\n", argc - 1,
           grandTotalBytes, grandTotalBytes / 1024.0, grandTotalBytes / (1024.0 * 1024.0));
    return 0;
}
