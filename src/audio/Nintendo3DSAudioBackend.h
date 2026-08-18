// Nintendo3DSAudioBackend.h
//
// Phase 10 — Nintendo 3DS backend. IAudioBackend implementation over
// libctru's ndsp (Nintendo DSP) service: real channel reservation, pause/
// resume, and per-channel volume control against actual 3DS audio
// hardware.
//
// IMPORTANT SCOPE NOTE (carried forward honestly from Phase 6, not new to
// Phase 10): flash3ds-runtime does not yet decode any SWF sound codec
// (ADPCM/MP3/uncompressed PCM framing) into raw samples — see
// swf/DefineSoundTag.h and docs/audio.md. ndsp itself can only play PCM8/
// PCM16/DSPADPCM sample buffers; without a decode step there is no sample
// buffer to hand it. This backend therefore does real ndsp channel
// bookkeeping (so the plumbing exists and is exercised) but playSound()
// currently has nothing to actually queue, and logs that fact rather than
// silently doing nothing — this is the SAME functional limitation
// NullAudioBackend already had, just now backed by a real ndsp channel
// instead of a no-op, so a future codec-decode phase can wire samples
// straight into ndspChnWaveBufAdd() here without touching runtime/ or
// avm1/.
//
// This file is only compiled for the 3DS target (guarded by __3DS__).

#pragma once

#ifndef __3DS__
#error "Nintendo3DSAudioBackend.h is only valid in a 3DS cross-compile (__3DS__ not defined)"
#endif

#include <3ds.h>

#include <array>
#include <cstdint>
#include <unordered_map>

#include "audio/IAudioBackend.h"

namespace flash3ds::audio {

class Nintendo3DSAudioBackend : public IAudioBackend {
public:
    // Calls ndspInit(). Callers must have already initialized whatever
    // else ndsp needs (nothing else, per libctru's docs — ndspInit() is
    // self-contained). Logs and leaves the backend in a safe no-op state
    // if ndspInit() fails (matches this project's existing "log and
    // degrade gracefully" convention rather than crashing the app over
    // audio).
    Nintendo3DSAudioBackend();
    ~Nintendo3DSAudioBackend() override;

    Nintendo3DSAudioBackend(const Nintendo3DSAudioBackend&) = delete;
    Nintendo3DSAudioBackend& operator=(const Nintendo3DSAudioBackend&) = delete;

    void playSound(uint16_t soundId, int loopCount) override;
    void stopSound(uint16_t soundId) override;
    void stopAllSounds() override;

private:
    static constexpr int kNumChannels = 24;  // ndsp's fixed channel count (0..23)

    // Assigns (or reuses, if soundId is already bound to a channel) an
    // ndsp channel for soundId. Returns -1 if all channels are in use.
    int channelFor(uint16_t soundId);

    bool initialized_ = false;
    std::unordered_map<uint16_t, int> soundIdToChannel_;
    std::array<bool, kNumChannels> channelInUse_{};
};

}  // namespace flash3ds::audio
