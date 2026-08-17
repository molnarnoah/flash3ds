#include "avm1/GlobalObject.h"

namespace flash3ds::avm1 {

std::shared_ptr<Object> GlobalObject::create() { return std::make_shared<Object>(); }

}  // namespace flash3ds::avm1
