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

// --- edge detection (input-transitions phase, 2026-08-19) ------------------
//
// Model under test: commitFrame() is called once per simulated "input
// tick" (mirroring Nintendo3DSInput::poll()'s real call pattern — see
// InputState.h's commitFrame() doc comment). isKeyDown()/isMouseDown()
// stay live at all times (matching section 8's "preserve existing isDown
// behavior" requirement); isKeyPressed()/isKeyReleased()/isMousePressed()/
// isMouseReleased() only reflect the transition computed by the MOST
// RECENT commitFrame() call.

// --- §8: minimal button tests (the exact UP/DOWN/HELD/RELEASE/RELEASED
// matrix from the task spec) ------------------------------------------------

TEST_CASE(InputState_KeyEdge_InitialUp_NoTransition) {
    InputState input;
    input.commitFrame();  // "initial: UP" -- nothing ever set, commit anyway
    CHECK(!input.isKeyDown(InputState::kLeft));
    CHECK(!input.isKeyPressed(InputState::kLeft));
    CHECK(!input.isKeyReleased(InputState::kLeft));
}

TEST_CASE(InputState_KeyEdge_UpToDown_IsPressedNotReleased) {
    InputState input;
    input.commitFrame();  // establish "previous = UP"
    input.setKeyDown(InputState::kLeft, true);
    input.commitFrame();  // UP -> DOWN transition
    CHECK(input.isKeyDown(InputState::kLeft));
    CHECK(input.isKeyPressed(InputState::kLeft));
    CHECK(!input.isKeyReleased(InputState::kLeft));
}

TEST_CASE(InputState_KeyEdge_DownToDown_IsHeldNotPressed) {
    InputState input;
    input.commitFrame();
    input.setKeyDown(InputState::kLeft, true);
    input.commitFrame();  // UP -> DOWN (pressed)
    input.commitFrame();  // DOWN -> DOWN, no setKeyDown call in between at all
    CHECK(input.isKeyDown(InputState::kLeft));
    CHECK(!input.isKeyPressed(InputState::kLeft));  // no longer "just" pressed
    CHECK(!input.isKeyReleased(InputState::kLeft));
}

TEST_CASE(InputState_KeyEdge_DownToUp_IsReleasedNotPressed) {
    InputState input;
    input.commitFrame();
    input.setKeyDown(InputState::kLeft, true);
    input.commitFrame();  // UP -> DOWN
    input.setKeyDown(InputState::kLeft, false);
    input.commitFrame();  // DOWN -> UP
    CHECK(!input.isKeyDown(InputState::kLeft));
    CHECK(!input.isKeyPressed(InputState::kLeft));
    CHECK(input.isKeyReleased(InputState::kLeft));
}

TEST_CASE(InputState_KeyEdge_UpToUp_NoTransition) {
    InputState input;
    input.commitFrame();
    input.setKeyDown(InputState::kLeft, true);
    input.commitFrame();
    input.setKeyDown(InputState::kLeft, false);
    input.commitFrame();  // released
    input.commitFrame();  // UP -> UP, still nothing pressed
    CHECK(!input.isKeyDown(InputState::kLeft));
    CHECK(!input.isKeyPressed(InputState::kLeft));
    CHECK(!input.isKeyReleased(InputState::kLeft));
}

TEST_CASE(InputState_KeyEdge_BeforeAnyCommit_ReportsNoTransition) {
    // Requirement: querying isKeyPressed()/isKeyReleased() before
    // commitFrame() has EVER been called must not crash or report a
    // spurious edge -- there's simply no transition to report yet.
    InputState input;
    input.setKeyDown(InputState::kLeft, true);
    CHECK(input.isKeyDown(InputState::kLeft));  // isKeyDown() stays live
    CHECK(!input.isKeyPressed(InputState::kLeft));   // but no edge computed yet
    CHECK(!input.isKeyReleased(InputState::kLeft));
}

// --- §12: edge cases ---------------------------------------------------

TEST_CASE(InputState_KeyEdge_HeldForManyFrames_PressedOnlyOnFirstCommit) {
    InputState input;
    input.commitFrame();
    input.setKeyDown(InputState::kLeft, true);
    input.commitFrame();
    CHECK(input.isKeyPressed(InputState::kLeft));
    for (int i = 0; i < 10; ++i) {
        input.commitFrame();  // held, no setter calls -- state unchanged
        CHECK(input.isKeyDown(InputState::kLeft));
        CHECK(!input.isKeyPressed(InputState::kLeft));
        CHECK(!input.isKeyReleased(InputState::kLeft));
    }
}

TEST_CASE(InputState_KeyEdge_VeryShortPress_WithinOneTick_IsInvisible) {
    // Documented, deliberate limitation (see InputState.h's commitFrame()
    // doc comment): a press AND release that both happen between two
    // commitFrame() calls (no commit in between) is invisible -- only the
    // LAST setKeyDown() call before a commit is what's diffed. This
    // matches libctru's own hidKeysDown()/hidKeysUp() semantics (computed
    // once per hidScanInput(), same fundamental limitation).
    InputState input;
    input.commitFrame();
    input.setKeyDown(InputState::kLeft, true);
    input.setKeyDown(InputState::kLeft, false);  // released before ever committed
    input.commitFrame();
    CHECK(!input.isKeyDown(InputState::kLeft));
    CHECK(!input.isKeyPressed(InputState::kLeft));   // never observed as pressed
    CHECK(!input.isKeyReleased(InputState::kLeft));  // never observed as released
}

TEST_CASE(InputState_KeyEdge_ReleaseWithoutAnotherPress_StaysReleasedOnly) {
    InputState input;
    input.commitFrame();
    input.setKeyDown(InputState::kLeft, true);
    input.commitFrame();
    input.setKeyDown(InputState::kLeft, false);
    input.commitFrame();
    CHECK(input.isKeyReleased(InputState::kLeft));
    input.commitFrame();  // one more tick, nothing changes
    CHECK(!input.isKeyReleased(InputState::kLeft));
    CHECK(!input.isKeyPressed(InputState::kLeft));
    CHECK(!input.isKeyDown(InputState::kLeft));
}

TEST_CASE(InputState_KeyEdge_RepeatedPressRelease_TogglesCorrectlyEachTime) {
    InputState input;
    input.commitFrame();
    for (int cycle = 0; cycle < 3; ++cycle) {
        input.setKeyDown(InputState::kSpace, true);
        input.commitFrame();
        CHECK(input.isKeyPressed(InputState::kSpace));
        CHECK(!input.isKeyReleased(InputState::kSpace));

        input.setKeyDown(InputState::kSpace, false);
        input.commitFrame();
        CHECK(input.isKeyReleased(InputState::kSpace));
        CHECK(!input.isKeyPressed(InputState::kSpace));
    }
}

TEST_CASE(InputState_KeyEdge_SimultaneousDifferentButtons_BothPressedSameTick) {
    InputState input;
    input.commitFrame();
    input.setKeyDown(InputState::kLeft, true);
    input.setKeyDown(InputState::kRight, true);
    input.commitFrame();
    CHECK(input.isKeyPressed(InputState::kLeft));
    CHECK(input.isKeyPressed(InputState::kRight));
    CHECK(input.isKeyDown(InputState::kLeft));
    CHECK(input.isKeyDown(InputState::kRight));
}

TEST_CASE(InputState_KeyEdge_SimultaneousTouchAndButton_BothIndependentlyCorrect) {
    InputState input;
    input.commitFrame();
    input.setKeyDown(InputState::kLeft, true);
    input.setMousePosition(10.0, 20.0);
    input.setMouseDown(true);
    input.commitFrame();
    CHECK(input.isKeyPressed(InputState::kLeft));
    CHECK(input.isTouchPressed());
    CHECK(input.isTouchDown());
    CHECK_EQ(input.mouseX(), 10.0);
    CHECK_EQ(input.mouseY(), 20.0);
}

TEST_CASE(InputState_KeyEdge_NoSetterCallsBetweenCommits_NoSpuriousEdges) {
    // "what happens if input disappears/becomes unavailable for one poll":
    // a commitFrame() with literally no setter calls before it (state
    // unchanged from whatever was last committed) must never manufacture
    // an edge.
    InputState input;
    input.commitFrame();
    input.setKeyDown(InputState::kUp, true);
    input.commitFrame();
    CHECK(input.isKeyPressed(InputState::kUp));

    input.commitFrame();  // nothing set in between -- simulates a "missed" poll
    CHECK(!input.isKeyPressed(InputState::kUp));
    CHECK(!input.isKeyReleased(InputState::kUp));
    CHECK(input.isKeyDown(InputState::kUp));  // still held, unaffected
}

TEST_CASE(InputState_KeyEdge_AliasedKeyCodes_SecondButtonProducesNoNewEdge) {
    // Documents a real, pre-existing (Phase 10) consequence, NOT a bug in
    // edge detection itself: Nintendo3DSInput maps BOTH the A button and
    // the START button onto the same InputState::kEnter code (see
    // Nintendo3DSInput.cpp). If A is already held and START also goes
    // down (or vice versa), kEnter was ALREADY true, so no new press edge
    // fires for the second physical button -- edge detection is correct
    // at the key-CODE level; the lossy many-to-one physical-button-to-
    // AS2-key mapping upstream is what collapses the two. Flagged, not
    // fixed -- out of scope for this phase (see docs/input.md).
    InputState input;
    input.commitFrame();
    input.setKeyDown(InputState::kEnter, true);  // simulates physical A going down
    input.commitFrame();
    CHECK(input.isKeyPressed(InputState::kEnter));

    input.setKeyDown(InputState::kEnter, true);  // simulates physical START ALSO going down
    input.commitFrame();
    CHECK(input.isKeyDown(InputState::kEnter));
    CHECK(!input.isKeyPressed(InputState::kEnter));  // no edge -- code was already true
}

// --- §9: touch test matrix (UP/DOWN/HELD/RELEASE + coordinates) --------

TEST_CASE(InputState_TouchEdge_Up_NoTransition) {
    InputState input;
    input.commitFrame();
    CHECK(!input.isTouchDown());
    CHECK(!input.isTouchPressed());
    CHECK(!input.isTouchReleased());
}

TEST_CASE(InputState_TouchEdge_PressAtCoordinates_IsPressedWithCorrectPosition) {
    InputState input;
    input.commitFrame();
    input.setMousePosition(100.0, 200.0);
    input.setMouseDown(true);
    input.commitFrame();
    CHECK(input.isTouchDown());
    CHECK(input.isTouchPressed());
    CHECK(!input.isTouchReleased());
    CHECK_EQ(input.mouseX(), 100.0);
    CHECK_EQ(input.mouseY(), 200.0);
}

TEST_CASE(InputState_TouchEdge_HeldWhileMoving_PositionUpdatesEdgeDoesNot) {
    InputState input;
    input.commitFrame();
    input.setMousePosition(100.0, 200.0);
    input.setMouseDown(true);
    input.commitFrame();  // UP -> DOWN (pressed)
    CHECK(input.isTouchPressed());

    input.setMousePosition(150.0, 250.0);  // touch moves while still held
    input.setMouseDown(true);
    input.commitFrame();
    CHECK(input.isTouchDown());
    CHECK(!input.isTouchPressed());  // held, not a fresh press
    CHECK(!input.isTouchReleased());
    // Coordinate conversion is independent of the edge transition -- the
    // raw position simply reflects the latest setMousePosition() call,
    // unaffected by (and not gating) the pressed/held/released state.
    CHECK_EQ(input.mouseX(), 150.0);
    CHECK_EQ(input.mouseY(), 250.0);
}

TEST_CASE(InputState_TouchEdge_ReleaseAfterMovement_ReleasedAtFinalPosition) {
    InputState input;
    input.commitFrame();
    input.setMousePosition(100.0, 200.0);
    input.setMouseDown(true);
    input.commitFrame();  // pressed

    input.setMousePosition(150.0, 250.0);
    input.setMouseDown(true);
    input.commitFrame();  // held, moved

    input.setMousePosition(180.0, 280.0);  // final position at release
    input.setMouseDown(false);
    input.commitFrame();
    CHECK(!input.isTouchDown());
    CHECK(!input.isTouchPressed());
    CHECK(input.isTouchReleased());
    CHECK_EQ(input.mouseX(), 180.0);
    CHECK_EQ(input.mouseY(), 280.0);
}
