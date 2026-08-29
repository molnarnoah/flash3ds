#include "audio/NullAudioBackend.h"

#include "platform/Log.h"

namespace flash3ds::audio {

void NullAudioBackend::loadSound(uint16_t soundId, const int16_t* samples, size_t sampleCount,
                                  int sampleRate, int channels) {
    (void)samples;
    LOG_DEBUG("AUDIO",
              "NullAudioBackend: loadSound soundId=%u sampleCount=%zu sampleRate=%d channels=%d "
              "(no-op, PCM discarded)",
              soundId, sampleCount, sampleRate, channels);
}

void NullAudioBackend::playSound(uint16_t soundId, int loopCount, uint32_t startFrame,
                                  uint32_t endFrame) {
    // Explicit unsigned-int casts: uint32_t != `unsigned int` on the 3DS
    // ARM cross-compile (see Nintendo3DSAudioBackend.cpp's matching
    // comment for the full explanation) -- silent on desktop, flagged
    // there.
    LOG_DEBUG("AUDIO",
              "NullAudioBackend: play soundId=%u loopCount=%d startFrame=%u endFrame=%u (no-op)",
              soundId, loopCount, static_cast<unsigned int>(startFrame),
              static_cast<unsigned int>(endFrame));
}

void NullAudioBackend::stopSound(uint16_t soundId) {
    LOG_DEBUG("AUDIO", "NullAudioBackend: stop soundId=%u (no-op)", soundId);
}

void NullAudioBackend::stopAllSounds() { LOG_DEBUG("AUDIO", "NullAudioBackend: stopAll (no-op)"); }

}  // namespace flash3ds::audio
