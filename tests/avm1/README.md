# tests/avm1/

**Compatibility-audit phase (2026-08-18).** Reserved for standalone AVM1
bytecode fixtures/scripts that need to be exercised outside the existing
`tests/test_avm1_*.cpp` unit suite (e.g. a real `.swf` containing only a
`DoAction` tag, for CLI-level `--timeline`/trace verification, as opposed
to the in-memory bytecode assembler `tests/Avm1TestFixtures.cpp` already
uses for the 30+ `Interpreter_*` unit tests).

**Currently empty — no file-level gap identified yet that isn't already
covered by `tests/test_avm1_interpreter.cpp`/`test_avm1_scope.cpp`/
`test_avm1_value.cpp`** (189/189 passing; see `docs/avm1-compatibility.md`
for the full per-opcode status this suite backs). The two concrete
candidates flagged during this phase's audit that would belong here once
picked up:

1. A real `.swf` reproducing the dynamic-property-name pattern
   (`_root["color" + x]`) — see `docs/compatibility-matrix.md` §4, flagged
   NOT TESTED (not necessarily broken — `GetMember`/`SetMember` bytecode
   already handles arbitrary computed string keys generically — just never
   independently exercised against this exact real-world pattern).
2. A real `.swf` exercising `Math.*`/`Date`/`Number()`/`String()`/
   `Boolean()` — see `docs/known-limitations.md` priority #3 — expected to
   FAIL until that limitation is picked up; useful as a standing repro once
   it's created.
