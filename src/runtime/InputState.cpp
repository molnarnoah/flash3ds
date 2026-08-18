#include "runtime/InputState.h"

namespace flash3ds::runtime {

void InputState::setKeyDown(int keyCode, bool down) {
    if (down) {
        keysDown_.insert(keyCode);
        lastKeyCode_ = keyCode;
    } else {
        keysDown_.erase(keyCode);
    }
}

bool InputState::isKeyDown(int keyCode) const { return keysDown_.count(keyCode) != 0; }

void InputState::setMousePosition(double xPixels, double yPixels) {
    mouseX_ = xPixels;
    mouseY_ = yPixels;
}

}  // namespace flash3ds::runtime
