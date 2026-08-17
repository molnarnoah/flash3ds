#include "swf/DefineShapeTag.h"

#include "swf/TagCode.h"

namespace flash3ds::swf {

std::optional<ShapeDef> parseDefineShape(SwfReader& reader, uint16_t tagCode) {
    int shapeVersion;
    switch (static_cast<TagCode>(tagCode)) {
        case TagCode::DefineShape: shapeVersion = 1; break;
        case TagCode::DefineShape2: shapeVersion = 2; break;
        case TagCode::DefineShape3: shapeVersion = 3; break;
        default: return std::nullopt;  // DefineShape4 and non-shape tags not supported
    }

    ShapeDef def;
    def.characterId = reader.readU16();
    if (reader.failed()) return std::nullopt;

    def.bounds = reader.readRect();
    if (reader.failed()) return std::nullopt;

    def.shape = readShapeWithStyle(reader, shapeVersion);
    return def;
}

}  // namespace flash3ds::swf
