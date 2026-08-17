# AVM1 (ActionScript 2) Support Status

**Status: Phase 4 VM core implemented.** Phase 5 (MovieClip API / scene-graph
wiring) is not started.

Phase 4 built a complete, standalone AVM1 bytecode interpreter
(`src/avm1/`) that runs against a raw bytecode buffer and an
`ExecutionContext` — it is **not yet wired into `Movie`/`Timeline`**:
`DoAction`/`DoInitAction` tag bodies are still only parsed as raw bytes at
the tag level (see `docs/swf-support.md`) and are not executed. That
wiring — plumbing a `Timeline`/`DisplayList`-backed `HostBindings`
implementation into the interpreter, and calling it from `DoAction`/frame
processing — is Phase 5's job, per the project's phase-by-phase plan (see
`docs/architecture.md`).

## Module layout

```
src/avm1/
  Value.h/.cpp            — the dynamic Value type + Object (property bag,
                             Array, Function)
  Stack.h                 — operand stack (safe on underflow)
  Scope.h/.cpp             — variable scope chain (GetVariable/SetVariable/
                             DefineLocal/Delete2 semantics)
  GlobalObject.h/.cpp      — constructs the top-level/`_global` object
  ActionCode.h/.cpp        — AVM1 opcode enum + name table
  HostBindings.h           — the seam to MovieClip-affecting actions (all
                             no-op stubs in Phase 4 — see below)
  ExecutionContext.h/.cpp  — stack + scope + registers + constant pool +
                             `this` + host + trace/random/clock sources +
                             shared call-depth counter
  Interpreter.h/.cpp       — the dispatch loop
```

## Value model

`Value` is AVM1's dynamic type: Undefined, Null, Boolean, Number (double),
String, or a reference to an `Object` (plain object / Array / Function).
Coercions (`toBoolean`/`toNumber`/`toString`/`toInt32`) follow ECMA-262-3 /
real AS2 runtime rules, with two documented simplifications:

- **No user-overridable `valueOf()`/`toString()` dispatch** on plain
  objects — coercion to Number/String uses fixed built-in rules rather than
  calling a script-defined method. `Array.toString()` (comma-joined
  elements) and `Function.toString()` (`"[type Function]"`) ARE built in,
  since those are intrinsic, not user-overridable in typical content.
- **No true multi-byte-string awareness** — `MBString*`/`MBCharToAscii`/
  `MBAsciiToChar` actions behave identically to their byte-oriented
  non-MB counterparts.

`Object` supports a prototype chain (`getMember`/`setMember`, bounded-depth
walk to survive a cyclic prototype), Array semantics (`length` and numeric
index properties map to a real `std::vector<Value> elements`, capped at 10M
elements against a malformed/malicious length assignment), and Function
objects (captured closure scope + bytecode body — see below).

## Opcode status

Every opcode below is at minimum **correctly parsed/skipped** (so the
bytecode stream never desyncs regardless of implementation status).
"Executed" means it has real, tested behavior; "stubbed" means it's
recognized, forwards to `HostBindings` (a no-op in Phase 4 — logs at debug
level and continues), or is otherwise deliberately deferred.

### Stack / values — executed

Push (all 8 operand types: String, Float, Null, Undefined, Register,
Boolean, Double, Integer, Constant8, Constant16), Pop, PushDuplicate,
StackSwap, StoreRegister, ConstantPool.

### Arithmetic / comparison — executed

Legacy numeric-only `Add`/`Subtract`/`Multiply`/`Divide`/`Modulo`/
`Equals`/`Less`/`And`/`Or`/`Not` (SWF4-era), plus the polymorphic SWF5+
`Add2` (string concat if either operand is a string, else numeric),
`Less2`/`Greater` (string-lexicographic if both strings, else numeric;
NaN-safe), `Equals2` (simplified loose equality — see below),
`StrictEquals`, `Increment`/`Decrement`, `ToInteger`/`ToNumber`/`ToString`,
`TypeOf` (including the `typeof null === "object"` ECMA quirk).

`Equals2`'s simplification: no `ToPrimitive` dispatch on objects, so an
`Object` value is never loosely equal to a primitive here (real AS2 rarely
relies on this for simple game-logic comparisons).

### Bitwise — executed

`BitAnd`/`BitOr`/`BitXor`/`BitLShift`/`BitRShift`/`BitURShift`, all via
`Value::toInt32()` (NaN/Infinity → 0, truncate toward zero, wrap to 32-bit
signed — matches ECMA `ToInt32`).

### Strings — executed

`StringAdd`, `StringEquals`, `StringLess`, `StringGreater`, `StringLength`/
`MBStringLength`, `StringExtract`/`MBStringExtract`, `CharToAscii`/
`MBCharToAscii`, `AsciiToChar`/`MBAsciiToChar` (byte-oriented; see the MB
simplification note above).

### Variables / scope — executed

`GetVariable`, `SetVariable`, `DefineLocal`, `DefineLocal2`, `Delete`,
`Delete2`. Scope-chain semantics (innermost-to-outermost search for
`GetVariable`/existing-binding update for `SetVariable`, always-innermost
for `DefineLocal`) are covered by `tests/test_avm1_scope.cpp`.

### Objects / arrays — executed

`InitObject`, `InitArray`, `GetMember`, `SetMember`, `Enumerate`/
`Enumerate2` (property-name enumeration order is **not** deterministic —
backed by `std::unordered_map`; ECMA doesn't guarantee an order either, so
this is a non-issue for correctness, just not bit-for-bit reproducible
across runs), `NewObject` (builtin `"Object"`/`"Array"` handled directly;
any other name resolves a user-defined constructor via the scope chain and
invokes it with a fresh object as `this`, wiring up `prototype` if
present).

### Functions — executed

`DefineFunction`, `DefineFunction2` (register-or-named parameter binding,
all preload/suppress flags parsed and applied where meaningful — see the
confidence note below), `CallFunction`, `CallMethod`, `NewMethod`,
`Return`. Closures capture the defining scope chain at `DefineFunction`
time (verified by `Interpreter_Closures_FunctionSeesEnclosingScopeVariable`
in `tests/test_avm1_interpreter.cpp`); recursion works (verified by a
factorial test) and is bounded by a shared call-depth counter
(`Interpreter::kMaxCallDepth = 256`) so a runaway/infinitely-recursive
script returns gracefully instead of overflowing the C++ stack (verified
by `Interpreter_DeepRecursion_DoesNotCrashOrHang`).

**Confidence note on `DefineFunction2` preload register slots**: preloaded
values (`this`/`arguments`/`super`/`_root`/`_parent`/`_global`) are placed
into fixed register numbers 1 through 6 in that declared order when their
flag is set — this is the commonly-documented convention, but has not been
independently cross-checked against a real Flash-authored `SWF`. `super`/
`_root`/`_parent` have no real value to preload yet anyway (no class model,
no scene-graph wiring — Phase 5+), so their slots are currently always
reserved-but-undefined.

### Classes (minimal) — executed, reduced scope

`Extends` (wires a subclass's `prototype` to chain to the superclass's
`prototype` — **operand pop order is an unverified assumption**, see the
code comment in `Interpreter.cpp`; `extends` is rare in simple Hobo-style
AS2 game scripts, so this is a low-priority correctness risk), `InstanceOf`,
`CastOp` (both walk the prototype chain by reference identity, bounded
depth). `ImplementsOp` consumes its operands correctly but doesn't enforce
interface conformance (no interface-checking model — logged at debug
level).

### Control flow — executed

`Jump`, `If` (both via signed 16-bit branch offsets relative to the byte
position immediately after the offset field, matching the spec; an
out-of-range target fails the underlying reader gracefully — the run just
ends, never reads out of bounds). `With` (pushes the popped target object
as a new innermost scope frame for its block only, correctly reading the
block's bytecode from *outside* its own declared action length — the same
"body follows header" pattern as `DefineFunction`/`DefineFunction2` — then
restores the prior scope afterward). Both `With`-block execution and
function calls share one call-depth guard
(`Interpreter::kMaxCallDepth`) so deeply nested/recursive combinations of
either still can't exhaust the C++ stack.

`Try`: **parsed and skipped only** — the try/catch/finally block's own
bytecode is never executed (not even the try-block on the no-exception
path). This was a deliberate scope decision: `Try`'s flag-byte layout
(register-vs-name catch variable, `HasFinallyBlock`, etc.) has enough
undocumented-here ambiguity that guessing wrong risks silently corrupting
which bytes are read as the try/catch/finally boundaries — safer to skip
the whole record (which is fully self-contained within its own declared
action length, unlike `With`/`DefineFunction`) than risk desyncing the
rest of the script. `try`/`catch` is uncommon in simple AS2 game logic;
revisit if a target title needs it.

`Throw`: logs the thrown value and continues (no exception propagation —
consistent with `Try` not being implemented).

### MovieClip / timeline actions — stubbed (HostBindings, Phase 5)

`GotoFrame`, `GotoFrame2`, `GotoLabel`, `Play`, `Stop`, `NextFrame`,
`PreviousFrame`, `GetProperty`, `SetProperty`, `CloneSprite`,
`RemoveSprite`, `StartDrag`, `EndDrag`, `SetTarget`, `SetTarget2`. All are
correctly parsed (including popping the right number/shape of stack
operands, so the interpreter's stack stays balanced) and forwarded to
`ExecutionContext::host` (a `HostBindings*`, `nullptr` in Phase 4) — with
no host bound, they log at `LOG_DEBUG` and are otherwise no-ops. See
`src/avm1/HostBindings.h` for the exact interface Phase 5 needs to
implement against a real `Timeline`/`DisplayList`.

`WaitForFrame`/`WaitForFrame2`: simplified to "always considered loaded"
(never takes the skip branch) — correct once a real `Timeline` always has
already-parsed frame data available synchronously (true for this
runtime's design; streaming/progressive loading isn't a target use case).

`Call` (frame-as-subroutine): pops and discards its target; not wired.

`TargetPath`: pushes `""` (no scene graph to resolve a target path
against yet).

`GetURL`/`GetURL2`: parsed and logged; browser navigation is out of scope
entirely for a Nintendo 3DS runtime (no browser to navigate).

`ToggleQuality`/`StopSounds`: recognized no-ops (rendering-quality control
isn't modeled; audio isn't wired until Phase 6).

## Push's DOUBLE encoding — confidence note

`ActionPush`'s type-6 (Double) operand stores a standard IEEE-754 double
with its two 32-bit words swapped relative to normal little-endian
layout — a documented SWF-specific quirk. The convention implemented here
(first 4 bytes = high-order word, next 4 = low-order word) is believed
correct per common third-party SWF parser implementations, but has **not**
been independently verified against a real Flash-authored double literal.
Integer (`Push` type 7) and Float32 (`Push` type 1) literals cover the
overwhelming majority of real DoAction content, so this is a low-priority
correctness risk — `tests/Avm1TestFixtures.cpp`'s `pushDouble()` encodes
using the exact same convention as `Interpreter.cpp`'s decoder (both
written independently from the same documented rule), so the round-trip
test passing confirms internal self-consistency, not correctness against
real SWF output.

## Testing

- `tests/test_avm1_value.cpp` — `Value` coercions, `Object` own-property
  CRUD, prototype-chain `getMember` (including a cyclic-prototype
  hang-guard test), Array length/index semantics, Array `toString()`.
- `tests/test_avm1_scope.cpp` — scope-chain `getVariable`/`setVariable`/
  `defineLocal`/`deleteVariable` semantics (shadowing, chain-search update
  vs. always-innermost declare).
- `tests/test_avm1_interpreter.cpp` — 30 tests covering arithmetic,
  comparisons, logic, bitwise ops, string ops, variables, objects/arrays,
  a real `Jump`/`If` loop (sums 1..5), `DefineFunction`/`CallFunction`
  (including recursion — factorial), `DefineFunction2` register params +
  `PreloadThis` + `CallMethod`, closures, `NewObject` (both builtin
  `Array` and a user-defined constructor), `Trace` via a custom sink,
  `ConstantPool`/`Push` constant, `StoreRegister`/register `Push`, the
  deep-recursion defensive limit, `With`, `HostBindings` forwarding (via a
  test `HostBindings` subclass that records calls) with and without a
  bound host, malformed/underflowed-stack bytecode (must not crash), and
  all `Push` operand types round-tripping.
- `tests/Avm1TestFixtures.h/.cpp` — an independent AVM1 bytecode assembler
  (with label-based `Jump`/`If` offset resolution) used to build every test
  fixture above, written from the same public spec the interpreter itself
  is implemented against (not sharing code with it).

## Next (Phase 5)

Wire this interpreter into the scene graph: implement a real
`HostBindings` backed by `Timeline`/`DisplayList`, execute `DoAction`/
`DoInitAction` tag bodies during frame processing, add the `MovieClip` API
surface (`_root`, `_parent`, per-instance properties via `GetProperty`/
`SetProperty`'s fixed property-index table, `onClipEvent` handlers,
independent per-sprite-instance playheads — see the limitation already
flagged in `docs/renderer.md`).
