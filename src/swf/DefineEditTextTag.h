// DefineEditTextTag.h
//
// Parses DefineEditText (tag 37) — a dynamic/input text field ("TextField"
// in AS2) — structurally: bounds, every documented flag bit, and the
// optional font/color/layout/variable-binding/initial-text fields. This is
// PARSING ONLY: variable binding (`_root.myField` <-> the field's displayed
// text), user input/editing, and word-wrap/scrolling are NOT implemented —
// see docs/avm1-support.md's Known Phase 8 limitations. Rendering the
// field's `initialText` (when an embedded font is referenced) is handled by
// renderer/SceneRenderer.cpp, reusing the same glyph-run drawing path as
// DefineText — see DefineTextTag.h.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "swf/ShapeRecords.h"
#include "swf/SwfReader.h"

namespace flash3ds::swf {

struct EditTextDef {
    uint16_t characterId = 0;
    Rect bounds;  // twips

    bool wordWrap = false;
    bool multiline = false;
    bool password = false;
    bool readOnly = false;
    bool autoSize = false;
    bool noSelect = false;
    bool border = false;
    bool wasStatic = false;
    bool html = false;
    bool useOutlines = false;

    std::optional<uint16_t> fontId;
    std::optional<uint16_t> fontHeightTwips;
    std::optional<RgbaColor> textColor;
    std::optional<uint16_t> maxLength;

    // Only present if HasLayout was set.
    std::optional<uint8_t> align;  // 0=left, 1=right, 2=center, 3=justify
    std::optional<uint16_t> leftMarginTwips;
    std::optional<uint16_t> rightMarginTwips;
    std::optional<int16_t> indentTwips;
    std::optional<int16_t> leadingTwips;

    std::string variableName;  // always present, may be empty
    std::optional<std::string> initialText;
};

// `tagCode` must be TagCode::DefineEditText (37).
std::optional<EditTextDef> parseDefineEditText(SwfReader& reader);

}  // namespace flash3ds::swf
