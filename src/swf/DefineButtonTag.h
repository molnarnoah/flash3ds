// DefineButtonTag.h
//
// Parses DefineButton (tag 7, "v1") and DefineButton2 (tag 34, "v2")
// bodies: a list of BUTTONRECORDs (one per character placed in one or more
// of the button's four states — Up/Over/Down/HitTest) plus action bytecode
// (v1: a single block, conventionally understood to run on the OverDown ->
// OverUp transition i.e. a mouse click; v2: a list of BUTTONCONDACTIONs,
// each keyed by an explicit condition bitmask covering every state
// transition plus an optional key-press trigger).
//
// SCOPE NOTE: v2 BUTTONRECORDs can optionally carry a FILTERLIST and/or
// BlendMode (SWF 8 button visual effects) — NOT implemented (this runtime
// doesn't apply filters/blend modes to ANY rendered content yet, shapes
// included). A button record with either flag set aborts parsing the
// REMAINDER of that button's records (rather than mis-reading an unknown-
// length FILTERLIST and desyncing everything after it) and logs a warning
// — see parseDefineButton2's implementation. This is expected to be rare:
// filters are an uncommon, later-era authoring feature.
//
// Neither DefineButtonCxform (tag 23, v1-only per-state color transform)
// nor DefineButtonSound (tag 17, per-state sound triggers) is parsed here
// — see docs/swf-support.md.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "swf/SwfRecords.h"
#include "swf/SwfReader.h"

namespace flash3ds::swf {

// One placed character within a button's states. A single record can (and
// commonly does) appear in more than one state simultaneously — e.g. the
// same shape for both Up and HitTest.
struct ButtonRecordDef {
    bool stateUp = false;
    bool stateOver = false;
    bool stateDown = false;
    bool stateHitTest = false;
    uint16_t characterId = 0;
    int32_t depth = 0;
    Matrix matrix;
    std::optional<ColorTransform> colorTransform;  // v2 only (CXFORMWITHALPHA)
};

// One BUTTONCONDACTION (v2 only): `conditions` is the raw 9-bit state-
// transition bitmask (see ButtonCondition below); `keyCode` is present only
// when the CondKeyPress bits are non-zero.
enum class ButtonCondition : uint16_t {
    kIdleToOverUp = 1u << 0,
    kOverUpToIdle = 1u << 1,
    kOverUpToOverDown = 1u << 2,
    kOverDownToOverUp = 1u << 3,
    kOverDownToOutDown = 1u << 4,
    kOutDownToOverDown = 1u << 5,
    kOutDownToIdle = 1u << 6,
    kIdleToOverDown = 1u << 7,
    kOverDownToIdle = 1u << 8,
};

struct ButtonCondAction {
    uint16_t conditions = 0;       // OR of ButtonCondition bits
    std::optional<uint8_t> keyCode;  // present iff the record's 7-bit CondKeyPress field != 0
    std::vector<uint8_t> actionBytes;
};

struct ButtonDef {
    uint16_t characterId = 0;
    bool trackAsMenu = false;  // v2 only
    std::vector<ButtonRecordDef> records;

    // Exactly one of these is populated, depending on which tag defined
    // the button (mirrors ClipActionRecord's actionBytes convention: a raw
    // ActionEnd-terminated AVM1 bytecode stream, not yet dispatched
    // anywhere — see docs/avm1-support.md's Known Phase 8 limitations).
    std::vector<uint8_t> actionsV1;             // DefineButton only
    std::vector<ButtonCondAction> condActionsV2;  // DefineButton2 only
};

// `tagCode` must be TagCode::DefineButton (7) or TagCode::DefineButton2
// (34).
std::optional<ButtonDef> parseDefineButton(SwfReader& reader, uint16_t tagCode);

}  // namespace flash3ds::swf
