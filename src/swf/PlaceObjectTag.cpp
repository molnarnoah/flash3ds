#include "swf/PlaceObjectTag.h"

#include "swf/TagCode.h"

namespace flash3ds::swf {

std::optional<PlaceObjectRecord> parsePlaceObject(SwfReader& reader, uint16_t tagCode) {
    PlaceObjectRecord rec;

    if (static_cast<TagCode>(tagCode) == TagCode::PlaceObject) {
        rec.version = 1;
        rec.move = false;  // SWF1 PlaceObject always introduces a new character.

        uint16_t characterId = reader.readU16();
        uint16_t depth = reader.readU16();
        if (reader.failed()) return std::nullopt;

        rec.characterId = characterId;
        rec.depth = depth;
        rec.matrix = readMatrix(reader);
        if (reader.failed()) return std::nullopt;

        // ColorTransform (no alpha) is optional: present only if bytes
        // remain in the tag body.
        if (!reader.atEnd()) {
            rec.colorTransform = readColorTransform(reader, /*withAlpha=*/false);
        }
        return rec;
    }

    if (static_cast<TagCode>(tagCode) == TagCode::PlaceObject2) {
        rec.version = 2;

        uint8_t flags = reader.readU8();
        bool hasClipActions = (flags & 0x80) != 0;
        bool hasClipDepth = (flags & 0x40) != 0;
        bool hasName = (flags & 0x20) != 0;
        bool hasRatio = (flags & 0x10) != 0;
        bool hasColorTransform = (flags & 0x08) != 0;
        bool hasMatrix = (flags & 0x04) != 0;
        bool hasCharacter = (flags & 0x02) != 0;
        bool move = (flags & 0x01) != 0;

        uint16_t depth = reader.readU16();
        if (reader.failed()) return std::nullopt;
        rec.depth = depth;
        rec.move = move;

        if (hasCharacter) {
            rec.characterId = reader.readU16();
        }
        if (hasMatrix) {
            rec.matrix = readMatrix(reader);
        }
        if (hasColorTransform) {
            rec.colorTransform = readColorTransform(reader, /*withAlpha=*/true);
        }
        if (hasRatio) {
            rec.ratio = reader.readU16();
        }
        if (hasName) {
            rec.name = reader.readCString();
        }
        if (hasClipDepth) {
            rec.clipDepth = static_cast<int32_t>(reader.readU16());
        }
        // ClipActionRecords (hasClipActions) are not parsed in Phase 2 —
        // that's AVM1 event-handler territory (Phase 4+). We simply don't
        // read them; the caller only ever looks at the bytes via a reader
        // scoped to this tag's body, so leaving a tail unread is harmless.
        (void)hasClipActions;

        if (reader.failed()) return std::nullopt;
        return rec;
    }

    return std::nullopt;  // not a PlaceObject-family tag
}

std::optional<RemoveObjectRecord> parseRemoveObject(SwfReader& reader, uint16_t tagCode) {
    RemoveObjectRecord rec;

    if (static_cast<TagCode>(tagCode) == TagCode::RemoveObject) {
        uint16_t characterId = reader.readU16();
        uint16_t depth = reader.readU16();
        if (reader.failed()) return std::nullopt;
        rec.characterId = characterId;
        rec.depth = depth;
        return rec;
    }

    if (static_cast<TagCode>(tagCode) == TagCode::RemoveObject2) {
        uint16_t depth = reader.readU16();
        if (reader.failed()) return std::nullopt;
        rec.depth = depth;
        return rec;
    }

    return std::nullopt;
}

std::optional<std::string> parseFrameLabel(SwfReader& reader) {
    std::string name = reader.readCString();
    if (reader.failed()) return std::nullopt;
    return name;
}

}  // namespace flash3ds::swf
