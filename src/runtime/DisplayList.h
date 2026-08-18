// DisplayList.h
//
// A depth-indexed display list, per the SWF display model: at most one
// object occupies a given depth at a time; PlaceObject2's `Move` and
// `HasCharacter` flags determine whether a tag adds a new object, replaces
// the character at an existing depth, or just updates an existing object's
// transform/name/ratio in place.
//
// This matches the add/replace distinction our Shift-DX Ghidra RE
// confirmed exists in the reference gameswf-derived runtime (the strings
// "sprite::add_display_object(): unknown cid = %d" and
// "sprite::replace_display_object(): unknown cid = %d" — see
// docs/shift-dx-behavior.md) — cited here as a behavioral cross-check only,
// no code from that runtime is used.

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "swf/PlaceObjectTag.h"
#include "swf/SwfRecords.h"

namespace flash3ds::runtime {

struct DisplayListEntry {
    int32_t depth = 0;
    uint16_t characterId = 0;
    swf::Matrix matrix = swf::Matrix::identity();
    swf::ColorTransform colorTransform = swf::ColorTransform::identity();
    std::optional<uint16_t> ratio;
    std::optional<std::string> name;
    std::optional<int32_t> clipDepth;
    std::vector<swf::ClipActionRecord> clipActions;  // Phase 6 — onClipEvent handlers, if any
};

class DisplayList {
public:
    // Applies a parsed PlaceObject/PlaceObject2 record, implementing the
    // standard SWF semantics:
    //   - v1 PlaceObject, or PlaceObject2 with Move=0: adds a NEW object at
    //     `depth` (requires a characterId — v1 always has one; a
    //     PlaceObject2 with Move=0 and no characterId is malformed input
    //     and is ignored).
    //   - PlaceObject2 with Move=1 and HasCharacter=1: REPLACES whatever
    //     object is at `depth` with a new instance of the given character
    //     (matrix/colorTransform/ratio/name/clipDepth default to identity/
    //     absent unless also given in this same record).
    //   - PlaceObject2 with Move=1 and HasCharacter=0: UPDATES the existing
    //     object at `depth` in place (keeps its characterId; only the
    //     fields present in the record — matrix, colorTransform, ratio,
    //     name, clipDepth — are overwritten). If nothing currently occupies
    //     `depth`, this is a no-op (malformed/out-of-order input).
    void applyPlaceObject(const swf::PlaceObjectRecord& record);

    // Removes whatever object is at `depth`, if any. No-op if empty.
    void remove(int32_t depth);

    void clear() { entries_.clear(); }

    const std::map<int32_t, DisplayListEntry>& entries() const { return entries_; }
    size_t size() const { return entries_.size(); }

    const DisplayListEntry* find(int32_t depth) const;

private:
    std::map<int32_t, DisplayListEntry> entries_;
};

}  // namespace flash3ds::runtime
