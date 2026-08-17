#include "avm1/Scope.h"

namespace flash3ds::avm1 {

Value Scope::getVariable(const std::string& name) const {
    for (const auto& obj : chain_) {
        if (obj && obj->hasOwnProperty(name)) {
            return obj->getOwnProperty(name);
        }
    }
    return Value::undefined();
}

void Scope::setVariable(const std::string& name, Value value) {
    for (const auto& obj : chain_) {
        if (obj && obj->hasOwnProperty(name)) {
            obj->setOwnProperty(name, std::move(value));
            return;
        }
    }
    // Not found anywhere in the chain — create it on the innermost object.
    if (!chain_.empty() && chain_.front()) {
        chain_.front()->setOwnProperty(name, std::move(value));
    }
}

void Scope::defineLocal(const std::string& name, Value value) {
    if (!chain_.empty() && chain_.front()) {
        chain_.front()->setOwnProperty(name, std::move(value));
    }
}

bool Scope::deleteVariable(const std::string& name) {
    for (const auto& obj : chain_) {
        if (obj && obj->hasOwnProperty(name)) {
            obj->deleteOwnProperty(name);
            return true;
        }
    }
    return false;
}

}  // namespace flash3ds::avm1
