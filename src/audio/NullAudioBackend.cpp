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

void NullAudioBackend::playSound(uint16_t soundId, int loopCount) {
    LOG_DEBUG("AUDIO", "NullAudioBackend: play soundId=%u loopCount=%d (no-op)", soundId,
              loopCount);
}

void NullAudioBackend::stopSound(uint16_t soundId) {
    LOG_DEBUG("AUDIO", "NullAudioBackend: stop soundId=%u (no-op)", soundId);
}

void NullAudioBackend::stopAllSounds() { LOG_DEBUG("AUDIO", "NullAudioBackend: stopAll (no-op)"); }

}  // namespace flash3ds::audio
