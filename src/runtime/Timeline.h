// Timeline.h
//
// A timeline/playhead model for any SWF-style tag stream that contains
// ShowFrame boundaries: a Movie's own top-level tags, OR a DefineSprite
// character's nested control-tag stream (see CharacterDictionary.h) — both
// are just "a list of TagRecords with ShowFrame markers" as far as Timeline
// is concerned.
//
// Groups the given tag list into per-frame buckets at ShowFrame boundaries,
// then lets a caller drive a playhead over those frames
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
#include "swf/StartSoundTag.h"

namespace flash3ds::runtime {

class Timeline {
public:
    // Builds a Timeline from an explicit tag list (e.g. a DefineSprite
    // character's nested control tags — see CharacterDictionary.h). `tags`
    // is copied (it's typically small); tag *bodies* are still read from
    // `movie.data` on demand via Movie::tagBodyReader, so bodyOffset values
    // in `tags` must be valid absolute offsets into `movie.data`. `movie`
    // must outlive the returned Timeline. Returns nullptr if `movie` isn't
    // valid.
    static std::unique_ptr<Timeline> build(const Movie& movie,
                                            const std::vector<swf::TagRecord>& tags);

    // Convenience overload: builds a Timeline from `movie`'s own top-level
    // tags (movie.tags).
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

    // 2026-08-27 (task #68): a NEUTRAL reposition -- moves the playhead and
    // rebuilds the display list exactly like gotoAndStop/gotoAndPlay above,
    // but deliberately does NOT touch isPlaying(). This is what the SWF
    // spec's bare ActionGotoFrame/ActionGotoLabel action codes need: per
    // the standard AVM1 compiler output, `gotoAndPlay(n)` in AS2 source
    // compiles to ActionGotoFrame(n) ALONE (no accompanying ActionPlay --
    // the target is expected to already default to playing), while
    // `gotoAndStop(n)` compiles to ActionGotoFrame(n) followed by a
    // SEPARATE ActionStop right after it in the same action stream. Only
    // ActionGotoFrame2 (the newer, flag-based action) explicitly decides
    // play-vs-stop itself via its own PlayFlag bit -- see
    // Interpreter.cpp's GotoFrame2 case, which already calls play()/stop()
    // explicitly after repositioning and was therefore NEVER affected by
    // this bug. Found via task #68's investigation into why hobo.swf's
    // "preloader" sprite (a real, unmodified corpus file) never advances
    // past its own local frame 1: its frame-1 script is exactly
    // `gotoAndPlay(3)` -- bare ActionGotoFrame(2), no ActionStop -- and
    // before this fix, gotoFrame()/gotoLabel() (below) called
    // gotoAndStop() directly, silently turning EVERY bare-ActionGotoFrame-
    // based gotoAndPlay() in the corpus into a gotoAndStop() instead. See
    // docs/known-limitations.md for the full writeup.
    void gotoFrame(uint32_t frameIndex);
    bool gotoFrame(const std::string& label);

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

    // Byte bodies of every DoAction tag (tag 12) belonging to the CURRENT
    // frame (currentFrame()), in tag-stream order. Read fresh on each call
    // (a scoped-reader read over already-resident Movie::data, not a
    // reparse of anything expensive) — not cached, since it's expected to
    // be called at most once per frame change. Timeline itself has no
    // AVM1 dependency and never executes these bytes; this just exposes
    // them for a caller (runtime::MovieClipInstance, Phase 5) that does.
    std::vector<std::vector<uint8_t>> currentFrameDoActionBodies() const;

    // StartSound (tag 15) records belonging to the CURRENT frame, in
    // tag-stream order — same "read fresh on each call, not cached" design
    // as currentFrameDoActionBodies() above, and for the same reason
    // (Timeline has no audio dependency and never plays anything itself;
    // this just exposes the parsed records for a caller — MovieClipInstance,
    // Phase 6 — that dispatches them to an IAudioBackend).
    std::vector<swf::StartSoundRecord> currentFrameStartSoundEvents() const;

    // Escape hatch for AVM1's CloneSprite/RemoveSprite actions (Phase 5):
    // lets a script insert or remove a display-list entry OUTSIDE of the
    // normal tag-driven applyFrame() flow, exactly like a real player's
    // runtime-only display objects. NOT used by anything else — every
    // other DisplayList mutation in this class stays purely tag-driven.
    DisplayList& mutableDisplayListForScripting() { return displayList_; }

private:
    struct FrameOps {
        std::vector<size_t> tagIndices;  // indices into tags_
    };

    explicit Timeline(const Movie& movie) : movie_(&movie) {}

    // Rebuilds displayList_ from scratch by replaying frames [1, frameIndex]
    // in order. Simple and correct; Phase 2 has no performance requirement
    // that would justify incremental forward-only application.
    void applyFrame(uint32_t frameIndex);

    const Movie* movie_;
    std::vector<swf::TagRecord> tags_;
    std::vector<FrameOps> frames_;
    std::vector<std::pair<std::string, uint32_t>> labels_;  // name -> 1-based frame index

    DisplayList displayList_;
    uint32_t currentFrame_ = 0;
    bool playing_ = true;
};

}  // namespace flash3ds::runtime
