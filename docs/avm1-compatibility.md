# AVM1 Opcode Compatibility

**Compatibility-audit phase (2026-08-18) deliverable.** Built by reading
`src/avm1/ActionCode.h` (the 100-entry opcode enum) and `src/avm1/
Interpreter.cpp`'s dispatch switch directly — every opcode below has an
exact case-line citation. This supersedes/re-confirms `docs/avm1-support.md`'s
narrative opcode-status prose with a flat, audit-oriented table; see that
doc for the fuller design discussion per opcode group.

Status legend: **EXECUTED** (real, tested behavior) · **STUB** (recognized,
consumes its operands correctly so the bytecode stream never desyncs, but
either forwards to `HostBindings` with no local effect when no host is
bound, or is a pure log/no-op by design) · **NOT IMPLEMENTED** (parsed and
discarded entirely, e.g. `Try`).

**Confirmed: all 100 `ActionCode` enum values have an explicit `case` in
`Interpreter.cpp`'s dispatch switch — none fall through to `default`.**
(`default:` at `Interpreter.cpp:1232-1234` only fires for a raw byte that
doesn't map to ANY named `ActionCode` at all.)

## Stack / values

| Opcode | Status | Interpreter.cpp | Notes |
|---|---|---|---|
| Push | EXECUTED | 515-575 | all 8 operand types incl. Double (unverified word-order, see Safety notes) |
| Pop | EXECUTED | 506-508 | |
| PushDuplicate | EXECUTED | 509-511 | |
| StackSwap | EXECUTED | 512-514 | |
| StoreRegister | EXECUTED | 576-581 | |
| ConstantPool | EXECUTED | 582-592 | |

## Arithmetic / comparison / logic

| Opcode | Status | Interpreter.cpp | Notes |
|---|---|---|---|
| Add, Subtract, Multiply, Divide, Modulo | EXECUTED | 595-619 | legacy SWF4 numeric-only |
| Equals, Less, And, Or, Not | EXECUTED | 620-645 | |
| Add2, Less2, Greater, Equals2, StrictEquals | EXECUTED | 648-672 | SWF5+ polymorphic; `Equals2` has no `ToPrimitive` object dispatch (documented simplification) |
| Increment, Decrement | EXECUTED | 673-682 | |
| ToInteger, ToNumber, ToString, TypeOf | EXECUTED | 683-714 | `TypeOf` includes the `typeof null === "object"` ECMA quirk |

## Bitwise

| Opcode | Status | Interpreter.cpp |
|---|---|---|
| BitAnd, BitOr, BitXor, BitLShift, BitRShift, BitURShift | EXECUTED | 717-750, via `Value::toInt32()` (NaN/Infinity→0, wraps to 32-bit signed) |

## Strings

| Opcode | Status | Interpreter.cpp | Notes |
|---|---|---|---|
| StringAdd, StringEquals, StringLess, StringGreater, StringLength, StringExtract | EXECUTED | 753-794 | |
| CharToAscii, AsciiToChar, MBCharToAscii, MBStringLength, MBStringExtract, MBAsciiToChar | EXECUTED | 795-808 | all byte-oriented, no true multi-byte awareness (documented simplification, shares code with the non-MB opcodes) |

## Variables / scope

| Opcode | Status | Interpreter.cpp |
|---|---|---|
| GetVariable, SetVariable | EXECUTED | 811-821 |
| DefineLocal, DefineLocal2 | EXECUTED | 822-835 |
| Delete, Delete2 | EXECUTED | 836-851 |

## Objects / arrays

| Opcode | Status | Interpreter.cpp | Notes |
|---|---|---|---|
| InitObject, InitArray | EXECUTED | 854-877 | counts clamped to 100,000 against malformed input |
| GetMember, SetMember | EXECUTED | 878-899 | dynamic/computed property names (`obj[expr]`) go through this same generic path — see `docs/compatibility-matrix.md` §4 for the Hobo-pattern caveat (not independently re-tested this phase) |
| Enumerate, Enumerate2 | EXECUTED | 900-926 | enumeration order not deterministic (unordered_map-backed; matches ECMA, which doesn't guarantee order either) |
| NewObject | EXECUTED | 927-953 | `"Object"`/`"Array"` special-cased directly; anything else resolves via scope-chain lookup — `GlobalObject` registers zero named CONSTRUCTORS (`new Date()`/`new Number()`/etc. still fail unless user-defined), see `docs/compatibility-matrix.md` §3a. Note this is separate from `Math`, which is a plain object (Roadmap Phase 8) never invoked with `new` — see "Global built-ins" below. |

## Global built-ins (`GlobalObject`)

Roadmap Phase 8 (2026-08-25) added the first real named built-in: `Math`
(`src/avm1/GlobalObject.cpp`), scoped strictly to real corpus evidence — a
static disassembly pass (`tools/real_game_harness/avm1_loader_disasm.cpp`,
keyword-filtered for `Math`/`String`/`Number`/`Boolean`/`Date`) across all
8 real corpus games plus a standalone `hobo.swf` copy found:

| Global | Evidence | Status |
|---|---|---|
| `Math.random()` | Called in hobo2/hobo3/hobo5/hobo6/hobo7 (66 sites) | EXECUTED |
| `Math.ceil()` | Called in hobo2/hobo3/hobo5/hobo6/hobo7 (66 sites, always paired with `Math.random()` — the `Math.ceil(Math.random() * n)` idiom) | EXECUTED |
| `Math.floor/round/abs/sqrt/pow/min/max/PI/E` | No traced call site in this corpus | EXECUTED (added alongside the evidenced two — same trivial/stateless shape, zero extra cost, matches the roadmap's own "at minimum" baseline) |
| `String.fromCharCode(...)` (static) | No traced call site — added task #67 (2026-08-27) as a falsifiable test of the Extreme Pamplona obfuscated-name-builder hypothesis in `docs/known-limitations.md`'s L6 addendum, not new corpus evidence | EXECUTED |
| `someString.charAt/.charCodeAt/.substr(...)` (instance methods on string PRIMITIVES, autoboxed) | Same task #67 rationale as `fromCharCode` above | EXECUTED — special-cased in `Interpreter.cpp`'s `ActionCode::CallMethod` (`tryStringPrimitiveMethod()`), not a real `String.prototype` chain (`Object` has no `[[PrimitiveValue]]` wrapper slot) |
| `String` (as `new String(...)`, a real boxed object) / `Number`/`Boolean`/`Date` (as global constructors/conversion functions) | Zero `CallFunction`/`NewObject`/`CallMethod` hits anywhere in the corpus | **DELIBERATELY NOT IMPLEMENTED** — see `docs/known-limitations.md` L2 (`Number`/`Boolean`/`Date`) and its L6 addendum (why `String`'s narrow slice above was added despite no direct evidence, and why the rest of `String` still isn't) |

`Math` is a plain object (`GlobalObject::create()` populates it directly,
no `nativeImpl`-framework changes needed — reuses the exact seam Phase 6's
`Key`/`Mouse`/`Sound` already established in `ScriptEnvironment`'s
constructor, just at the `GlobalObject` level instead since `Math` needs
no per-`ScriptEnvironment` captured state). `Math.random()` reuses
`ExecutionContext::randomSource` — the same injectable RNG seam
`ActionRandomNumber` already uses — rather than a second independent PRNG,
so tests can get deterministic `Math.random()` output the same way
existing `ActionRandomNumber` tests already do.

Caveat: `avm1_loader_disasm` is a linear, non-control-flow-following
static disassembler (its own header comment) — it can miss calls after a
branch/jump desyncs its symbolic stack. Absence of a hit for hobo1/hobo4/
extreme_pamplona is "not found by this pass," not proof those files never
call any of these globals.

## Functions

| Opcode | Status | Interpreter.cpp | Notes |
|---|---|---|---|
| DefineFunction, DefineFunction2 | EXECUTED | 956-1010 | register/named param binding, closures (verified — captures scope by value at definition time), preload flags all real |
| CallFunction, CallMethod, NewMethod | EXECUTED | 1016-1077 | |
| Return | EXECUTED | 1011-1015 | |
| Extends | EXECUTED | 1080-1100 | stack pop order (subclass-then-superclass) is an **unverified assumption**, flagged in-code |
| InstanceOf, CastOp | EXECUTED | 1111-1156 | 64-deep-capped prototype walk (own reimplementation, not shared with `Object::getMember`'s own identical cap — a maintenance risk if one constant changes without the other) |
| ImplementsOp | STUB | 1101-1110 | pops operands correctly, logs, enforces nothing |

## Classes / exceptions

| Opcode | Status | Interpreter.cpp | Notes |
|---|---|---|---|
| Try | **NOT IMPLEMENTED** | 1188-1195 | parsed and entirely skipped — no try/catch/finally body ever executes, not even the try-block on the no-exception path |
| Throw | STUB | 1180-1187 | pops the value, logs it, continues — no exception propagation mechanism exists anywhere |

## Control flow

| Opcode | Status | Interpreter.cpp | Notes |
|---|---|---|---|
| Jump, If | EXECUTED | 1212-1226 | signed 16-bit branch offsets, bounds-checked via `SwfReader::seek` |
| With | EXECUTED | 1196-1211 | pushes target as new innermost scope frame, shares the call-depth guard with function calls |
| End | EXECUTED (loop-terminator) | 1228-1230 | handled pre-switch; case is dead code |

## MovieClip / timeline actions (real only when a `HostBindings` is bound)

| Opcode | Status | Interpreter.cpp | Notes |
|---|---|---|---|
| NextFrame, PreviousFrame, Play, Stop | STUB (host-forwarded) | 326-341 | real behavior lives in `MovieClipHostBindings` (`src/runtime/`), not `avm1/` itself — see compatibility-matrix §5 |
| GotoFrame, GotoFrame2, GotoLabel, SetTarget, SetTarget2 | STUB (host-forwarded) | 348-411 | |
| GetProperty, SetProperty | STUB (host-forwarded) | 412-435 | property-index switch (0-21) lives in `MovieClipInstance.cpp:337-382`, not here; indices 16-19 (`_highquality`/`_focusrect`/`_soundbuftime`/`_quality`) silently no-op — see compatibility-matrix §5 |
| CloneSprite, RemoveSprite | STUB (host-forwarded) | 436-453 | |
| StartDrag, EndDrag | STUB (host-forwarded, but the host-side implementation is fully real) | 454-478 | L/T/R/B pop order is an **unverified assumption**, flagged in-code |
| ToggleQuality | STUB | 342-344 | pure log, no host call at all |
| StopSounds | STUB | 345-347 | pure log, not wired to `IAudioBackend::stopAllSounds()` |
| WaitForFrame, WaitForFrame2 | STUB | 381-396 | no skip logic — frames are always "loaded" (correct for this runtime's synchronous-load design) |
| TargetPath | STUB | 498-503 | pushes hardcoded `""` |
| Call | STUB | 479-483 | pops target frame, pure log — frame-as-subroutine not wired |
| GetURL, GetURL2 | STUB | 484-497 | parsed and logged; browser navigation genuinely out of scope for a 3DS runtime |

## Diagnostics

| Opcode | Status | Interpreter.cpp |
|---|---|---|
| Trace | EXECUTED | 1159-1163 |
| RandomNumber, GetTime | EXECUTED | 1164-1179 |

## Safety — malformed/adversarial bytecode

No out-of-bounds read or UB was found anywhere in the dispatch loop. Every
risky primitive is explicitly bounds-checked or capped:

- **Stack** underflow returns `Value::undefined()` rather than reading OOB (`Stack.h`).
- **Registers** bounds-checked (`ExecutionContext.cpp:7-22`).
- **Constant pool** index bounds-checked (`Interpreter.cpp:551-566`).
- **Jump/If targets**: `SwfReader::seek()` rejects any absolute position
  exceeding buffer size, including one computed from a negative offset that
  wraps to a huge `size_t` (well-defined modular wraparound, still caught).
- **`SwfReader` primitives**: every byte read is bounds-checked; `readBytes`
  clamps to available length.
- **`InitObject`/`InitArray`/`ImplementsOp` counts**: clamped to 100,000.
- **Recursion** (function calls, `With` nesting): shared depth counter
  capped at `kMaxCallDepth = 256`.
- **Instruction count**: capped at 1,000,000 per `execute()` call, guarding
  against infinite `Jump`/`If` loops.
- **Prototype-chain walks**: capped at 64 in three independent
  implementations (a duplication risk noted above, not a safety risk).

## Unverified-assumption inventory (flagged in-code, not independently cross-checked against real Flash-authored output)

1. `Push` type-6 Double word order (`Interpreter.cpp:26-35`).
2. `StartDrag`'s L/T/R/B stack pop order (`Interpreter.cpp:460-465`).
3. `Extends`'s subclass/superclass stack pop order (`Interpreter.cpp:1081-1087`).
4. `DefineFunction2` preload register slot assignment/order (`Interpreter.cpp:243-250`).

None of these are correctness risks in the sense of crashing or corrupting
unrelated state — each is isolated to its own opcode's specific behavior —
but each is a candidate root cause if a specific real-content script (using
`extends`, `startDrag` with a constraint rect, or `DefineFunction2`'s
register-preload flags) ever misbehaves.
