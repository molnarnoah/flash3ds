// PlaceObjectTag.h
//
// Clean-room parsers for the display-list control tags targeted by Phase 2:
// PlaceObject (tag 4), PlaceObject2 (tag 26), RemoveObject (tag 5),
// RemoveObject2 (tag 28), and FrameLabel (tag 43).
//
// Each parse function takes a SwfReader scoped to exactly that tag's body
// (see Movie::tagBodyReader) and returns std::nullopt on malformed input
// instead of throwing/crashing.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "swf/SwfReader.h"
#include "swf/SwfRecords.h"

namespace flash3ds::swf {

// Normalized result of parsing either PlaceObject or PlaceObject2. Fields
// that weren't present in the tag are left as std::nullopt — see
// DisplayList::applyPlaceObject for how `move`/`hasCharacter` combine to
// mean "add new", "replace character at depth", or "update transform only".
struct PlaceObjectRecord {
    int version = 1;  // 1 == PlaceObject, 2 == PlaceObject2
    int32_t depth = 0;

    bool move = false;  // PlaceObject2's "Move" flag (always true-ish for v1: see notes below)
    std::optional<uint16_t> characterId;
    std::optional<Matrix> matrix;
    std::optional<ColorTransform> colorTransform;
    std::optional<uint16_t> ratio;
    std::optional<std::string> name;
    std::optional<int32_t> clipDepth;
};

struct RemoveObjectRecord {
    int32_t depth = 0;
    std::optional<uint16_t> characterId;  // only present for RemoveObject (v1)
};

// `tagCode` must be TagCode::PlaceObject (4) or TagCode::PlaceObject2 (26).
std::optional<PlaceObjectRecord> parsePlaceObject(SwfReader& reader, uint16_t tagCode);

// `tagCode` must be TagCode::RemoveObject (5) or TagCode::RemoveObject2 (28).
std::optional<RemoveObjectRecord> parseRemoveObject(SwfReader& reader, uint16_t tagCode);

// FrameLabel tag (43) body: a single NUL-terminated name.
std::optional<std::string> parseFrameLabel(SwfReader& reader);

}  // namespace flash3ds::swf
