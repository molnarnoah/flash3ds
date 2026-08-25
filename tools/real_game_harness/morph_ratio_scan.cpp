// morph_ratio_scan.cpp
//
// Roadmap Phase 9 (`docs/implementation-roadmap-2026-08-21-part2.md`)
// evidence-gathering step: "confirm via the real corpus what `ratio`
// range/usage pattern these files actually exercise, if any beyond a
// static single-ratio placement — informs whether the start-shape-only
// simplification is visually acceptable or whether animated morphing is
// load-bearing." Read-only, standalone (not registered in CMakeLists.txt,
// matching this directory's other ad-hoc evidence tools).
//
// Method: find every DefineMorphShape(46) character ID by reading each
// such tag's leading UI16 CharacterId (cheap — no full shape parse
// needed). Then walk every PlaceObject2(26) record (top-level AND
// recursively inside DefineSprite's nested tag streams — a morph could be
// placed inside a sprite, same reasoning CharacterDictionary::
// scanTagsForCharacters already documents for character-defining tags
// generally) whose CharacterId references one of those morph characters,
// and report every Ratio field found. (PlaceObject3/tag 70 is not scanned
// — this codebase doesn't parse it at all yet, see swf/PlaceObjectTag.h,
// so it can't drive a ratio through DisplayList regardless.)
//
// Usage: morph_ratio_scan <path.swf>

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "runtime/Movie.h"
#include "swf/PlaceObjectTag.h"
#include "swf/SwfLoader.h"
#include "swf/TagCode.h"
#include "swf/TagDispatcher.h"

using namespace flash3ds;

namespace {

void collectTags(const runtime::Movie& movie, const std::vector<swf::TagRecord>& tags,
                  std::set<uint16_t>& morphIds,
                  std::vector<std::pair<uint16_t, std::optional<uint16_t>>>& placements,
                  int depth) {
    for (const auto& tag : tags) {
        auto code = static_cast<swf::TagCode>(tag.code);
        if (code == swf::TagCode::DefineMorphShape) {
            swf::SwfReader r = movie.tagBodyReader(tag);
            uint16_t id = r.readU16();
            if (!r.failed()) morphIds.insert(id);
        } else if (code == swf::TagCode::PlaceObject2) {
            swf::SwfReader r = movie.tagBodyReader(tag);
            auto rec = swf::parsePlaceObject(r, tag.code);
            if (rec && rec->characterId) {
                placements.push_back({*rec->characterId, rec->ratio});
            }
        } else if (code == swf::TagCode::DefineSprite && depth < 8) {
            if (tag.bodyLength < 4) continue;
            swf::SwfReader full(movie.data.data(), movie.data.size());
            full.seek(tag.bodyOffset + 4);
            size_t endOffset = tag.bodyOffset + tag.bodyLength;
            std::vector<swf::TagRecord> nested;
            while (full.position() < endOffset && !full.failed()) {
                swf::TagRecord nt;
                if (!swf::TagDispatcher::readTagHeader(full, nt)) break;
                nested.push_back(nt);
                if (static_cast<swf::TagCode>(nt.code) == swf::TagCode::End) break;
                full.skip(nt.bodyLength);
            }
            collectTags(movie, nested, morphIds, placements, depth + 1);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path.swf>\n", argv[0]);
        return 1;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "could not open %s\n", argv[1]);
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();

    auto movie = swf::SwfLoader::loadSwf(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
    if (!movie || !movie->valid) {
        std::fprintf(stderr, "load failed\n");
        return 1;
    }

    std::set<uint16_t> morphIds;
    std::vector<std::pair<uint16_t, std::optional<uint16_t>>> placements;
    collectTags(*movie, movie->tags, morphIds, placements, 0);

    std::printf("=== %s ===\n", argv[1]);
    std::printf("DefineMorphShape character IDs found: %zu\n", morphIds.size());

    int morphPlacements = 0, withRatio = 0, nonZeroRatio = 0;
    std::set<uint16_t> distinctRatios;
    for (const auto& [charId, ratio] : placements) {
        if (morphIds.count(charId) == 0) continue;
        morphPlacements++;
        if (ratio) {
            withRatio++;
            distinctRatios.insert(*ratio);
            if (*ratio != 0) nonZeroRatio++;
        }
    }
    std::printf("PlaceObject2 records targeting a morph character: %d\n", morphPlacements);
    std::printf("  of those, with an explicit Ratio field: %d\n", withRatio);
    std::printf("  of those, with a NON-ZERO ratio: %d\n", nonZeroRatio);
    std::printf("  distinct ratio values seen: %zu -> ", distinctRatios.size());
    for (uint16_t r : distinctRatios) std::printf("%u ", r);
    std::printf("\n");

    return 0;
}
