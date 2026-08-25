#include "runtime/CharacterBounds.h"

#include <algorithm>
#include <cstdint>
#include <variant>

namespace flash3ds::runtime {

swf::Rect emptyBoundsRect() {
    return swf::Rect{INT32_MAX, INT32_MIN, INT32_MAX, INT32_MIN};
}

bool isEmptyBoundsRect(const swf::Rect& r) { return r.xMin > r.xMax || r.yMin > r.yMax; }

swf::Rect unionBoundsRect(const swf::Rect& a, const swf::Rect& b) {
    if (isEmptyBoundsRect(a)) return b;
    if (isEmptyBoundsRect(b)) return a;
    return swf::Rect{std::min(a.xMin, b.xMin), std::max(a.xMax, b.xMax), std::min(a.yMin, b.yMin),
                      std::max(a.yMax, b.yMax)};
}

swf::Rect characterOwnBoundsRect(const CharacterDef& def, const CharacterDictionary& characters) {
    if (const auto* shape = std::get_if<swf::ShapeDef>(&def)) return shape->bounds;
    if (const auto* text = std::get_if<swf::TextDef>(&def)) return text->bounds;
    if (const auto* editText = std::get_if<swf::EditTextDef>(&def)) return editText->bounds;
    // Start-side bounds only, matching this phase's start-shape-only
    // rendering simplification (see swf/DefineMorphShapeTag.h) — real
    // corpus evidence found zero non-zero-ratio placements, so EndBounds
    // is never the visually-active bounds for any character this codebase
    // actually renders.
    if (const auto* morph = std::get_if<swf::MorphShapeDef>(&def)) return morph->startBounds;
    if (const auto* button = std::get_if<swf::ButtonDef>(&def)) {
        bool anyHitTest = false;
        for (const auto& rec : button->records) {
            if (rec.stateHitTest) {
                anyHitTest = true;
                break;
            }
        }
        swf::Rect result = emptyBoundsRect();
        for (const auto& rec : button->records) {
            bool use = anyHitTest ? rec.stateHitTest : rec.stateUp;
            if (!use) continue;
            const CharacterDef* nested = characters.find(rec.characterId);
            if (!nested) continue;
            swf::Rect nestedBounds;
            if (const auto* nShape = std::get_if<swf::ShapeDef>(nested)) {
                nestedBounds = nShape->bounds;
            } else if (const auto* nText = std::get_if<swf::TextDef>(nested)) {
                nestedBounds = nText->bounds;
            } else if (const auto* nEdit = std::get_if<swf::EditTextDef>(nested)) {
                nestedBounds = nEdit->bounds;
            } else {
                continue;
            }
            result = unionBoundsRect(result, swf::transformRect(rec.matrix, nestedBounds));
        }
        return result;
    }
    return emptyBoundsRect();
}

}  // namespace flash3ds::runtime
