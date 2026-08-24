// button_scan.cpp
//
// Corpus-wide census of every DefineButton2 character's condActionsV2
// mouse-vs-keypress-vs-empty condition mix. Written during
// implementation-roadmap.md Phase 1 to explain click_probe's "no
// observable effect" result for hobo.swf's 3 frame-1 buttons -- see
// docs/real-game-readiness.md's Phase 1 findings for the resulting
// evidence-based conclusion (this is a real content property, not a
// dispatcher bug: only 1 button per Hobo file carries a real mouse
// condition; the rest are keypress-only or purely visual).
//
// Usage: button_scan <path.swf>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "runtime/CharacterDictionary.h"
#include "runtime/Movie.h"
#include "swf/SwfLoader.h"

using namespace flash3ds;
using runtime::CharacterDictionary;

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
    CharacterDictionary chars = CharacterDictionary::build(*movie);

    int totalButtons = 0, withMouseCond = 0, keyOnly = 0, empty = 0;
    for (uint32_t id = 1; id < 65536; ++id) {
        const auto* def = chars.find(static_cast<uint16_t>(id));
        if (!def) continue;
        const auto* btn = std::get_if<swf::ButtonDef>(def);
        if (!btn) continue;
        ++totalButtons;

        bool hasMouseCond = false;
        for (const auto& ca : btn->condActionsV2) {
            if (ca.conditions != 0) hasMouseCond = true;
        }
        if (btn->condActionsV2.empty()) {
            ++empty;
        } else if (hasMouseCond) {
            ++withMouseCond;
            std::printf("char %u: HAS mouse condition(s)\n", id);
        } else {
            ++keyOnly;
        }
    }

    std::printf("\ntotal DefineButton2 chars=%d  withMouseCondActions=%d  keyPressOnly=%d  noCondActions=%d\n",
                totalButtons, withMouseCond, keyOnly, empty);
    return 0;
}
