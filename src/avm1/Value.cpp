#include "avm1/Value.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "platform/Log.h"

namespace flash3ds::avm1 {

namespace {

// Defensive cap on Array length (via "length" assignment or a numeric-index
// write) to avoid a malformed/malicious script driving an unbounded
// allocation. Real AS2 content never needs anywhere near this many
// elements.
constexpr size_t kMaxArrayLength = 10'000'000;

bool isAllDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

}  // namespace

std::string numberToAs2String(double n) {
    if (std::isnan(n)) return "NaN";
    if (std::isinf(n)) return n > 0 ? "Infinity" : "-Infinity";
    if (n == 0.0) return "0";  // collapses -0 to "0"

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.15g", n);
    return std::string(buf);
}

bool Value::toBoolean() const {
    switch (type_) {
        case ValueType::kUndefined:
        case ValueType::kNull:
            return false;
        case ValueType::kBoolean:
            return bool_;
        case ValueType::kNumber:
            return number_ != 0.0 && !std::isnan(number_);
        case ValueType::kString:
            return !string_.empty();
        case ValueType::kObject:
            return true;
    }
    return false;
}

double Value::toNumber() const {
    switch (type_) {
        case ValueType::kUndefined:
            return std::numeric_limits<double>::quiet_NaN();
        case ValueType::kNull:
            return 0.0;
        case ValueType::kBoolean:
            return bool_ ? 1.0 : 0.0;
        case ValueType::kNumber:
            return number_;
        case ValueType::kString: {
            size_t start = string_.find_first_not_of(" \t\n\r\f\v");
            if (start == std::string::npos) return 0.0;  // empty/whitespace-only
            size_t end = string_.find_last_not_of(" \t\n\r\f\v");
            std::string trimmed = string_.substr(start, end - start + 1);

            const char* cstr = trimmed.c_str();
            char* endPtr = nullptr;
            double result = std::strtod(cstr, &endPtr);
            if (endPtr != cstr + trimmed.size()) {
                return std::numeric_limits<double>::quiet_NaN();  // trailing garbage
            }
            return result;
        }
        case ValueType::kObject:
            // No valueOf() dispatch (see Value.h file header) — objects
            // coerce to NaN, matching what real AS2 does for plain objects
            // that don't implement it.
            return std::numeric_limits<double>::quiet_NaN();
    }
    return std::numeric_limits<double>::quiet_NaN();
}

int32_t Value::toInt32() const {
    double n = toNumber();
    if (std::isnan(n) || std::isinf(n)) return 0;
    double truncated = std::trunc(n);
    double mod = std::fmod(truncated, 4294967296.0);  // 2^32
    if (mod < 0) mod += 4294967296.0;
    uint32_t u = static_cast<uint32_t>(mod);
    return static_cast<int32_t>(u);
}

std::string Value::toString() const {
    switch (type_) {
        case ValueType::kUndefined:
            return "undefined";
        case ValueType::kNull:
            return "null";
        case ValueType::kBoolean:
            return bool_ ? "true" : "false";
        case ValueType::kNumber:
            return numberToAs2String(number_);
        case ValueType::kString:
            return string_;
        case ValueType::kObject: {
            if (!object_) return "null";
            if (object_->isFunction()) return "[type Function]";
            if (object_->isArray()) {
                std::string out;
                for (size_t i = 0; i < object_->elements.size(); ++i) {
                    if (i != 0) out += ",";
                    const Value& e = object_->elements[i];
                    // AS2's Array.toString() renders undefined/null
                    // elements as an empty string between commas.
                    if (!e.isUndefined() && !e.isNull()) out += e.toString();
                }
                return out;
            }
            return "[object Object]";
        }
    }
    return "";
}

bool Object::hasOwnProperty(const std::string& name) const {
    if (nativeGet) {
        Value probe;
        if (nativeGet(name, probe)) return true;
    }
    return properties_.find(name) != properties_.end();
}

Value Object::getOwnProperty(const std::string& name) const {
    if (nativeGet) {
        Value v;
        if (nativeGet(name, v)) return v;
    }
    auto it = properties_.find(name);
    return it == properties_.end() ? Value::undefined() : it->second;
}

void Object::setOwnProperty(const std::string& name, Value value) {
    if (nativeSet && nativeSet(name, value)) return;
    properties_[name] = std::move(value);
}

void Object::deleteOwnProperty(const std::string& name) { properties_.erase(name); }

Value Object::getMember(const std::string& name) const {
    if (nativeGet) {
        Value v;
        if (nativeGet(name, v)) return v;
    }

    if (kind_ == Kind::kArray) {
        if (name == "length") {
            return Value::number(static_cast<double>(elements.size()));
        }
        if (isAllDigits(name)) {
            unsigned long idx = std::strtoul(name.c_str(), nullptr, 10);
            if (idx < elements.size()) return elements[idx];
            return Value::undefined();  // in-range-looking numeric miss: don't fall through
        }
    }

    auto it = properties_.find(name);
    if (it != properties_.end()) return it->second;

    const Object* current = prototype.get();
    int depth = 0;
    while (current != nullptr && depth < kMaxPrototypeChainDepth) {
        auto protoIt = current->properties_.find(name);
        if (protoIt != current->properties_.end()) return protoIt->second;
        current = current->prototype.get();
        ++depth;
    }
    if (depth >= kMaxPrototypeChainDepth) {
        LOG_WARN("AVM1", "getMember('%s'): prototype chain depth limit exceeded (cyclic?)",
                  name.c_str());
    }
    return Value::undefined();
}

void Object::setMember(const std::string& name, Value value) {
    if (nativeSet && nativeSet(name, value)) return;

    if (kind_ == Kind::kArray) {
        if (name == "length") {
            double n = value.toNumber();
            size_t newLen = n > 0 ? static_cast<size_t>(n) : 0;
            if (newLen > kMaxArrayLength) {
                LOG_WARN("AVM1", "Array length assignment %zu exceeds cap (%zu) — ignored",
                          newLen, kMaxArrayLength);
                return;
            }
            elements.resize(newLen);
            return;
        }
        if (isAllDigits(name)) {
            unsigned long idx = std::strtoul(name.c_str(), nullptr, 10);
            if (idx >= kMaxArrayLength) {
                LOG_WARN("AVM1", "Array index %lu exceeds cap (%zu) — ignored", idx,
                          kMaxArrayLength);
                return;
            }
            if (idx >= elements.size()) {
                elements.resize(idx + 1);
            }
            elements[idx] = std::move(value);
            return;
        }
    }
    properties_[name] = std::move(value);
}

std::shared_ptr<Object> makeNativeFunction(std::string name, Object::FunctionDef::NativeFn fn) {
    auto obj = std::make_shared<Object>(Object::Kind::kFunction);
    obj->function = std::make_unique<Object::FunctionDef>();
    obj->function->name = std::move(name);
    obj->function->nativeImpl = std::move(fn);
    return obj;
}

}  // namespace flash3ds::avm1
