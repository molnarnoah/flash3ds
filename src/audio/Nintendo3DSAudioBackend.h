// Nintendo3DSAudioBackend.h
//
// Phase 10 — Nintendo 3DS backend. IAudioBackend implementation over
// libctru's ndsp (Nintendo DSP) service: real channel reservation, pause/
// resume, and per-channel volume control against actual 3DS audio
// hardware.
//
// SCOPE UPDATE (2026-08-21, Roadmap Phase 3 — see docs/known-limitations.md
// L1): flash3ds-runtime now decodes MP3-format DefineSound audio (see
// src/audio/Mp3Decoder.h and runtime::ScriptEnvironment::playSoundById()),
// and this backend's loadSound()/playSound() now actually queue that
// decoded PCM through ndsp — replacing what used to be a pure "channel
// reserved, nothing to queue" no-op (that gap is carried forward honestly
// from Phase 6/10, see below for exactly what's still missing). Other SWF
// sound codecs (ADPCM/Nellymoser/Speex/uncompressed) still aren't decoded
// — for a soundId in one of those formats, loadSound() is simply never
// called (ScriptEnvironment only decodes MP3), so playSound() here falls
// back to the original "channel reserved, nothing to queue, logged" path
// for those, same as before this phase.
//
// STILL NOT DONE (honest carry-over, not attempted this phase): a real
// finite StartSound loop count (`loopCount > 1`) is NOT implemented as a
// true repeat — see playSound()'s own comment for exactly what happens
// instead and why a real fix (chaining multiple queued ndspWaveBufs) was
// deferred rather than guessed at without on-device verification. This
// backend's code has never been run on real 3DS hardware or in an
// emulator (see docs/3ds-toolchain.md's "What's verified vs. not") — it
// compiles cleanly against the real bootstrapped libctru headers
// (docs/3ds-toolchain.md) but the actual audio behavior described here is
// unverified beyond that.
//
// playTestTone() (below) is a separate, DIAGNOSTIC-ONLY addition — a
// synthesized sine wave, not an SWF sound — added so the dual-screen test
// app (nintendo3ds_main.cpp) can prove the ndsp audio pipeline is actually
// audible on real hardware/an emulator, independent of the still-missing
// codec-decode step. It is not part of the SWF audio pipeline and is not
// reached from playSound()/StartSound/AS2 Sound.*.
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

    // Copies `samples` into a freshly linearAlloc'd buffer (ndsp sample
    // buffers must live in the 3DS's DMA-accessible "linear" heap, same
    // constraint as playTestTone()'s buffer below — a caller-owned
    // std::vector's regular-heap allocation is not guaranteed reachable
    // the same way), replacing any previously-loaded buffer for this
    // soundId. Does not itself start playback — see playSound().
    void loadSound(uint16_t soundId, const int16_t* samples, size_t sampleCount, int sampleRate,
                    int channels) override;
    void playSound(uint16_t soundId, int loopCount) override;
    void stopSound(uint16_t soundId) override;
    void stopAllSounds() override;

    // DIAGNOSTIC ONLY (see header note above) — synthesizes `durationSeconds`
    // of a mono PCM16 sine wave at `frequencyHz` and queues it on a
    // dedicated ndsp channel (kTestToneChannel, reserved separately from
    // the SWF soundId channel pool so it never collides with real playback
    // bookkeeping). Replaces any previously-playing test tone. No-op if
    // ndspInit() failed.
    void playTestTone(double frequencyHz, double durationSeconds);

private:
    // ndsp has 24 hardware channels (0..23). One (kTestToneChannel) is
    // reserved exclusively for playTestTone() and never handed out by
    // channelFor(); real SWF-sound bookkeeping only ever uses the other
    // kNumSoundChannels.
    static constexpr int kNumChannels = 24;
    static constexpr int kTestToneChannel = kNumChannels - 1;
    static constexpr int kNumSoundChannels = kNumChannels - 1;

    // Assigns (or reuses, if soundId is already bound to a channel) an
    // ndsp channel for soundId. Returns -1 if all channels are in use.
    int channelFor(uint16_t soundId);

    // A soundId's decoded PCM, as registered via loadSound(). `buffer` is
    // linearAlloc'd (see loadSound()'s own comment) and must be freed
    // (linearFree()) before being replaced or when this backend is
    // destroyed — see freeLoadedSound().
    struct LoadedSound {
        int16_t* buffer = nullptr;
        size_t frameCount = 0;  // samples PER CHANNEL, matching ndsp's
                                 // ndspWaveBuf::nsamples convention (NOT
                                 // total interleaved sample count) — see
                                 // audio::DecodedAudio::frameCount().
        int sampleRate = 0;
        int channels = 1;
        // Owned by this struct so it stays alive for as long as ndsp
        // might still be reading it (same "can't be a playSound() local"
        // reasoning as playTestTone()'s testToneWaveBuf_ below).
        ndspWaveBuf waveBuf{};
    };
    void freeLoadedSound(LoadedSound& sound);

    bool initialized_ = false;
    std::unordered_map<uint16_t, int> soundIdToChannel_;
    std::array<bool, kNumChannels> channelInUse_{};
    std::unordered_map<uint16_t, LoadedSound> loadedSounds_;

    // Test-tone playback state. The sample buffer must live in libctru's
    // "linear" heap (DSP-DMA-accessible memory — a plain std::vector's
    // regular-heap allocation is not guaranteed reachable the same way) and
    // must stay alive for as long as ndsp might still be reading it, hence
    // owned here (freed on the next playTestTone() call or in the
    // destructor) rather than as a local in playTestTone() itself.
    int16_t* testToneBuffer_ = nullptr;
    ndspWaveBuf testToneWaveBuf_{};
};

}  // namespace flash3ds::audio
