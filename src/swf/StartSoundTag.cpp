#include "swf/StartSoundTag.h"

namespace flash3ds::swf {

SoundInfo readSoundInfo(SwfReader& reader) {
    SoundInfo info;
    uint8_t flags = reader.readU8();
    info.hasInPoint = (flags & 0x01) != 0;
    info.hasOutPoint = (flags & 0x02) != 0;
    info.hasLoops = (flags & 0x04) != 0;
    info.hasEnvelope = (flags & 0x08) != 0;
    info.syncNoMultiple = (flags & 0x10) != 0;
    info.syncStop = (flags & 0x20) != 0;
    // Top 2 bits (0xC0) are Reserved — ignored, per spec.

    if (info.hasInPoint) info.inPointSamples = reader.readU32();
    if (info.hasOutPoint) info.outPointSamples = reader.readU32();
    if (info.hasLoops) info.loopCount = reader.readU16();
    if (info.hasEnvelope) {
        uint8_t count = reader.readU8();
        info.envelope.reserve(count);
        for (uint8_t i = 0; i < count && !reader.failed(); ++i) {
            SoundEnvelopePoint pt;
            pt.pos44 = reader.readU32();
            pt.leftLevel = reader.readU16();
            pt.rightLevel = reader.readU16();
            info.envelope.push_back(pt);
        }
    }
    return info;
}

std::optional<StartSoundRecord> parseStartSound(SwfReader& reader) {
    StartSoundRecord rec;
    rec.soundId = reader.readU16();
    if (reader.failed()) return std::nullopt;
    rec.info = readSoundInfo(reader);
    if (reader.failed()) return std::nullopt;
    return rec;
}

}  // namespace flash3ds::swf
