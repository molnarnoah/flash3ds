#include "runtime/Timeline.h"

#include <algorithm>

#include "platform/Log.h"
#include "swf/PlaceObjectTag.h"
#include "swf/TagCode.h"

namespace flash3ds::runtime {

std::unique_ptr<Timeline> Timeline::build(const Movie& movie,
                                           const std::vector<swf::TagRecord>& tags) {
    if (!movie.valid) {
        return nullptr;
    }

    // Use a raw `new` since the constructor is private and make_unique
    // can't reach it from outside the class.
    std::unique_ptr<Timeline> timeline(new Timeline(movie));
    timeline->tags_ = tags;

    Timeline::FrameOps current;
    for (size_t i = 0; i < timeline->tags_.size(); ++i) {
        const swf::TagRecord& tag = timeline->tags_[i];
        current.tagIndices.push_back(i);

        if (static_cast<swf::TagCode>(tag.code) == swf::TagCode::FrameLabel) {
            swf::SwfReader reader = movie.tagBodyReader(tag);
            auto label = swf::parseFrameLabel(reader);
            if (label) {
                // Frame this label belongs to is the one currently being
                // accumulated, i.e. frames_.size() + 1.
                timeline->labels_.emplace_back(
                    *label, static_cast<uint32_t>(timeline->frames_.size() + 1));
            }
        }

        if (static_cast<swf::TagCode>(tag.code) == swf::TagCode::ShowFrame) {
            timeline->frames_.push_back(std::move(current));
            current = Timeline::FrameOps{};
        }
    }
    // Any trailing tags after the last ShowFrame (e.g. a movie that ends
    // right at the End tag without a final ShowFrame) are dropped from the
    // frame model — there's no frame boundary to attach them to. This
    // matches how real players treat a missing final ShowFrame: nothing to
    // display for an incomplete frame.
    if (!current.tagIndices.empty()) {
        LOG_WARN("TIMELINE",
                  "%zu trailing tag(s) after the last ShowFrame have no frame to attach to",
                  current.tagIndices.size());
    }

    if (timeline->frameCount() > 0) {
        timeline->applyFrame(1);
        timeline->currentFrame_ = 1;
        timeline->playing_ = true;
    }

    return timeline;
}

std::unique_ptr<Timeline> Timeline::build(const Movie& movie) {
    auto timeline = build(movie, movie.tags);
    if (timeline && movie.frameCount != timeline->frameCount()) {
        LOG_WARN("TIMELINE", "Header FrameCount=%u does not match actual ShowFrame count=%u",
                  movie.frameCount, timeline->frameCount());
    }
    return timeline;
}

void Timeline::applyFrame(uint32_t frameIndex) {
    displayList_.clear();
    if (frameIndex == 0 || frameIndex > frameCount()) {
        return;
    }
    for (uint32_t f = 1; f <= frameIndex; ++f) {
        for (size_t tagIndex : frames_[f - 1].tagIndices) {
            const swf::TagRecord& tag = tags_[tagIndex];
            auto code = static_cast<swf::TagCode>(tag.code);

            if (code == swf::TagCode::PlaceObject || code == swf::TagCode::PlaceObject2) {
                swf::SwfReader reader = movie_->tagBodyReader(tag);
                auto record = swf::parsePlaceObject(reader, tag.code, movie_->version);
                if (record) {
                    displayList_.applyPlaceObject(*record);
                } else {
                    LOG_WARN("TIMELINE", "Failed to parse %s at offset=%zu", tag.name.c_str(),
                              tag.bodyOffset);
                }
            } else if (code == swf::TagCode::RemoveObject ||
                       code == swf::TagCode::RemoveObject2) {
                swf::SwfReader reader = movie_->tagBodyReader(tag);
                auto record = swf::parseRemoveObject(reader, tag.code);
                if (record) {
                    displayList_.remove(record->depth);
                } else {
                    LOG_WARN("TIMELINE", "Failed to parse %s at offset=%zu", tag.name.c_str(),
                              tag.bodyOffset);
                }
            }
            // Other tags (ShowFrame, FrameLabel, DoAction, character
            // definitions, ...) don't affect the display list directly at
            // this phase — DoAction execution is Phase 4, character
            // definitions are consumed by CharacterDictionary/SceneRenderer.
        }
    }
}

void Timeline::gotoAndStop(uint32_t frameIndex) {
    if (frameCount() == 0) return;
    // Phase 10 portability fix: the bare `1u` literal is `unsigned int`,
    // which is NOT guaranteed to be the same type as `uint32_t` (they
    // happen to coincide on x86_64 desktop builds, but not on this
    // project's ARM/newlib cross-compile target, where uint32_t is
    // `long unsigned int` -- a distinct type as far as std::clamp's single-
    // type template deduction is concerned, so the desktop build silently
    // compiled something that was never actually portable). Cast to
    // uint32_t explicitly so all three std::clamp arguments share one type
    // on every platform.
    frameIndex = std::clamp(frameIndex, static_cast<uint32_t>(1), frameCount());
    applyFrame(frameIndex);
    currentFrame_ = frameIndex;
    playing_ = false;
}

void Timeline::gotoAndPlay(uint32_t frameIndex) {
    if (frameCount() == 0) return;
    // See gotoAndStop()'s comment above -- same Phase 10 portability fix.
    frameIndex = std::clamp(frameIndex, static_cast<uint32_t>(1), frameCount());
    applyFrame(frameIndex);
    currentFrame_ = frameIndex;
    playing_ = true;
}

std::optional<uint32_t> Timeline::frameForLabel(const std::string& label) const {
    for (const auto& [name, frameIndex] : labels_) {
        if (name == label) {
            return frameIndex;
        }
    }
    return std::nullopt;
}

bool Timeline::gotoAndStop(const std::string& label) {
    auto frameIndex = frameForLabel(label);
    if (!frameIndex) return false;
    gotoAndStop(*frameIndex);
    return true;
}

bool Timeline::gotoAndPlay(const std::string& label) {
    auto frameIndex = frameForLabel(label);
    if (!frameIndex) return false;
    gotoAndPlay(*frameIndex);
    return true;
}

void Timeline::nextFrame() {
    if (frameCount() == 0 || currentFrame_ >= frameCount()) return;
    gotoAndStop(currentFrame_ + 1);
}

void Timeline::prevFrame() {
    if (frameCount() == 0 || currentFrame_ <= 1) return;
    gotoAndStop(currentFrame_ - 1);
}

std::vector<std::vector<uint8_t>> Timeline::currentFrameDoActionBodies() const {
    std::vector<std::vector<uint8_t>> result;
    if (currentFrame_ == 0 || currentFrame_ > frameCount()) return result;
    for (size_t tagIndex : frames_[currentFrame_ - 1].tagIndices) {
        const swf::TagRecord& tag = tags_[tagIndex];
        if (static_cast<swf::TagCode>(tag.code) == swf::TagCode::DoAction) {
            swf::SwfReader reader = movie_->tagBodyReader(tag);
            result.push_back(reader.readBytes(reader.size()));
        }
    }
    return result;
}

std::vector<swf::StartSoundRecord> Timeline::currentFrameStartSoundEvents() const {
    std::vector<swf::StartSoundRecord> result;
    if (currentFrame_ == 0 || currentFrame_ > frameCount()) return result;
    for (size_t tagIndex : frames_[currentFrame_ - 1].tagIndices) {
        const swf::TagRecord& tag = tags_[tagIndex];
        if (static_cast<swf::TagCode>(tag.code) == swf::TagCode::StartSound) {
            swf::SwfReader reader = movie_->tagBodyReader(tag);
            auto rec = swf::parseStartSound(reader);
            if (rec) {
                result.push_back(*rec);
            } else {
                LOG_WARN("TIMELINE", "Failed to parse StartSound at offset=%zu", tag.bodyOffset);
            }
        }
    }
    return result;
}

void Timeline::advanceOneFrame() {
    if (!playing_ || frameCount() == 0) return;
    if (currentFrame_ >= frameCount()) {
        applyFrame(1);
        currentFrame_ = 1;
    } else {
        applyFrame(currentFrame_ + 1);
        currentFrame_ += 1;
    }
    // playing_ deliberately left unchanged (unlike gotoAndStop/nextFrame):
    // this is the "tick" path, not a scripted jump.
}

}  // namespace flash3ds::runtime
