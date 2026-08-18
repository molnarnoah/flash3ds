// Nintendo3DSAudioBackend.cpp
//
// See Nintendo3DSAudioBackend.h for the codec-decode scope note.

#include "audio/Nintendo3DSAudioBackend.h"

#include "platform/Log.h"

namespace flash3ds::audio {

Nintendo3DSAudioBackend::Nintendo3DSAudioBackend() {
    Result rc = ndspInit();
    if (R_FAILED(rc)) {
        LOG_ERROR("AUDIO", "Nintendo3DSAudioBackend: ndspInit() failed (0x%08lX); audio disabled",
                   static_cast<unsigned long>(rc));
        return;
    }
    initialized_ = true;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    channelInUse_.fill(false);
    LOG_INFO("AUDIO", "Nintendo3DSAudioBackend: ndsp initialized (%d channels available)",
             kNumChannels);
}

Nintendo3DSAudioBackend::~Nintendo3DSAudioBackend() {
    if (initialized_) {
        ndspExit();
    }
}

int Nintendo3DSAudioBackend::channelFor(uint16_t soundId) {
    auto it = soundIdToChannel_.find(soundId);
    if (it != soundIdToChannel_.end()) {
        return it->second;
    }
    for (int i = 0; i < kNumChannels; ++i) {
        if (!channelInUse_[i]) {
            channelInUse_[i] = true;
            soundIdToChannel_[soundId] = i;
            ndspChnReset(i);
            ndspChnInitParams(i);
            return i;
        }
    }
    return -1;  // all channels in use
}

void Nintendo3DSAudioBackend::playSound(uint16_t soundId, int loopCount) {
    if (!initialized_) {
        return;
    }
    const int channel = channelFor(soundId);
    if (channel < 0) {
        LOG_WARN("AUDIO", "Nintendo3DSAudioBackend: no free ndsp channel for soundId=%u",
                 soundId);
        return;
    }
    // See the header's scope note: without a decoded PCM sample buffer for
    // soundId (flash3ds-runtime does not yet decode any SWF sound codec),
    // there is nothing to hand ndspChnWaveBufAdd() here. The channel is
    // reserved and ready; a future codec-decode phase plugs in at exactly
    // this point.
    LOG_DEBUG("AUDIO",
              "Nintendo3DSAudioBackend: play soundId=%u loopCount=%d on ndsp channel %d "
              "(reserved, no decoded PCM available yet -- see header scope note)",
              soundId, loopCount, channel);
    ndspChnSetPaused(channel, false);
}

void Nintendo3DSAudioBackend::stopSound(uint16_t soundId) {
    if (!initialized_) {
        return;
    }
    auto it = soundIdToChannel_.find(soundId);
    if (it == soundIdToChannel_.end()) {
        return;
    }
    const int channel = it->second;
    ndspChnWaveBufClear(channel);
    ndspChnReset(channel);
    channelInUse_[channel] = false;
    soundIdToChannel_.erase(it);
    LOG_DEBUG("AUDIO", "Nintendo3DSAudioBackend: stop soundId=%u (freed ndsp channel %d)",
              soundId, channel);
}

void Nintendo3DSAudioBackend::stopAllSounds() {
    if (!initialized_) {
        return;
    }
    for (int i = 0; i < kNumChannels; ++i) {
        if (channelInUse_[i]) {
            ndspChnWaveBufClear(i);
            ndspChnReset(i);
            channelInUse_[i] = false;
        }
    }
    soundIdToChannel_.clear();
    LOG_DEBUG("AUDIO", "Nintendo3DSAudioBackend: stopAll (all ndsp channels freed)");
}

}  // namespace flash3ds::audio
