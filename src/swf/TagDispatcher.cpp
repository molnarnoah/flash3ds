#include "swf/TagDispatcher.h"

#include "swf/TagCode.h"

namespace flash3ds::swf {

bool TagDispatcher::readTagHeader(SwfReader& reader, TagRecord& outTag) {
    if (reader.atEnd() || reader.failed()) {
        return false;
    }

    uint16_t header = reader.readU16();
    if (reader.failed()) {
        return false;
    }

    uint16_t code = header >> 6;
    uint32_t length = header & 0x3F;
    bool longHeader = false;

    if (length == 0x3F) {
        length = reader.readU32();
        longHeader = true;
        if (reader.failed()) {
            return false;
        }
    }

    outTag.code = code;
    outTag.name = tagCodeName(code);
    outTag.bodyOffset = reader.position();
    outTag.bodyLength = length;
    outTag.isLongHeader = longHeader;
    return true;
}

bool TagDispatcher::isActionScriptTag(uint16_t code) {
    switch (static_cast<TagCode>(code)) {
        case TagCode::DoAction:
        case TagCode::DoInitAction:
        case TagCode::DoABC:
        case TagCode::DoABC2:
            return true;
        default:
            return false;
    }
}

}  // namespace flash3ds::swf
