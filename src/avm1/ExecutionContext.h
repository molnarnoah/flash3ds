// ExecutionContext.h
//
// Everything a single AVM1 bytecode run (a top-level DoAction body, or a
// user-defined function's body) needs: the operand Stack, the variable
// Scope chain, a register file (ActionPush register-type / StoreRegister),
// a constant pool (ActionConstantPool / Push constant8/16), the current
// `this` binding, a pointer to the global object, an optional HostBindings
// for movie-clip-ish actions (nullptr in Phase 4 — see HostBindings.h), a
// trace output sink, and a call-depth counter (recursion guard, shared
// across a whole call chain via `callDepthCounter`).

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "avm1/HostBindings.h"
#include "avm1/Scope.h"
#include "avm1/Stack.h"
#include "avm1/Value.h"

namespace flash3ds::avm1 {

class ExecutionContext {
public:
    ExecutionContext(Scope scopeChain, std::shared_ptr<Object> global)
        : scope(std::move(scopeChain)), globalObject(std::move(global)) {
        registers.resize(kDefaultRegisterCount);
    }

    Stack stack;
    Scope scope;
    std::vector<Value> registers;
    std::vector<std::string> constantPool;
    Value thisValue;
    std::shared_ptr<Object> globalObject;

    // Non-owning; null in Phase 4 (see HostBindings.h). Set by whatever
    // sets up the ExecutionContext (Phase 5+) to wire GotoFrame/Play/
    // GetProperty/etc. into a real Timeline.
    HostBindings* host = nullptr;

    // Called by ActionTrace. Defaults (set by Interpreter::execute if left
    // unset) to writing through Log at LOG_INFO with the "AVM1" category.
    std::function<void(const std::string&)> traceSink;

    // Roadmap Phase 4 (2026-08-21) diagnostic hook — OPTIONAL, nullptr by
    // default (zero behavior/perf change for every existing caller, same
    // convention as randomSource/clockSource below). When set, the
    // interpreter reports every ActionCode::{CallFunction,CallMethod,
    // NewMethod,NewObject,GetURL,GetURL2} it actually executes, with their
    // REAL resolved (runtime, not static-guessed) name/target/argument
    // values — see each call site's own comment. This exists specifically
    // because a purely static disassembly of real, compiler-obfuscated
    // content (see docs/known-limitations.md L6: zero literal-string hits
    // for "loadMovie" anywhere in Extreme Pamplona despite it clearly
    // loading external content) cannot see through dynamically-computed
    // names/strings the way actually RUNNING the real interpreter can —
    // by the time a CallMethod fires, any decrypt/concat logic that built
    // its method-name string has already executed for real. Propagated
    // across function calls exactly like traceSink/randomSource/
    // clockSource/host (see invokeFunction() in Interpreter.cpp).
    std::function<void(const std::string&)> callTraceSink;

    // ActionRandomNumber's source (returns a uniformly-distributed 32-bit
    // value; the interpreter reduces it mod the requested maximum).
    // Defaults to a real PRNG if left unset — tests can override this for
    // determinism.
    std::function<uint32_t()> randomSource;

    // ActionGetTime's source, in milliseconds. Defaults to a real
    // steady_clock-based value if left unset — tests can override this for
    // determinism.
    std::function<double()> clockSource;

    // Shared across an entire call chain (a function call's new
    // ExecutionContext should copy this pointer, not reset it) so the
    // recursion-depth guard in Interpreter.cpp actually bounds the whole
    // chain, not just one frame. Owned by whoever created the outermost
    // ExecutionContext.
    int* callDepthCounter = nullptr;

    // Bounds-checked register access: out-of-range reads return undefined,
    // out-of-range writes are silently ignored (both logged at debug
    // level) rather than resizing unboundedly or indexing out of bounds.
    Value getRegister(size_t index) const;
    void setRegister(size_t index, Value value);

private:
    static constexpr size_t kDefaultRegisterCount = 4;
};

}  // namespace flash3ds::avm1
