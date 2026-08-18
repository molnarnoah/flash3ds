// IAudioBackend.h
//
// Phase 6: abstract sound-output seam, mirroring IRenderer's design
// (src/renderer/IRenderer.h) — flash3ds_core stays platform-independent,
// a desktop backend can exist for testing/real playback, and a Nintendo
// 3DS/ndsp backend arrives later (Phase 10) without touching runtime/ or
// avm1/.
//
// Deliberately minimal and structural: this phase parses DefineSound's
// header fields (format/rate/size/type/sample count) and StartSound's
// SOUNDINFO record, and wires StartSound tag dispatch + AVM1's Sound
// object to call through here — but does NOT decode any compressed audio
// codec (ADPCM/MP3/etc.). A real backend implementation is therefore only
// as useful as its own codec support; NullAudioBackend (the only
// implementation as of Phase 6) does nothing but log.
//
// `soundId` is a SWF character ID (DefineSound's SoundId), the same
// identifier space CharacterDictionary keys its SoundDef entries by.

#pragma once

#include <cstdint>

namespace flash3ds::audio {

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    // Starts playing `soundId`. `loopCount` matches AS2 Sound.start()'s
    // second argument / a SOUNDINFO record's LoopCount (at least 1; a
    // caller should normalize "no loop info" to 1, not 0, before calling
    // this — see MovieClipInstance::runCurrentFrameSounds()).
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
