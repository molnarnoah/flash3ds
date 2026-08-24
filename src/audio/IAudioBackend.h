// IAudioBackend.h
//
// Phase 6: abstract sound-output seam, mirroring IRenderer's design
// (src/renderer/IRenderer.h) — flash3ds_core stays platform-independent,
// a desktop backend can exist for testing/real playback, and a Nintendo
// 3DS/ndsp backend arrives later (Phase 10) without touching runtime/ or
// avm1/.
//
// Deliberately minimal and structural: Phase 6 parsed DefineSound's
// header fields (format/rate/size/type/sample count) and StartSound's
// SOUNDINFO record, and wired StartSound tag dispatch + AVM1's Sound
// object to call through here. Phase 3 of the compatibility-audit-era
// roadmap (2026-08-21, see docs/implementation-roadmap.md) added the
// actual codec decode this seam was always missing: `runtime::
// ScriptEnvironment` (src/runtime/MovieClipInstance.h/.cpp) now decodes a
// soundId's MP3 payload (src/audio/Mp3Decoder.h — MP3 only; ADPCM/
// Nellymoser/Speex still aren't decoded, see docs/known-limitations.md
// L1) on first reference and hands the resulting PCM to loadSound()
// below, BEFORE calling playSound() for that trigger. A backend that
// doesn't override loadSound() (the default here) just never gets PCM
// data and playSound() alone is all it ever sees — exactly Phase 6's
// original behavior, so NullAudioBackend needs no changes to keep
// working, and this is why loadSound() and playSound() are two separate
// calls rather than one combined call: playSound() alone is still a
// complete, meaningful signal ("this soundId was triggered") even for a
// backend that can't or doesn't want to hold PCM.
//
// `soundId` is a SWF character ID (DefineSound's SoundId), the same
// identifier space CharacterDictionary keys its SoundDef entries by.

#pragma once

#include <cstddef>
#include <cstdint>

namespace flash3ds::audio {

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    // Registers (or replaces, if already registered) `soundId`'s decoded
    // PCM samples, so a subsequent playSound(soundId, ...) has real audio
    // to play. `samples` is interleaved PCM16 (frame-major for multi-
    // channel: L0 R0 L1 R1 ... for stereo), `sampleCount` is the TOTAL
    // interleaved sample count (i.e. `samples.size()`, not divided by
    // `channels` — see audio::DecodedAudio::frameCount() in Mp3Decoder.h
    // for the per-channel frame count a backend may need separately, e.g.
    // for an ndsp-style API that wants frames rather than total samples).
    // `samples` is only guaranteed valid for the duration of this call —
    // a backend that wants to keep it must copy it (see
    // Nintendo3DSAudioBackend::loadSound() for why: ndsp sample buffers
    // must live in the 3DS's DMA-accessible "linear" heap, which a
    // caller-owned std::vector is not guaranteed to be).
    virtual void loadSound(uint16_t soundId, const int16_t* samples, size_t sampleCount,
                            int sampleRate, int channels) {
        (void)soundId;
        (void)samples;
        (void)sampleCount;
        (void)sampleRate;
        (void)channels;
    }

    // Starts playing `soundId`. `loopCount` matches AS2 Sound.start()'s
    // second argument / a SOUNDINFO record's LoopCount (at least 1; a
    // caller should normalize "no loop info" to 1, not 0, before calling
    // this — see MovieClipInstance::runCurrentFrameSounds()). If
    // loadSound() was called for this soundId first (i.e. decode
    // succeeded), a real backend can use the PCM it registered; if not
    // (decode failed, or the sound's format isn't decodable yet — see
    // docs/known-limitations.md L1), this is still called, matching
    // Phase 6's original "at least log/track that this was triggered"
    // behavior.
    virtual void playSound(uint16_t soundId, int loopCount) {
        (void)soundId;
        (void)loopCount;
    }

    // Stops one specific sound (AS2 Sound.stop() when a sound is
    // attached, or a StartSound tag whose SOUNDINFO SyncStop flag is set).
    virtual void stopSound(uint16_t soundId) { (void)soundId; }

    // Stops everything currently playing (AS2 Sound.stop() with no sound
    // attached / the legacy ActionStopSounds action).
    virtual void stopAllSounds() {}
};

}  // namespace flash3ds::audio
