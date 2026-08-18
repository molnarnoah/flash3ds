#include "runtime/DisplayList.h"

#include "platform/Log.h"

namespace flash3ds::runtime {

const DisplayListEntry* DisplayList::find(int32_t depth) const {
    auto it = entries_.find(depth);
    return it == entries_.end() ? nullptr : &it->second;
}

void DisplayList::applyPlaceObject(const swf::PlaceObjectRecord& record) {
    bool isUpdateOnly = record.move && !record.characterId.has_value();

    if (isUpdateOnly) {
        auto it = entries_.find(record.depth);
        if (it == entries_.end()) {
            LOG_WARN("DISPLAYLIST", "PlaceObject2 Move=1 update at depth=%d but nothing is there",
                      record.depth);
            return;
        }
        DisplayListEntry& entry = it->second;
        if (record.matrix) entry.matrix = *record.matrix;
        if (record.colorTransform) entry.colorTransform = *record.colorTransform;
        if (record.ratio) entry.ratio = record.ratio;
        if (record.name) entry.name = record.name;
        if (record.clipDepth) entry.clipDepth = record.clipDepth;
        if (!record.clipActions.empty()) entry.clipActions = record.clipActions;
        return;
    }

    // Either a brand-new placement (Move=0) or a character replacement at
    // an existing depth (Move=1, HasCharacter=1). Both create a fresh
    // entry — the difference is purely semantic (whether something used to
    // be there), which we don't need to distinguish structurally: a fresh
    // DisplayListEntry with defaults, overridden by whatever fields the
    // record supplied, is correct for both cases.
    if (!record.characterId) {
        LOG_WARN("DISPLAYLIST",
                  "PlaceObject%s at depth=%d has no characterId and isn't an update — ignoring "
                  "malformed record",
                  record.version == 1 ? "" : "2", record.depth);
        return;
    }

    DisplayListEntry entry;
    entry.depth = record.depth;
    entry.characterId = *record.characterId;
    if (record.matrix) entry.matrix = *record.matrix;
    if (record.colorTransform) entry.colorTransform = *record.colorTransform;
    entry.ratio = record.ratio;
    entry.name = record.name;
    entry.clipDepth = record.clipDepth;
    entry.clipActions = record.clipActions;

    entries_[record.depth] = std::move(entry);
}

void DisplayList::remove(int32_t depth) { entries_.erase(depth); }

}  // namespace flash3ds::runtime
