// DefineShapeTag.h
//
// Parses DefineShape (tag 2), DefineShape2 (tag 22), and DefineShape3 (tag
// 32) bodies: CharacterId, ShapeBounds (RECT), then a ShapeWithStyle. See
// ShapeRecords.h for the shape sub-format itself.
//
// DefineShape4 (tag 83) is NOT supported (adds EdgeBounds + flags +
// LineStyle2, which this Phase-3 pass doesn't implement — see
// docs/swf-support.md).

#pragma once

#include <cstdint>
#include <optional>

#include "swf/ShapeRecords.h"
#include "swf/SwfReader.h"

namespace flash3ds::swf {

struct ShapeDef {
    uint16_t characterId = 0;
    Rect bounds;  // in twips
    Shape shape;
};

// `tagCode` must be one of TagCode::DefineShape/DefineShape2/DefineShape3.
std::optional<ShapeDef> parseDefineShape(SwfReader& reader, uint16_t tagCode);

}  // namespace flash3ds::swf
