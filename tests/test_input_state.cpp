// test_input_state.cpp — Phase 6: InputState's own unit tests, independent
// of AVM1/MovieClipInstance (see test_movieclip_instance.cpp for Key.
// isDown()/_xmouse/_ymouse/StartDrag integration tests).

#include "TestFramework.h"
#include "runtime/InputState.h"

using flash3ds::runtime::InputState;

TEST_CASE(InputState_KeyDown_TracksIndependentKeys) {
    InputState input;
    CHECK(!input.isKeyDown(InputState::kLeft));
    input.setKeyDown(InputState::kLeft, true);
    CHECK(input.isKeyDown(InputState::kLeft));
    CHECK(!input.isKeyDown(InputState::kRight));

    input.setKeyDown(InputState::kLeft, false);
    CHECK(!input.isKeyDown(InputState::kLeft));
}

TEST_CASE(InputState_LastKeyCode_TracksMostRecentKeyDown) {
    InputState input;
    CHECK_EQ(input.lastKeyCode(), 0);
    input.setKeyDown(InputState::kUp, true);
    CHECK_EQ(input.lastKeyCode(), static_cast<int>(InputState::kUp));
    input.setKeyDown(InputState::kSpace, true);
    CHECK_EQ(input.lastKeyCode(), static_cast<int>(InputState::kSpace));
    // Releasing a key doesn't change lastKeyCode() (it tracks the most
    // recent key to go DOWN, not the currently-held one).
    input.setKeyDown(InputState::kSpace, false);
    CHECK_EQ(input.lastKeyCode(), static_cast<int>(InputState::kSpace));
}

TEST_CASE(InputState_MousePosition_RoundTrips) {
    InputState input;
    CHECK_EQ(input.mouseX(), 0.0);
    CHECK_EQ(input.mouseY(), 0.0);
    input.setMousePosition(123.5, 456.25);
    CHECK_EQ(input.mouseX(), 123.5);
    CHECK_EQ(input.mouseY(), 456.25);
}

TEST_CASE(InputState_MouseDown_RoundTrips) {
    InputState input;
    CHECK(!input.isMouseDown());
    input.setMouseDown(true);
    CHECK(input.isMouseDown());
    input.setMouseDown(false);
    CHECK(!input.isMouseDown());
}
