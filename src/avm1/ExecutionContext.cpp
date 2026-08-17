#include "avm1/ExecutionContext.h"

#include "platform/Log.h"

namespace flash3ds::avm1 {

Value ExecutionContext::getRegister(size_t index) const {
    if (index >= registers.size()) {
        LOG_DEBUG("AVM1", "register read out of range (%zu >= %zu)", index, registers.size());
        return Value::undefined();
    }
    return registers[index];
}

void ExecutionContext::setRegister(size_t index, Value value) {
    if (index >= registers.size()) {
        LOG_DEBUG("AVM1", "register write out of range (%zu >= %zu) — ignored", index,
                  registers.size());
        return;
    }
    registers[index] = std::move(value);
}

}  // namespace flash3ds::avm1
