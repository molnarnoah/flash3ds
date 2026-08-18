// StartSoundTag.h
//
// Clean-room parser for StartSound (tag 15) and its embedded SOUNDINFO
// record (also reused, per spec, by DefineButtonSound — not parsed in
// Phase 6, buttons aren't implemented yet, see docs/swf-support.md).
//
// SOUNDINFO's flag-byte layout (Reserved[2] SyncStop SyncNoMultiple
// HasEnvelope HasLoops HasOutPoint HasInPoint, MSB-first) is per the public
// SWF spec's bit table for this record — same "read the spec's bit table
// top-to-bottom into the flag byte's MSB-to-LSB" convention already used by
// PlaceObject2's flag byte (see PlaceObjectTag.cpp) and DefineSound's
// format/rate/size/type byte (DefineSoundTag.cpp).

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "swf/SwfReader.h"

namespace flash3ds::swf {

struct SoundEnvelopePoint {
    uint32_t pos44 = 0;   // sample position at 44.1kHz, regardless of the sound's actual rate
    uint16_t leftLevel = 0;
    uint16_t rightLevel = 0;
};

struct SoundInfo {
    bool syncStop = false;
    bool syncNoMultiple = false;
    bool hasEnvelope = false;
    bool hasLoops = false;
    bool hasOutPoint = false;
    bool hasInPoint = false;

    std::optional<uint32_t> inPointSamples;
    std::optional<uint32_t> outPointSamples;
    std::optional<uint16_t> loopCount;
    std::vector<SoundEnvelopePoint> envelope;
};

struct StartSoundRecord {
    uint16_t soundId = 0;
    SoundInfo info;
};

// Reads a standalone SOUNDINFO record from the current (byte-aligned)
// reader position.
SoundInfo readSoundInfo(SwfReader& reader);

// StartSound (15) tag body: SoundId (UI16) then a SOUNDINFO record.
std::optional<StartSoundRecord> parseStartSound(SwfReader& reader);

}  // namespace flash3ds::swf
