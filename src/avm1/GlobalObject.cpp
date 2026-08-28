#include "avm1/GlobalObject.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

#include "avm1/ExecutionContext.h"

namespace flash3ds::avm1 {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kE = 2.71828182845904523536;

// Mirrors Interpreter.cpp's defaultRandom() (ActionRandomNumber's own
// fallback) — same shape, separate translation unit, so GlobalObject
// doesn't need to export Interpreter.cpp's anonymous-namespace helper.
// Math.random() prefers ctx.randomSource when a caller (a test, or a
// future deterministic-replay host) has set one, exactly like
// ActionRandomNumber already does — same seam, same testability.
uint32_t defaultRandomU32() {
    static thread_local std::mt19937 rng(std::random_device{}());
    return rng();
}

// One-argument math functions (floor/ceil/round/abs/sqrt) share this
// exact shape: read args[0] as a number (0 if missing/non-numeric,
// matching Value::toNumber()'s own NaN-to-0 coercion elsewhere in this
// codebase), apply `fn`, return a Value. A small helper beats seven
// near-identical lambdas.
std::shared_ptr<Object> makeUnaryMathFn(const char* name, double (*fn)(double)) {
    return makeNativeFunction(name, [fn](ExecutionContext&, const Value&,
                                          const std::vector<Value>& args) {
        double x = args.empty() ? 0.0 : args[0].toNumber();
        return Value::number(fn(x));
    });
}

}  // namespace

std::shared_ptr<Object> GlobalObject::create() {
    auto global = std::make_shared<Object>();

    // --- Math (Roadmap Phase 8, 2026-08-25) ---------------------------------
    // See GlobalObject.h's Phase 8 doc comment for exactly which of these
    // are corpus-evidenced (ceil/random) vs. included "for free" alongside
    // them. A plain object, used AS2-statically (Math.floor(x), never `new
    // Math()`) — same convention as Phase 6's Key/Mouse.
    auto mathObj = std::make_shared<Object>();
    mathObj->setOwnProperty("floor", Value::object(makeUnaryMathFn("floor", std::floor)));
    mathObj->setOwnProperty("ceil", Value::object(makeUnaryMathFn("ceil", std::ceil)));
    mathObj->setOwnProperty("round", Value::object(makeNativeFunction(
                                          "round", [](ExecutionContext&, const Value&,
                                                       const std::vector<Value>& args) {
                                              // AS2's Math.round(): floor(x + 0.5) — NOT
                                              // std::round(), which rounds half-away-from-
                                              // zero (differs from AS2 for negative halves,
                                              // e.g. Math.round(-1.5) is -1 in real Flash,
                                              // not -2).
                                              double x = args.empty() ? 0.0 : args[0].toNumber();
                                              return Value::number(std::floor(x + 0.5));
                                          })));
    mathObj->setOwnProperty("abs", Value::object(makeUnaryMathFn("abs", std::fabs)));
    mathObj->setOwnProperty("sqrt", Value::object(makeUnaryMathFn("sqrt", std::sqrt)));
    mathObj->setOwnProperty(
        "pow", Value::object(makeNativeFunction(
                   "pow", [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                       double base = args.size() > 0 ? args[0].toNumber() : 0.0;
                       double exp = args.size() > 1 ? args[1].toNumber() : 0.0;
                       return Value::number(std::pow(base, exp));
                   })));
    mathObj->setOwnProperty(
        "min", Value::object(makeNativeFunction(
                   "min", [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                       if (args.empty()) return Value::number(-std::numeric_limits<double>::infinity());
                       double result = args[0].toNumber();
                       for (size_t i = 1; i < args.size(); ++i) result = std::min(result, args[i].toNumber());
                       return Value::number(result);
                   })));
    mathObj->setOwnProperty(
        "max", Value::object(makeNativeFunction(
                   "max", [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                       if (args.empty()) return Value::number(-std::numeric_limits<double>::infinity());
                       double result = args[0].toNumber();
                       for (size_t i = 1; i < args.size(); ++i) result = std::max(result, args[i].toNumber());
                       return Value::number(result);
                   })));
    mathObj->setOwnProperty(
        "random", Value::object(makeNativeFunction(
                      "random", [](ExecutionContext& ctx, const Value&, const std::vector<Value>&) {
                          uint32_t raw = ctx.randomSource ? ctx.randomSource() : defaultRandomU32();
                          // Uniform in [0, 1) — dividing a full-range uint32_t by 2^32
                          // (not 2^32-1) keeps the result strictly < 1.0, matching real
                          // Flash's Math.random() range.
                          return Value::number(static_cast<double>(raw) / 4294967296.0);
                      })));
    mathObj->setOwnProperty("PI", Value::number(kPi));
    mathObj->setOwnProperty("E", Value::number(kE));
    global->setOwnProperty("Math", Value::object(mathObj));

    // --- String (task #67, 2026-08-27) --------------------------------------
    // See GlobalObject.h's doc comment for the full evidence trail. Only
    // `fromCharCode` lives here: it's called as the STATIC
    // `String.fromCharCode(...)`, so it's an ordinary native function
    // property on this constructor object, resolved through the normal
    // CallMethod path exactly like Math's methods above — no interpreter
    // change needed. The corresponding INSTANCE methods
    // (charAt/charCodeAt/substr, for a call like `someString.charAt(0)`)
    // are handled separately in Interpreter.cpp's CallMethod
    // (tryStringPrimitiveMethod()), since a bare string primitive has no
    // Object/prototype to attach a method to here.
    auto stringCtor = std::make_shared<Object>();
    stringCtor->setOwnProperty(
        "fromCharCode",
        Value::object(makeNativeFunction(
            "fromCharCode", [](ExecutionContext&, const Value&, const std::vector<Value>& args) {
                std::string result;
                result.reserve(args.size());
                for (const auto& a : args) {
                    // Byte-oriented (truncates to a single byte), matching
                    // this codebase's documented non-Unicode simplification
                    // — the exact inverse of charCodeAt()'s
                    // static_cast<unsigned char> in Interpreter.cpp.
                    result.push_back(static_cast<char>(static_cast<int64_t>(a.toNumber())));
                }
                return Value::string(result);
            })));
    global->setOwnProperty("String", Value::object(stringCtor));

    return global;
}

}  // namespace flash3ds::avm1
