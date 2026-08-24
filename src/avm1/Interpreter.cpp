#include "avm1/Interpreter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>

#include "avm1/ActionCode.h"
#include "platform/Log.h"
#include "swf/SwfReader.h"

namespace flash3ds::avm1 {

namespace {

// --- ExecutionContext::callTraceSink formatting helper (Roadmap Phase 4,
// 2026-08-21 — see that field's own doc comment) --------------------------

std::string formatTraceArgs(const std::vector<Value>& args) {
    std::string s;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) s += ", ";
        s += args[i].isString() ? ("\"" + args[i].toString() + "\"") : args[i].toString();
    }
    return s;
}

// --- small numeric helpers ------------------------------------------------

double readFloat32(swf::SwfReader& r) {
    uint32_t bits = r.readU32();
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return static_cast<double>(f);
}

// ActionPush's DOUBLE (type 6) operand stores a standard IEEE-754 double
// with its two 32-bit words swapped relative to normal little-endian
// layout — a well-documented SWF-specific quirk. Convention used here:
// the first 4 bytes are the double's high-order word (sign + exponent +
// top mantissa bits), the next 4 are the low-order word. Believed correct
// per common third-party SWF parser implementations; NOT independently
// verified against a real Flash-authored double literal (see
// docs/avm1-support.md) — integer/Float32 literals cover the overwhelming
// majority of real DoAction content, so this is a low-priority
// correctness risk, not a load-bearing assumption for common content.
double readAvm1Double(swf::SwfReader& r) {
    uint32_t hi = r.readU32();
    uint32_t lo = r.readU32();
    uint64_t bits = (static_cast<uint64_t>(hi) << 32) | static_cast<uint64_t>(lo);
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

uint32_t defaultRandom() {
    static thread_local std::mt19937 rng(std::random_device{}());
    return rng();
}

double defaultClockMs() {
    using namespace std::chrono;
    return static_cast<double>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// Defensive cap for InitObject/InitArray/ImplementsOp counts read off the
// stack — a malformed count shouldn't drive a pathological loop.
size_t clampCount(double raw) {
    if (!(raw > 0)) return 0;
    constexpr double kMaxCount = 100000.0;
    return static_cast<size_t>(std::min(raw, kMaxCount));
}

// Pops `numArgs` call arguments off the stack and returns them in original
// call order. Compiled AS2 pushes args in call order (arg0 first), then
// the count, then (for CallFunction) the name — so by the time we're
// popping args here, they come off in REVERSE call order; this fills the
// result vector back-to-front to restore it. Capped defensively (a
// malformed numArgs shouldn't drive an unbounded pop loop — each pop() is
// itself safe on an empty stack, but bounding it keeps behavior
// predictable).
std::vector<Value> popArgs(Stack& stack, double numArgsRaw) {
    size_t numArgs = clampCount(numArgsRaw) > 10000 ? 10000 : clampCount(numArgsRaw);
    std::vector<Value> args(numArgs);
    for (size_t i = 0; i < numArgs; ++i) {
        args[numArgs - 1 - i] = stack.pop();
    }
    return args;
}

// --- value operations (bytecode-level semantics, not core Value methods) -

Value add2(const Value& a, const Value& b) {
    if (a.isString() || b.isString()) {
        return Value::string(a.toString() + b.toString());
    }
    return Value::number(a.toNumber() + b.toNumber());
}

// Abstract relational "less than", matching ActionLess2/Greater's rules:
// string/string comparison is lexicographic; anything else coerces both
// sides to Number. Any NaN comparison is false (ECMA's "undefined ->
// false" collapse for abstract relational comparison).
bool lessThan(const Value& a, const Value& b) {
    if (a.isString() && b.isString()) {
        return a.asStringRaw() < b.asStringRaw();
    }
    double an = a.toNumber();
    double bn = b.toNumber();
    if (std::isnan(an) || std::isnan(bn)) return false;
    return an < bn;
}

bool strictEqualsFn(const Value& a, const Value& b) {
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case ValueType::kUndefined:
        case ValueType::kNull:
            return true;
        case ValueType::kBoolean:
            return a.asBoolRaw() == b.asBoolRaw();
        case ValueType::kNumber:
            return a.asNumberRaw() == b.asNumberRaw();  // NaN != NaN via IEEE-754 ==
        case ValueType::kString:
            return a.asStringRaw() == b.asStringRaw();
        case ValueType::kObject:
            return a.asObject().get() == b.asObject().get();
    }
    return false;
}

// Simplified ECMA "abstract equality comparison" (loose ==). See Value.h's
// file header for the documented simplification: no ToPrimitive dispatch
// on objects (a plain object is never loosely equal to a primitive here).
bool looseEquals(const Value& a, const Value& b) {
    if (a.type() == b.type()) {
        return strictEqualsFn(a, b);
    }
    if ((a.isUndefined() && b.isNull()) || (a.isNull() && b.isUndefined())) return true;
    if (a.isNull() || a.isUndefined() || b.isNull() || b.isUndefined()) return false;
    if (a.isObject() || b.isObject()) return false;
    return a.toNumber() == b.toNumber();
}

// Forward-declared: recursively executes a nested bytecode block (a With
// block, or — in a later phase — a Try block) sharing the SAME
// ExecutionContext (stack/registers/scope all persist across it, unlike a
// real function call). Applies the same call-depth guard as function
// calls, propagating/restoring `ctx.callDepthCounter` correctly so nested
// With/Try blocks inside a deeply recursive function call still count
// against the shared budget, and so a top-level (non-function-call)
// nested block gets its own bounded budget without leaving a dangling
// pointer in `ctx` afterward.
Value runNested(ExecutionContext& ctx, const uint8_t* code, size_t length) {
    int localDepth = 0;
    bool ownsCounter = (ctx.callDepthCounter == nullptr);
    if (ownsCounter) ctx.callDepthCounter = &localDepth;

    Value result;
    if (*ctx.callDepthCounter >= Interpreter::kMaxCallDepth) {
        LOG_WARN("AVM1", "nested execution depth limit (%d) exceeded — skipping block",
                  Interpreter::kMaxCallDepth);
    } else {
        ++*ctx.callDepthCounter;
        result = Interpreter::execute(ctx, code, length);
        --*ctx.callDepthCounter;
    }

    if (ownsCounter) ctx.callDepthCounter = nullptr;  // avoid a dangling pointer to localDepth
    return result;
}

// Invokes an AVM1 Function object: builds a fresh activation record, binds
// parameters (named for DefineFunction, register-or-named for
// DefineFunction2), applies preload flags, and recursively runs the
// function body via Interpreter::execute with a freshly-scoped
// ExecutionContext (its own stack/registers, but sharing the interpreter-
// wide global object / host / trace / random / clock sources and — most
// importantly — the SAME call-depth counter as the caller, so a chain of
// nested calls is bounded as a whole).
Value invokeFunction(ExecutionContext& callerCtx, const std::shared_ptr<Object>& funcObj,
                      Value thisVal, std::vector<Value> args) {
    if (!funcObj || !funcObj->isFunction() || !funcObj->function) {
        return Value::undefined();
    }

    int localDepth = 0;
    int* depthCounter = callerCtx.callDepthCounter ? callerCtx.callDepthCounter : &localDepth;
    if (*depthCounter >= Interpreter::kMaxCallDepth) {
        LOG_WARN("AVM1", "call depth limit (%d) exceeded — returning undefined without executing",
                  Interpreter::kMaxCallDepth);
        return Value::undefined();
    }
    ++*depthCounter;

    const Object::FunctionDef& def = *funcObj->function;

    // Phase 6: native (C++-backed) functions short-circuit here — no
    // scope/activation/register setup, just a direct call. This is the
    // ONLY change needed anywhere in the interpreter to support native AS2
    // built-ins (Key/Mouse/Sound, ...); NewObject/CallFunction/CallMethod
    // above are untouched because they already resolve to a generic
    // Function object via Scope::getVariable/Object::getMember and always
    // route through invokeFunction() to actually call it.
    if (def.nativeImpl) {
        Value result = def.nativeImpl(callerCtx, thisVal, args);
        --*depthCounter;
        return result;
    }

    auto closureScopePtr = std::static_pointer_cast<Scope>(def.capturedScope);
    Scope baseScope = closureScopePtr ? *closureScopePtr : callerCtx.scope;

    auto activation = std::make_shared<Object>();

    if (!def.registerParams.empty()) {
        for (size_t i = 0; i < def.registerParams.size(); ++i) {
            const auto& rp = def.registerParams[i];
            if (rp.reg == 0 && !rp.name.empty()) {
                Value argVal = i < args.size() ? args[i] : Value::undefined();
                activation->setOwnProperty(rp.name, argVal);
            }
        }
    } else {
        for (size_t i = 0; i < def.paramNames.size(); ++i) {
            Value argVal = i < args.size() ? args[i] : Value::undefined();
            activation->setOwnProperty(def.paramNames[i], argVal);
        }
    }

    Scope newScope = baseScope.pushed(activation);
    ExecutionContext newCtx(newScope, callerCtx.globalObject);
    newCtx.host = callerCtx.host;
    newCtx.traceSink = callerCtx.traceSink;
    newCtx.randomSource = callerCtx.randomSource;
    newCtx.clockSource = callerCtx.clockSource;
    newCtx.callTraceSink = callerCtx.callTraceSink;
    newCtx.callDepthCounter = depthCounter;
    newCtx.thisValue = thisVal;

    size_t registerCount = def.registerCount == 0 ? 4 : def.registerCount;
    newCtx.registers.assign(std::max<size_t>(registerCount, 1), Value::undefined());

    if (!def.registerParams.empty()) {
        for (size_t i = 0; i < def.registerParams.size(); ++i) {
            const auto& rp = def.registerParams[i];
            if (rp.reg != 0) {
                Value argVal = i < args.size() ? args[i] : Value::undefined();
                newCtx.setRegister(rp.reg, argVal);
            }
        }
    }

    // Preload flags (DefineFunction2 only — always false/no-op for
    // DefineFunction v1). Conventionally occupy fixed register slots 1-6
    // in this order when requested; see docs/avm1-support.md for the
    // confidence note on this convention. `super`/`_root`/`_parent` have
    // no real object to preload yet (no class model / no scene graph
    // wiring in Phase 4) — their slots are reserved (left undefined)
    // rather than skipped, so later preloads still land in the
    // spec-documented slot numbers.
    uint8_t nextPreloadRegister = 1;
    if (def.preloadThis) newCtx.setRegister(nextPreloadRegister, thisVal);
    if (def.preloadThis || def.suppressThis) ++nextPreloadRegister;

    std::shared_ptr<Object> argumentsObj;
    if (!def.suppressArguments) {
        argumentsObj = std::make_shared<Object>(Object::Kind::kArray);
        argumentsObj->elements = args;
        if (def.preloadArguments) {
            newCtx.setRegister(nextPreloadRegister, Value::object(argumentsObj));
        } else {
            activation->setOwnProperty("arguments", Value::object(argumentsObj));
        }
    }
    if (def.preloadArguments || def.suppressArguments) ++nextPreloadRegister;

    if (def.preloadSuper || def.suppressSuper) ++nextPreloadRegister;
    if (def.preloadRoot) ++nextPreloadRegister;
    if (def.preloadParent) ++nextPreloadRegister;
    if (def.preloadGlobal) {
        newCtx.setRegister(nextPreloadRegister, Value::object(callerCtx.globalObject));
    }

    if (!def.suppressThis) {
        activation->setOwnProperty("this", thisVal);
    }

    Value result = Interpreter::execute(newCtx, def.body.data(), def.body.size());

    --*depthCounter;
    return result;
}

}  // namespace

// Phase 7: public wrapper around the anonymous-namespace invokeFunction()
// helper above, so native/host code (e.g. ScriptEnvironment's
// ExternalInterface.addCallback dispatch) can invoke an AS2 Function value
// directly. See Interpreter.h's doc comment for behavior notes.
Value Interpreter::callFunction(ExecutionContext& callerCtx, const std::shared_ptr<Object>& funcObj,
                                 Value thisVal, std::vector<Value> args) {
    return invokeFunction(callerCtx, funcObj, thisVal, std::move(args));
}

Value Interpreter::execute(ExecutionContext& ctx, const uint8_t* code, size_t length) {
    if (!ctx.traceSink) {
        ctx.traceSink = [](const std::string& msg) { LOG_INFO("AVM1", "%s", msg.c_str()); };
    }

    swf::SwfReader reader(code, length);
    int instructionCount = 0;
    bool returned = false;
    Value returnValue;

    while (!reader.atEnd() && !reader.failed()) {
        if (++instructionCount > kMaxInstructionsPerRun) {
            LOG_WARN("AVM1", "instruction limit (%d) exceeded — aborting this run",
                      kMaxInstructionsPerRun);
            break;
        }

        uint8_t code8 = reader.readU8();
        if (reader.failed()) break;
        if (code8 == 0x00) break;  // ActionEnd

        std::vector<uint8_t> data;
        if (code8 & 0x80) {
            uint16_t len = reader.readU16();
            data = reader.readBytes(len);
        }

        ActionCode action = static_cast<ActionCode>(code8);

        switch (action) {
            // --- timeline control (stubbed — see HostBindings.h) --------
            case ActionCode::NextFrame:
                if (ctx.host) ctx.host->nextFrame();
                else LOG_DEBUG("AVM1", "NextFrame — no HostBindings wired");
                break;
            case ActionCode::PreviousFrame:
                if (ctx.host) ctx.host->previousFrame();
                else LOG_DEBUG("AVM1", "PreviousFrame — no HostBindings wired");
                break;
            case ActionCode::Play:
                if (ctx.host) ctx.host->play();
                else LOG_DEBUG("AVM1", "Play — no HostBindings wired");
                break;
            case ActionCode::Stop:
                if (ctx.host) ctx.host->stop();
                else LOG_DEBUG("AVM1", "Stop — no HostBindings wired");
                break;
            case ActionCode::ToggleQuality:
                LOG_DEBUG("AVM1", "ToggleQuality — rendering quality control not modeled");
                break;
            case ActionCode::StopSounds:
                LOG_DEBUG("AVM1", "StopSounds — audio not wired (Phase 6)");
                break;
            case ActionCode::GotoFrame: {
                swf::SwfReader r2(data.data(), data.size());
                uint16_t frame = r2.readU16();
                if (ctx.host) ctx.host->gotoFrame(frame);
                else LOG_DEBUG("AVM1", "GotoFrame(%u) — no HostBindings wired", frame);
                break;
            }
            case ActionCode::GotoLabel: {
                swf::SwfReader r2(data.data(), data.size());
                std::string label = r2.readCString();
                if (ctx.host) ctx.host->gotoLabel(label);
                else
                    LOG_DEBUG("AVM1", "GotoLabel('%s') — no HostBindings wired",
                              label.c_str());
                break;
            }
            case ActionCode::GotoFrame2: {
                swf::SwfReader r2(data.data(), data.size());
                uint8_t flags = r2.readU8();
                bool hasSceneBias = (flags & 0x01) != 0;
                bool playAfter = (flags & 0x02) != 0;
                if (hasSceneBias) r2.readU16();  // SceneBias — multi-scene movies not supported
                Value frameVal = ctx.stack.pop();
                if (ctx.host) {
                    if (frameVal.isString()) ctx.host->gotoLabel(frameVal.toString());
                    else ctx.host->gotoFrame(static_cast<uint32_t>(frameVal.toNumber()));
                    if (playAfter) ctx.host->play();
                    else ctx.host->stop();
                } else {
                    LOG_DEBUG("AVM1", "GotoFrame2 — no HostBindings wired");
                }
                break;
            }
            case ActionCode::WaitForFrame: {
                // Simplified (see docs/avm1-support.md): frames are always
                // considered "loaded" without a real Timeline, so the skip
                // branch is never taken; SkipCount's target actions always
                // run normally.
                swf::SwfReader r2(data.data(), data.size());
                r2.readU16();  // Frame
                r2.readU8();   // SkipCount
                break;
            }
            case ActionCode::WaitForFrame2: {
                swf::SwfReader r2(data.data(), data.size());
                r2.readU8();      // SkipCount
                ctx.stack.pop();  // Frame (this variant pops it from the stack)
                break;
            }
            case ActionCode::SetTarget: {
                swf::SwfReader r2(data.data(), data.size());
                std::string target = r2.readCString();
                if (ctx.host) ctx.host->setTarget(target);
                else
                    LOG_DEBUG("AVM1", "SetTarget('%s') — no HostBindings wired",
                              target.c_str());
                break;
            }
            case ActionCode::SetTarget2: {
                Value target = ctx.stack.pop();
                if (ctx.host) ctx.host->setTarget(target.toString());
                else LOG_DEBUG("AVM1", "SetTarget2 — no HostBindings wired");
                break;
            }
            case ActionCode::GetProperty: {
                Value indexVal = ctx.stack.pop();
                Value targetVal = ctx.stack.pop();
                if (ctx.host) {
                    ctx.stack.push(ctx.host->getProperty(targetVal.toString(),
                                                            static_cast<int>(indexVal.toNumber())));
                } else {
                    LOG_DEBUG("AVM1", "GetProperty — no HostBindings wired");
                    ctx.stack.push(Value::undefined());
                }
                break;
            }
            case ActionCode::SetProperty: {
                Value valueVal = ctx.stack.pop();
                Value indexVal = ctx.stack.pop();
                Value targetVal = ctx.stack.pop();
                if (ctx.host) {
                    ctx.host->setProperty(targetVal.toString(), static_cast<int>(indexVal.toNumber()),
                                            valueVal);
                } else {
                    LOG_DEBUG("AVM1", "SetProperty — no HostBindings wired");
                }
                break;
            }
            case ActionCode::CloneSprite: {
                Value depthVal = ctx.stack.pop();
                Value newNameVal = ctx.stack.pop();
                Value targetVal = ctx.stack.pop();
                if (ctx.host) {
                    ctx.host->cloneSprite(targetVal.toString(), newNameVal.toString(),
                                            static_cast<int>(depthVal.toNumber()));
                } else {
                    LOG_DEBUG("AVM1", "CloneSprite — no HostBindings wired");
                }
                break;
            }
            case ActionCode::RemoveSprite: {
                Value targetVal = ctx.stack.pop();
                if (ctx.host) ctx.host->removeSprite(targetVal.toString());
                else LOG_DEBUG("AVM1", "RemoveSprite — no HostBindings wired");
                break;
            }
            case ActionCode::StartDrag: {
                Value targetVal = ctx.stack.pop();
                HostBindings::DragOptions options;
                options.lockCenter = ctx.stack.pop().toBoolean();  // LockCenter
                options.hasConstraint = ctx.stack.pop().toBoolean();  // Constrain
                if (options.hasConstraint) {
                    // Pop order preserved from the pre-Phase-6 code (which
                    // discarded these into the void): L, T, R, B. NOT
                    // independently verified against real Flash-compiled
                    // output — see docs/avm1-support.md. A wrong L/T/R/B
                    // order only affects StartDrag's constraint rectangle,
                    // never crashes or corrupts other state.
                    options.left = ctx.stack.pop().toNumber();
                    options.top = ctx.stack.pop().toNumber();
                    options.right = ctx.stack.pop().toNumber();
                    options.bottom = ctx.stack.pop().toNumber();
                }
                if (ctx.host) ctx.host->startDrag(targetVal.toString(), options);
                else LOG_DEBUG("AVM1", "StartDrag — no HostBindings wired");
                break;
            }
            case ActionCode::EndDrag:
                if (ctx.host) ctx.host->endDrag();
                else LOG_DEBUG("AVM1", "EndDrag — no HostBindings wired");
                break;
            case ActionCode::Call: {
                ctx.stack.pop();  // Target frame — subroutine-style calls not wired (Phase 5)
                LOG_DEBUG("AVM1", "Call — no HostBindings wired");
                break;
            }
            case ActionCode::GetURL: {
                swf::SwfReader r2(data.data(), data.size());
                std::string url = r2.readCString();
                std::string target = r2.readCString();
                if (ctx.callTraceSink) {
                    ctx.callTraceSink("GetURL url=\"" + url + "\" target=\"" + target + "\"");
                }
                LOG_DEBUG("AVM1", "GetURL('%s','%s') — browser navigation out of scope for a 3DS runtime",
                          url.c_str(), target.c_str());
                break;
            }
            case ActionCode::GetURL2: {
                Value target = ctx.stack.pop();
                Value url = ctx.stack.pop();
                if (ctx.callTraceSink) {
                    ctx.callTraceSink("GetURL2 url=" + url.toString() + " target=" + target.toString());
                }
                LOG_DEBUG("AVM1", "GetURL2 — browser navigation out of scope for a 3DS runtime");
                break;
            }
            case ActionCode::TargetPath: {
                ctx.stack.pop();
                LOG_DEBUG("AVM1", "TargetPath — no scene graph wired; pushing \"\"");
                ctx.stack.push(Value::string(""));
                break;
            }

            // --- stack / values -----------------------------------------
            case ActionCode::Pop:
                ctx.stack.pop();
                break;
            case ActionCode::PushDuplicate:
                ctx.stack.duplicateTop();
                break;
            case ActionCode::StackSwap:
                ctx.stack.swapTop();
                break;
            case ActionCode::Push: {
                swf::SwfReader pushReader(data.data(), data.size());
                while (!pushReader.atEnd() && !pushReader.failed()) {
                    uint8_t type = pushReader.readU8();
                    if (pushReader.failed()) break;
                    switch (type) {
                        case 0:
                            ctx.stack.push(Value::string(pushReader.readCString()));
                            break;
                        case 1:
                            ctx.stack.push(Value::number(readFloat32(pushReader)));
                            break;
                        case 2:
                            ctx.stack.push(Value::null());
                            break;
                        case 3:
                            ctx.stack.push(Value::undefined());
                            break;
                        case 4: {
                            uint8_t reg = pushReader.readU8();
                            ctx.stack.push(ctx.getRegister(reg));
                            break;
                        }
                        case 5: {
                            uint8_t b = pushReader.readU8();
                            ctx.stack.push(Value::boolean(b != 0));
                            break;
                        }
                        case 6:
                            ctx.stack.push(Value::number(readAvm1Double(pushReader)));
                            break;
                        case 7: {
                            int32_t iv = pushReader.readS32();
                            ctx.stack.push(Value::number(static_cast<double>(iv)));
                            break;
                        }
                        case 8: {
                            uint8_t idx = pushReader.readU8();
                            if (idx < ctx.constantPool.size())
                                ctx.stack.push(Value::string(ctx.constantPool[idx]));
                            else
                                ctx.stack.push(Value::undefined());
                            break;
                        }
                        case 9: {
                            uint16_t idx = pushReader.readU16();
                            if (idx < ctx.constantPool.size())
                                ctx.stack.push(Value::string(ctx.constantPool[idx]));
                            else
                                ctx.stack.push(Value::undefined());
                            break;
                        }
                        default:
                            LOG_WARN("AVM1", "Push: unknown operand type 0x%02X — stopping this Push",
                                      type);
                            pushReader.seek(pushReader.size());
                            break;
                    }
                }
                break;
            }
            case ActionCode::StoreRegister: {
                swf::SwfReader r2(data.data(), data.size());
                uint8_t reg = r2.readU8();
                ctx.setRegister(reg, ctx.stack.peek());  // does NOT pop, per spec
                break;
            }
            case ActionCode::ConstantPool: {
                swf::SwfReader r2(data.data(), data.size());
                uint16_t count = r2.readU16();
                std::vector<std::string> pool;
                pool.reserve(count);
                for (uint16_t i = 0; i < count && !r2.failed(); ++i) {
                    pool.push_back(r2.readCString());
                }
                ctx.constantPool = std::move(pool);
                break;
            }

            // --- arithmetic (legacy, SWF4-era — numeric-only) -----------
            case ActionCode::Add: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::number(a.toNumber() + b.toNumber()));
                break;
            }
            case ActionCode::Subtract: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::number(a.toNumber() - b.toNumber()));
                break;
            }
            case ActionCode::Multiply: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::number(a.toNumber() * b.toNumber()));
                break;
            }
            case ActionCode::Divide: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::number(a.toNumber() / b.toNumber()));
                break;
            }
            case ActionCode::Modulo: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::number(std::fmod(a.toNumber(), b.toNumber())));
                break;
            }
            case ActionCode::Equals: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::boolean(a.toNumber() == b.toNumber()));
                break;
            }
            case ActionCode::Less: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                double an = a.toNumber(), bn = b.toNumber();
                ctx.stack.push(Value::boolean(!std::isnan(an) && !std::isnan(bn) && an < bn));
                break;
            }
            case ActionCode::And: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::boolean(a.toBoolean() && b.toBoolean()));
                break;
            }
            case ActionCode::Or: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::boolean(a.toBoolean() || b.toBoolean()));
                break;
            }
            case ActionCode::Not: {
                Value a = ctx.stack.pop();
                ctx.stack.push(Value::boolean(!a.toBoolean()));
                break;
            }

            // --- arithmetic / comparison (SWF5+, polymorphic) -----------
            case ActionCode::Add2: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(add2(a, b));
                break;
            }
            case ActionCode::Less2: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::boolean(lessThan(a, b)));
                break;
            }
            case ActionCode::Greater: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::boolean(lessThan(b, a)));
                break;
            }
            case ActionCode::Equals2: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::boolean(looseEquals(a, b)));
                break;
            }
            case ActionCode::StrictEquals: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::boolean(strictEqualsFn(a, b)));
                break;
            }
            case ActionCode::Increment: {
                Value a = ctx.stack.pop();
                ctx.stack.push(Value::number(a.toNumber() + 1));
                break;
            }
            case ActionCode::Decrement: {
                Value a = ctx.stack.pop();
                ctx.stack.push(Value::number(a.toNumber() - 1));
                break;
            }
            case ActionCode::ToInteger: {
                Value a = ctx.stack.pop();
                double n = a.toNumber();
                ctx.stack.push(Value::number(std::isnan(n) ? 0.0 : std::trunc(n)));
                break;
            }
            case ActionCode::ToNumber: {
                Value a = ctx.stack.pop();
                ctx.stack.push(Value::number(a.toNumber()));
                break;
            }
            case ActionCode::ToString: {
                Value a = ctx.stack.pop();
                ctx.stack.push(Value::string(a.toString()));
                break;
            }
            case ActionCode::TypeOf: {
                Value v = ctx.stack.pop();
                const char* t = "undefined";
                switch (v.type()) {
                    case ValueType::kUndefined: t = "undefined"; break;
                    case ValueType::kNull: t = "object"; break;  // ECMA/AS2 quirk
                    case ValueType::kBoolean: t = "boolean"; break;
                    case ValueType::kNumber: t = "number"; break;
                    case ValueType::kString: t = "string"; break;
                    case ValueType::kObject:
                        t = (v.asObject() && v.asObject()->isFunction()) ? "function" : "object";
                        break;
                }
                ctx.stack.push(Value::string(t));
                break;
            }

            // --- bitwise --------------------------------------------------
            case ActionCode::BitAnd: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::number(static_cast<double>(a.toInt32() & b.toInt32())));
                break;
            }
            case ActionCode::BitOr: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::number(static_cast<double>(a.toInt32() | b.toInt32())));
                break;
            }
            case ActionCode::BitXor: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::number(static_cast<double>(a.toInt32() ^ b.toInt32())));
                break;
            }
            case ActionCode::BitLShift: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                int32_t shift = b.toInt32() & 0x1F;
                ctx.stack.push(Value::number(static_cast<double>(a.toInt32() << shift)));
                break;
            }
            case ActionCode::BitRShift: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                int32_t shift = b.toInt32() & 0x1F;
                ctx.stack.push(Value::number(static_cast<double>(a.toInt32() >> shift)));
                break;
            }
            case ActionCode::BitURShift: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                uint32_t shift = static_cast<uint32_t>(b.toInt32()) & 0x1Fu;
                uint32_t av = static_cast<uint32_t>(a.toInt32());
                ctx.stack.push(Value::number(static_cast<double>(av >> shift)));
                break;
            }

            // --- strings ----------------------------------------------------
            case ActionCode::StringAdd: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::string(a.toString() + b.toString()));
                break;
            }
            case ActionCode::StringEquals: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::boolean(a.toString() == b.toString()));
                break;
            }
            case ActionCode::StringLess: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::boolean(a.toString() < b.toString()));
                break;
            }
            case ActionCode::StringGreater: {
                Value b = ctx.stack.pop(), a = ctx.stack.pop();
                ctx.stack.push(Value::boolean(a.toString() > b.toString()));
                break;
            }
            case ActionCode::StringLength:
            case ActionCode::MBStringLength: {
                Value v = ctx.stack.pop();
                ctx.stack.push(Value::number(static_cast<double>(v.toString().size())));
                break;
            }
            case ActionCode::StringExtract:
            case ActionCode::MBStringExtract: {
                double countD = ctx.stack.pop().toNumber();
                double indexD = ctx.stack.pop().toNumber();
                std::string s = ctx.stack.pop().toString();
                long start = std::isnan(indexD) ? 0 : static_cast<long>(indexD);
                long count = std::isnan(countD) ? 0 : static_cast<long>(countD);
                if (start < 0) start = 0;
                if (count < 0) count = 0;
                std::string result;
                if (static_cast<size_t>(start) < s.size() && count > 0) {
                    result = s.substr(static_cast<size_t>(start), static_cast<size_t>(count));
                }
                ctx.stack.push(Value::string(result));
                break;
            }
            case ActionCode::CharToAscii:
            case ActionCode::MBCharToAscii: {
                std::string s = ctx.stack.pop().toString();
                ctx.stack.push(
                    Value::number(s.empty() ? 0.0 : static_cast<double>(static_cast<unsigned char>(s[0]))));
                break;
            }
            case ActionCode::AsciiToChar:
            case ActionCode::MBAsciiToChar: {
                double code = ctx.stack.pop().toNumber();
                char c = static_cast<char>(static_cast<int>(code) & 0xFF);
                ctx.stack.push(Value::string(std::string(1, c)));
                break;
            }

            // --- variables / scope --------------------------------------
            case ActionCode::GetVariable: {
                Value name = ctx.stack.pop();
                ctx.stack.push(ctx.scope.getVariable(name.toString()));
                break;
            }
            case ActionCode::SetVariable: {
                Value value = ctx.stack.pop();
                Value name = ctx.stack.pop();
                ctx.scope.setVariable(name.toString(), value);
                break;
            }
            case ActionCode::DefineLocal: {
                Value value = ctx.stack.pop();
                Value name = ctx.stack.pop();
                ctx.scope.defineLocal(name.toString(), value);
                break;
            }
            case ActionCode::DefineLocal2: {
                Value name = ctx.stack.pop();
                const auto& innermost = ctx.scope.innermost();
                if (!innermost || !innermost->hasOwnProperty(name.toString())) {
                    ctx.scope.defineLocal(name.toString(), Value::undefined());
                }
                break;
            }
            case ActionCode::Delete: {
                Value nameVal = ctx.stack.pop();
                Value objVal = ctx.stack.pop();
                bool ok = false;
                if (objVal.isObject() && objVal.asObject()) {
                    objVal.asObject()->deleteOwnProperty(nameVal.toString());
                    ok = true;
                }
                ctx.stack.push(Value::boolean(ok));
                break;
            }
            case ActionCode::Delete2: {
                Value nameVal = ctx.stack.pop();
                ctx.stack.push(Value::boolean(ctx.scope.deleteVariable(nameVal.toString())));
                break;
            }

            // --- objects / arrays -----------------------------------------
            case ActionCode::InitObject: {
                double raw = ctx.stack.pop().toNumber();
                size_t numProps = clampCount(raw);
                auto obj = std::make_shared<Object>();
                for (size_t i = 0; i < numProps; ++i) {
                    Value value = ctx.stack.pop();
                    Value name = ctx.stack.pop();
                    obj->setOwnProperty(name.toString(), value);
                }
                ctx.stack.push(Value::object(obj));
                break;
            }
            case ActionCode::InitArray: {
                double raw = ctx.stack.pop().toNumber();
                size_t numElements = clampCount(raw);
                std::vector<Value> elements(numElements);
                for (size_t i = 0; i < numElements; ++i) {
                    elements[numElements - 1 - i] = ctx.stack.pop();
                }
                auto arr = std::make_shared<Object>(Object::Kind::kArray);
                arr->elements = std::move(elements);
                ctx.stack.push(Value::object(arr));
                break;
            }
            case ActionCode::GetMember: {
                Value nameVal = ctx.stack.pop();
                Value objVal = ctx.stack.pop();
                std::string name = nameVal.toString();
                if (objVal.isObject() && objVal.asObject()) {
                    ctx.stack.push(objVal.asObject()->getMember(name));
                } else if (objVal.isString() && name == "length") {
                    ctx.stack.push(Value::number(static_cast<double>(objVal.asStringRaw().size())));
                } else {
                    ctx.stack.push(Value::undefined());
                }
                break;
            }
            case ActionCode::SetMember: {
                Value valueVal = ctx.stack.pop();
                Value nameVal = ctx.stack.pop();
                Value objVal = ctx.stack.pop();
                if (objVal.isObject() && objVal.asObject()) {
                    objVal.asObject()->setMember(nameVal.toString(), valueVal);
                }
                break;
            }
            case ActionCode::Enumerate: {
                Value nameVal = ctx.stack.pop();
                Value target = ctx.scope.getVariable(nameVal.toString());
                ctx.stack.push(Value::null());
                if (target.isObject() && target.asObject()) {
                    for (const auto& entry : target.asObject()->ownProperties()) {
                        ctx.stack.push(Value::string(entry.first));
                    }
                    for (const auto& name : target.asObject()->enumerableNativeNames()) {
                        ctx.stack.push(Value::string(name));
                    }
                }
                break;
            }
            case ActionCode::Enumerate2: {
                Value target = ctx.stack.pop();
                ctx.stack.push(Value::null());
                if (target.isObject() && target.asObject()) {
                    for (const auto& name : target.asObject()->enumerableNativeNames()) {
                        ctx.stack.push(Value::string(name));
                    }
                    for (const auto& entry : target.asObject()->ownProperties()) {
                        ctx.stack.push(Value::string(entry.first));
                    }
                }
                break;
            }
            case ActionCode::NewObject: {
                Value classNameVal = ctx.stack.pop();
                double numArgsRaw = ctx.stack.pop().toNumber();
                std::vector<Value> args = popArgs(ctx.stack, numArgsRaw);
                std::string className = classNameVal.toString();
                if (ctx.callTraceSink) {
                    ctx.callTraceSink("NewObject new " + className + "(" + formatTraceArgs(args) + ")");
                }

                if (className == "Object") {
                    ctx.stack.push(Value::object(std::make_shared<Object>()));
                } else if (className == "Array") {
                    auto arr = std::make_shared<Object>(Object::Kind::kArray);
                    arr->elements = args;
                    ctx.stack.push(Value::object(arr));
                } else {
                    Value ctorVal = ctx.scope.getVariable(className);
                    if (ctorVal.isObject() && ctorVal.asObject() && ctorVal.asObject()->isFunction()) {
                        auto newObj = std::make_shared<Object>();
                        Value protoVal = ctorVal.asObject()->getMember("prototype");
                        if (protoVal.isObject()) newObj->prototype = protoVal.asObject();
                        Value result = invokeFunction(ctx, ctorVal.asObject(), Value::object(newObj), args);
                        ctx.stack.push(result.isObject() ? result : Value::object(newObj));
                    } else {
                        LOG_WARN("AVM1", "NewObject: unknown constructor '%s'", className.c_str());
                        ctx.stack.push(Value::undefined());
                    }
                }
                break;
            }

            // --- functions --------------------------------------------------
            case ActionCode::DefineFunction:
            case ActionCode::DefineFunction2: {
                swf::SwfReader header(data.data(), data.size());
                auto func = std::make_shared<Object>(Object::Kind::kFunction);
                func->function = std::make_unique<Object::FunctionDef>();
                Object::FunctionDef& def = *func->function;

                def.name = header.readCString();

                if (action == ActionCode::DefineFunction) {
                    uint16_t numParams = header.readU16();
                    def.paramNames.reserve(numParams);
                    for (uint16_t i = 0; i < numParams && !header.failed(); ++i) {
                        def.paramNames.push_back(header.readCString());
                    }
                    def.registerCount = 4;
                } else {
                    uint16_t numParams = header.readU16();
                    def.registerCount = header.readU8();
                    uint16_t flags = header.readU16();
                    def.preloadThis = (flags & 0x0001) != 0;
                    def.suppressThis = (flags & 0x0002) != 0;
                    def.preloadArguments = (flags & 0x0004) != 0;
                    def.suppressArguments = (flags & 0x0008) != 0;
                    def.preloadSuper = (flags & 0x0010) != 0;
                    def.suppressSuper = (flags & 0x0020) != 0;
                    def.preloadRoot = (flags & 0x0040) != 0;
                    def.preloadParent = (flags & 0x0080) != 0;
                    def.preloadGlobal = (flags & 0x0100) != 0;
                    def.registerParams.reserve(numParams);
                    for (uint16_t i = 0; i < numParams && !header.failed(); ++i) {
                        uint8_t reg = header.readU8();
                        std::string name = header.readCString();
                        def.registerParams.push_back({reg, std::move(name)});
                    }
                }

                uint16_t codeSize = header.readU16();
                // The function BODY is NOT part of `data` — per the SWF
                // spec's DefineFunction(2) encoding, it follows immediately
                // in the *outer* bytecode stream. Read it directly from the
                // outer reader (advancing past it) so subsequent actions in
                // this run parse correctly; this is the one case (besides
                // With) where an action's true extent exceeds its own
                // declared Length.
                def.body = reader.readBytes(codeSize);
                def.capturedScope = std::make_shared<Scope>(ctx.scope);

                if (!def.name.empty()) {
                    ctx.scope.defineLocal(def.name, Value::object(func));
                } else {
                    ctx.stack.push(Value::object(func));
                }
                break;
            }
            case ActionCode::Return: {
                returnValue = ctx.stack.pop();
                returned = true;
                break;
            }
            case ActionCode::CallFunction: {
                Value nameVal = ctx.stack.pop();
                double numArgsRaw = ctx.stack.pop().toNumber();
                std::vector<Value> args = popArgs(ctx.stack, numArgsRaw);
                std::string name = nameVal.toString();
                if (ctx.callTraceSink) {
                    ctx.callTraceSink("CallFunction " + name + "(" + formatTraceArgs(args) + ")");
                }
                Value funcVal = ctx.scope.getVariable(name);
                if (funcVal.isObject() && funcVal.asObject() && funcVal.asObject()->isFunction()) {
                    ctx.stack.push(invokeFunction(ctx, funcVal.asObject(), Value::undefined(), args));
                } else {
                    LOG_WARN("AVM1", "CallFunction: '%s' is not a function", name.c_str());
                    ctx.stack.push(Value::undefined());
                }
                break;
            }
            case ActionCode::CallMethod: {
                Value methodNameVal = ctx.stack.pop();
                Value objectVal = ctx.stack.pop();
                double numArgsRaw = ctx.stack.pop().toNumber();
                std::vector<Value> args = popArgs(ctx.stack, numArgsRaw);

                if (!objectVal.isObject() || !objectVal.asObject()) {
                    LOG_WARN("AVM1", "CallMethod: target is not an object");
                    ctx.stack.push(Value::undefined());
                    break;
                }
                std::string methodName = methodNameVal.toString();
                if (ctx.callTraceSink) {
                    ctx.callTraceSink("CallMethod " + objectVal.toString() + "." + methodName + "(" +
                                       formatTraceArgs(args) + ")");
                }
                Value funcVal = (methodNameVal.isUndefined() || methodName.empty())
                                     ? objectVal
                                     : objectVal.asObject()->getMember(methodName);
                if (funcVal.isObject() && funcVal.asObject() && funcVal.asObject()->isFunction()) {
                    ctx.stack.push(invokeFunction(ctx, funcVal.asObject(), objectVal, args));
                } else {
                    LOG_WARN("AVM1", "CallMethod: '%s' is not a function", methodName.c_str());
                    ctx.stack.push(Value::undefined());
                }
                break;
            }
            case ActionCode::NewMethod: {
                Value methodNameVal = ctx.stack.pop();
                Value objectVal = ctx.stack.pop();
                double numArgsRaw = ctx.stack.pop().toNumber();
                std::vector<Value> args = popArgs(ctx.stack, numArgsRaw);

                Value ctorVal;
                std::string methodName = methodNameVal.toString();
                if (ctx.callTraceSink) {
                    ctx.callTraceSink("NewMethod new " + objectVal.toString() + "." + methodName + "(" +
                                       formatTraceArgs(args) + ")");
                }
                if (objectVal.isObject() && objectVal.asObject()) {
                    ctorVal = (methodNameVal.isUndefined() || methodName.empty())
                                  ? objectVal
                                  : objectVal.asObject()->getMember(methodName);
                }
                if (ctorVal.isObject() && ctorVal.asObject() && ctorVal.asObject()->isFunction()) {
                    auto newObj = std::make_shared<Object>();
                    Value protoVal = ctorVal.asObject()->getMember("prototype");
                    if (protoVal.isObject()) newObj->prototype = protoVal.asObject();
                    Value result = invokeFunction(ctx, ctorVal.asObject(), Value::object(newObj), args);
                    ctx.stack.push(result.isObject() ? result : Value::object(newObj));
                } else {
                    LOG_WARN("AVM1", "NewMethod: '%s' is not a constructor", methodName.c_str());
                    ctx.stack.push(Value::undefined());
                }
                break;
            }

            // --- classes (minimal prototype-chain wiring only) -------------
            case ActionCode::Extends: {
                // Assumed stack order: pop Subclass ctor, then Superclass
                // ctor (i.e. `SuperCtor, SubCtor, Extends` was pushed in
                // that order) — not independently cross-checked against
                // real Flash-compiled `class X extends Y` output; see
                // docs/avm1-support.md. `extends` is rare in simple
                // Hobo-style AS2 game scripts, so this is a low-priority
                // correctness risk.
                Value subCtor = ctx.stack.pop();
                Value superCtor = ctx.stack.pop();
                if (subCtor.isObject() && subCtor.asObject() && superCtor.isObject() &&
                    superCtor.asObject()) {
                    auto newProto = std::make_shared<Object>();
                    Value superProto = superCtor.asObject()->getMember("prototype");
                    if (superProto.isObject()) newProto->prototype = superProto.asObject();
                    subCtor.asObject()->setOwnProperty("prototype", Value::object(newProto));
                } else {
                    LOG_WARN("AVM1", "Extends: operands are not both objects");
                }
                break;
            }
            case ActionCode::ImplementsOp: {
                double countRaw = ctx.stack.pop().toNumber();
                size_t count = clampCount(countRaw);
                ctx.stack.pop();  // class constructor
                for (size_t i = 0; i < count; ++i) ctx.stack.pop();  // interface constructors
                LOG_DEBUG("AVM1",
                          "ImplementsOp: interface list consumed, not enforced (no "
                          "interface-checking model)");
                break;
            }
            case ActionCode::InstanceOf: {
                Value ctorVal = ctx.stack.pop();
                Value objVal = ctx.stack.pop();
                bool result = false;
                if (objVal.isObject() && objVal.asObject() && ctorVal.isObject() && ctorVal.asObject()) {
                    Value protoVal = ctorVal.asObject()->getMember("prototype");
                    if (protoVal.isObject() && protoVal.asObject()) {
                        const Object* target = protoVal.asObject().get();
                        const Object* current = objVal.asObject()->prototype.get();
                        int depth = 0;
                        while (current != nullptr && depth < 64) {
                            if (current == target) {
                                result = true;
                                break;
                            }
                            current = current->prototype.get();
                            ++depth;
                        }
                    }
                }
                ctx.stack.push(Value::boolean(result));
                break;
            }
            case ActionCode::CastOp: {
                Value ctorVal = ctx.stack.pop();
                Value objVal = ctx.stack.pop();
                bool isInstance = false;
                if (objVal.isObject() && objVal.asObject() && ctorVal.isObject() && ctorVal.asObject()) {
                    Value protoVal = ctorVal.asObject()->getMember("prototype");
                    if (protoVal.isObject() && protoVal.asObject()) {
                        const Object* target = protoVal.asObject().get();
                        const Object* current = objVal.asObject()->prototype.get();
                        int depth = 0;
                        while (current != nullptr && depth < 64) {
                            if (current == target) {
                                isInstance = true;
                                break;
                            }
                            current = current->prototype.get();
                            ++depth;
                        }
                    }
                }
                ctx.stack.push(isInstance ? objVal : Value::null());
                break;
            }

            // --- misc ---------------------------------------------------
            case ActionCode::Trace: {
                Value msg = ctx.stack.pop();
                if (ctx.traceSink) ctx.traceSink(msg.toString());
                break;
            }
            case ActionCode::RandomNumber: {
                double maxD = ctx.stack.pop().toNumber();
                if (!(maxD > 0)) {
                    ctx.stack.push(Value::number(0));
                    break;
                }
                uint32_t maxN = static_cast<uint32_t>(std::min(maxD, 4294967295.0));
                uint32_t r = ctx.randomSource ? ctx.randomSource() : defaultRandom();
                ctx.stack.push(Value::number(static_cast<double>(maxN == 0 ? 0 : r % maxN)));
                break;
            }
            case ActionCode::GetTime: {
                double ms = ctx.clockSource ? ctx.clockSource() : defaultClockMs();
                ctx.stack.push(Value::number(ms));
                break;
            }
            case ActionCode::Throw: {
                Value v = ctx.stack.pop();
                LOG_WARN("AVM1",
                          "Throw: %s (no exception handling implemented — value discarded, "
                          "execution continues)",
                          v.toString().c_str());
                break;
            }
            case ActionCode::Try: {
                // Fully self-contained within `data` (unlike DefineFunction/
                // With) — Phase 4 parses-and-skips only; try-block execution
                // is deferred (see docs/avm1-support.md). `data` was already
                // consumed generically by the outer loop; nothing more to do.
                LOG_DEBUG("AVM1", "Try: parsed and skipped (block execution not yet implemented)");
                break;
            }
            case ActionCode::With: {
                swf::SwfReader header(data.data(), data.size());
                uint16_t blockSize = header.readU16();
                std::vector<uint8_t> block = reader.readBytes(blockSize);

                Value targetVal = ctx.stack.pop();
                if (targetVal.isObject() && targetVal.asObject()) {
                    Scope savedScope = ctx.scope;
                    ctx.scope = ctx.scope.pushed(targetVal.asObject());
                    runNested(ctx, block.data(), block.size());
                    ctx.scope = savedScope;
                } else {
                    LOG_DEBUG("AVM1", "With: target is not an object — skipping block body");
                }
                break;
            }
            case ActionCode::Jump: {
                swf::SwfReader r2(data.data(), data.size());
                int16_t offset = r2.readS16();
                reader.seek(static_cast<size_t>(static_cast<int64_t>(reader.position()) + offset));
                break;
            }
            case ActionCode::If: {
                swf::SwfReader r2(data.data(), data.size());
                int16_t offset = r2.readS16();
                bool cond = ctx.stack.pop().toBoolean();
                if (cond) {
                    reader.seek(static_cast<size_t>(static_cast<int64_t>(reader.position()) + offset));
                }
                break;
            }

            case ActionCode::End:
                // Handled before the switch; unreachable here.
                break;

            default:
                LOG_WARN("AVM1", "Unhandled action code 0x%02X (%s)", code8, actionCodeName(code8));
                break;
        }

        if (returned) break;
    }

    return returned ? returnValue : Value::undefined();
}

}  // namespace flash3ds::avm1
