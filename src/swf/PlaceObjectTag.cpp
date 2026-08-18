#include "swf/PlaceObjectTag.h"

#include <utility>

#include "swf/TagCode.h"

namespace flash3ds::swf {

namespace {

// Parses the CLIPACTIONS structure that follows a PlaceObject2 body when
// HasClipActions is set: UI16 Reserved, CLIPEVENTFLAGS AllEventFlags, then
// CLIPACTIONRECORDs until a zero-flags terminator record. See
// PlaceObjectTag.h's ClipEventFlag/ClipActionRecord doc comments for the
// bit-layout confidence note.
std::vector<ClipActionRecord> parseClipActions(SwfReader& reader, uint8_t swfVersion) {
    std::vector<ClipActionRecord> result;
    bool wideFlags = swfVersion >= 6;
    auto readFlags = [&]() -> uint32_t {
        return wideFlags ? reader.readU32() : static_cast<uint32_t>(reader.readU16());
    };

    reader.readU16();  // Reserved — always 0, not otherwise meaningful
    readFlags();        // AllEventFlags — a summary OR of every record below;
                         // not needed since we just read every record directly.

    while (!reader.failed()) {
        uint32_t eventFlags = readFlags();
        if (eventFlags == 0) break;  // CLIPACTIONEND terminator

        uint32_t actionRecordSize = reader.readU32();
        if (reader.failed()) break;
        size_t recordEnd = reader.position() + actionRecordSize;

        ClipActionRecord rec;
        rec.eventFlags = eventFlags;
        if (eventFlags & static_cast<uint32_t>(ClipEventFlag::kKeyPress)) {
            rec.keyCode = reader.readU8();
        }
        size_t remaining = recordEnd > reader.position() ? recordEnd - reader.position() : 0;
        rec.actionBytes = reader.readBytes(remaining);
        result.push_back(std::move(rec));
    }
    return result;
}

}  // namespace

std::optional<PlaceObjectRecord> parsePlaceObject(SwfReader& reader, uint16_t tagCode,
                                                    uint8_t swfVersion) {
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
        // Phase 6: ClipActionRecords are now parsed (onClipEvent/button
        // on() handler bytecode) — see parseClipActions() above. The
        // caller only ever looks at bytes via a reader scoped to this
        // tag's body, so even if this parse is wrong/incomplete for some
        // exotic content, leaving a tail unread stays harmless (matches
        // this parser's existing graceful-degradation philosophy).
        if (hasClipActions) {
            rec.clipActions = parseClipActions(reader, swfVersion);
        }

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
