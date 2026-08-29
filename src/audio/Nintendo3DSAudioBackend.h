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
#include <vector>

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

    // Added 2026-08-29 while investigating a real "no sound at all" report
    // (docs/audio.md's "no sound at all investigation" section): a real
    // on-device test (the on-screen indicator these accessors feed, in
    // nintendo3ds_main.cpp) CONFIRMED ndspInit() fails on Azahar's direct
    // "Load File" launch path with Result module=41 (RM_DSP), description
    // =1018 (RD_NOT_FOUND) -- exactly libctru's own
    // ndspFindAndLoadComponent() (source/ndsp/ndsp.c) failure for "no DSP
    // firmware component found". Per devkitPro's own 3ds-examples audio
    // README: real hardware needs a genuine dspfirm.cdc dump at
    // sdmc:/3ds/dspfirm.cdc, but Citra-family emulators (Azahar included --
    // same lineage) only check that a file exists at that path; a 0-byte
    // placeholder is sufficient because the emulator's own DSP HLE ignores
    // the actual content. This is a one-time SD-card/emulator-userdata
    // setup step on the USER's side, not a code bug -- there is nothing
    // this backend can do differently to make ndspInit() succeed without
    // that file (and this project will never bundle a real dspfirm.cdc
    // dump -- see CLAUDE.md's "public sources only" rule). These accessors
    // exist purely so a caller can show ndspInit()'s real outcome on
    // screen (no log access needed, matching this project's established
    // diagnostic convention) to confirm the placeholder file actually
    // fixes it once created.
    bool isInitialized() const { return initialized_; }
    Result initResult() const { return initResult_; }

    // Copies `samples` into a freshly linearAlloc'd buffer (ndsp sample
    // buffers must live in the 3DS's DMA-accessible "linear" heap, same
    // constraint as playTestTone()'s buffer below — a caller-owned
    // std::vector's regular-heap allocation is not guaranteed reachable
    // the same way), replacing any previously-loaded buffer for this
    // soundId. Does not itself start playback — see playSound().
    void loadSound(uint16_t soundId, const int16_t* samples, size_t sampleCount, int sampleRate,
                    int channels) override;
    // startFrame/endFrame: see IAudioBackend::playSound()'s doc comment
    // (SOUNDINFO InPoint/OutPoint, per-channel frame positions). Applied
    // as a simple pointer-offset + shortened nsamples against the SAME
    // already-loaded PCM buffer (no extra copy/upload) — see this
    // method's own .cpp comment for the clamping rules.
    void playSound(uint16_t soundId, int loopCount, uint32_t startFrame = 0,
                    uint32_t endFrame = kPlayToEnd) override;
    void stopSound(uint16_t soundId) override;
    void stopAllSounds() override;

    // Added 2026-08-29 (docs/flash-fidelity-audit.md TASK 1, divergence #1)
    // — `volume` is normalized [0.0, 1.0], see IAudioBackend::setVolume()'s
    // own doc comment for the unit contract. Always records `volume` in
    // soundVolumes_ (so a LATER playSound(soundId, ...) picks it up even
    // if nothing is playing yet), and additionally applies it immediately
    // via ndspChnSetMix() if soundId currently owns an active channel —
    // real Flash lets a script raise/lower volume on a sound that's
    // already playing (hobo.swf's own mute button is exactly this: it
    // calls setVolume(0)/setVolume(100) on a Sound object that may already
    // be attached to a live channel), so waiting for the next playSound()
    // call would be audibly wrong (the sound would stay at its old volume
    // until it happened to restart).
    void setVolume(uint16_t soundId, float volume) override;

    // Added 2026-08-29 (docs/flash-fidelity-audit.md TASK 1, divergence
    // #4) — reports whether soundId currently owns an active channel AND
    // that channel's queued/playing wavebufs haven't all finished yet
    // (same ndspChnIsPlaying() check reclaimFinishedChannels() already
    // uses, reused here rather than duplicated). Does NOT itself reclaim
    // a finished channel as a side effect — that stays channelFor()'s job
    // (called lazily, once per playSound() trigger) so a read-only query
    // like this one has no surprising mutating side effect.
    bool isPlaying(uint16_t soundId) override;

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

    // Added 2026-08-29 (docs/flash-fidelity-audit.md TASK 1, divergence
    // #3): caps how many chained ndspWaveBufs a single playSound()
    // counted-repeat request can queue (see playSound()'s own comment).
    // Bounds memory/queue growth against a pathological AS2
    // Sound.start(0, loopCount)/SOUNDINFO LoopCount value -- no real
    // corpus content has been seen anywhere near this many real repeats
    // (see docs/audio.md).
    static constexpr int kMaxQueuedRepeats = 32;

    // Assigns (or reuses, if soundId is already bound to a channel) an
    // ndsp channel for soundId. Returns -1 if all channels are in use.
    // Calls reclaimFinishedChannels() first (see its own doc comment).
    int channelFor(uint16_t soundId);

    // Added 2026-08-29 (docs/flash-fidelity-audit.md TASK 1, divergence
    // #2): a channel was previously only ever freed by an explicit
    // stopSound()/stopAllSounds() call -- a fire-and-forget StartSound
    // (the overwhelming majority of real SWF sound triggers, which never
    // call Sound.stop()) permanently occupied its channel for the
    // backend's whole lifetime, so all kNumSoundChannels could eventually
    // be exhausted by ordinary one-shot playback alone. Sweeps every
    // channel this backend currently thinks is in use and, for any whose
    // queued/playing wavebufs have all actually finished (per
    // ndspChnIsPlaying()), reclaims it -- erasing its soundIdToChannel_
    // entry and clearing channelInUse_ -- so a LATER channelFor() call for
    // a DIFFERENT soundId can reuse it without needing an explicit
    // stopSound() first. Called at the top of channelFor(), i.e. once per
    // playSound() trigger -- lazy reclamation, not a background sweep.
    void reclaimFinishedChannels();

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
        // Added 2026-08-29 (TASK 1, divergence #3, replacing the single
        // `waveBuf` field this struct used to have): one ndspWaveBuf per
        // queued repeat, all pointing at the SAME `buffer` above --
        // confirmed correct against libctru's own ndsp-channel.c,
        // ndspChnWaveBufAdd() builds a real singly-linked play-queue via
        // each buffer's own `next` field (set automatically -- see
        // ndsp.h's own "Used internally, do not modify" doc comment), so
        // ndsp advances through and plays each buffer back-to-back with
        // no gap/callback needed from this code. Owned by this struct
        // (not a playSound() local) so it stays alive for as long as ndsp
        // might still be reading it -- same reasoning as
        // playTestTone()'s testToneWaveBuf_ below. Re-populated on every
        // playSound() call (see that method) AFTER ndspChnWaveBufClear()
        // has already dropped ndsp's references to whatever was queued
        // from a prior trigger, so reassigning this vector (which may
        // reallocate) is safe.
        std::vector<ndspWaveBuf> repeatWaveBufs;
    };
    void freeLoadedSound(LoadedSound& sound);

    bool initialized_ = false;
    Result initResult_ = 0;  // see initResult()'s doc comment above
    std::unordered_map<uint16_t, int> soundIdToChannel_;
    std::array<bool, kNumChannels> channelInUse_{};
    std::unordered_map<uint16_t, LoadedSound> loadedSounds_;

    // Per-soundId volume, set via setVolume() (see its own doc comment
    // above). Absent == 1.0f (full volume) — matches playSound()'s
    // previous hardcoded behavior exactly for any soundId setVolume() has
    // never been called for, so this is purely additive.
    std::unordered_map<uint16_t, float> soundVolumes_;

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
