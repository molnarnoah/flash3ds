// Nintendo3DSAudioBackend.cpp
//
// See Nintendo3DSAudioBackend.h for the codec-decode scope note and the
// playTestTone() diagnostic-only note.

#include "audio/Nintendo3DSAudioBackend.h"

#include <3ds/allocator/linear.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "platform/Log.h"

namespace flash3ds::audio {

namespace {
constexpr int kTestToneSampleRate = 22050;
}  // namespace

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
    if (testToneBuffer_) {
        linearFree(testToneBuffer_);
        testToneBuffer_ = nullptr;
    }
    if (initialized_) {
        ndspExit();
    }
}

int Nintendo3DSAudioBackend::channelFor(uint16_t soundId) {
    auto it = soundIdToChannel_.find(soundId);
    if (it != soundIdToChannel_.end()) {
        return it->second;
    }
    // kTestToneChannel (kNumSoundChannels..kNumChannels-1, currently just
    // the single last channel) is reserved for playTestTone() and never
    // handed out here.
    for (int i = 0; i < kNumSoundChannels; ++i) {
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

void Nintendo3DSAudioBackend::playTestTone(double frequencyHz, double durationSeconds) {
    if (!initialized_) {
        return;
    }
    const int numSamples =
        std::max(1, static_cast<int>(durationSeconds * kTestToneSampleRate));

    // Stop whatever the test channel is doing and free its old buffer
    // before allocating a new one — ndsp must not still be reading a
    // buffer we're about to free.
    ndspChnWaveBufClear(kTestToneChannel);
    ndspChnReset(kTestToneChannel);
    if (testToneBuffer_) {
        linearFree(testToneBuffer_);
        testToneBuffer_ = nullptr;
    }

    // Sample buffers ndsp actually plays from must live in libctru's
    // "linear" heap (DSP-DMA-accessible memory) — a plain new[]/malloc
    // buffer is not guaranteed reachable the same way. linearAlloc's
    // 0x80-byte alignment guarantee is more than PCM16 needs, but costs
    // nothing here.
    testToneBuffer_ =
        static_cast<int16_t*>(linearAlloc(static_cast<size_t>(numSamples) * sizeof(int16_t)));
    if (!testToneBuffer_) {
        LOG_ERROR("AUDIO", "Nintendo3DSAudioBackend: linearAlloc failed for test tone (%d samples)",
                   numSamples);
        return;
    }

    // A short linear fade-out over the final ~10% of samples avoids an
    // audible click at the end of the buffer (the raw sine simply stops
    // otherwise, which is a sharp discontinuity unless it happens to land
    // near a zero-crossing).
    const int fadeSamples = std::max(1, numSamples / 10);
    for (int i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i) / kTestToneSampleRate;
        double envelope = 1.0;
        if (i >= numSamples - fadeSamples) {
            envelope = static_cast<double>(numSamples - i) / fadeSamples;
        }
        const double sample = 30000.0 * envelope * std::sin(2.0 * M_PI * frequencyHz * t);
        testToneBuffer_[i] = static_cast<int16_t>(sample);
    }

    // The ARM11 side just wrote this buffer through the data cache; the
    // DSP reads physical memory directly via DMA, so the cache must be
    // flushed before queuing it or the DSP may read stale/uninitialized
    // memory.
    DSP_FlushDataCache(testToneBuffer_,
                        static_cast<u32>(numSamples) * sizeof(int16_t));

    ndspChnInitParams(kTestToneChannel);
    ndspChnSetFormat(kTestToneChannel, NDSP_FORMAT_MONO_PCM16);
    ndspChnSetRate(kTestToneChannel, static_cast<float>(kTestToneSampleRate));
    float mix[12] = {0.0f};
    mix[0] = mix[1] = 1.0f;  // full volume to both front-left/front-right mix slots
    ndspChnSetMix(kTestToneChannel, mix);

    std::memset(&testToneWaveBuf_, 0, sizeof(testToneWaveBuf_));
    testToneWaveBuf_.data_pcm16 = testToneBuffer_;
    testToneWaveBuf_.nsamples = static_cast<u32>(numSamples);
    testToneWaveBuf_.looping = false;
    testToneWaveBuf_.status = NDSP_WBUF_FREE;

    ndspChnWaveBufAdd(kTestToneChannel, &testToneWaveBuf_);

    LOG_DEBUG("AUDIO",
              "Nintendo3DSAudioBackend: playTestTone freq=%.1fHz duration=%.2fs (%d samples) on "
              "reserved ndsp channel %d",
              frequencyHz, durationSeconds, numSamples, kTestToneChannel);
}

}  // namespace flash3ds::audio
