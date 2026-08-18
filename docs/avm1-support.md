# AVM1 (ActionScript 2) Support Status

**Status: Phase 4 VM core implemented; Phase 5 wired it into the scene
graph; Phase 6 added Sound/Input; Phase 7 added ExternalInterface.**
`DoAction`/`DoInitAction` now actually execute against a real `MovieClip`
tree, with per-instance playheads and a real `HostBindings`, native
(C++-backed) `Key`/`Mouse`/`Sound`/`ExternalInterface` built-ins, real
`StartDrag`/`EndDrag`, `onClipEvent`'s `Load`/`Unload`/`EnterFrame` handlers,
and AS2 <-> native/host communication in both directions.

Phase 4 built a complete, standalone AVM1 bytecode interpreter
(`src/avm1/`) that runs against a raw bytecode buffer and an
`ExecutionContext`. `src/avm1/` itself remains host-agnostic on purpose —
it still has zero dependency on `runtime/` or `swf/` (besides `SwfReader`
for byte parsing) — but Phase 5 added the integration layer,
`src/runtime/MovieClipInstance.h/.cpp` (in `runtime/`, deliberately the
one place in the codebase allowed to depend on BOTH `runtime/` and
`avm1/`), which:

- Runs a `DoAction`/`DoInitAction` tag's bytecode via `avm1::Interpreter::
  execute()` against a real `ExecutionContext`, scoped to a specific
  `MovieClipInstance` as `this`/the innermost scope object.
- Implements a real `HostBindings` (`MovieClipHostBindings`, private to
  `MovieClipInstance.cpp`) backed by an actual `Timeline`/`DisplayList`.
- Gives every placed sprite/MovieClip its own `Timeline` — a genuinely
  independent playhead, not a shared per-character cache (see
  `docs/renderer.md`).
- Exposes `_x`/`_y`/`_currentframe`/`_root`/`_parent`/named-child-clip
  access etc. through `avm1::Object`'s new `nativeGet`/`nativeSet`/
  `nativeEnumerate` hooks (see "Value model" below) — so both `GetProperty`/
  `SetProperty` bytecode AND plain `.member`/bare-variable bytecode resolve
  the same intrinsic properties consistently.

Phase 6 added Sound/Input on top of that wiring without touching the
interpreter's dispatch loop at all (see "Native (C++-backed) functions"
below): `ScriptEnvironment` now owns an `InputState` (host-settable
keyboard/mouse), an `audio::IAudioBackend*` (defaults to a no-op
`NullAudioBackend`), and populates its `_global` object with `Key`/`Mouse`/
`Sound` built-ins backed by native C++ closures over those two. `StartDrag`/
`EndDrag` are real (`ScriptEnvironment::startDrag/endDrag/updateDrag()`),
and `PlaceObject2`'s `ClipActionRecord` section is now parsed and partially
dispatched (`Load`/`Unload`/`EnterFrame` — see "Known Phase 6 limitations").

Phase 7 added `ExternalInterface` — AS2 <-> native/host communication in
both directions (see "ExternalInterface" below) — reusing the exact same
`nativeImpl` seam Phase 6 introduced for `Key`/`Mouse`/`Sound`, plus one new
interpreter-level building block: a public `Interpreter::callFunction()`
that lets native/host code invoke an already-constructed AS2 `Function`
value directly, without going through `CallFunction`/`CallMethod` bytecode
dispatch (needed for the native -> AS2 direction, `ExternalInterface.
addCallback`). Since this runtime embeds AS2 in the SAME process as its
native/host code (no browser, unlike real Flash's JS bridge), `avm1::Value`
crosses the boundary directly in both directions — a deliberate, documented
simplification/improvement over real `ExternalInterface`'s JS/XML
marshalling.

## Module layout

```
src/avm1/                      (host-agnostic — no runtime/ or swf/ dependency
                                 besides SwfReader for byte parsing)
  Value.h/.cpp            — the dynamic Value type + Object (property bag,
                             Array, Function, + Phase 5's native property
                             hooks)
  Stack.h                 — operand stack (safe on underflow)
  Scope.h/.cpp             — variable scope chain (GetVariable/SetVariable/
                             DefineLocal/Delete2 semantics)
  GlobalObject.h/.cpp      — constructs the top-level/`_global` object
  ActionCode.h/.cpp        — AVM1 opcode enum + name table
  HostBindings.h           — the abstract seam to MovieClip-affecting
                             actions (see below for who implements it)
  ExecutionContext.h/.cpp  — stack + scope + registers + constant pool +
                             `this` + host + trace/random/clock sources +
                             shared call-depth counter
  Interpreter.h/.cpp       — the dispatch loop; Phase 7 added the public
                             `Interpreter::callFunction()` entry point (see
                             "ExternalInterface" below)

src/runtime/MovieClipInstance.h/.cpp   (Phase 5 — depends on BOTH avm1/ and
                                         runtime/; the scene-graph/AVM1
                                         integration point)
  ScriptEnvironment    — owns the shared `_global` object + DoInitAction
                         bookkeeping for one loaded movie's whole clip tree;
                         Phase 6 also: InputState, IAudioBackend*, drag
                         state, and populates `_global` with native
                         Key/Mouse/Sound; Phase 7 also: registered host
                         functions/AS2 callbacks and populates `_global`
                         with native `ExternalInterface`
  MovieClipInstance    — one placed MovieClip (root or a sprite instance):
                         owns its own Timeline, a scripting Object, its
                         parsed ClipActionRecords (Phase 6), and
                         (privately) the real MovieClipHostBindings
                         implementation

src/runtime/InputState.h/.cpp          (Phase 6 — host-settable keyboard/
                                         mouse state; no avm1/runtime
                                         dependency in either direction)

src/audio/IAudioBackend.h              (Phase 6 — abstract sound-output
src/audio/NullAudioBackend.h/.cpp       seam, mirrors renderer/IRenderer.h;
                                         NullAudioBackend is the only
                                         implementation so far — logs, plays
                                         nothing)
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

**Native property hooks (Phase 5):** `Object` optionally carries
`nativeGet`/`nativeSet`/`nativeEnumerate` `std::function` hooks a host
embedder can install to intercept specific property names — checked BEFORE
the normal own-property map on every read/write path
(`hasOwnProperty`/`getOwnProperty`/`setOwnProperty`, which `Scope` uses for
plain-variable access, AND `getMember`/`setMember`, which `.member`
bytecode uses), so `_x = 10;` (bare variable) and `this._x = 10;` (explicit
member access) both resolve identically. This keeps `avm1/` itself
completely unaware of what a "MovieClip" is — `runtime::MovieClipInstance`
is the only thing that ever installs these hooks (see
`MovieClipInstance::wireScriptObject()`), same seam philosophy as
`HostBindings`. A native property always shadows a same-named plain
property, matching real AS2 (you cannot override `_x` by assignment).

**Native (C++-backed) functions (Phase 6):** `Object::FunctionDef` gained an
optional `nativeImpl` (`std::function<Value(ExecutionContext&, const Value&
thisVal, const std::vector<Value>& args)>`). `invokeFunction()` (a static
helper in `Interpreter.cpp`) checks it FIRST and, if set, calls it directly
instead of interpreting `body` (which is empty for a native function) —
`NewObject`/`CallFunction`/`CallMethod`/`NewMethod` needed ZERO changes to
support this, since they already resolve to a generic `Function` `Object`
via `Scope::getVariable`/`Object::getMember` and always call it through
`invokeFunction()`. `avm1::makeNativeFunction(name, fn)` builds one. This is
how `Key`/`Mouse`/`Sound` (Phase 6, populated by `ScriptEnvironment`'s
constructor) exist without teaching `avm1/` anything about MovieClips,
input, or audio — same seam philosophy as `HostBindings`/native property
hooks. Phase 7's `ExternalInterface` (see below) reuses this exact
mechanism, plus one small addition: `Interpreter::callFunction()` — a
public static wrapper around the same interpreter-internal
`invokeFunction()` helper `nativeImpl` calls through — lets native/host code
invoke an AS2 `Function` value directly (not via bytecode dispatch), which
`nativeImpl` alone couldn't do since native functions never have anything
to route native -> AS2 calls back through the interpreter.

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

### MovieClip / timeline actions — executed (Phase 5, via `MovieClipHostBindings`)

`GotoFrame`, `GotoFrame2`, `GotoLabel`, `Play`, `Stop`, `NextFrame`,
`PreviousFrame`, `GetProperty`, `SetProperty`, `CloneSprite`,
`RemoveSprite`, `SetTarget`, `SetTarget2` all have real behavior when
`ExecutionContext::host` points at a `runtime::MovieClipHostBindings`
(which `runtime::ScriptEnvironment::run()` always wires up — see
`src/runtime/MovieClipInstance.cpp`). Without a host bound (e.g. the
isolated `test_avm1_interpreter.cpp` tests), they still log at `LOG_DEBUG`
and are no-ops, exactly like Phase 4 — `avm1/` itself hasn't changed.

- `GotoFrame`/`GotoFrame2`/`GotoLabel`/`Play`/`Stop`/`NextFrame`/
  `PreviousFrame` act on the "current target" (whatever `SetTarget` last
  pointed at, defaulting to the clip whose script is running) —
  `GotoFrame`'s 0-based frame number is converted to `Timeline`'s 1-based
  convention.
- `GetProperty`/`SetProperty` resolve their explicit Target operand via
  `MovieClipInstance::resolvePath()` (an empty target string means "current
  target") and dispatch on the spec's fixed 0-21 property-index table to
  `MovieClipInstance` getters/setters (`_x`/`_y`/`_xscale`/`_yscale`/
  `_currentframe`/`_totalframes`/`_alpha`/`_visible`/`_rotation`/`_target`/
  `_framesloaded`/`_name`; `_width`/`_height`/`_droptarget`/`_url` are
  recognized but not computed/modeled — see `docs/swf-support.md`).
- `CloneSprite`/`RemoveSprite` mutate the target's PARENT display list
  synchronously (via `Timeline::mutableDisplayListForScripting()`, a
  deliberately narrow escape hatch — see its doc comment in `Timeline.h` —
  for exactly this AVM1 use case, kept separate from `Timeline`'s normal
  tag-driven `DisplayList` updates) and create/destroy the corresponding
  `MovieClipInstance` child immediately, matching real synchronous clone/
  remove semantics.
- `SetTarget`/`SetTarget2` resolve an AS2 target path (slash-syntax
  `"/a/b"`/`".."`, or dot-syntax `"_root.a.b"`/`"a.b"`, both accepted by
  `MovieClipInstance::resolvePath()`) and change which clip subsequent
  target-less actions in the SAME script run affect.

`StartDrag`/`EndDrag` are real as of Phase 6: `HostBindings::startDrag()`
now takes a `DragOptions{lockCenter, hasConstraint, left, top, right,
bottom}` struct (previously the interpreter popped and discarded
`LockCenter`/`Constrain`/`L,T,R,B` — see the pop-order confidence note in
`Interpreter.cpp`, same "documented-but-unverified" caveat as `Extends`'
operand order). `MovieClipHostBindings::startDrag/endDrag` forward to
`ScriptEnvironment::startDrag/endDrag`, which tracks at most one dragged
clip; `ScriptEnvironment::updateDrag()` repositions it from `InputState`'s
current mouse position once per full-tree tick (called only from the ROOT
`MovieClipInstance::advanceFrame()`, so children ticking underneath don't
reapply it redundantly). Non-`lockCenter` drags preserve the grab offset
captured at `startDrag()` time; a constraint rectangle clamps the result.

### Input (`Key`/`Mouse`/`_xmouse`/`_ymouse`) — Phase 6

`ScriptEnvironment`'s constructor populates `_global` with:

- **`Key`** — a plain object (used AS2-statically, never `new Key()`):
  native `isDown(code)`/`getCode()` backed by `runtime::InputState`, plus
  the standard named constants (`Key.LEFT`, `Key.SPACE`, `Key.ENTER`, ...)
  as plain numeric properties. `Key.getAscii()` is NOT modeled (see "Known
  Phase 6 limitations").
- **`Mouse`** — `show()`/`hide()` are recognized no-ops (this runtime has no
  cursor rendering model — headless-first, 3DS-bound).
- `_xmouse`/`_ymouse` — wired both via `GetProperty`(20/21) and as bare
  `MovieClipInstance` native-get properties, both reading `InputState::
  mouseX()/mouseY()`.

`InputState` (`src/runtime/InputState.h/.cpp`) is a small host-settable bag
with no `avm1`/`runtime` dependency in either direction — a test harness or
the eventual desktop/3DS input backend calls `ScriptEnvironment::
inputState().setKeyDown(...)`/`setMousePosition(...)` before ticking
`advanceFrame()`.

### Sound (`Sound` object, `StartSound`/`DefineSound` tags) — Phase 6

`DefineSound` (14) and `StartSound` (15, via its `SOUNDINFO` record) are
parsed structurally (`src/swf/DefineSoundTag.h`/`src/swf/StartSoundTag.h`)
— header fields only (format/rate/size/type/sample count, loop count,
sync flags, in/out points, envelope points), no audio codec decode.
`CharacterDictionary` resolves `DefineSound` into its `CharacterDef`
variant exactly like `ShapeDef`/`SpriteDef`. `Timeline::
currentFrameStartSoundEvents()` exposes the current frame's `StartSound`
records; `MovieClipInstance::runCurrentFrameSounds()` dispatches each one
to `ScriptEnvironment::audioBackend()` (an `audio::IAudioBackend&` — see
`docs/architecture.md`'s module layout) — `playSound(soundId, loopCount)`
normally, or `stopSound(soundId)` if the record's `SyncStop` flag is set.

AVM1's `Sound` object (`new Sound()`, prototype methods `attachSound`/
`start`/`stop`/`setVolume`/`getVolume`) is wired the same way, with one
resolution gap: `attachSound(id)` only resolves when `id` is a **Number**
(treated directly as a `DefineSound` character ID — validated against
`CharacterDictionary` if bound, warned-and-still-stored if not found). Real
AS2's actual form, `attachSound(linkageName: String)`, can't be resolved
without `ExportAssets` tag parsing, which doesn't exist yet — calling it
with a String logs a warning and does nothing. `setVolume`/`getVolume`
round-trip through an own `_volume` property on the `Sound` instance (no
backend effect, since `NullAudioBackend` doesn't do anything with it
either way).

`WaitForFrame`/`WaitForFrame2`: simplified to "always considered loaded"
(never takes the skip branch) — correct once a real `Timeline` always has
already-parsed frame data available synchronously (true for this
runtime's design; streaming/progressive loading isn't a target use case).

`Call` (frame-as-subroutine): pops and discards its target; not wired.

`TargetPath`: pushes `""` (no scene graph to resolve a target path
against yet).

`GetURL`/`GetURL2`: parsed and logged; browser navigation is out of scope
entirely for a Nintendo 3DS runtime (no browser to navigate).

`ToggleQuality`: recognized no-op (rendering-quality control isn't
modeled). `StopSounds` (the legacy global "stop all sounds" action,
distinct from `Sound.stop()`) is still recognized/logged only — it isn't
wired to `IAudioBackend::stopAllSounds()` (low-priority: `Sound.stop()`
already covers the common per-instance case, and this action is rare in
practice).

### ExternalInterface (AS2 <-> native/host) — Phase 7

Real Flash's `ExternalInterface` bridges AS2 to browser JavaScript, with
values crossing a JS<->XML<->AS2 marshalling boundary. This runtime has no
browser — the eventual target is native Nintendo 3DS code in the SAME
process — so, as a deliberate documented simplification/improvement over
real `ExternalInterface` semantics, `avm1::Value` crosses the boundary
directly in both directions; there is no string/XML serialization step.

`ScriptEnvironment`'s constructor populates `_global` with an
`ExternalInterface` plain object (used AS2-statically, like `Key`/`Mouse`):

- **`available`** — always `true` (a plain Boolean property, not a getter —
  this runtime always has a host/native bridge available by construction).
- **`call(methodName, ...args)`** — AS2 -> native. Looks up `methodName` in
  a table of C++ functions registered ahead of time via
  `ScriptEnvironment::registerHostFunction(name, fn)` (`fn` is a
  `std::function<avm1::Value(const std::vector<avm1::Value>&)>`) and invokes
  it with `args`. Calling an unregistered name logs a warning and returns
  `undefined` rather than throwing — matches this codebase's "never crash on
  untrusted/malformed input" principle (`docs/architecture.md`'s design
  principles), applied here to "unexpected script behavior" rather than
  "malformed bytecode".
- **`addCallback(methodName, instance, function)`** — native -> AS2. Stores
  `function` (an AS2 `Function` value) bound to `instance` as `this` in a
  table keyed by `methodName`, via `ScriptEnvironment::registerCallback()`
  (private — only reachable through `addCallback`'s `nativeImpl`). Returns
  `true` if `function` is actually a callable Function value, `false`
  otherwise (matches real AS2's Boolean return, though real Flash's
  false-case reasons — e.g. browser refusing the registration — don't apply
  here).

Host/native code drives the native -> AS2 direction with two more
`ScriptEnvironment` methods, NOT exposed to AS2 itself:
`hasCallback(name)` (query whether a callback was registered) and
`invokeCallback(name, args)` (actually run it, via `Interpreter::
callFunction()` against a **fresh top-level `Scope`/`ExecutionContext` with
no `HostBindings` bound** — see "Known Phase 7 limitations" below for what
that means in practice). Calling `invokeCallback()` for a name that was
never registered logs a warning and returns `undefined`, same
graceful-degradation policy as `ExternalInterface.call()`.

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
- `tests/test_movieclip_instance.cpp` (Phase 5) — the interpreter actually
  driving a `MovieClipInstance` tree, as opposed to the isolated
  `test_avm1_*.cpp` tests above: `DoAction` on a clip's frame setting a
  variable on its scripting object; `GetProperty`/`SetProperty` AND
  `GetMember`/`SetMember` both round-tripping `_x` through the SAME
  underlying state; two clips with different frame counts advancing
  independently under repeated `advanceFrame()` ticks; `Stop`/`SetTarget`
  affecting only the intended clip, not the whole tree; `CloneSprite`
  creating a real new child at a new depth; `RemoveSprite` tearing one
  down; `_root`/`_parent` identity from a child's perspective; and
  `resolvePath()`'s relative/absolute/`_root.`/`..` path forms.
- `tests/test_character_dictionary.cpp` also gained a Phase 5 regression
  test: a character defined NESTED inside a `DefineSprite`'s own tag stream
  (legal per spec — the character dictionary is global across the file, not
  scoped per-sprite) now resolves correctly; this was a real bug caught
  while building the Phase 5 manual/CLI smoke test, not a hypothetical. Its
  Phase 6 addition: `DefineSound` resolves into the `CharacterDef` variant.
- `tests/test_avm1_interpreter.cpp` (Phase 6 additions) — a native function
  invoked via `CallFunction` (with args) and via `CallMethod` (verifying
  `thisVal` is the calling object), both using `avm1::makeNativeFunction()`
  directly against a raw `ExecutionContext` (no `MovieClipInstance`
  involved) to prove the interpreter-level mechanism in isolation.
- `tests/test_place_object_tag.cpp` (Phase 6 additions) — `ClipActionRecord`
  parsing: `Load`+`EnterFrame` records, a `KeyPress` record's `KeyCode`
  byte, and confirming `clipActions` stays empty when `HasClipActions` is
  unset.
- `tests/test_define_sound_tag.cpp` (Phase 6, new) — `DefineSound` header
  field parsing (format/rate/16-bit/stereo/sample count/data offset+length)
  and `SOUNDINFO` parsing (all optional fields, `SyncStop`).
- `tests/test_input_state.cpp` (Phase 6, new) — `InputState` in isolation:
  independent per-key down/up tracking, `lastKeyCode()`, mouse position and
  button state round-tripping.
- `tests/test_movieclip_instance.cpp` (Phase 6 additions) — `Key.isDown()`
  reading `InputState`; `_xmouse`/`_ymouse` via both `GetProperty` and bare
  `.member` access; `StartDrag` (`lockCenter` following the mouse each
  tick, stopping after `EndDrag`) and its constraint-rectangle clamping;
  `onClipEvent(load)` firing once at creation, `onClipEvent(enterFrame)`
  firing once per tick, `onClipEvent(unload)` firing on `removeFromParent()`
  (all via a `PlaceObject2` fixture built with `ClipActionRecord`s);
  `StartSound` tag dispatch (including `SyncStop`) and the AVM1 `Sound`
  object's numeric `attachSound`/`start` path, both verified against a spy
  `IAudioBackend` implementation local to the test file.
- `tests/test_avm1_interpreter.cpp` (Phase 7 additions) — `Interpreter::
  callFunction()` invoking a native function directly (verifying both
  `thisVal` and args reach it) and invoking a real scripted
  (`DefineFunction2`-built) AS2 function directly, confirming its body
  actually runs and its `Return` value comes back — both WITHOUT going
  through `CallFunction`/`CallMethod` bytecode dispatch.
- `tests/test_movieclip_instance.cpp` (Phase 7 additions) —
  `ExternalInterface.available` reads `true`; `ExternalInterface.call()`
  dispatching to a `registerHostFunction()`-registered native function with
  the right args and return value; `ExternalInterface.call()` on an
  unregistered name returning `undefined` gracefully; `ExternalInterface.
  addCallback()` registering an AS2 function that `hasCallback()`/
  `invokeCallback()` (native-side) can then find and run, round-tripping a
  value through it; `invokeCallback()` on an unregistered name returning
  `undefined` gracefully.

## Known Phase 5 limitations

- **A child's authored placement transform is applied only once, at
  creation.** A later frame's `PlaceObject2` "update in place" tag
  targeting an already-existing sprite instance's depth is NOT re-applied —
  only a genuine character replacement (or a brand-new placement) picks up
  a fresh transform. This deliberately trades a rarer fidelity gap (an
  author re-authoring a clip's position mid-timeline while it's ALSO
  independently script-driven) for a much more common one it avoids
  (script-set `_x`/`_y` being silently stomped back to the placement
  transform on every single tick). See `MovieClipInstance.h`'s file header
  for the full reasoning.
- **`_width`/`_height` are not computed** — always return 0 (would need a
  full recursive subtree bounding-box computation).
- **A clip removed via `RemoveSprite`/`removeMovieClip` mid-script leaves
  any `MovieClipHostBindings::current_`/raw pointers referencing it
  dangling** if the SAME script keeps running target-less actions
  afterward. Real content overwhelmingly follows `removeMovieClip(this);
  return;` (or simply doesn't reference the clip again), so this is a
  documented edge-case risk, not something actively guarded against.
- **`_xscale`/`_yscale`/`_rotation`** decompose the placement `MATRIX`
  assuming no independent (non-rotational) skew — exact for any transform
  actually produced by `_xscale`/`_yscale`/`_rotation` themselves, an
  approximation for a hand-authored skewed matrix (rare in practice).

## Known Phase 6 limitations

- **`onClipEvent` only dispatches `Load`/`Unload`/`EnterFrame`.**
  `ClipActionRecord`s are fully parsed (`swf::ClipEventFlag`, all 19
  documented bits — see the confidence note on `swf/PlaceObjectTag.h`) and
  stored per-`MovieClipInstance` (`clipActions_`), but `Mouse*`/`Press`/
  `Release`/`ReleaseOutside`/`RollOver`/`RollOut`/`DragOver`/`DragOut`/
  `KeyDown`/`KeyUp`/`Data`/`Initialize`/`Construct` are never fired — the
  mouse-related ones need hit-testing/bounds (which needs `_width`/
  `_height`, above), and none of button `on()`'s handlers exist since
  `DefineButton`/`DefineButton2` aren't parsed (Phase 8+).
- **`ClipActionRecord`'s bit layout is spec-derived, not independently
  verified** — same confidence caveat as `Extends`'/`StartDrag`'s operand
  order elsewhere in this doc: getting a rare handler's bit wrong would
  misfire/miss just that one handler type, never corrupt anything else
  (each flag is checked independently).
- **`Sound.attachSound()` only resolves a numeric ID** — the real AS2
  `String` linkage-name form needs `ExportAssets` tag parsing, which
  doesn't exist yet (see the Sound section above).
- **No audio codec decode** — `DefineSound`'s compressed sample data is
  never touched; `IAudioBackend` implementations decide entirely for
  themselves whether/how to play a given `soundId`. `NullAudioBackend` (the
  only implementation so far) always no-ops.
- **`Key.getAscii()` is not modeled** — only `isDown()`/`getCode()`/the
  named constants exist.
- **CloneSprite-created clips never carry `ClipActionRecord`s** —
  `MovieClipInstance::cloneSprite()` builds a synthetic `PlaceObjectRecord`
  with no `clipActions`, unlike real Flash (which clones the source
  character's handlers too). Low-priority: script-driven clones with
  `onClipEvent` handlers are an uncommon pattern.

## Known Phase 7 limitations

- **`invokeCallback()` runs with NO `HostBindings` bound.** It builds a
  fresh top-level `Scope`/`ExecutionContext` (not scoped to any particular
  `MovieClipInstance`), matching Phase 4's original "host-less no-op"
  precedent (`ExecutionContext::host == nullptr` logs at debug level and
  no-ops rather than crashing). Concretely: `GotoFrame`/`Play`/`Stop`/
  `GetProperty`/`SetProperty`/`CloneSprite`/`RemoveSprite`/`StartDrag`/
  `EndDrag`/`SetTarget` called DIRECTLY inside an `addCallback`-registered
  function's body are silently no-ops (`GetProperty` reads back
  `undefined`) — but ordinary computation, global-variable/member access,
  and calling other AS2 functions/objects (including ones that themselves
  close over and later act on a `MovieClipInstance`'s scripting `Object`,
  since THAT still works — only the `HostBindings`-mediated actions are
  affected) work normally. A callback that legitimately needs to touch the
  scene graph should currently be written to call a wrapper AS2 function
  defined on a specific clip's scope (invoked in the ordinary way, with a
  real `HostBindings` bound) rather than doing scene-graph actions directly
  in the callback body itself.
- **`ExternalInterface.call()`/`addCallback()` do no argument-count/type
  marshalling or validation beyond the bare minimum** (`addCallback`
  requires its 3rd argument to actually be a callable Function; everything
  else is passed through as-is). This matches the "Value crosses directly,
  no serialization" design choice, but also means a native host function
  registered via `registerHostFunction()` must defensively check
  `args.size()`/types itself, same as any native (Phase 6) built-in.
- **No linkage-based lookup** — `ExternalInterface` only supports the
  explicit `call`/`addCallback` API; there's no equivalent of a
  browser embed tag's `id` used to route calls between multiple SWF
  instances (not meaningful here: one `ScriptEnvironment` already IS one
  loaded movie's whole bridge).

## Next (Phase 8)

Text/Font/Button, per the project's phase-by-phase plan (see
`docs/architecture.md`): `DefineFont`/`DefineFont2`/`DefineText`/
`DefineEditText` parsing and (at least static) glyph rendering, and
`DefineButton`/`DefineButton2` parsing so button `on()` handlers and the
mouse-related `onClipEvent`s deferred since Phase 6 (`Press`/`Release`/
`RollOver`/... — see "Known Phase 6 limitations" above) have something to
hit-test against. Concrete scope to work out at the start of that phase.
