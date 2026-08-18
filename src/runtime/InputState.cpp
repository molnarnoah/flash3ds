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

void InputState::setViewportSize(double widthPixels, double heightPixels) {
    viewportWidth_ = widthPixels;
    viewportHeight_ = heightPixels;
}

void InputState::commitFrame() {
    // See the header's commitFrame() doc comment for the full model. This
    // is the ONLY place pressedKeys_/releasedKeys_/mousePressed_/
    // mouseReleased_ are ever written — setKeyDown()/setMouseDown() never
    // touch them, which is what guarantees calling commitFrame() N times
    // computes edges N times (once per real transition), regardless of how
    // many individual setter calls happened in between.
    pressedKeys_.clear();
    releasedKeys_.clear();
    for (int code : keysDown_) {
        if (previousKeysDown_.find(code) == previousKeysDown_.end()) {
            pressedKeys_.insert(code);
        }
    }
    for (int code : previousKeysDown_) {
        if (keysDown_.find(code) == keysDown_.end()) {
            releasedKeys_.insert(code);
        }
    }
    previousKeysDown_ = keysDown_;

    mousePressed_ = mouseDown_ && !previousMouseDown_;
    mouseReleased_ = !mouseDown_ && previousMouseDown_;
    previousMouseDown_ = mouseDown_;
}

bool InputState::isKeyPressed(int keyCode) const {
    return pressedKeys_.find(keyCode) != pressedKeys_.end();
}

bool InputState::isKeyReleased(int keyCode) const {
    return releasedKeys_.find(keyCode) != releasedKeys_.end();
}

}  // namespace flash3ds::runtime
