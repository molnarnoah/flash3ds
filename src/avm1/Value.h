// Value.h
//
// The AVM1 dynamic value type: Undefined, Null, Boolean, Number (double),
// String, or a reference to an Object (plain object, Array, or Function).
// Modeled after ECMA-262-3 / AS2's dynamic typing, which is what real SWF6-8
// content's DoAction bytecode assumes.
//
// Clean-room implementation against the public SWF/AVM1 bytecode
// documentation — not derived from gameswf or any Shift-DX code.
//
// KNOWN SIMPLIFICATIONS (documented, not oversights — see
// docs/avm1-support.md for the full list):
//   - No user-overridable valueOf()/toString() dispatch: coercions of
//     Object values to Number/String use fixed built-in rules rather than
//     calling a script-defined method. Most real DoAction content doesn't
//     rely on this for simple game logic.
//   - No true multi-byte-string awareness: MBString* actions behave the
//     same as their non-MB counterparts (byte-oriented, not codepoint-
//     aware).

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace flash3ds::avm1 {

class Object;

enum class ValueType {
    kUndefined,
    kNull,
    kBoolean,
    kNumber,
    kString,
    kObject,
};

class Value {
public:
    Value() = default;  // undefined

    static Value undefined() { return Value(); }
    static Value null() {
        Value v;
        v.type_ = ValueType::kNull;
        return v;
    }
    static Value boolean(bool b) {
        Value v;
        v.type_ = ValueType::kBoolean;
        v.bool_ = b;
        return v;
    }
    static Value number(double n) {
        Value v;
        v.type_ = ValueType::kNumber;
        v.number_ = n;
        return v;
    }
    static Value string(std::string s) {
        Value v;
        v.type_ = ValueType::kString;
        v.string_ = std::move(s);
        return v;
    }
    static Value object(std::shared_ptr<Object> obj) {
        Value v;
        if (obj) {
            v.type_ = ValueType::kObject;
            v.object_ = std::move(obj);
        }
        return v;
    }

    ValueType type() const { return type_; }
    bool isUndefined() const { return type_ == ValueType::kUndefined; }
    bool isNull() const { return type_ == ValueType::kNull; }
    bool isBoolean() const { return type_ == ValueType::kBoolean; }
    bool isNumber() const { return type_ == ValueType::kNumber; }
    bool isString() const { return type_ == ValueType::kString; }
    bool isObject() const { return type_ == ValueType::kObject; }

    // Raw accessors — only meaningful when the corresponding is*() is true;
    // otherwise return a harmless default (0 / "" / nullptr), never UB.
    bool asBoolRaw() const { return bool_; }
    double asNumberRaw() const { return number_; }
    const std::string& asStringRaw() const { return string_; }
    const std::shared_ptr<Object>& asObject() const { return object_; }

    // --- ECMA-262-3-style coercions (AS2's actual runtime rules) ---------

    // ToBoolean: undefined/null -> false; boolean -> itself; number ->
    // nonzero-and-not-NaN; string -> nonempty; object -> true.
    bool toBoolean() const;

    // ToNumber: undefined -> NaN; null -> 0; boolean -> 1/0; number ->
    // itself; string -> parsed as a number (empty string -> 0, unparsable
    // -> NaN); object -> NaN (no valueOf() dispatch — see file header).
    double toNumber() const;

    // ToInt32-ish helper built on toNumber(), matching AVM1's bitwise-op
    // operand coercion (NaN/Infinity -> 0, truncates toward zero, wraps to
    // 32-bit signed).
    int32_t toInt32() const;

    // ToString: undefined -> "undefined"; null -> "null"; boolean ->
    // "true"/"false"; number -> shortest round-tripping decimal form
    // (integers print without a decimal point); string -> itself; object ->
    // Array -> comma-joined elements, Function -> "[type Function]", plain
    // Object -> "[object Object]" (no toString() dispatch — see file
    // header).
    std::string toString() const;

private:
    ValueType type_ = ValueType::kUndefined;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::shared_ptr<Object> object_;
};

// Formats a double the way AS2's Number.toString()/implicit string
// coercion does: "NaN"/"Infinity"/"-Infinity" for non-finite values,
// integral doubles without a trailing ".0", otherwise a compact decimal
// form. Exposed separately from Value::toString() so Object's Array
// element formatting can reuse it.
std::string numberToAs2String(double n);

// An AVM1 object: a dynamic property bag, optionally an Array (indexed
// elements + a live "length") or a Function (bytecode + captured closure
// scope — see ExecutionContext.h for Scope). Objects are always accessed
// through shared_ptr since AS2 has reference semantics for objects/arrays/
// functions (assignment and parameter passing copy the reference, not the
// contents).
class Object : public std::enable_shared_from_this<Object> {
public:
    enum class Kind { kPlain, kArray, kFunction };

    explicit Object(Kind kind = Kind::kPlain) : kind_(kind) {}

    Kind kind() const { return kind_; }
    bool isArray() const { return kind_ == Kind::kArray; }
    bool isFunction() const { return kind_ == Kind::kFunction; }

    // --- generic named properties (own properties only; see getMember()
    // for the prototype-chain-aware lookup most bytecode should use) -----
    bool hasOwnProperty(const std::string& name) const;
    Value getOwnProperty(const std::string& name) const;  // undefined if absent
    void setOwnProperty(const std::string& name, Value value);
    void deleteOwnProperty(const std::string& name);

    // Prototype-chain-aware get: checks own properties, then Array
    // length/index special-casing, then walks `prototype` (bounded depth,
    // defensive against a cyclic prototype chain). Returns undefined if
    // never found.
    Value getMember(const std::string& name) const;
    // Prototype-chain-UNAWARE set: AS2 property assignment always creates
    // an OWN property on the target object (it never writes through to a
    // prototype), matching real AS2 semantics.
    void setMember(const std::string& name, Value value);

    // Read-only iteration over this object's OWN properties (not the
    // prototype chain, not Array elements) — used by ActionEnumerate/
    // Enumerate2's for-in support and potential future debugging tools.
    const std::unordered_map<std::string, Value>& ownProperties() const { return properties_; }

    // Names of this object's NATIVE (host-intercepted) properties that
    // should also show up in for-in enumeration — see `nativeEnumerate`
    // below. Empty if `nativeEnumerate` is unset.
    std::vector<std::string> enumerableNativeNames() const {
        return nativeEnumerate ? nativeEnumerate() : std::vector<std::string>{};
    }

    // --- native property interception (Phase 5) --------------------------
    // Optional hooks a host embedder (e.g. runtime::MovieClipInstance, which
    // wraps a Timeline/DisplayList) can install to expose intrinsic
    // properties (_x, _y, _currentframe, named child clips, ...) through
    // the *normal* property-access path — hasOwnProperty/getOwnProperty/
    // setOwnProperty (and therefore Scope's plain-variable lookup) as well
    // as getMember/setMember (explicit `.member` access) — without avm1/
    // needing to know anything about MovieClip/Timeline. This keeps avm1/
    // host-agnostic (same seam philosophy as HostBindings.h) while still
    // letting `this._x = 10;` and a bare `_x = 10;` inside a clip's script
    // both resolve correctly.
    //
    // nativeGet(name, out): return true (and set `out`) if `name` is a
    // recognized native property; return false to fall through to normal
    // own-property storage. Checked BEFORE the own-property map on every
    // read path, so a native property always shadows a same-named plain
    // property (matches real AS2: you cannot shadow _x by assignment).
    // nativeSet(name, value): return true if `name` was handled (including
    // "recognized but read-only, write silently ignored"); return false to
    // fall through to normal own-property storage.
    // nativeEnumerate(): extra property names to report alongside
    // `ownProperties()` (e.g. named child clip instances).
    std::function<bool(const std::string&, Value&)> nativeGet;
    std::function<bool(const std::string&, const Value&)> nativeSet;
    std::function<std::vector<std::string>()> nativeEnumerate;

    std::shared_ptr<Object> prototype;

    // --- Array support (Kind::kArray) ------------------------------------
    std::vector<Value> elements;  // index i <-> AS2 array index i

    // --- Function support (Kind::kFunction) ------------------------------
    struct RegisterParam {
        uint8_t reg = 0;    // 0 means "bind as a named local var instead"
        std::string name;
    };
    struct FunctionDef {
        std::string name;  // may be empty (anonymous function expression)
        std::vector<std::string> paramNames;        // DefineFunction (v1)
        std::vector<RegisterParam> registerParams;   // DefineFunction2
        uint8_t registerCount = 4;
        bool preloadThis = false;
        bool suppressThis = false;
        bool preloadArguments = false;
        bool suppressArguments = false;
        bool preloadSuper = false;
        bool suppressSuper = false;
        bool preloadRoot = false;
        bool preloadParent = false;
        bool preloadGlobal = false;
        std::vector<uint8_t> body;  // AVM1 bytecode, captured verbatim
        // Opaque closure pointer — actually a std::shared_ptr<Scope>, but
        // Scope isn't defined yet at this point in the header graph
        // (ExecutionContext.h depends on Value.h, not the other way
        // around). Stored type-erased and cast back in Interpreter.cpp.
        std::shared_ptr<void> capturedScope;
    };
    std::unique_ptr<FunctionDef> function;  // set iff kind_ == kFunction

private:
    Kind kind_;
    std::unordered_map<std::string, Value> properties_;

    // Guards against a malformed/cyclic prototype chain during getMember().
    static constexpr int kMaxPrototypeChainDepth = 64;
};

}  // namespace flash3ds::avm1
