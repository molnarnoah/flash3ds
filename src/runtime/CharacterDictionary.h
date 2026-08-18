// CharacterDictionary.h
//
// Resolves SWF character IDs (as referenced by DisplayListEntry::characterId)
// to their definitions. Phase 3 added two character kinds: shapes
// (DefineShape/2/3) and sprites (DefineSprite — a nested, independently
// timed mini-movie). Phase 6 added a third: sounds (DefineSound — see
// swf/DefineSoundTag.h; structural header fields only, no codec decode).
// Bitmap/Text/Button characters are recognized by tag but not parsed yet
// (Phase 8+; see docs/swf-support.md).
//
// DefineSprite's body is CharacterId, FrameCount, then a *nested* tag
// stream (its own ShowFrame/PlaceObject*/RemoveObject*/DoAction/... control
// tags, terminated by an End tag) living inside that same DefineSprite tag's
// body bytes. Critically, that nested stream is scanned here using
// *absolute* offsets into Movie::data (by seeking a reader into the shared
// buffer rather than constructing an isolated sub-reader), so the resulting
// TagRecords are directly usable with Movie::tagBodyReader — and so
// Timeline::build() doesn't need to know or care whether it's building a
// movie's main timeline or a sprite's nested one.

#pragma once

#include <cstdint>
#include <unordered_map>
#include <variant>
#include <vector>

#include "runtime/Movie.h"
#include "swf/DefineShapeTag.h"
#include "swf/DefineSoundTag.h"
#include "swf/TagDispatcher.h"

namespace flash3ds::runtime {

struct SpriteDef {
    uint16_t characterId = 0;
    uint16_t frameCount = 0;
    std::vector<swf::TagRecord> tags;  // nested control tags; bodyOffset is absolute into Movie::data
};

using CharacterDef = std::variant<swf::ShapeDef, SpriteDef, swf::SoundDef>;

class CharacterDictionary {
public:
    // Scans `movie`'s top-level tags for DefineShape/DefineShape2/
    // DefineShape3/DefineSprite and parses each into the dictionary, keyed
    // by character ID. Tags that fail to parse (malformed input) are
    // logged and skipped rather than aborting the whole scan.
    static CharacterDictionary build(const Movie& movie);

    const CharacterDef* find(uint16_t characterId) const;
    size_t size() const { return characters_.size(); }

private:
    std::unordered_map<uint16_t, CharacterDef> characters_;
};

}  // namespace flash3ds::runtime
