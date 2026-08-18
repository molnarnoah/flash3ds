# tests/runtime/

**Compatibility-audit phase (2026-08-18).** Reserved for whole-runtime
diagnostics/harnesses that don't fit `tests/test_*.cpp`'s per-subsystem unit
style — e.g. a future configurable-debug-level diagnostic run (`[SWF]`,
`[TAG]`, `[AVM1]`, `[DISPLAY]`, `[AUDIO]`, `[INPUT]`, `[EXTERNAL]` tagged
logging, per the audit charter's "real Hobo test" requirements) that walks
a real content file end-to-end and records: does it load, is parsing
complete, what was the last successfully processed tag, is initialization
complete, does the first frame execute, does AVM1 start, does rendering
start, does audio initialize, does input work, does it freeze or crash.

**Currently empty.** The project's existing `flash_runtime` CLI
(`tools/flash_runtime/main.cpp`) already provides `--quiet`
(tag-by-tag trace), `--timeline`, and `--render <frame> <out.ppm>` — this
phase's real-content verification work (see `docs/known-limitations.md`
priority #1's before/after `hobo.swf` byte-diff) used that CLI directly
rather than a bespoke harness, and it was sufficient for a single targeted
fix. A dedicated harness belongs here once a limitation needs the fuller
diagnostic-level tracing described above (most likely once Priority #2 —
button/mouse interactivity — is picked up and `hobo.swf`'s gameplay frames
become reachable for the first time, per `docs/compatibility.md`'s "Not yet
tested" list).
