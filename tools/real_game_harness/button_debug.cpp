// button_debug.cpp
//
// Dumps every condActionsV2 record (raw condition bitmask, decoded
// condition names, CondKeyPress code, action-byte length) and every
// button-state record for named DefineButton2 character IDs. Written
// during implementation-roadmap.md Phase 1 to investigate why
// click_probe's simulated mouse clicks against hobo.swf's 3 frame-1
// buttons produced no observable effect -- see
// docs/real-game-readiness.md's Phase 1 findings for the answer this
// tool's output led to (those 3 buttons' condActionsV2 records are
// genuinely keypress-only in the SWF's own binary -- not a dispatcher
// bug).
//
// Usage: button_debug <path.swf> [charId ...]

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "runtime/CharacterDictionary.h"
#include "runtime/Movie.h"
#include "swf/SwfLoader.h"

using namespace flash3ds;
using runtime::CharacterDictionary;

const char* condName(uint16_t bit) {
    switch (bit) {
        case 1u << 0: return "IdleToOverUp";
        case 1u << 1: return "OverUpToIdle";
        case 1u << 2: return "OverUpToOverDown";
        case 1u << 3: return "OverDownToOverUp";
        case 1u << 4: return "OverDownToOutDown";
        case 1u << 5: return "OutDownToOverDown";
        case 1u << 6: return "OutDownToIdle";
        case 1u << 7: return "IdleToOverDown";
        case 1u << 8: return "OverDownToIdle";
        default: return "?";
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <path.swf> [charId ...]\n", argv[0]);
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
    CharacterDictionary characters = CharacterDictionary::build(*movie);

    for (int i = 2; i < argc; ++i) {
        uint16_t charId = static_cast<uint16_t>(std::stoi(argv[i]));
        const auto* def = characters.find(charId);
        if (!def) {
            std::printf("char %u: not found\n", charId);
            continue;
        }
        const auto* btn = std::get_if<swf::ButtonDef>(def);
        if (!btn) {
            std::printf("char %u: not a ButtonDef (variant index=%zu)\n", charId, def->index());
            continue;
        }
        std::printf("char %u: ButtonDef, trackAsMenu=%d, %zu records, %zu condActionsV2:\n", charId,
                    btn->trackAsMenu, btn->records.size(), btn->condActionsV2.size());
        for (const auto& ca : btn->condActionsV2) {
            std::printf("  conditions=0x%03x [", ca.conditions);
            for (int b = 0; b < 9; ++b) {
                if (ca.conditions & (1u << b)) std::printf("%s ", condName(1u << b));
            }
            std::string keyStr = ca.keyCode ? std::to_string(*ca.keyCode) : std::string("(none)");
            std::printf("] keyCode=%s actionBytes=%zu\n", keyStr.c_str(), ca.actionBytes.size());
        }
        for (const auto& r : btn->records) {
            std::printf("  record: char=%u depth=%d up=%d over=%d down=%d hit=%d\n", r.characterId, r.depth,
                        r.stateUp, r.stateOver, r.stateDown, r.stateHitTest);
        }
    }

    return 0;
}
