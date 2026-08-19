#include "runtime/ButtonInstance.h"

#include "runtime/CharacterBounds.h"
#include "runtime/MovieClipInstance.h"

namespace flash3ds::runtime {

ButtonInstance::ButtonInstance(const swf::ButtonDef& def, uint16_t characterId,
                                MovieClipInstance* parent, int32_t depthInParent,
                                std::string name)
    : def_(&def),
      characterId_(characterId),
      parent_(parent),
      depthInParent_(depthInParent),
      name_(std::move(name)) {}

swf::Matrix ButtonInstance::worldMatrix() const {
    return parent_ ? swf::concatMatrix(parent_->worldMatrix(), matrix_) : matrix_;
}

bool ButtonInstance::updateState(bool isOver, bool mouseDown) {
    previousState_ = state_;
    state_ = !isOver ? State::kUp : (mouseDown ? State::kDown : State::kOver);
    return state_ != previousState_;
}

bool ButtonInstance::hitTestLocal(const swf::Point& localPoint,
                                   const CharacterDictionary& characters) const {
    if (!visible_) return false;
    const CharacterDef* charDef = characters.find(characterId_);
    if (!charDef) return false;
    swf::Rect hitBounds = characterOwnBoundsRect(*charDef, characters);
    if (isEmptyBoundsRect(hitBounds)) return false;
    swf::Matrix inverseMatrix;
    if (!swf::invertMatrix(matrix_, &inverseMatrix)) return false;
    swf::Point ownSpacePoint = swf::transformPoint(inverseMatrix, localPoint);
    return swf::rectContainsPoint(hitBounds, ownSpacePoint);
}

void ButtonInstance::wireScriptObject() {
    scriptObject_ = std::make_shared<avm1::Object>();
    // Minimal nativeGet hook — see the class header's "AS2 OBJECT IDENTITY"
    // section (event-dispatch phase, 2026-08-19). `parent_` outlives this
    // instance (same non-owning-pointer convention as everywhere else in
    // this class), so capturing the raw pointer by value here is safe for
    // scriptObject_'s own lifetime.
    MovieClipInstance* parent = parent_;
    scriptObject_->nativeGet = [parent](const std::string& name, avm1::Value& out) -> bool {
        if (name == "_parent") {
            out = parent ? avm1::Value::object(parent->scriptObject()) : avm1::Value::undefined();
            return true;
        }
        if (name == "_root") {
            out = parent ? avm1::Value::object(parent->rootInstance().scriptObject())
                          : avm1::Value::undefined();
            return true;
        }
        return false;
    };
}

}  // namespace flash3ds::runtime
