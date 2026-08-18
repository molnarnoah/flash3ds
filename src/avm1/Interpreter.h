// Interpreter.h
//
// The AVM1 bytecode interpreter: a linear, tree-walking dispatch loop over
// an action-record stream (a DoAction tag body, or a captured function
// body) — "interpreter, not JIT" per the project spec. Clean-room
// implementation against the public SWF File Format Specification's
// "Actions" chapter, cross-referenced with the observed real-world
// bytecode shape (not the Shift-DX binary's own AVM1 dispatcher, which we
// deliberately never decompiled/copied — see docs/shift-dx-behavior.md).
//
// Phase 4 scope: the pure computational core (stack, arithmetic,
// comparisons, string ops, variables, objects/arrays, user-defined
// functions/closures, control flow) runs fully. MovieClip-affecting
// actions (GotoFrame, Play, GetProperty, ...) are recognized, correctly
// parsed/skipped so the bytecode stream never desyncs, and forwarded to an
// optional HostBindings (see HostBindings.h) that Phase 5 will implement —
// with `ExecutionContext::host == nullptr` (Phase 4's default), they log at
// debug level and are otherwise no-ops. See docs/avm1-support.md for the
// exact per-opcode status table.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "avm1/ExecutionContext.h"

namespace flash3ds::avm1 {

class Object;

class Interpreter {
public:
    // Executes `code` (length `length` bytes) against `ctx`, starting at
    // offset 0, until ActionEnd (code byte 0x00), the end of the buffer, an
    // ActionReturn, or a defensive limit is hit (see the constants below).
    // Returns the value passed to ActionReturn, or undefined if execution
    // ended without one. Never throws; never crashes on malformed/
    // adversarial bytecode — see the per-opcode notes in Interpreter.cpp.
    static Value execute(ExecutionContext& ctx, const uint8_t* code, size_t length);

    // Phase 7: public entry point for invoking an already-constructed AVM1
    // Function value directly, without going through bytecode dispatch
    // (CallFunction/CallMethod/NewObject). This is what lets native/host
    // code call back into AS2 — e.g. ExternalInterface.addCallback's
    // registered function, or (in principle) any other native->AS2
    // callback hook added later. `callerCtx` supplies the shared globals/
    // host/trace/random/clock sources and (critically) the shared call-
    // depth counter, exactly as if this were a nested bytecode call site.
    // Trivial forwarding wrapper around the interpreter-internal
    // invokeFunction() helper (Interpreter.cpp, anonymous namespace) — see
    // that function's doc comment for the full behavior (native nativeImpl
    // short-circuit, closure scope, register/preload handling, bounded
    // recursion, ...). Returns undefined if `funcObj` isn't a callable
    // Function object.
    static Value callFunction(ExecutionContext& callerCtx, const std::shared_ptr<Object>& funcObj,
                               Value thisVal, std::vector<Value> args);

    // Defensive limits (see docs/architecture.md's "Defensive resource
    // limits" principle): guard against malformed/adversarial bytecode
    // driving an infinite loop, unbounded C++ recursion (via nested
    // function calls, With, or Try/Finally blocks), or runaway allocation.
    static constexpr int kMaxInstructionsPerRun = 1'000'000;
    static constexpr int kMaxCallDepth = 256;
};

}  // namespace flash3ds::avm1
