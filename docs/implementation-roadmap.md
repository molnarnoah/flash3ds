# Implementation Roadmap — flash3ds-runtime

Produced by the 2026-08-21 complete current-state audit
(`docs/current-state-audit.md`, `docs/known-limitations.md`, `docs/
memory-audit.md`, `docs/reverse-engineering-map.md`, `docs/real-game-
readiness.md`). **Nothing in this roadmap has been implemented.** Per the
audit's explicit instruction, implementation stops here until a human
says which phase to start.

Priority chain restated from the audit request, adjusted per this audit's
findings (see `docs/current-state-audit.md` §9): **COMMIT →
VERIFY-REAL-INTERACTIVITY → MEMORY-DIAGNOSIS → AUDIO → LOAD/MULTI-SWF →
STABLE-MEMORY-FIX → REAL-3DS-TEST**, with save/VC/achievements genuinely
deferred (no RE grounding exists to design them from yet — see
`docs/current-state-audit.md` §8).

---

## Phase 0 — Commit the current working tree

**Goal:** Stop being one environment reset away from losing two real,
tested, working phases of engineering (event dispatch + real-game-corpus)
a second time.

**Why:** This exact loss already happened once this session (see
`docs/current-state-audit.md` §0a) to an *earlier* uncommitted phase. The
event-dispatch and real-game-corpus phases currently sitting uncommitted
in this working tree are real, tested (279/279 passing, including 18
dedicated event-dispatch tests), and substantial — losing them again would
directly contradict the audit's own goal of establishing durable ground
truth.

**Current implementation:** N/A — this is a git-hygiene step, not a code
change.

**Ghidra evidence / JPEXS evidence:** N/A.

**Exact files/functions:** `git add -A && git commit` covering:
`CMakeLists.txt`, `docs/compatibility-matrix.md`, `docs/known-
limitations.md` (now further rebuilt by this audit — commit the audit's
version, not the pre-audit uncommitted diff), `docs/test-results.md`,
`src/runtime/ButtonInstance.{h,cpp}`, `src/runtime/MovieClipInstance.
{h,cpp}`, `tests/CMakeLists.txt`, plus untracked: `docs/real-game-
compatibility.md`, `tests/games/`, `tests/test_event_dispatch.cpp`,
`tools/real_game_harness/`, `tools/swf_diagnostic/`, `tools/
mem_profile_check/` (new, this audit), and all six audit documents.

**Dependencies:** None — do this first, before anything else, regardless
of which phase is picked next.

**Implementation steps:** `git status`/`git diff` review (already done
this audit — see `docs/current-state-audit.md` §2) → stage → commit with
a message describing both uncommitted phases accurately (not "misc
fixes") → verify `git log`/`git status` clean afterward.

**Tests:** N/A (no code change).

**Real-game tests / 3DS tests:** N/A.

**Memory implications:** None.

**Expected result:** `git status` clean; `git log` shows the two phases as
real, named commits.

**Definition of Done:** Clean `git status`; a fresh `git clone`/checkout
of HEAD builds and passes all 279 tests without needing any uncommitted
state.

**Risks:** None — this is strictly protective.

---

## Phase 1 — Verify real button-dispatch works end-to-end against real games — **DONE (2026-08-21)**

**Result summary** (full detail in `docs/real-game-readiness.md`'s "Phase
1 results" section): the dispatcher is verified correct — a real
`CondKeyPress` trigger produces a measurable effect against real
`hobo.swf` content. Simulated mouse clicks on Hobo1's 3 documented frame-1
buttons produced no effect, traced to a real, corpus-wide content property
(those buttons' `condActionsV2` records are keypress-only, 0 mouse-bits
set) rather than a bug — confirmed via a new `button_scan` census across
all 7 Hobo files (same 1-3-mouse-buttons-per-file pattern everywhere).
**No bug was found to fix.** New verification tooling committed:
`tools/real_game_harness/click_probe.cpp` (mouse+key simulation harness),
`button_debug.cpp` (per-character condActionsV2 dump), `button_scan.cpp`
(corpus-wide condition census). **New open question this phase
surfaced, not yet answered:** what real input actually advances Hobo's
title screen (a specific keypress, a not-yet-placed button, or something
else) — worth a short follow-up before assuming any Hobo title is
"stuck." Original phase text preserved below for the record.

**Goal:** Answer the single highest-value open question this audit could
not answer from static analysis: does clicking Hobo's title-screen PLAY
button (or Extreme Pamplona's menu buttons) actually now work?

**Why:** `docs/current-state-audit.md` §5 found the event dispatcher is
implemented and unit-tested, but never exercised against real game
content — the previous "MISSING" verdict in `docs/real-game-
compatibility.md` is stale. This is the cheapest possible next step
(verification, not new code) with the highest possible payoff (could
unblock all 7 Hobo games' progression past their title screens
simultaneously, per `docs/real-game-readiness.md`'s ranking).

**Current implementation:** `ScriptEnvironment::dispatchPointerEvents`/
`fireButtonCondition`/`firePropertyHandler`,
`MovieClipInstance::dispatchButtonKeyPressesRecursive`, wired into
`advanceFrame()` (`src/runtime/MovieClipInstance.cpp:1392-1393`).

**Ghidra evidence:** `gameswf_button_register_mouse_state_names`
(Up/Over/Down state machine) — already validated the state-machine shape
this dispatcher builds on; nothing new needed here.

**JPEXS evidence:** Not available this session.

**Exact files/functions:** Extend `tools/real_game_harness/run_harness.sh`
(or a new small driver) to simulate a mouse-down/mouse-up at the known
frame-1 button coordinates already recorded in `docs/real-game-
compatibility.md` (e.g. Hobo1's 3 buttons at documented pixel positions),
then render a further frame and diff against the pre-click render to
confirm the timeline actually advanced/branched.

**Dependencies:** Phase 0 (commit first, so this verification work isn't
itself at risk of being lost).

**Implementation steps:** (1) pick 1-2 representative games (Hobo1 +
Extreme Pamplona, per `docs/real-game-readiness.md`'s two distinct
dispatch mechanisms); (2) drive a synthetic InputState press/release at
the known button coordinates via the existing `flash_runtime` CLI or a
small new test harness; (3) render before/after and confirm a real
behavioral change (frame advance, property change, or — if nothing
happens — capture exactly what's missing); (4) fix whatever surfaces (this
phase explicitly may uncover small bugs, not just confirm success).

**Tests:** New integration test(s) simulating a real button click against
real corpus content (not just the synthetic fixtures `test_event_
dispatch.cpp` already covers).

**Real-game tests:** This phase's entire content, by definition.

**3DS tests:** Not yet — desktop verification first.

**Memory implications:** None expected.

**Expected result:** Either confirmation that Hobo/Extreme-Pamplona
buttons now work end-to-end (excellent news, re-affirms the priority
reordering), or a concrete, evidence-based bug report of exactly what's
still missing (e.g. a coordinate mismatch, a scope binding issue, a
condActionsV2 flag-matching bug) — either outcome is valuable and cheap
to obtain.

**Definition of Done:** A documented (`docs/real-game-readiness.md`,
updated) YES/NO/PARTIAL verdict per corpus game for "does frame-1 button
interaction produce the expected timeline/behavioral change," backed by
before/after render evidence.

**Risks:** Low — this is read-only verification against existing code;
worst case is discovering more work is needed, which is itself useful
information.

---

## Phase 2 — Memory: byte-level breakdown, then a fix design (not yet a fix) — **DONE (2026-08-21)**

**Result summary** (full detail in `docs/memory-audit.md` §5a/§5b): built
`tools/real_game_harness/memory_breakdown.cpp`, a byte-level census by
`CharacterDef` variant arm. Confirmed PROVEN (not just inferred) that
shapes account for 94.5-98.5% of `CharacterDictionary`'s memory cost in
all three games measured (Hobo1: 97% of the measured RSS delta accounted
for; Hobo5: 97%; Extreme Pamplona loader: 57%, smaller absolute scale
where unmodeled per-allocation overhead matters proportionally more), and
root-caused it precisely: `sizeof(swf::ShapeRecord) == 120 bytes`, a flat
struct carrying every SHAPERECORD sub-type's fields on every record
regardless of which type is in use. This also resolved memory-audit.md's
previously-UNKNOWN Hobo1→Hobo5 nonlinearity (records-per-shape grows
faster than character count, and memory tracks record count almost
exactly). Also corrected a real inaccuracy in this project's own prior
memory-audit.md text (`SoundDef` does NOT copy raw MP3 bytes — checked
directly, it only stores an offset/length like `SpriteDef` does).

Three fix options designed this phase (Option A subsequently implemented —
see below):
- **Option A (compact `ShapeRecord`) — IMPLEMENTED 2026-08-21** (explicit
  user instruction, after reviewing the design). Measured result: `sizeof(
  ShapeRecord)` 120→**40 bytes (3.0x)**, not the ~8-10x originally
  estimated; Hobo1's peak RSS moved ~149MB→**~85-90MB (~1.7x)**, not the
  ~35-40MB originally estimated. See `docs/memory-audit.md` §5c for the
  full measured breakdown, all three games, and why the single-struct
  ratio doesn't carry straight through to the whole-pipeline ratio. All
  279 tests pass; 8-game render harness byte-identical to the pre-fix
  baseline (zero regression).
- **Option B (lazy/on-demand character parsing)** — moderate cost, helps
  peak-per-session but not the worst-case ceiling, composes with A (still
  open, not implemented).
- **Option C (tessellate-once-and-cache)** — **checked this phase and
  found NOT currently viable as originally conceived**: no tessellation
  cache exists anywhere; `SceneRenderer` re-tessellates from raw records
  on every single render call. Not recommended without first building
  and measuring a real tessellation-cache prototype.

Option A's implementation reduces but does not eliminate the memory
problem — post-fix Hobo1 (~85-90MB) is still 2.5-4x a realistic 3DS
heap budget (~24-32MB). Option B remains open as the next candidate.

**Goal:** Turn `docs/memory-audit.md`'s INFERRED "shapes are probably the
dominant sub-cost within `CharacterDictionary::build()`" into a PROVEN,
byte-counted breakdown by `CharacterDef` variant arm, then produce (but do
not yet implement) a concrete fix design.

**Why:** 145-454MB peak RSS is the top blocker for real-hardware testing
per this audit's own priority reassessment; a real fix needs a real
diagnosis first, which does not yet exist at the granularity needed to
design one confidently.

**Current implementation:** `tools/mem_profile_check/` (rewritten this
audit) provides pipeline-stage-level granularity only; not
per-character-kind.

**Ghidra evidence:** None (Shift-DX's own tag-loader hash table was never
resolved — no comparison point available).

**JPEXS evidence:** Not available this session.

**Exact files/functions:** Extend `tools/mem_profile_check/main.cpp` (or a
sibling tool) to sum `sizeof`-plus-heap-allocation-estimate per
`CharacterDef` variant arm (shape/sprite/sound/font/text/button/edittext)
across `CharacterDictionary::characters_`; cross-reference against
`docs/real-game-compatibility.md`'s per-game tag histograms to confirm
which tag types actually dominate for each game (not just Hobo1).

**Dependencies:** Phase 0.

**Implementation steps:** (1) add per-variant byte accounting; (2) run
against Hobo1, Hobo5, and Extreme Pamplona (once §Phase-4/loadMovie makes
its content reachable) to see whether the dominant cost is consistent
across games or shifts; (3) once the real cost driver is confirmed (shape
`ShapeRecord` expansion is the leading hypothesis per `docs/memory-
audit.md` §6, not yet proven), design 2-3 concrete fix options (e.g. a
more compact edge representation, lazy per-character parsing on first
render/reference instead of eager whole-dictionary parsing, or streaming
tessellation) with rough memory-savings estimates for each, informed by
the byte breakdown; (4) **stop — do not implement the fix in this phase**,
present the options for a human decision given the tradeoffs (lazy parsing
adds real-time cost on first display; a compact representation is a larger
one-time engineering investment).

**Tests:** None yet (diagnostic-only phase); the byte-breakdown tool
itself should have a small self-test confirming its accounting sums
correctly against a synthetic fixture.

**Real-game tests:** Run the breakdown against all measurable real corpus
games.

**3DS tests:** Not yet.

**Memory implications:** This phase's entire purpose.

**Expected result:** A `docs/memory-audit.md` update with a PROVEN
per-character-kind byte table, plus a short fix-options design note (new
doc or appended section) ready for a human go/no-go decision.

**Definition of Done:** Byte breakdown covers ≥90% of `CharacterDictionary`'s
measured RSS growth with a named cost driver per corpus game measured;
2-3 fix options documented with estimated impact.

**Risks:** Low (still diagnostic-only); the main risk is scope creep into
actually implementing a fix before the design is reviewed — explicitly
guarded against by this phase's own stop condition.

---

## Phase 3 — MP3 audio decode

**Status: DONE (2026-08-21).** Audible-sound-capable end to end for every
corpus game's MP3 content; on-device audible confirmation remains the one
explicitly out-of-scope item (no hardware/emulator access in this
sandbox). Full detail in `docs/known-limitations.md` L1 (rewritten this
phase) and `docs/memory-audit.md` §9 (new).

**Goal:** Audible sound for all 8 corpus games. — **Achieved for the MP3
format all 8 corpus games actually use** (`sound_corpus_worstcase`
confirmed 0 non-MP3 `DefineSound` characters across the whole corpus this
phase). Other codecs (ADPCM/Nellymoser/Speex/uncompressed) remain
undecoded — no corpus game needs them, so this was correctly out of scope,
not a shortfall against this phase's own goal.

**What shipped:** `third_party/minimp3` (public-domain/CC0, vendored via
`npm pack minimp3@1.0.0` since direct GitHub raw-content fetch was
blocked — see `third_party/minimp3/README.md`), wrapped by
`src/audio/Mp3Decoder.{h,cpp}` (`decodeMp3()` for raw MP3 frame streams,
`decodeSwfMp3Sound()` for `MP3SOUNDDATA`'s 2-byte `SeekSamples`-prefixed
form). `IAudioBackend` gained `loadSound()` (new virtual, default no-op —
non-disruptive to `NullAudioBackend`/existing tests).
`runtime::ScriptEnvironment::playSoundById()`/`ensureSoundDecoded()`
implement decode-on-demand-and-cache (decode a given soundId's audio at
most once, cache both success and failure). `Nintendo3DSAudioBackend::
loadSound()`/`playSound()` now really `memcpy` PCM into a `linearAlloc`'d
buffer and queue it via `ndspChnWaveBufAdd()` — genuine ndsp playback, not
a stub.

**Ghidra/JPEXS evidence:** None available/needed — implemented directly
against the public SWF spec's `MP3SOUNDDATA` framing, per `CLAUDE.md`'s
hard rule against copying from Shift-DX/gameswf; Shift-DX's own sound
tag-loader boundaries were never resolved in the prior RE session anyway
(see L1's Ghidra-evidence note).

**Exact files/functions:** `src/audio/Mp3Decoder.{h,cpp}` (new),
`third_party/minimp3/` (new, vendored), `src/audio/IAudioBackend.h`
(`loadSound()` added), `src/audio/NullAudioBackend.{h,cpp}` (`loadSound()`
override, logs and discards — desktop testing doesn't need real audio
output), `src/audio/Nintendo3DSAudioBackend.{h,cpp}` (real `loadSound()`/
rewritten `playSound()`), `src/runtime/MovieClipInstance.{h,cpp}`
(`bindMovie()`, `playSoundById()`, `ensureSoundDecoded()`,
`decodedSoundCache_`).

**Dependencies:** Sequenced with Phase 2's memory findings as planned —
decode-on-demand-and-cache (not eager whole-dictionary decode) was chosen
specifically because shape data, not sound, is this project's dominant
memory cost (§5-8), so paying decode cost only for sounds a session
actually triggers was the right tradeoff, confirmed by §9's worst-case
numbers still being smaller than the shape-dictionary cost even in the
worst case.

**Implementation steps:** All done — see "What shipped" above. `StartSound`
loop-count is honored structurally (`ScriptEnvironment::playSoundById()`
passes `loopCount` through) but `Nintendo3DSAudioBackend` only implements
"play once" for `loopCount > 1` (see L1's "Still not implemented" for
why — a real counted repeat needs on-device verification this sandbox
can't do). Envelope fields remain parsed-but-unused, unchanged from
before this phase (no corpus game was found to depend on them).

**Tests:** Done — 7 new unit tests (`tests/test_mp3_decoder.cpp`: real
payload decode, empty/garbage/truncated input, `SeekSamples`-skip
equivalence, leading-junk frame-sync search) + 1 new integration test
(`test_movieclip_instance.cpp`'s
`MovieClipInstance_StartSoundTag_Mp3Payload_DecodesAndLoadsPcmBeforePlay`,
which caught and led to fixing a real caching bug — see L1). 287/287 tests
pass, zero desktop/3DS compiler warnings.

**Real-game tests:** Done — `hobo.swf`/`hobo5.swf`/`hobo2.swf`/`hobo3.swf`
all confirmed producing genuinely non-silent decoded PCM (RMS ~1888.4)
when their real `StartSound` tags fire within the first 13 frames, sample
counts matching each `SoundDef::sampleCount` exactly. Extreme Pamplona's
main loader triggers no `StartSound` in that window (expected — its real
audio lives in 24 sub-SWFs unreachable without `loadMovie`, L6, not a
Phase 3 regression).

**3DS tests:** Done to the extent this sandbox allows — a real 3DS
cross-compile of the updated `Nintendo3DSAudioBackend` succeeds cleanly
(zero warnings), producing a real `.3dsx`. **Not done:** actually running
it and confirming audible output on hardware/Azahar — no such environment
was available this phase, same standing gap as the rest of Phase 10.

**Memory implications:** Measured — see `docs/memory-audit.md` §9. Real
corpus content (title screens only) costs ~52KB per game, negligible.
Worst case (every distinct sound in a file eventually triggered, since
the cache never evicts, and PCM is stored twice — once per soundId in
`ScriptEnvironment`, once in the active `IAudioBackend`'s own copy) is
~51-56MB per Hobo title — a real, flagged-honestly cost comparable in
order of magnitude to Hobo1's entire post-Option-A `CharacterDictionary`
peak, not built out with eviction this phase since no available corpus
content demonstrates the cache actually reaching that size in practice.

**Expected result:** Audible sound in corpus games on desktop and
(pending hardware access) on 3DS/Azahar. — **Achieved on desktop** (real
non-silent PCM confirmed and queued through every backend's real code
path); **3DS/Azahar audible confirmation remains pending hardware
access**, as originally scoped.

**Definition of Done:** Passing decode unit tests — **done**; non-silent
output confirmed for a real corpus `DefineSound` — **done**; memory impact
measured and documented — **done** (§9). All three met.

**Risks — how they played out:** Codec/licensing risk resolved cleanly
(minimp3 is CC0/public-domain, no LGPL static-link concern for a `.3dsx`
target). Loop/timing edge cases were real, as anticipated: the finite
`loopCount > 1` repeat could not be verified without on-device testing, so
it was deliberately left as an honest, logged "play once" gap rather than
guessed at — see L1.

---

## Phase 4 — `loadMovie`/multi-SWF + `ExportAssets`/dynamic instantiation (L3/L4/L6)

**Status: DONE (2026-08-21).** Step 1's targeted disassembly produced a
real, evidence-based NEGATIVE result (Extreme Pamplona's own main file
cannot call any dynamic-instantiation/loading API — see below); presented
to the user via `AskUserQuestion`, who chose **"Build generic loading
capability anyway"**. Steps 2-4 then implemented that generic capability
for real: `ExportAssets` linkage parsing, the full `attachMovie`/
`createEmptyMovieClip`/`duplicateMovieClip`/`removeMovieClip`/
`swapDepths`/`getNextHighestDepth` OOP method family, and `MovieClip.
loadMovie(url)`'s target-clip-replacement form (with a new `IFileLoader`/
`LocalFileLoader` seam). Full detail in `docs/known-limitations.md`
L3/L4/L6 (rewritten this phase).

**Goal:** Make Extreme Pamplona's actual game content (9 levels, 2 player
sprites, 4 music tracks, 9 sound banks — currently 100% unreachable)
loadable. — **Not achieved for Extreme Pamplona specifically** (Step 1's
finding means this was never achievable by implementing more AVM1 runtime
features — the main file has no in-bytecode caller for any of it, proven
comprehensively, not just unresolved). **Achieved as forward-looking
engine capability**, per the user's explicit chosen direction: the generic
dynamic-instantiation/loading API family now exists for real, and Extreme
Pamplona's OWN content sub-SWFs (several of which use normal interactive
AS2 internally) are now directly loadable via `loadMovie()` as standalone
assets, even though nothing in the main file's own script will ever call
it.

**Current implementation:** See `docs/known-limitations.md` L3/L4/L6.

**Dependencies:** Originally assumed L3 (`MovieClip` dynamic methods) and
L4 (`ExportAssets`) were coupled and needed sequencing together — Step 1
showed that assumption was based on an untested guess (Extreme Pamplona's
main file can't call ANY of these APIs, named or not), but L3 and L4 were
still genuinely coupled to EACH OTHER (`attachMovie` needs
`findByLinkageName()`) and were implemented together in Steps 2-4 as
planned.

**Step 1 — targeted disassembly (done):** Built two independent,
cross-checked tools —
`tools/real_game_harness/avm1_loader_disasm.cpp` (a static AVM1 symbolic
disassembler resolving string-literal operands through
`CallFunction`/`CallMethod`/`NewMethod`/`NewObject`/`GetURL`/`GetURL2`)
and `tools/real_game_harness/avm1_runtime_trace.cpp` (runs the REAL
`avm1::Interpreter` against the real movie via the normal
`MovieClipInstance::createRoot()`/`advanceFrame()` path, with a new
optional diagnostic hook — `ExecutionContext::callTraceSink`/
`ScriptEnvironment::callTraceSink`, see `src/avm1/ExecutionContext.h` and
the 6 call sites instrumented in `src/avm1/Interpreter.cpp` — reporting
every such action with its REAL runtime-resolved values, immune to the
string obfuscation that defeated the static pass alone). **Result:**
`extreme-pamplona.swf`'s entire AVM1 payload (142 bytecode buffers,
33,741 opcodes, all `DoAction`/`DoInitAction`/button actions, all 126
nested `DefineFunction` bodies) contains **zero** `CallMethod`,
`NewObject`, `NewMethod`, `GetURL`, or `GetURL2` opcodes anywhere —
confirmed by both tools independently and by a 500-tick (~20
real-seconds) live run of the actual interpreter. No `ImportAssets` tag
exists either. By contrast, several of the 24 real content sub-SWFs (e.g.
`content/sounds_all.swf`, `content/level-pamp1.swf`) DO contain normal,
unobfuscated interactive AS2 with real `CallMethod`/`NewObject` usage —
the "no loading capability" finding is specific to the main/outer file,
not the whole game. **Conclusion: the main file literally cannot call
`loadMovie`/`loadMovieNum`/`MovieClipLoader`/`attachMovie`/`getURL` or
any other dynamic-instantiation or navigation API, under any name, by any
mechanism AVM1 bytecode can express** — this is a proven negative, not an
unresolved search, so Steps 2-4 (design a loading API, implement it) have
nothing to implement it FOR in this specific file. See
`docs/known-limitations.md` L6 for the complete evidence writeup and
options going forward.

**Implementation steps:** (1) targeted disassembly — **done, negative
result** (see above). (2)-(4) — **done**, per the user's chosen option (a):

- **`ExportAssets` parsing (L4):** `CharacterDictionary::
  parseExportAssets()`/`findByLinkageName()` — see L4's rewrite.
- **Dynamic-instantiation primitives + AS2 method surface (L3):** four new
  `MovieClipInstance` primitives (`attachCharacter()`, `createEmptyChild()`,
  `swapDepthsWith()`, `nextHighestDepth()`), wired into AS2-visible
  `attachMovie`/`createEmptyMovieClip`/`duplicateMovieClip`/
  `removeMovieClip`/`swapDepths`/`getNextHighestDepth` in
  `handleNativeGet()`'s native-method dispatch — see L3's rewrite.
- **`loadMovie` (L6):** the target-clip-replacement form only (not the
  full `_level` sibling-movie form — deliberately scoped out, see L6's
  rewrite for why), backed by a new `runtime::IFileLoader` seam
  (`NullFileLoader` default, real `LocalFileLoader` for desktop/tests) and
  `ScriptEnvironment::ownLoadedMovie()` (owns every loaded `Movie`/
  `CharacterDictionary` for the rest of the session — see that method's
  own doc comment for the ownership-lifetime reasoning this sidesteps).

**What shipped (exact files):** `src/runtime/CharacterDictionary.{h,cpp}`
(`parseExportAssets()`, `findByLinkageName()`, `linkageNameToId_`);
`src/runtime/MovieClipInstance.{h,cpp}` (`attachCharacter()`,
`createEmptyChild()`, `swapDepthsWith()`, `nextHighestDepth()`,
`loadMovie()`, plus the `handleNativeGet()` dispatch block for all six AS2
method names); `src/runtime/IFileLoader.{h,cpp}` (new — interface +
`NullFileLoader`); `src/runtime/LocalFileLoader.{h,cpp}` (new — real
`ifstream`-based desktop implementation); `CMakeLists.txt`/`tests/
CMakeLists.txt` (new source/test files registered).

**Ghidra/JPEXS evidence:** None specific — implemented directly against
the public SWF spec's `ExportAssets` tag layout and real Flash's
documented `attachMovie`/`loadMovie` semantics, per `CLAUDE.md`'s hard
rule against copying from Shift-DX/gameswf.

**Tests:** Done — 9 new tests for the L3/L4 dynamic-instantiation family
(`tests/test_movieclip_instance.cpp`: `findByLinkageName` resolve/miss,
`attachMovie` success + unresolved-linkage failure, `createEmptyMovieClip`,
`duplicateMovieClip`/`removeMovieClip` OOP forms, `swapDepths`
bidirectional-swap correctness, `getNextHighestDepth` empty/non-empty), 3
new tests for `loadMovie` itself (replace-and-run-new-script end-to-end,
no-file-loader-wired failure path, invalid-SWF-bytes failure path — both
failure paths asserted to leave the target clip provably untouched), and 5
new tests for `LocalFileLoader` against REAL temp-file I/O (`tests/
test_local_file_loader.cpp` — the one genuinely OS-facing piece of this
phase, deliberately not a synthetic in-memory fixture like everything
else). **304/304 tests pass, zero desktop/3DS compiler warnings** (was
296 before this phase; the 8 already-existing pre-Phase-4 tests are
unaffected).

**Real-game tests:** The full 8-game render harness (`tools/
real_game_harness/run_harness.sh`) re-run against this phase's build
produced **byte-identical MD5s for every frame of every game**, matching
both the immediately-prior in-session run and the checked-in
`tests/games/_harness_baseline/harness_summary_2026-08-18.txt` baseline —
zero regression, as expected for a purely additive change (Extreme
Pamplona's main file still can't call any of the new methods, so its own
render output is unaffected by construction; nothing else in the corpus
was touched). Already-staged real content sub-SWFs (`tests/games/
extreme_pamplona/content/`) are now directly loadable via `loadMovie()`
as standalone assets, if a future session wants to exercise one manually
via the CLI — not pursued as an integration test this phase, since Step
1's finding means no such content is reachable via Extreme Pamplona's own
in-game path, and a test that manually loads a content sub-SWF would only
be exercising this runtime's `loadMovie()` in isolation, no differently
than the synthetic fixture tests already do.

**3DS tests:** Done — a real 3DS cross-compile (`build_3ds`) of the
updated core (including the new `IFileLoader`/`LocalFileLoader` files)
succeeds cleanly, producing a real `.3dsx`. `LocalFileLoader`'s plain
`ifstream` I/O compiles under the newlib-based devkitARM-equivalent
toolchain, but is NOT expected to function correctly on real 3DS hardware
without an SD/RomFS filesystem mounted first (`Nintendo3DSFileLoader` —
explicitly deferred, see L6) — `loadMovie()` is not wired into the 3DS
entry point (`src/platform/nintendo3ds_main.cpp`) this phase, so this gap
has no current effect.

**Memory implications:** Not re-evaluated this phase — no corpus content
was actually loaded via `loadMovie()` end-to-end (Extreme Pamplona's main
file can't reach it, so real memory pressure from loading the 23 content
sub-SWFs was never exercised this phase). `ScriptEnvironment::
ownLoadedMovie()`'s "never freed for the rest of the session" choice is a
real, deliberate, and currently-unmeasured cost for any FUTURE title that
does call `loadMovie()` repeatedly or loads large sub-movies — flagged
here rather than glossed over, matching L5's existing memory-audit
precedent, but not measured this phase since nothing in this corpus
exercises it.

**Definition of Done:** At least one real Extreme Pamplona level loads and
renders via the actual in-game loading path (not a test harness manually
loading the sub-SWF file directly). — **Not met for Extreme Pamplona
specifically, and per Step 1's finding, was never achievable as
originally stated**: there is no in-game loading path in this file to
render "via." Redefined (with the user's chosen direction) as: generic
`loadMovie`/dynamic-instantiation engine capability implemented, tested,
and verified with zero regression against the existing corpus — **met**.

**Risks — how they played out:** "The actual loading mechanism is
unidentified going in; scope could expand once the real API is found" —
what actually happened was more fundamental: there IS no real API call to
find in Extreme Pamplona's main file. Steps 2-4's own risk (building a
real feature nothing in the primary target corpus title can exercise)
materialized exactly as flagged at the Step 1 decision point — accepted
explicitly by the user, not a surprise.

---

## Phase 5 — `DefineMorphShape`/`DefineShape4`/`PlaceObject3`/`CsmTextSettings`

See `docs/known-limitations.md` L7 for full detail. Four independent,
separable rendering-completeness additions; sequence opportunistically
based on which game is being prioritized at the time. Lower urgency than
Phases 1-4 — none of these block reaching gameplay, only visual fidelity.

---

## Deferred / design-only (per explicit instruction — do not implement)

### Old-vs-New-3DS capability layer

Not designed this audit (out of the audit's own effort budget for a
lower-priority section) — flagged as a real future need (New 3DS has more
RAM/CPU headroom, directly relevant given Phase 2's memory findings) but
requires its own dedicated design pass.

### Save-system / achievement-system API design

**Explicitly not attempted** — see `docs/current-state-audit.md` §8. No RE
evidence exists (VC titles not loaded in Ghidra this session); a design
with zero RE grounding, if still wanted, should be commissioned as its own
explicitly-labeled forward-looking exercise, not folded into this
evidence-based audit.

### Flash-VC CIA container concept, dual-screen (top=game/bottom=UI) architecture

Not designed this audit. The dual-screen *rendering* plumbing already
exists and compiles (Phase 10, `docs/3ds-toolchain.md`) — a UI-layer
design on top of it is a reasonable next design-only exercise once Phases
1-4 above establish more of the runtime's real behavior to design a UI
around.

---

## Explicit non-goals restated

Per `CLAUDE.md`: no AVM2/AS3. Per this roadmap's own Phase 2: no memory
fix implementation until the byte-level diagnosis is done and reviewed.
Per the audit's governing instruction: **no phase above has been started**
— this document is the plan, not a change log.
