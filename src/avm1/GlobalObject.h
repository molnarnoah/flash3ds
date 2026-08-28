// GlobalObject.h
//
// Constructs the AVM1 global object — AS2's `_global`, and (in Phase 4,
// before AVM1 is wired into the scene graph) also the outermost scope for
// a top-level script's variables. Originally deliberately minimal (Phase 4
// didn't need a populated built-in library since NewObject special-cases
// the handful of constructor names ["Object", "Array"] it supports without
// needing them registered here).
//
// Roadmap Phase 8 (2026-08-25) added the first real built-in: `Math`.
// Scoped strictly to real corpus evidence, per this project's evidence-
// before-implementation convention (CLAUDE.md; see the Phase 5/Phase 7
// precedents) — a static disassembly pass (`tools/real_game_harness/
// avm1_loader_disasm.cpp`, keyword-filtered for Math/String/Number/
// Boolean/Date) across all 8 real corpus games plus the standalone
// hobo.swf copy found ONLY `Math.random()`/`Math.ceil()` calls (the
// classic `Math.ceil(Math.random() * n)` random-integer idiom, in
// hobo2/hobo3/hobo5/hobo6/hobo7 — see docs/avm1-compatibility.md's Phase 8
// section for the full per-game count) and ZERO CallFunction/NewObject
// hits for String/Number/Boolean/Date as global constructors anywhere in
// the corpus. `String`/`Number`/`Boolean`/`Date` are therefore
// DELIBERATELY NOT implemented this phase — same reasoning as Phase 6's
// "don't build against a hypothetical" precedent — and should only be
// added once real corpus evidence (or a specific failing script) shows
// they're needed. `floor`/`round`/`abs`/`min`/`max`/`sqrt`/`pow`/`PI`/`E`
// are included alongside the two evidenced methods anyway: they're the
// same shape of trivial, stateless, zero-RAM-cost native function as
// `ceil`/`random`, cost nothing to add, and match the roadmap's own "at
// minimum" baseline set for a `Math` implementation — but `ceil`/`random`
// are the only two with an actual traced call site in this corpus.
//
// Task #67 (2026-08-27) added a narrow `String` — NOT because of new
// corpus evidence of a real call site (there still isn't one — see
// docs/known-limitations.md's L6 addendum), but as a falsifiable test of
// that addendum's specific hypothesis: that Extreme Pamplona's main file's
// 126 anonymous, always-empty-name `CallFunction` calls are an obfuscated
// name-builder using `String.fromCharCode`/`.charAt`/`.charCodeAt`/
// `.substr` that this runtime previously couldn't resolve at all. Only
// `fromCharCode` (a static method on the `String` object below) plus the
// instance methods `charAt`/`charCodeAt`/`substr` (special-cased directly
// against string primitives in Interpreter.cpp's CallMethod — see
// tryStringPrimitiveMethod()'s doc comment there) were added — exactly the
// set the addendum named, nothing broader. `Number`/`Boolean`/`Date`
// remain out of scope; so does `new String(...)` (a real boxed String
// object) — only the specific static/instance surface above.
//
// Later phases can extend GlobalObject::create() to seed more built-ins
// without changing callers.

#pragma once

#include <memory>

#include "avm1/Value.h"

namespace flash3ds::avm1 {

class GlobalObject {
public:
    static std::shared_ptr<Object> create();
};

}  // namespace flash3ds::avm1
