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

void ButtonInstance::wireScriptObject() { scriptObject_ = std::make_shared<avm1::Object>(); }

}  // namespace flash3ds::runtime
