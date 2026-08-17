#include "SwfTestFixtures.h"
#include "TestFramework.h"
#include "runtime/Timeline.h"
#include "swf/SwfLoader.h"

namespace fixtures = flash3ds::test::fixtures;
using flash3ds::runtime::Timeline;
using flash3ds::swf::SwfLoader;

namespace {

// Builds a 3-frame movie:
//   Frame 1 (labeled "start"): place character 100 at depth 1, x=0
//   Frame 2: update depth 1's transform to x=2.5px (no character change)
//   Frame 3: remove depth 1
std::vector<uint8_t> buildThreeFrameMovie() {
    std::vector<fixtures::FixtureTag> tags = {
        {43 /* FrameLabel */, fixtures::buildFrameLabelBytes("start")},
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(1, false, 100, fixtures::buildMatrixBytes(0, 0))},
        {1 /* ShowFrame */, {}},
        {26 /* PlaceObject2 */,
         fixtures::buildPlaceObject2Bytes(1, true, std::nullopt, fixtures::buildMatrixBytes(50, 0))},
        {1 /* ShowFrame */, {}},
        {28 /* RemoveObject2 */, fixtures::buildRemoveObject2Bytes(1)},
        {1 /* ShowFrame */, {}},
    };
    auto body = fixtures::buildMovieBody(400 * 20, 300 * 20, 12.0, 3, tags);
    return fixtures::wrapFws(6, body);
}

}  // namespace

TEST_CASE(Timeline_Build_FrameCountMatchesShowFrameTags) {
    auto bytes = buildThreeFrameMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    CHECK(movie->valid);

    auto timeline = Timeline::build(*movie);
    CHECK(timeline != nullptr);
    CHECK_EQ(timeline->frameCount(), 3u);
    CHECK_EQ(timeline->currentFrame(), 1u);  // build() lands on frame 1
    CHECK(timeline->isPlaying());
}

TEST_CASE(Timeline_Frame1_HasPlacedObject) {
    auto bytes = buildThreeFrameMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto timeline = Timeline::build(*movie);

    CHECK_EQ(timeline->displayList().size(), 1u);
    auto* entry = timeline->displayList().find(1);
    CHECK(entry != nullptr);
    CHECK_EQ(entry->characterId, 100);
    CHECK_EQ(entry->matrix.translateXTwips, 0);
}

TEST_CASE(Timeline_GotoAndStop_Frame2_UpdatesTransformKeepsCharacter) {
    auto bytes = buildThreeFrameMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto timeline = Timeline::build(*movie);

    timeline->gotoAndStop(2);
    CHECK_EQ(timeline->currentFrame(), 2u);
    CHECK(!timeline->isPlaying());
    CHECK_EQ(timeline->displayList().size(), 1u);
    auto* entry = timeline->displayList().find(1);
    CHECK(entry != nullptr);
    CHECK_EQ(entry->characterId, 100);  // unchanged by the update-only PlaceObject2
    CHECK_EQ(entry->matrix.translateXTwips, 50);
}

TEST_CASE(Timeline_GotoAndStop_Frame3_ObjectRemoved) {
    auto bytes = buildThreeFrameMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto timeline = Timeline::build(*movie);

    timeline->gotoAndStop(3);
    CHECK_EQ(timeline->displayList().size(), 0u);
}

TEST_CASE(Timeline_GotoAndStop_BackwardJump_ReplaysCorrectly) {
    auto bytes = buildThreeFrameMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto timeline = Timeline::build(*movie);

    timeline->gotoAndStop(3);
    CHECK_EQ(timeline->displayList().size(), 0u);

    timeline->gotoAndStop(1);
    CHECK_EQ(timeline->displayList().size(), 1u);
    CHECK_EQ(timeline->displayList().find(1)->matrix.translateXTwips, 0);
}

TEST_CASE(Timeline_NextFrame_PrevFrame_StopPlayback) {
    auto bytes = buildThreeFrameMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto timeline = Timeline::build(*movie);

    timeline->nextFrame();
    CHECK_EQ(timeline->currentFrame(), 2u);
    CHECK(!timeline->isPlaying());

    timeline->prevFrame();
    CHECK_EQ(timeline->currentFrame(), 1u);
    CHECK(!timeline->isPlaying());

    // Clamped: prevFrame at frame 1 is a no-op.
    timeline->prevFrame();
    CHECK_EQ(timeline->currentFrame(), 1u);
}

TEST_CASE(Timeline_PlayStop_TogglesFlagOnly) {
    auto bytes = buildThreeFrameMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto timeline = Timeline::build(*movie);

    timeline->stop();
    CHECK(!timeline->isPlaying());
    CHECK_EQ(timeline->currentFrame(), 1u);  // frame unchanged by stop()

    timeline->play();
    CHECK(timeline->isPlaying());
}

TEST_CASE(Timeline_AdvanceOneFrame_LoopsPastLastFrame) {
    auto bytes = buildThreeFrameMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto timeline = Timeline::build(*movie);

    timeline->gotoAndPlay(1);
    CHECK(timeline->isPlaying());

    timeline->advanceOneFrame();
    CHECK_EQ(timeline->currentFrame(), 2u);
    timeline->advanceOneFrame();
    CHECK_EQ(timeline->currentFrame(), 3u);
    timeline->advanceOneFrame();  // past the end -> loops back to frame 1
    CHECK_EQ(timeline->currentFrame(), 1u);
    CHECK(timeline->isPlaying());  // advanceOneFrame doesn't touch playing_
}

TEST_CASE(Timeline_FrameLabels_LookupAndGoto) {
    auto bytes = buildThreeFrameMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto timeline = Timeline::build(*movie);

    auto frame = timeline->frameForLabel("start");
    CHECK(frame.has_value());
    CHECK_EQ(*frame, 1u);
    CHECK(!timeline->frameForLabel("missing").has_value());

    timeline->gotoAndStop(3);
    CHECK_EQ(timeline->displayList().size(), 0u);

    bool ok = timeline->gotoAndStop("start");
    CHECK(ok);
    CHECK_EQ(timeline->currentFrame(), 1u);
    CHECK_EQ(timeline->displayList().size(), 1u);

    bool missing = timeline->gotoAndPlay("nope");
    CHECK(!missing);
    CHECK_EQ(timeline->currentFrame(), 1u);  // unchanged by the failed lookup
}

TEST_CASE(Timeline_GotoAndStop_ClampsOutOfRange) {
    auto bytes = buildThreeFrameMovie();
    auto movie = SwfLoader::loadSwf(bytes.data(), bytes.size());
    auto timeline = Timeline::build(*movie);

    timeline->gotoAndStop(999);
    CHECK_EQ(timeline->currentFrame(), 3u);

    timeline->gotoAndStop(0);
    CHECK_EQ(timeline->currentFrame(), 1u);
}
