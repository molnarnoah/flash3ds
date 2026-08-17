// Timeline.h
//
// Phase 2 timeline/playhead model for a Movie's top-level (main) timeline.
// Nested timelines (DefineSprite/MovieClip bodies) are Phase 3+ — this only
// walks the Movie's own tag list.
//
// Groups the flat Movie::tags list into per-frame buckets at ShowFrame
// boundaries, then lets a caller drive a playhead over those frames
// (gotoAndStop/gotoAndPlay/nextFrame/prevFrame/play/stop), maintaining a
// DisplayList that reflects PlaceObject/PlaceObject2/RemoveObject/
// RemoveObject2 tags cumulatively up to the current frame — exactly like a
// real SWF player's timeline (objects placed on frame N persist on later
// frames until removed).

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "runtime/DisplayList.h"
#include "runtime/Movie.h"

namespace flash3ds::runtime {

class Timeline {
public:
    // Builds a Timeline from `movie`'s top-level tags. `movie` must outlive
    // the returned Timeline (it is not copied — Timeline reads tag bodies
    // out of movie.data on demand while replaying frames). Returns nullptr
    // if `movie` isn't valid.
    static std::unique_ptr<Timeline> build(const Movie& movie);

    // Total number of frames, i.e. the number of ShowFrame tags actually
    // encountered while building (which may differ from the SWF header's
    // declared FrameCount for a malformed file — see
    // Movie::frameCount vs Timeline::frameCount).
    uint32_t frameCount() const { return static_cast<uint32_t>(frames_.size()); }

    // 1-based; 0 only if frameCount() == 0.
    uint32_t currentFrame() const { return currentFrame_; }

    bool isPlaying() const { return playing_; }

    // Frame indices are 1-based and clamped into [1, frameCount()]. No-op
    // if frameCount() == 0.
    void gotoAndStop(uint32_t frameIndex);
    void gotoAndPlay(uint32_t frameIndex);

    // Label-based overloads. Returns false (no-op) if the label isn't
    // found.
    bool gotoAndStop(const std::string& label);
    bool gotoAndPlay(const std::string& label);

    // AS2 semantics: moving the playhead by one frame always stops
    // playback (matches MovieClip.nextFrame()/prevFrame() in real Flash).
    // No-op at the first/last frame respectively.
    void nextFrame();
    void prevFrame();

    void play() { playing_ = true; }
    void stop() { playing_ = false; }

    // Advances the playhead by one frame if isPlaying(); loops back to
    // frame 1 past the last frame (matches a real timeline with no
    // explicit stop() on its last frame). No-op if frameCount() == 0.
    // Intended to be called once per host "tick" by a future frame loop.
    void advanceOneFrame();

    const DisplayList& displayList() const { return displayList_; }

    // Frame index (1-based) for a FrameLabel tag's name, if one exists.
    std::optional<uint32_t> frameForLabel(const std::string& label) const;

    // All (label name, 1-based frame index) pairs found via FrameLabel
    // tags, in the order they appear in the tag stream.
    const std::vector<std::pair<std::string, uint32_t>>& labels() const { return labels_; }

private:
    struct FrameOps {
        std::vector<size_t> tagIndices;  // indices into movie_->tags
    };

    explicit Timeline(const Movie& movie) : movie_(&movie) {}

    // Rebuilds displayList_ from scratch by replaying frames [1, frameIndex]
    // in order. Simple and correct; Phase 2 has no performance requirement
    // that would justify incremental forward-only application.
    void applyFrame(uint32_t frameIndex);

    const Movie* movie_;
    std::vector<FrameOps> frames_;
    std::vector<std::pair<std::string, uint32_t>> labels_;  // name -> 1-based frame index

    DisplayList displayList_;
    uint32_t currentFrame_ = 0;
    bool playing_ = true;
};

}  // namespace flash3ds::runtime
