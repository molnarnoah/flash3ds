#include "swf/DefineEditTextTag.h"

namespace flash3ds::swf {

namespace {

RgbaColor readRgba(SwfReader& reader) {
    RgbaColor c;
    c.r = reader.readU8();
    c.g = reader.readU8();
    c.b = reader.readU8();
    c.a = reader.readU8();
    return c;
}

}  // namespace

std::optional<EditTextDef> parseDefineEditText(SwfReader& reader) {
    EditTextDef def;
    def.characterId = reader.readU16();
    if (reader.failed()) return std::nullopt;

    def.bounds = reader.readRect();
    if (reader.failed()) return std::nullopt;

    uint8_t flags1 = reader.readU8();
    uint8_t flags2 = reader.readU8();
    if (reader.failed()) return std::nullopt;

    bool hasText = (flags1 & 0x80) != 0;
    def.wordWrap = (flags1 & 0x40) != 0;
    def.multiline = (flags1 & 0x20) != 0;
    def.password = (flags1 & 0x10) != 0;
    def.readOnly = (flags1 & 0x08) != 0;
    bool hasTextColor = (flags1 & 0x04) != 0;
    bool hasMaxLength = (flags1 & 0x02) != 0;
    bool hasFont = (flags1 & 0x01) != 0;

    bool hasFontClass = (flags2 & 0x80) != 0;
    def.autoSize = (flags2 & 0x40) != 0;
    bool hasLayout = (flags2 & 0x20) != 0;
    def.noSelect = (flags2 & 0x10) != 0;
    def.border = (flags2 & 0x08) != 0;
    def.wasStatic = (flags2 & 0x04) != 0;
    def.html = (flags2 & 0x02) != 0;
    def.useOutlines = (flags2 & 0x01) != 0;

    if (hasFont) def.fontId = reader.readU16();
    if (hasFontClass) reader.readCString();  // FontClass (SWF9 device-font selector) — unused
    if (hasFont) def.fontHeightTwips = reader.readU16();
    if (hasTextColor) def.textColor = readRgba(reader);
    if (hasMaxLength) def.maxLength = reader.readU16();
    if (hasLayout) {
        def.align = reader.readU8();
        def.leftMarginTwips = reader.readU16();
        def.rightMarginTwips = reader.readU16();
        def.indentTwips = reader.readS16();
        def.leadingTwips = reader.readS16();
    }
    def.variableName = reader.readCString();
    if (hasText) def.initialText = reader.readCString();

    if (reader.failed()) return std::nullopt;
    return def;
}

}  // namespace flash3ds::swf
