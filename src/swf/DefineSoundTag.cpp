#include "swf/DefineSoundTag.h"

namespace flash3ds::swf {

std::optional<SoundDef> parseDefineSound(SwfReader& reader, size_t bodyAbsoluteOffset) {
    SoundDef def;
    def.soundId = reader.readU16();
    uint8_t flags = reader.readU8();
    def.format = static_cast<SoundFormat>((flags >> 4) & 0x0F);
    def.rate = static_cast<SoundRate>((flags >> 2) & 0x03);
    def.is16Bit = (flags & 0x02) != 0;
    def.stereo = (flags & 0x01) != 0;
    def.sampleCount = reader.readU32();
    if (reader.failed()) return std::nullopt;

    def.dataOffset = bodyAbsoluteOffset + reader.position();
    def.dataLength = reader.bytesRemaining();
    return def;
}

double soundRateHz(SoundRate rate) {
    switch (rate) {
        case SoundRate::k5512Hz: return 5512.5;
        case SoundRate::k11025Hz: return 11025.0;
        case SoundRate::k22050Hz: return 22050.0;
        case SoundRate::k44100Hz: return 44100.0;
    }
    return 5512.5;
}

}  // namespace flash3ds::swf
