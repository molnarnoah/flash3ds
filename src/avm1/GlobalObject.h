// GlobalObject.h
//
// Constructs the AVM1 global object — AS2's `_global`, and (in Phase 4,
// before AVM1 is wired into the scene graph) also the outermost scope for
// a top-level script's variables. Deliberately minimal: Phase 4 doesn't
// need a populated built-in library (Math/String.prototype/etc. — see
// docs/avm1-support.md) since NewObject special-cases the handful of
// constructor names ("Object", "Array") it supports without needing them
// registered here. Later phases can extend GlobalObject::create() to seed
// more built-ins without changing callers.

#pragma once

#include <memory>

#include "avm1/Value.h"

namespace flash3ds::avm1 {

class GlobalObject {
public:
    static std::shared_ptr<Object> create();
};

}  // namespace flash3ds::avm1
