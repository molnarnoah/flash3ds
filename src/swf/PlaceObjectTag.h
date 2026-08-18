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
#include <vector>

#include "swf/SwfReader.h"
#include "swf/SwfRecords.h"

namespace flash3ds::swf {

// PlaceObject2's optional ClipActionRecord event bitmask (Phase 6).
// Bit assignments per the public SWF spec's CLIPEVENTFLAGS table — a
// well-documented, widely cross-referenced layout, but NOT independently
// verified here against a real Flash-authored file containing every one of
// these handlers (see docs/avm1-support.md's confidence note). Getting a
// rare handler's bit wrong would misfire/miss that one handler type; it
// can't corrupt anything else (each flag is checked independently).
enum class ClipEventFlag : uint32_t {
    kLoad = 1u << 0,
    kEnterFrame = 1u << 1,
    kUnload = 1u << 2,
    kMouseMove = 1u << 3,
    kMouseDown = 1u << 4,
    kMouseUp = 1u << 5,
    kKeyDown = 1u << 6,
    kKeyUp = 1u << 7,
    kData = 1u << 8,
    kInitialize = 1u << 9,
    kPress = 1u << 10,
    kRelease = 1u << 11,
    kReleaseOutside = 1u << 12,
    kRollOver = 1u << 13,
    kRollOut = 1u << 14,
    kDragOver = 1u << 15,
    kDragOut = 1u << 16,
    kKeyPress = 1u << 17,
    kConstruct = 1u << 18,
};

// One CLIPACTIONRECORD: a raw AVM1 bytecode block (`actionBytes`, an
// ACTIONRECORD stream — ActionEnd-terminated, exactly like a DoAction tag's
// body) to run when any of `eventFlags`' bits fire. `keyCode` is present
// only when kKeyPress is set (the specific key that triggers this record).
struct ClipActionRecord {
    uint32_t eventFlags = 0;
    std::optional<uint8_t> keyCode;
    std::vector<uint8_t> actionBytes;
};

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
    std::vector<ClipActionRecord> clipActions;  // Phase 6 — empty if HasClipActions was unset
};

struct RemoveObjectRecord {
    int32_t depth = 0;
    std::optional<uint16_t> characterId;  // only present for RemoveObject (v1)
};

// `tagCode` must be TagCode::PlaceObject (4) or TagCode::PlaceObject2 (26).
// `swfVersion` picks CLIPACTIONS' field width when the tag has
// HasClipActions set (UI32 for SWF6+, UI16 for SWF5 and earlier, per spec)
// — defaults to 6 (this project's minimum target version, see CLAUDE.md),
// so existing callers that don't pass it still get the correct behavior
// for any real SWF6-8 file.
std::optional<PlaceObjectRecord> parsePlaceObject(SwfReader& reader, uint16_t tagCode,
                                                   uint8_t swfVersion = 6);

// `tagCode` must be TagCode::RemoveObject (5) or TagCode::RemoveObject2 (28).
std::optional<RemoveObjectRecord> parseRemoveObject(SwfReader& reader, uint16_t tagCode);

// FrameLabel tag (43) body: a single NUL-terminated name.
std::optional<std::string> parseFrameLabel(SwfReader& reader);

}  // namespace flash3ds::swf
