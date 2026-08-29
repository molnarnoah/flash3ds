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

void Nintendo3DSAudioBackend::reclaimFinishedChannels() {
    for (auto it = soundIdToChannel_.begin(); it != soundIdToChannel_.end();) {
        const int channel = it->second;
        if (!ndspChnIsPlaying(channel)) {
            LOG_DEBUG("AUDIO",
                      "Nintendo3DSAudioBackend: reclaiming ndsp channel %d (soundId=%u finished, "
                      "no explicit stopSound() call)",
                      channel, it->first);
            channelInUse_[channel] = false;
            it = soundIdToChannel_.erase(it);
        } else {
            ++it;
        }
    }
}

int Nintendo3DSAudioBackend::channelFor(uint16_t soundId) {
    reclaimFinishedChannels();
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

void Nintendo3DSAudioBackend::playSound(uint16_t soundId, int loopCount, uint32_t startFrame,
                                          uint32_t endFrame) {
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
    // Volume is per-soundId, set via setVolume() (default 1.0f/full volume
    // for any soundId that's never had setVolume() called — see
    // soundVolumes_'s own doc comment) — previously this was unconditionally
    // 1.0f regardless of any AS2 Sound.setVolume() call (docs/flash-
    // fidelity-audit.md TASK 1, divergence #1).
    auto volIt = soundVolumes_.find(soundId);
    const float volume = (volIt != soundVolumes_.end()) ? volIt->second : 1.0f;
    float mix[12] = {0.0f};
    mix[0] = mix[1] = volume;  // same normalized volume to both front-left/front-right mix slots
    ndspChnSetMix(channel, mix);

    // Counted StartSound loop repeat (docs/flash-fidelity-audit.md TASK 1,
    // divergence #3, fixed 2026-08-29): a real SWF StartSound loopCount > 1
    // means "repeat this sound N times" -- a single ndspWaveBuf's
    // `looping` flag can't express that (it means "loop forever"), so this
    // queues `repeats` separate ndspWaveBuf structs, all pointing at the
    // SAME underlying PCM buffer (see LoadedSound::repeatWaveBufs's own
    // doc comment in the header for why this is confirmed correct against
    // libctru's own ndsp-channel.c, not just assumed).
    const int repeats = std::clamp(loopCount, 1, kMaxQueuedRepeats);
    if (loopCount > repeats) {
        LOG_DEBUG("AUDIO",
                  "Nintendo3DSAudioBackend: soundId=%u requested loopCount=%d -- capped to %d "
                  "queued repeats (kMaxQueuedRepeats)",
                  soundId, loopCount, repeats);
    }

    // SOUNDINFO InPoint/OutPoint trim (docs/flash-fidelity-audit.md TASK 1,
    // divergence #7, fixed 2026-08-29): `startFrame`/`endFrame` are
    // per-channel frame positions into `sound.buffer` (see
    // IAudioBackend::playSound()'s doc comment for the unit contract).
    // Clamped defensively against the actual loaded length -- a
    // malformed/out-of-range SOUNDINFO trim degrades to "play nothing"
    // (nsamples=0, which ndspChnWaveBufAdd() itself already no-ops on --
    // see ndsp-channel.c) rather than reading out of bounds.
    const uint32_t totalFrames = static_cast<uint32_t>(sound.frameCount);
    const uint32_t trimStart = std::min(startFrame, totalFrames);
    uint32_t trimEnd = (endFrame == kPlayToEnd) ? totalFrames : std::min(endFrame, totalFrames);
    // Explicit unsigned-int casts on every %u argument below: uint32_t is
    // NOT the same type as `unsigned int` on this ARM target (it's `long
    // unsigned int` here, unlike x86_64 desktop where they coincide) --
    // same portability quirk CLAUDE.md's Phase 10 section already
    // documents for std::clamp/min/max; printf-style varargs need the
    // same explicit-type discipline or -Wformat correctly flags a
    // mismatch on this cross-compile (even though it's silent on desktop).
    if (trimEnd < trimStart) {
        LOG_WARN("AUDIO",
                  "Nintendo3DSAudioBackend: soundId=%u has out-of-order SOUNDINFO trim "
                  "(startFrame=%u > endFrame=%u after clamping to %u total frames) -- playing "
                  "nothing",
                  soundId, static_cast<unsigned int>(trimStart),
                  static_cast<unsigned int>(trimEnd), static_cast<unsigned int>(totalFrames));
        trimEnd = trimStart;
    }
    const uint32_t trimmedFrames = trimEnd - trimStart;
    int16_t* trimmedData = sound.buffer + static_cast<size_t>(trimStart) *
                                               static_cast<size_t>(sound.channels);
    if (startFrame != 0 || endFrame != kPlayToEnd) {
        LOG_DEBUG("AUDIO",
                  "Nintendo3DSAudioBackend: soundId=%u trimmed to frames [%u, %u) of %u total",
                  soundId, static_cast<unsigned int>(trimStart),
                  static_cast<unsigned int>(trimEnd), static_cast<unsigned int>(totalFrames));
    }

    // Reassigning (not just resizing) this vector is only safe because
    // ndspChnWaveBufClear(channel) above already dropped ndsp's references
    // to whatever repeatWaveBufs held from a PRIOR playSound() call on
    // this same soundId -- see the header's doc comment on this field.
    sound.repeatWaveBufs.assign(static_cast<size_t>(repeats), ndspWaveBuf{});
    for (ndspWaveBuf& buf : sound.repeatWaveBufs) {
        buf.data_pcm16 = trimmedData;
        buf.nsamples = static_cast<u32>(trimmedFrames);
        buf.looping = false;
        buf.status = NDSP_WBUF_FREE;
    }
    // Queued in a SEPARATE loop from the one above: ndspChnWaveBufAdd()
    // reads chn->waveBuf's existing tail to append to (see ndsp-channel.c),
    // so each call must see the previous one's `next` linkage already
    // committed -- interleaving the two loops would still work here (each
    // buf's own fields are fully set before its own Add call either way),
    // but keeping them separate matches the "set up the whole queue, then
    // submit it" structure real ndsp usage examples use, and reads clearer.
    for (ndspWaveBuf& buf : sound.repeatWaveBufs) {
        ndspChnWaveBufAdd(channel, &buf);
    }
    ndspChnSetPaused(channel, false);

    LOG_DEBUG("AUDIO",
              "Nintendo3DSAudioBackend: play soundId=%u frames=%u repeats=%d on ndsp channel %d "
              "(real decoded PCM queued)",
              soundId, static_cast<unsigned int>(trimmedFrames), repeats, channel);
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

void Nintendo3DSAudioBackend::setVolume(uint16_t soundId, float volume) {
    // Clamp defensively -- the AS2-side caller (MovieClipInstance.cpp)
    // already clamps vol/100.0 to [0,1] before calling here, but this is a
    // public seam any future caller could reach directly, and an
    // out-of-range mix value would otherwise silently clip/distort rather
    // than erroring.
    volume = std::clamp(volume, 0.0f, 1.0f);
    soundVolumes_[soundId] = volume;

    if (!initialized_) {
        return;
    }
    auto it = soundIdToChannel_.find(soundId);
    if (it == soundIdToChannel_.end()) {
        // Nothing currently playing for this soundId -- the stored value
        // above is still picked up by the NEXT playSound(soundId, ...)
        // call (see playSound()'s own soundVolumes_ lookup).
        return;
    }
    // Live update: apply immediately to the already-playing channel, not
    // just at the next playSound() call -- see this method's header
    // comment for why that distinction matters for hobo.swf's own mute
    // button specifically.
    const int channel = it->second;
    float mix[12] = {0.0f};
    mix[0] = mix[1] = volume;
    ndspChnSetMix(channel, mix);
    LOG_DEBUG("AUDIO", "Nintendo3DSAudioBackend: setVolume soundId=%u volume=%.3f (live update on channel %d)",
              soundId, static_cast<double>(volume), channel);
}

bool Nintendo3DSAudioBackend::isPlaying(uint16_t soundId) {
    if (!initialized_) {
        return false;
    }
    auto it = soundIdToChannel_.find(soundId);
    if (it == soundIdToChannel_.end()) {
        return false;
    }
    return ndspChnIsPlaying(it->second);
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
