#include "swf/DefineButtonTag.h"

#include "platform/Log.h"
#include "swf/TagCode.h"

namespace flash3ds::swf {

namespace {

std::optional<ButtonDef> parseDefineButtonV1(SwfReader& reader) {
    ButtonDef def;
    def.characterId = reader.readU16();
    if (reader.failed()) return std::nullopt;

    while (!reader.failed()) {
        uint8_t flags = reader.readU8();
        if (reader.failed() || flags == 0) break;  // end-of-records terminator

        ButtonRecordDef rec;
        rec.stateHitTest = (flags & 0x08) != 0;
        rec.stateDown = (flags & 0x04) != 0;
        rec.stateOver = (flags & 0x02) != 0;
        rec.stateUp = (flags & 0x01) != 0;
        rec.characterId = reader.readU16();
        rec.depth = reader.readU16();
        rec.matrix = readMatrix(reader);
        def.records.push_back(rec);
    }
    if (reader.failed()) return std::nullopt;

    // No length prefix — the action block runs to the end of the tag body
    // (conventionally understood as running on the OverDown -> OverUp
    // transition, i.e. a mouse click — not dispatched anywhere yet, see
    // DefineButtonTag.h's file header).
    def.actionsV1 = reader.readBytes(reader.bytesRemaining());
    return def;
}

std::optional<ButtonDef> parseDefineButtonV2(SwfReader& reader) {
    ButtonDef def;
    def.characterId = reader.readU16();
    if (reader.failed()) return std::nullopt;

    uint8_t flags = reader.readU8();
    def.trackAsMenu = (flags & 0x01) != 0;

    size_t actionOffsetFieldPos = reader.position();
    uint16_t actionOffset = reader.readU16();
    if (reader.failed()) return std::nullopt;

    bool aborted = false;
    while (!reader.failed()) {
        uint8_t recFlags = reader.readU8();
        if (reader.failed() || recFlags == 0) break;  // end-of-records terminator

        bool hasBlendMode = (recFlags & 0x20) != 0;
        bool hasFilterList = (recFlags & 0x10) != 0;

        ButtonRecordDef rec;
        rec.stateHitTest = (recFlags & 0x08) != 0;
        rec.stateDown = (recFlags & 0x04) != 0;
        rec.stateOver = (recFlags & 0x02) != 0;
        rec.stateUp = (recFlags & 0x01) != 0;
        rec.characterId = reader.readU16();
        rec.depth = reader.readU16();
        rec.matrix = readMatrix(reader);
        rec.colorTransform = readColorTransform(reader, /*withAlpha=*/true);

        if (hasFilterList) {
            // Unknown length (a FILTERLIST this runtime doesn't parse — see
            // the file header) — any further byte offset guess would
            // desync the rest of the tag, so stop here rather than mis-
            // read. Records already collected are still returned.
            LOG_WARN("BUTTON",
                      "DefineButton2 %u: button record with an unsupported FilterList — "
                      "stopping button-record parsing here (%zu record(s) kept)",
                      def.characterId, def.records.size());
            aborted = true;
            break;
        }
        if (hasBlendMode) {
            reader.readU8();  // BlendMode — fixed-size, not applied to rendering
        }
        def.records.push_back(std::move(rec));
    }
    if (reader.failed()) return std::nullopt;

    if (!aborted && actionOffset != 0) {
        reader.seek(actionOffsetFieldPos + actionOffset);
        while (!reader.failed()) {
            size_t recordStart = reader.position();
            uint16_t condActionSize = reader.readU16();
            uint16_t rawConditions = reader.readU16();
            if (reader.failed()) break;

            ButtonCondAction ca;
            uint16_t conditions = 0;
            if (rawConditions & 0x8000) conditions |= static_cast<uint16_t>(ButtonCondition::kIdleToOverDown);
            if (rawConditions & 0x4000) conditions |= static_cast<uint16_t>(ButtonCondition::kOutDownToIdle);
            if (rawConditions & 0x2000) conditions |= static_cast<uint16_t>(ButtonCondition::kOutDownToOverDown);
            if (rawConditions & 0x1000) conditions |= static_cast<uint16_t>(ButtonCondition::kOverDownToOutDown);
            if (rawConditions & 0x0800) conditions |= static_cast<uint16_t>(ButtonCondition::kOverDownToOverUp);
            if (rawConditions & 0x0400) conditions |= static_cast<uint16_t>(ButtonCondition::kOverUpToOverDown);
            if (rawConditions & 0x0200) conditions |= static_cast<uint16_t>(ButtonCondition::kOverUpToIdle);
            if (rawConditions & 0x0100) conditions |= static_cast<uint16_t>(ButtonCondition::kIdleToOverUp);
            if (rawConditions & 0x0001) conditions |= static_cast<uint16_t>(ButtonCondition::kOverDownToIdle);
            ca.conditions = conditions;
            uint8_t keyPress = static_cast<uint8_t>((rawConditions >> 1) & 0x7F);
            if (keyPress != 0) ca.keyCode = keyPress;

            bool isLast = condActionSize == 0;
            size_t remaining = isLast ? reader.bytesRemaining()
                                       : (recordStart + condActionSize > reader.position()
                                              ? recordStart + condActionSize - reader.position()
                                              : 0);
            ca.actionBytes = reader.readBytes(remaining);
            def.condActionsV2.push_back(std::move(ca));
            if (isLast) break;
        }
    }

    return def;
}

}  // namespace

std::optional<ButtonDef> parseDefineButton(SwfReader& reader, uint16_t tagCode) {
    switch (static_cast<TagCode>(tagCode)) {
        case TagCode::DefineButton: return parseDefineButtonV1(reader);
        case TagCode::DefineButton2: return parseDefineButtonV2(reader);
        default: return std::nullopt;
    }
}

}  // namespace flash3ds::swf
