# Current-State Audit — flash3ds-runtime

**Date:** 2026-08-21. **Method:** every claim below was checked against the
actual current source tree (`git log` HEAD `6b44405` + the uncommitted
working-tree changes that were already sitting there when this audit
started), by reading the code, building it, running its tests, and running
a fresh instrumented profiler against real corpus files — not by trusting
this project's own prior docs, this session's own prior summaries, or the
audit request's stated "ground truth" figures. Every discrepancy found
between those sources and direct measurement is called out explicitly
below rather than silently resolved.

This is an **audit + roadmap deliverable**. Nothing in this document set
was implemented as part of producing it — see `docs/implementation-
roadmap.md` for what to do next, and stop there until a human says go.

---

## 0. Corrections to the audit request's own stated "ground truth"

Per the request's own methodology ("do not trust old summaries/docs...
unless verified against actual code"), these figures were explicitly
checked and found to differ from what was asserted when the audit was
requested:

| Claim | Asserted | Actually measured | Evidence |
|---|---|---|---|
| Test count | "353/353 passing" | **279/279 passing** | `./build/tests/flash3ds_tests` run fresh this turn, full log captured |
| MP3/`DefineSound` character count | "426" | **423** across the full 8-game + 23-sub-SWF corpus (264 in Hobo1-7, 2 in the Extreme Pamplona loader, 157 in its 23 content sub-SWFs) | `swf_diagnostic` run against every corpus file individually this turn (see §4) |
| "JPEXS MCP is already working" | asserted as fact | **Not connected.** `ToolSearch` for jpexs/ffdecmcp-specific tools returns zero matches — only generic Ghidra tools and MCP infrastructure tools exist in this session's tool set | Two independent `ToolSearch` calls, this session |
| Four VC titles (Pokemon Red VC, Super Mario World VC, Miitopia, Tetris Axis) available in Ghidra | implied available for RE | **Not loaded.** The only Ghidra project connected in this session is Shift-DX itself (`ram: 0x0-0x44ffff`, matching Shift-DX's known ~4.5MB size) | `ghidra__list_segments` |
| "the demo `.3dsx` I sent you is checksum-identical" (from an earlier instruction in this same conversation) | asserted | Was already found false in a prior turn of this same session (stale `build_3ds/flash3ds_3ds.3dsx`, different MD5) — carried forward here since it bears on any 3DS-build claims elsewhere | git/file inspection, prior turn |

None of these corrections are hostile nitpicking — they matter because
several of the audit's own required sections (test-driven confidence,
per-game sound requirements, VC/save/achievement research, JPEXS-sourced
findings) depend on them being right. Two, in particular, block real work
downstream: **JPEXS cannot contribute any findings to this audit** (there
is nothing to query), and **the VC-title research in §8/§9 below cannot be
performed against real decompiled data** — both are reported honestly as
gaps rather than faked.

## 0a. Also found: a second environment discontinuity, now resolved

Earlier in this session (before this audit was requested), the workspace
was found to have reverted to an earlier checkpoint than a previously
completed and delivered session's work (missing an MP3-decode phase and a
dynamic-MovieClip/`ExportAssets` phase that had been built, tested, and
delivered earlier). That discontinuity is **still true** — those two
phases are still absent from this tree (confirmed again this turn, see
§4/§6) — but it is no longer a blocking mystery: this audit's entire
premise is to treat the **current tree, whatever it is, as ground truth**,
which is what the rest of this document does. Re-litigating what happened
to the lost session state is out of scope; rebuilding what it produced
(MP3 decode, dynamic MovieClips, `ExportAssets`) is explicitly **in**
scope for the roadmap in §11 / `docs/implementation-roadmap.md`, since
both are independently identified as high-value from this fresh audit too.

**Also newly discovered this turn, not previously known:** the current
tree contains a **complete, working, tested button-event dispatcher**
(`ScriptEnvironment::dispatchPointerEvents`/`fireButtonCondition`/
`firePropertyHandler`, `MovieClipInstance::dispatchButtonKeyPressesRecursive`)
that is *further along* than either this project's own committed docs or
the prior session summary indicated — see §3 and §5. It exists only in the
uncommitted working tree (`git status` shows `ButtonInstance.{h,cpp}` and
`MovieClipInstance.{h,cpp}` as modified, plus a new, passing
`tests/test_event_dispatch.cpp`), was never committed, and
`docs/known-limitations.md`'s own (also uncommitted) text still describes
it as "not implemented or even prototyped." **The code is real and
tested; the documentation describing it is stale.** This is the single
biggest "incorrectly documented" finding of this audit — see §5.

---

## 1. What this project actually is (frequently mis-stated — restated for clarity)

Per `/home/claude/flash3ds-runtime/CLAUDE.md`'s own hard rules: this is a
**clean-room, independently implemented** SWF/AVM1 runtime. It is a
*separate* project from the Shift-DX Ghidra reverse-engineering work
(tracked in the top-level `/home/claude/CLAUDE.md`). No code is ever
copied from the Shift-DX binary or the gameswf library it embeds; Ghidra
findings are used **only** as a behavioral cross-check, recorded in
`docs/shift-dx-behavior.md`.

This matters directly for the audit's Section 6 request ("Ghidra
`FUN_xxxxx` → runtime-component mapping"): **there is no such mapping to
produce**, because this codebase's functions were never derived from
Shift-DX's decompiled functions in the first place. What *does* exist is
a smaller, honest set of "this Ghidra finding informed/validated this
design decision" cross-references — see `docs/reverse-engineering-map.md`
for the full accounting (it is short, because the RE work's own
`shift-dx-behavior.md` says most of it plainly: the AVM1 opcode dispatcher
and the native `Key.isDown`/sound-loader bindings were never successfully
resolved in Ghidra in the first place, di the RE session's own "Open items"
list).

## 2. Git/build ground truth

- HEAD: `6b44405` ("Interactivity phase: ButtonInstance runtime for placed
  Button2 (sub-fix 5/N)"), branch `master`.
- Uncommitted modifications: `CMakeLists.txt`, `docs/compatibility-
  matrix.md`, `docs/known-limitations.md`, `docs/test-results.md`,
  `src/runtime/ButtonInstance.{h,cpp}`, `src/runtime/MovieClipInstance.
  {h,cpp}`, `tests/CMakeLists.txt`.
- Untracked (new files, never committed): `docs/real-game-compatibility.md`,
  `tests/games/` (full 8-game corpus manifests+diagnostics), `tests/
  test_event_dispatch.cpp`, `tools/real_game_harness/`, `tools/
  swf_diagnostic/`.
- **All of the above compiles clean** (`cmake --build build -j`, zero
  errors, zero new warnings beyond what already existed) and **all tests
  pass**: `279 passed, 0 failed, 279 total` (verified this turn — see §0).
- This represents at least two substantial, real, well-documented (via
  in-code comments, not just external docs) engineering phases that were
  never `git commit`-ed: a "ButtonInstance event-dispatch phase" (2026-08-19)
  and a "real-game-corpus phase" (2026-08-18). **Recommendation, not yet
  acted on:** commit this working tree before doing anything else — see
  Phase 0 of `docs/implementation-roadmap.md`. Losing this exact state
  again (as already happened once this session, per §0a) would be a real
  regression, not a inconvenience.

## 3. Subsystem status — verified, not asserted

| Subsystem | Status | Evidence |
|---|---|---|
| SWF load/decompress (FWS/CWS) | Implemented | `src/swf/SwfLoader.*`; `mem_profile_check` run this turn loads all 8 real corpus files successfully |
| Tag scan / `TagDispatcher` | Implemented for all tags this codebase recognizes; several real tags (`DefineShape4`, `PlaceObject3`, `CsmTextSettings`, two truly-unrecognized tag IDs 253/255 seen in Extreme Pamplona) are **not** recognized/parsed at all | `docs/real-game-compatibility.md` (this turn's own re-derivation, §4, agrees) |
| Display list / Timeline | Implemented, frame-accurate playhead per clip | `src/runtime/Timeline.*`, `DisplayList.*`; exercised by `mem_profile_check`'s `advanceFrame()` loop against real files without incident |
| Shape rendering | Implemented for `DefineShape`/`2`/`3`; `DefineShape4` and `DefineMorphShape`/`2` are parsed as *unrecognized* (shape4) or *recognized-but-not-resolved* (morph — 19-27 characters per Hobo file, confirmed real and simply invisible) | `docs/compatibility-matrix.md`, `docs/real-game-compatibility.md` |
| AVM1 interpreter (opcodes) | Implemented for the full opcode set the public SWF spec defines; **every opcode actually used by any of the 8 real corpus games has a real `case`** — confirmed by direct opcode-profile scan, not assumed | `docs/real-game-compatibility.md`'s per-game opcode sections |
| AVM1 built-ins (`GlobalObject`) | **`GlobalObject::create()` is a one-line stub — literally `return std::make_shared<Object>();`. Zero named built-ins.** No `Math`, `Date`, `Number()`, `String()`, `Boolean()` anywhere. | Read directly this turn: `src/avm1/GlobalObject.cpp` line 5 |
| `MovieClip` OOP method surface | Partial — only the specific methods a real failing script needed were ever added (`stop`/`play`/`nextFrame`/`prevFrame`/`gotoAndStop`/`gotoAndPlay`/`getBytesLoaded`/`getBytesTotal`, plus `_x`/`_y`/`_xscale`/`_yscale`/`_visible`/`_width`/`_height`/`_currentframe`/named-child access, `startDrag`/`stopDrag`, `hitTest`). **`swapDepths`, `duplicateMovieClip`, `attachMovie`, `createEmptyMovieClip`, `loadMovie`/`unloadMovie`, `getNextHighestDepth`, drawing API (`beginFill`/`lineTo`/…) are all absent.** | `grep` this turn found zero matches for `createEmptyMovieClip`/`loadMovie`/`attachMovie`/`ExportAssets` anywhere in `src/` |
| Button runtime + hit-testing | Implemented (`ButtonInstance`, bounding-box hit-testing, UP/OVER/DOWN state machine) | Committed, Phase-"Interactivity" work |
| **Button/clip event dispatch** (`onPress`/`onRelease`/`onRollOver`/`onRollOut` property handlers, `DefineButton2` `condActionsV2`, `CondKeyPress`) | **Implemented and wired into the per-tick root `advanceFrame()` call, with 18 passing dedicated tests** — this is UNCOMMITTED and the project's own docs (also uncommitted) still call it "not implemented" | `src/runtime/MovieClipInstance.cpp:1392-1393` calls `env_->dispatchPointerEvents(...)` and `dispatchButtonKeyPressesRecursive()` every root tick; `tests/test_event_dispatch.cpp`, all 18 cases passing in the 279/279 run |
| `onClipEvent` (the 15 mouse/key clip-event flags beyond Load/Unload/EnterFrame) | Not confirmed dispatched — `docs/onclipevent-compatibility.md` should be treated as the authority here; not independently re-verified this turn beyond confirming `onPress`/`onRelease`/`onRollOver`/`onRollOut` *button/clip-property-handler* dispatch (a related but distinct mechanism) is real | See table above |
| Sound tag parsing (`DefineSound`/`StartSound` headers) | Implemented (structural only) | `src/swf/DefineSoundTag.*` |
| Sound codec decode (MP3/ADPCM/…) | **Not implemented anywhere.** `Nintendo3DSAudioBackend::playSound()` has real `ndsp` channel-reservation plumbing but nothing to feed it — no decode step exists in this codebase at all | Read directly this turn: `src/audio/Nintendo3DSAudioBackend.{h,cpp}`, `src/audio/NullAudioBackend.cpp` |
| Multi-SWF / `loadMovie` / `_level` | **Not implemented at all.** `ExportAssets` (tag 56) is recognized by the `TagCode` enum only — no parser, no linkage-name resolution, no dynamic instantiation | `grep` this turn, zero hits in `src/` for any of these |
| 3DS backend (renderer/input/audio plumbing, dual-screen) | Implemented and toolchain-verified to compile/link; **boot on real Azahar/hardware has been user-confirmed once** for an earlier, single-screen build; the current dual-screen extension has **not** been separately re-confirmed running | `docs/3ds-toolchain.md` |
| Memory (peak RSS while loading a real game) | **~145-150MB peak for `hobo.swf` alone, confirmed reproduced fresh this turn against the actual current code** — dominant cost is `CharacterDictionary::build()` | §4 below, `docs/memory-audit.md` |

## 4. Memory — re-measured fresh, not reused

A `tools/mem_profile_check/main.cpp` diagnostic (previously written in a
now-lost session state, and explicitly **not** trusted — rewritten from
scratch this turn against the actual current headers/APIs) was compiled
against the real current `libflash3ds_core.a` and run against real corpus
files. Full breakdown, per-game comparison, and analysis: `docs/memory-
audit.md`. Headline: `hobo.swf` peaks at **149.2MB RSS** (**+145.6MB**
over process baseline), with **90%+ of that growth in one call** —
`CharacterDictionary::build()` — confirming the previously-reported
"145MB, localized to `CharacterDictionary::build`" finding is **real,
reproducible, and still true of the current code**, not a stale number
from a different checkpoint. `hobo5.swf` (the largest/most content-dense
Hobo file) peaks at **453.8MB** — over 3x hobo1, tracking character count
(8,626 vs 3,575) slightly super-linearly. Extreme Pamplona's main loader
alone (no sub-SWF loading exists to pull in its 23 content files) peaks at
a comparatively tiny **14.8MB**.

## 5. Corrections to this project's own prior documentation

Per the audit's explicit "identify incorrectly-documented" requirement:

1. **`docs/known-limitations.md`'s "priority #2: button event dispatch"
   entry (both the committed version and its own further uncommitted
   diff) is stale.** It says the two dispatch mechanisms it identifies
   (`condActionsV2` and `onPress`/`onRelease` property handlers) were
   "not implemented or even prototyped this phase." **They are
   implemented, wired in, and tested** (see §3 table). This is the single
   most consequential documentation gap found — it means the actual
   blocker priority order is different from what every doc in this repo
   currently states. See `docs/known-limitations.md` (rebuilt as part of
   this audit) for the corrected priority list.
2. **`docs/real-game-compatibility.md`'s own "Runtime compatibility
   status" tables** (written 2026-08-18, one day before the event-dispatch
   work) list `condActionsV2` dispatch and `onPress`/`onRelease` dispatch
   as "MISSING" for every game. Given finding #1, those specific rows are
   now stale too — the rest of that document (tag/opcode/AS2-API corpus
   data) remains accurate and was independently re-spot-checked this turn
   (§0's MP3 count cross-check).
3. **Nothing else in this project's docs was found to overclaim.** In
   particular `docs/audio.md`/`docs/known-limitations.md`'s "no codec
   decode exists" claim, `docs/compatibility-matrix.md`'s `GlobalObject`
   stub claim, and `CLAUDE.md`'s "loadMovie/attachMovie/
   createEmptyMovieClip not implemented" claim were all independently
   re-verified against source this turn and found **accurate**. This
   project's documentation discipline is otherwise good — the one gap is
   a matter of an uncommitted phase outrunning its own doc updates, not a
   pattern of inflated claims.

## 6. Real-game readiness (see `docs/real-game-readiness.md` for the full matrix)

All 8 corpus games (Hobo 1-7 + Extreme Pamplona) **load, parse, and
render frames 1-5 with zero crashes** (confirmed via
`tools/real_game_harness/run_harness.sh`, baseline recorded 2026-08-18,
not re-run this turn since nothing renderer-affecting changed — re-running
it is folded into Phase 1 of the roadmap). Given the event-dispatch
finding in §5, **the single most important open question this audit could
not answer from static analysis is whether Hobo's title-screen PLAY button
now actually advances past frame 13 when clicked**, since the dispatcher
that would make that happen exists and is unit-tested but has never been
exercised against real game content end-to-end. This is the highest-value,
lowest-effort next step and is Phase 1 of the roadmap.

## 7. Ghidra / JPEXS correlation — honest accounting

- **Ghidra: live, connected, correct binary** (`ram: 0x0-0x44ffff`,
  Shift-DX). Existing cross-references live in `docs/shift-dx-behavior.md`
  (read in full this turn — summarized in `docs/reverse-engineering-map.md`).
  Its own "Open items" section already discloses that the AVM1 opcode
  dispatcher, `Key.isDown()`, and the sound tag-loader bindings were never
  successfully resolved — so Ghidra cannot currently answer new questions
  about those subsystems without further RE work in the Ghidra GUI (raw
  memory reads for the relevant `DAT_` literal-pool words — a known,
  previously-documented blocker in the top-level `/home/claude/CLAUDE.md`).
- **JPEXS/ffdecmcp: not connected in this session.** No tool of that
  description exists in this session's tool set (`ToolSearch`, twice,
  zero matches). **Every "JPEXS-evidence" field required elsewhere in this
  audit's deliverables is filled with "not available this session," not
  fabricated.**
- **VC titles (Pokemon Red VC, Super Mario World VC, Miitopia, Tetris
  Axis): not loaded.** Ghidra is pointed at Shift-DX only. §8/§9's
  save-system/achievement-system/VC-architecture research **cannot be
  performed against real decompiled data this session** — see `docs/
  implementation-roadmap.md`'s note on this, and §9 below.

## 8. Save system / achievement system / VC architecture — explicitly NOT researched this session

Per the audit's own instruction ("do not assume evidence exists... must be
proven") and the honest accounting in §7: **no save-system or
achievement-system findings are reported, because no RE evidence was
available to derive them from.** Fabricating a design "informed by RE
evidence" that doesn't exist would violate the audit's own ground rules
more seriously than simply reporting the gap. If this research is still
wanted, it requires either (a) the user switching the connected Ghidra
project to one of the four VC titles, or (b) treating a save/achievement
design as a **pure forward-looking API design exercise with zero RE
grounding**, clearly labeled as such. Neither was done here. The dual-
screen (top=game, bottom=UI) architecture concept and the Old-vs-New-3DS
capability-layer question are lower-risk to sketch without RE grounding
(they're informed by publicly documented libctru/3DS hardware facts, not
by any specific game's internals) — a first-pass sketch of both is
included in `docs/implementation-roadmap.md`'s "Deferred/design-only"
section, explicitly marked as design-only per the instruction not to
implement either yet.

## 9. Priority reassessment

The audit request's own priority order (P0 real-SWF-testing blockers, P1
memory/stability, P2 sound, P3 AVM1/AS2, ...) is **still the right shape**,
but §5's finding changes what P0 concretely means: the highest-leverage,
lowest-effort next action is not "implement event dispatch" (already
done) but **"prove it actually works end-to-end against a real game and
fix whatever it surfaces"** — plus committing the current working tree so
this doesn't get lost a second time. See `docs/implementation-roadmap.md`
Phase 0/1 for the concrete plan, and the final chat summary for the full
A-P breakdown.

## 10. Documents produced by this audit

- `docs/current-state-audit.md` — this document.
- `docs/known-limitations.md` — rebuilt in place with the corrected
  priority list and the full per-item schema requested.
- `docs/memory-audit.md` — full checkpoint tables, per-game comparison,
  PROVEN/INFERRED/UNKNOWN grading.
- `docs/reverse-engineering-map.md` — honest Ghidra-cross-reference
  accounting (short, per §1/§7).
- `docs/real-game-readiness.md` — per-game matrix, synthesized from
  `docs/real-game-compatibility.md` plus this audit's event-dispatch
  correction.
- `docs/implementation-roadmap.md` — multi-phase plan, Phase 0-2 detailed
  to the full requested template, later phases scoped at lower detail.
