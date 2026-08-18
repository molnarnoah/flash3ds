// NullAudioBackend.h
//
// Default IAudioBackend: logs what it was asked to do and produces no
// actual sound. This is the backend ScriptEnvironment uses when nothing
// else is wired up (every test and the current CLI), the same role
// NullAudioBackend-shaped defaults play elsewhere in the project (compare
// how a host with no HostBindings wired just logs — avm1/Interpreter.cpp).

#pragma once

#include "audio/IAudioBackend.h"

namespace flash3ds::audio {

class NullAudioBackend : public IAudioBackend {
public:
    void playSound(uint16_t soundId, int loopCount) override;
    void stopSound(uint16_t soundId) override;
    void stopAllSounds() override;
};

}  // namespace flash3ds::audio
