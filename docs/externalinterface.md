# ExternalInterface

**Correction (2026-08-18, compatibility-audit phase): this file was stale
and factually wrong.** It previously read "Status: not started. Planned for
Phase 7" — but Phase 7 actually shipped a real, working `ExternalInterface`
back on 2026-08-18 (same calendar day, an earlier session), and this file
was simply never updated afterward. Ground-truth-traced against the current
source (not assumed from any doc, including this one) during the
compatibility-audit phase: `ScriptEnvironment`'s constructor
(`src/runtime/MovieClipInstance.cpp:189-214`) really does construct and
install a real `ExternalInterface` object onto `_global`, with `available`
(always `true`), `call(methodName, ...args)` (AS2 -> native, dispatches to
`callHostFunction`/`registerHostFunction`-registered C++ functions), and
`addCallback(methodName, instance, function)` (native -> AS2, via
`registerCallback`/`hasCallback`/`invokeCallback`) all real and covered by
passing tests (`tests/test_movieclip_instance.cpp`'s five
`MovieClipInstance_ExternalInterface_*` cases).

**This is exactly the kind of doc-vs-code drift the compatibility-audit
phase's charter warns about** ("Do not claim something is supported merely
because a class/function exists... trace the actual execution path" — and
the inverse failure mode is just as real: don't claim something is
*unsupported* because an old doc says so, either). This file is being fixed
rather than deleted so the drift and its correction are visible to a future
session, matching this project's practice of documenting mistakes/fixes in
place (see `docs/compatibility.md`, `docs/known-limitations.md`).

**For the actual, current, accurate ExternalInterface status**, see
`docs/avm1-support.md`'s "ExternalInterface (AS2 <-> native/host) — Phase 7"
section and `docs/compatibility-matrix.md`'s ExternalInterface row — both
independently re-verified against source during the compatibility-audit
phase, not just carried forward from Phase 7's original narrative.

Known real gaps (from `docs/avm1-support.md`'s "Known Phase 7 limitations",
re-confirmed during this phase's audit): `invokeCallback()` runs with no
`HostBindings` bound (scene-graph-affecting actions called directly inside
an `addCallback`-registered function's body are silent no-ops); no
argument-count/type marshalling beyond the bare minimum; no linkage-based
multi-instance routing (not meaningful here — one `ScriptEnvironment` already
is one loaded movie's whole bridge). None of these were exercised against
the exact Hobo-style pattern (`ExternalInterface.addCallback("SetUnlockedBonusIndex",
this, SetUnlockedBonusIndex)`, `ExternalInterface.call("OnBonusCancel")`,
etc.) with a real content SWF yet — see `docs/test-results.md`'s
ExternalInterface section for what's actually been run.
