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
    initResult_ = rc;  // see initResult()'s doc comment (Nintendo3DSAudioBackend.h)
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
    for (auto& [soundId, sound] : loadedSounds_) {
        (void)soundId;
        freeLoadedSound(sound);
    }
    loadedSounds_.clear();
    if (initialized_) {
        ndspExit();
    }
}

void Nintendo3DSAudioBackend::freeLoadedSound(LoadedSound& sound) {
    if (sound.buffer) {
        linearFree(sound.buffer);
        sound.buffer = nullptr;
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

void Nintendo3DSAudioBackend::loadSound(uint16_t soundId, const int16_t* samples,
                                          size_t sampleCount, int sampleRate, int channels) {
    if (!initialized_ || !samples || sampleCount == 0 || channels <= 0) {
        return;
    }

    auto& sound = loadedSounds_[soundId];  // inserts a default LoadedSound if new
    freeLoadedSound(sound);  // drop any previous buffer for this soundId first

    sound.buffer =
        static_cast<int16_t*>(linearAlloc(sampleCount * sizeof(int16_t)));
    if (!sound.buffer) {
        LOG_ERROR("AUDIO", "Nintendo3DSAudioBackend: linearAlloc failed for soundId=%u (%zu samples)",
                   soundId, sampleCount);
        sound.frameCount = 0;
        return;
    }
    std::memcpy(sound.buffer, samples, sampleCount * sizeof(int16_t));
    sound.frameCount = sampleCount / static_cast<size_t>(channels);
    sound.sampleRate = sampleRate;
    sound.channels = channels;

    // Same reasoning as playTestTone(): the DSP reads physical memory
    // directly via DMA, so the ARM11-side write above must be flushed out
    // of the data cache before ndsp is ever told to read this buffer.
    DSP_FlushDataCache(sound.buffer, static_cast<u32>(sampleCount * sizeof(int16_t)));

    LOG_DEBUG("AUDIO", "Nintendo3DSAudioBackend: loadSound soundId=%u frames=%zu rate=%d ch=%d",
              soundId, sound.frameCount, sampleRate, channels);
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

    auto it = loadedSounds_.find(soundId);
    if (it == loadedSounds_.end() || !it->second.buffer) {
        // No decoded PCM registered for this soundId (its codec isn't
        // decoded yet -- currently only MP3 is, see
        // runtime::ScriptEnvironment::playSoundById() and
        // docs/known-limitations.md L1). Same "reserved, nothing to
        // queue" fallback this backend has always had for that case.
        LOG_DEBUG("AUDIO",
                  "Nintendo3DSAudioBackend: play soundId=%u loopCount=%d on ndsp channel %d "
                  "(no decoded PCM loaded -- unsupported codec or decode failure)",
                  soundId, loopCount, channel);
        ndspChnSetPaused(channel, false);
        return;
    }

    LoadedSound& sound = it->second;
    ndspChnWaveBufClear(channel);  // stop/discard anything still queued from a prior trigger
    ndspChnReset(channel);
    ndspChnInitParams(channel);
    ndspChnSetFormat(channel, sound.channels == 2 ? NDSP_FORMAT_STEREO_PCM16
                                                    : NDSP_FORMAT_MONO_PCM16);
    ndspChnSetRate(channel, static_cast<float>(sound.sampleRate));
    float mix[12] = {0.0f};
    mix[0] = mix[1] = 1.0f;  // full volume to both front-left/front-right mix slots
    ndspChnSetMix(channel, mix);

    // KNOWN GAP (see this file's header comment): a real SWF StartSound
    // loopCount > 1 means "repeat this sound N times", but that is NOT
    // implemented as a true repeat here -- a single ndspWaveBuf's
    // `looping` flag means "loop forever" (not a counted repeat), and
    // building a real counted repeat means queuing N separate wavebufs
    // (all pointing at the same underlying PCM, per common ndsp usage),
    // which needs on-device verification this environment cannot do (see
    // docs/3ds-toolchain.md). Rather than guess at that without being
    // able to test it, loopCount is honored only as "play once" here --
    // logged, not silently dropped.
    if (loopCount > 1) {
        LOG_DEBUG("AUDIO",
                  "Nintendo3DSAudioBackend: soundId=%u requested loopCount=%d -- playing once "
                  "(counted-repeat queuing not implemented, see header comment)",
                  soundId, loopCount);
    }

    std::memset(&sound.waveBuf, 0, sizeof(sound.waveBuf));
    sound.waveBuf.data_pcm16 = sound.buffer;
    sound.waveBuf.nsamples = static_cast<u32>(sound.frameCount);
    sound.waveBuf.looping = false;
    sound.waveBuf.status = NDSP_WBUF_FREE;

    ndspChnWaveBufAdd(channel, &sound.waveBuf);
    ndspChnSetPaused(channel, false);

    LOG_DEBUG("AUDIO",
              "Nintendo3DSAudioBackend: play soundId=%u frames=%zu on ndsp channel %d "
              "(real decoded PCM queued)",
              soundId, sound.frameCount, channel);
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
