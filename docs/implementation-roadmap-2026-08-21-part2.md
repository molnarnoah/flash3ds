# Implementation Roadmap — Part 2 (Phases 5–16)

Continuation of `docs/implementation-roadmap.md` (Phases 1–4, done,
committed through `577b063`). Written as part of the 2026-08-21 Part 2
current-state audit (`docs/current-state-audit-2026-08-21-part2.md`) — see
that document's §M/§N/§O for the prioritization reasoning behind this
ordering. **Do not implement more than Phase 5 without re-checking in** —
per the audit's own instruction, only the single highest-value next phase
should be started without further direction.

Per-phase schema: Objective · Exact files · Evidence to gather · Ghidra
references · JPEXS references · Expected implementation · Tests · Real-
game validation · 3DS validation · RAM impact · Regression criteria ·
Completion criteria.

---

## Phase 5 — RAM Option B: lazy/on-demand character parsing — **DONE 2026-08-24**

**Completed exactly as specified below** — see `docs/memory-audit.md` §10
for full measured results (peak RSS reduction 1.37x-5.86x depending on
game; render harness zero-regression; 311/311 tests). Kept for reference
(the design specified here is what was actually built, confirmed by the
measured results matching this section's own predicted tradeoff — helps
peak-per-session, not the worst-case ceiling).

- **Objective:** Lower `CharacterDictionary`'s peak-at-any-moment RAM
  cost by parsing each character's shape/font/etc. data on first
  reference (placement or render) instead of eagerly parsing all 3,575+
  characters at `build()` time, composing with the already-shipped
  compact `ShapeRecord` (Option A).
- **Exact files:** `src/runtime/CharacterDictionary.{h,cpp}` (primary —
  needs a `TagRecord` offset/length stored per character up front,
  parsed lazily on `find()`/first reference, with a cache); any call
  site currently assuming `find()` always returns fully-parsed data
  synchronously and cheaply (grep every `CharacterDictionary::find(`
  call site before starting — `SceneRenderer.cpp`, `MovieClipInstance.
  cpp`, `Timeline.cpp` are the known ones from this audit's reading, not
  exhaustively re-verified).
- **Evidence to gather before coding:** re-run `tools/real_game_harness/
  memory_breakdown.cpp`'s per-character-kind walker to get a fresh
  baseline; instrument how many *distinct* characters a real 13-frame
  Hobo title-screen tick actually references vs. how many exist in the
  file (the "typical session only touches a fraction" claim in
  `memory-audit.md` §5b is currently INFERRED, not measured — this phase
  should measure it before committing to the design, since if a title
  screen already references most characters, Option B's benefit would be
  smaller than hoped).
- **Ghidra references:** None applicable — this is a representation
  decision independent of Shift-DX's own internals, consistent with
  `CLAUDE.md`'s "implement against the public spec" rule.
- **JPEXS references:** None available this session (§L of the audit).
- **Expected implementation:** `CharacterDictionary` stores a
  `pendingCharacters_: unordered_map<uint16_t, TagRecord>` populated
  during the existing `scanTagsForCharacters()` pass (cheap — just
  offset/length, matching `SpriteDef`'s/`SoundDef`'s existing pattern),
  plus `parsedCharacters_: unordered_map<uint16_t, CharacterDef>`
  populated lazily. `find()` checks `parsedCharacters_` first, falls
  back to parsing from `pendingCharacters_` on miss, caches the result.
  Existing `parseExportAssets()`/`linkageNameToId_` (Phase 4) must
  continue to resolve names without forcing eager parse of the target
  character (a linkage-name lookup should not force-parse — only actual
  placement/render should).
- **Tests:** New unit tests confirming (a) a character never referenced
  during a session never enters `parsedCharacters_` (a direct assertion
  on the map's size after a synthetic scan+never-place sequence); (b)
  first-reference parse produces identical `CharacterDef` content to the
  old eager-parse path (a golden-value regression test comparing this
  phase's lazy result against Phase-4-era eager-parse output for the
  same synthetic fixtures already in `tests/`); (c) repeated references
  don't re-parse (a call-counting test double or instrumentation
  counter). All 304 existing tests must continue passing unmodified in
  behavior (some may need mechanical updates if they assert on
  `CharacterDictionary`'s internal map directly rather than through
  `find()` — audit call sites first).
- **Real-game validation:** `tools/real_game_harness/run_harness.sh`
  against all 8 corpus games must produce byte-identical MD5 output vs.
  the current Phase-4 baseline (zero rendering regression is the bar, not
  "close enough") — same standard every prior phase has held itself to.
- **3DS validation:** Cross-compile clean (`docs/3ds-toolchain.md`'s
  existing process); no on-device execution available this environment,
  same standing caveat as every prior phase.
- **RAM impact:** Must be measured with `tools/mem_profile_check`,
  isolated-process methodology (this audit's own corrected methodology,
  §D of the current-state audit) — both peak-during-typical-session and
  worst-case-all-characters-eventually-referenced numbers should be
  reported, not just one.
- **Regression criteria:** Zero MD5 diff on the render harness; zero test
  count decrease; RAM must decrease for the measured typical-session
  case (if it doesn't, the phase has failed its own stated objective and
  should not be declared done).
- **Completion criteria:** Measured RAM numbers written to a fresh
  `docs/memory-audit.md` §10 addendum (not silently overwriting §5c's
  historical record); `docs/known-limitations.md` L5 updated to reflect
  the new peak figures; commit with the project's standard phase-commit
  convention.

---

## Phase 6 — `loadMovie` bounding + sound-cache eviction — **investigated 2026-08-24, deliberately NOT implemented**

Checked both unbounded-growth risks this phase (`decodedSoundCache_` and
`loadedMovies_`/`loadedCharacterDicts_`). No real corpus content available
in this environment exercises either enough to demonstrate them actually
growing large (at most 1 sound decoded, 0 `loadMovie` calls, across all 9
games' available content) — building eviction now would be tuning against
a hypothetical rather than a measured problem, which conflicts with this
project's own stated principle for eviction work (see
`docs/known-limitations.md`'s L5 entry and `docs/memory-audit.md` §9's
original reasoning, both restated this phase). Left open, flagged, not
silently declared fine — revisit if/when real (non-title-screen) gameplay
content becomes available to actually measure against.



- **Objective:** Close the two unbounded-growth risks newly surfaced by
  this audit (§D.1): `ScriptEnvironment::ownLoadedMovie()` never frees
  loaded movies; `decodedSoundCache_`/`Nintendo3DSAudioBackend::
  loadedSounds_` never evict.
- **Exact files:** `src/runtime/MovieClipInstance.{h,cpp}`
  (`ownLoadedMovie`, `loadedMovies_`, `loadedCharacterDicts_`,
  `decodedSoundCache_`); `src/audio/Nintendo3DSAudioBackend.{h,cpp}`
  (`loadedSounds_`).
- **Evidence to gather:** None beyond what this audit already
  established (§D.1's worst-case tables) — this is a bounded, well-
  understood fix, not a research phase.
- **Ghidra/JPEXS references:** None applicable.
- **Expected implementation:** A small bounded-size (or bounded-byte-
  budget) LRU wrapper around both caches — evict-oldest-on-overflow, not
  a complex generational scheme, matching this project's general
  preference for the simplest fix that actually bounds the problem.
  `loadMovie`'s LRU should be sized conservatively (e.g. cap at N=2-3
  loaded movies) given even one Hobo-scale movie is tens of MB.
- **Tests:** A test that calls `loadMovie` more times than the cap and
  asserts the oldest entry was evicted and its memory reclaimed (or at
  minimum, that the tracking map's size stays bounded); same pattern for
  the sound cache.
- **Real-game validation:** Re-run the render/sound harnesses to confirm
  no behavioral change for the (currently cap-respecting) real corpus
  usage patterns.
- **3DS validation:** Cross-compile clean.
- **RAM impact:** Directly the point of this phase — report before/after
  worst-case numbers using the same worst-case methodology as
  `memory-audit.md` §9's sound table.
- **Regression criteria:** Zero MD5 diff; zero test regression.
- **Completion criteria:** `docs/known-limitations.md` L1/L6 updated;
  commit.

---

## Phase 7 — Resolve Hobo's title-screen progression trigger — **DONE (negative result) 2026-08-25**

**Completed exactly as specified below** — see `docs/hobo-title-progression.md`
for the full writeup. Summary: no key progresses Hobo1 past a "title
screen" because no such root-timeline gate exists to progress past — the
root timeline never leaves frame 1 (single-frame/`onEnterFrame` game
architecture), and gameplay-shaped `Key.isDown()` polling is already
running from tick 0 regardless of any input. The `CondKeyPress=4` ("End")
trigger every frame-1 `DefineButton2` carries is real and dispatches
correctly, but its actual, concretely-traced effect (two
`GetURL armorgames.com` calls, a nested clip's `gotoAndStop(2)`, and
`Sound.setVolume(0)` — all persisting after a single tap, proving a one-
shot state flip, not a level-triggered hold) is a **pause/quit-to-portal
menu**, not a "start game" trigger. New tool:
`tools/real_game_harness/hobo_end_key_probe.cpp`. This is an honest
negative result on the phase's original framing, not a runtime gap —
`condActionsV2`/`CondKeyPress` dispatch is demonstrably working (that's how
End's real effect was observed at all). Feeds into Phase 8's scoping per
this phase's own completion criteria. 352/352 tests still passing (no new
unit tests — this was a real-corpus verification phase, whose own targeted
harness invocation is the test).

---

## Phase 7 (original spec) — Resolve Hobo's title-screen progression trigger

- **Objective:** Determine what actually causes Hobo1-family content to
  progress past its title screen — a verification task, not an
  implementation task, per this audit's §I finding that all 3 frame-1
  buttons in Hobo1 are keypress-only in their own binary `condActionsV2`
  data (0 mouse-transition bits), yet a real `CondKeyPress` ("End")
  trigger does produce a measurably different render.
- **Exact files:** None modified — this is disassembly/tracing work
  using the existing `tools/real_game_harness/avm1_loader_disasm.cpp`/
  `avm1_runtime_trace.cpp` tools (already built for Extreme Pamplona's
  Phase-4-Step-1 investigation, directly reusable here) plus
  `tools/real_game_harness/button_scan.cpp` (already built, already used
  to establish the keypress-only finding).
- **Evidence to gather:** Disassemble each of Hobo1's 16 `DefineButton2`
  records' `condActionsV2` key-code fields specifically (which key(s)
  each button actually listens for — `button_scan` already proved
  "keypress-only," this phase needs the specific key codes, not just the
  yes/no classification); cross-reference against `Key`'s 18 wired named
  constants (`docs/compatibility-matrix.md` §6) to confirm the runtime
  can express whatever key(s) are found.
- **Ghidra references:** None directly applicable (Hobo is real corpus
  content, not Shift-DX) — though if `Key.isDown()`'s native Shift-DX
  implementation is ever resolved (an open Ghidra item per `docs/
  reverse-engineering-map.md`'s "Explicitly unresolved" section), it
  could corroborate which keys a real 3DS Flash-VC title mapped for
  similar content, informationally only.
- **JPEXS references:** This is exactly the kind of question a JPEXS
  timeline/action-script viewer would answer quickly if connected — not
  available this session (§L).
- **Expected implementation:** None yet — this phase's output is a
  finding, feeding Phase 8's go/no-go decision and potentially closing
  out §I's "current top blocker" entry for the whole Hobo family with a
  concrete answer instead of an open question.
- **Tests:** A new targeted `real_game_harness` invocation exercising
  the specific key(s) found, confirming visible frame progression beyond
  frame 13 (or whatever the real title-screen boundary is) — this is the
  test.
- **Real-game validation:** IS the validation — this phase either
  confirms Hobo1 progresses with a specific input, or confirms it
  doesn't (in which case the blocker is something else entirely, and
  this phase's honest negative result should feed back into a re-scoped
  Phase 8).
- **3DS validation:** N/A (investigation phase).
- **RAM impact:** None expected.
- **Regression criteria:** N/A.
- **Completion criteria:** A new `docs/hobo-title-progression.md` (or an
  addendum to an existing doc) recording the specific finding either way,
  feeding directly into Phase 8's scoping.

---

## Phase 8 — `GlobalObject` builtins (`Math`/`String`/`Number`/`Boolean`/`Date`) — **DONE (Math only, by evidence) 2026-08-25**

**Completed exactly as specified below, scoped by the evidence-gathering
step this section itself requires** — see `docs/known-limitations.md` L2
and `docs/avm1-compatibility.md`'s "Global built-ins" section for full
detail. Summary: a static disassembly pass
(`tools/real_game_harness/avm1_loader_disasm.cpp`, keyword-filtered)
across all 8 real corpus games plus a standalone hobo.swf copy found
`Math.random()`/`Math.ceil()` calls (the `Math.ceil(Math.random() * n)`
idiom) in 5 of 8 games, and ZERO evidence of `String`/`Number`/`Boolean`/
`Date` used as global constructors anywhere in the corpus. Implemented
`Math` (`floor`/`ceil`/`round`/`abs`/`sqrt`/`pow`/`min`/`max`/`random`/
`PI`/`E` — the two evidenced methods plus the roadmap's own "at minimum"
baseline set, since the rest are free/trivial/stateless additions of the
same shape) in `GlobalObject::create()`, reusing Phase 6's `nativeImpl`
pattern and `ActionRandomNumber`'s existing `randomSource` seam for
`Math.random()`. Deliberately did NOT implement `String`/`Number`/
`Boolean`/`Date` — same "don't build against a hypothetical" reasoning as
Phase 6's eviction decision. 9 new tests (361/361 total passing, up from
352), byte-identical render-harness MD5s (frames 1-5, all 8 games,
verified via `git stash`).

---

## Phase 8 (original spec) — `GlobalObject` builtins (`Math`/`String`/`Number`/`Boolean`/`Date`)

- **Objective:** Install real native implementations for the AS2 global
  built-ins `GlobalObject::create()` currently omits entirely (§C item
  4), scoped to whichever specific methods Phase 7 (or a follow-up
  disassembly pass) confirms the Hobo family's 585–1,092 `CallMethod`
  opcodes/file actually target — not a speculative full AS2-spec
  implementation.
- **Exact files:** `src/avm1/GlobalObject.{h,cpp}` (primary); likely a
  new `src/avm1/builtins/` split by namespace (Math/String/Number/
  Boolean/Date) if the method count grows large enough to warrant it —
  design decision left to the implementing phase, not pre-specified
  here.
- **Evidence to gather:** A disassembly pass identifying exactly which
  `CallMethod` targets in Hobo's bytecode resolve to unbound globals
  (i.e., actually confirm the 585–1,092-per-file figure is a real
  built-in-method-call signal, not e.g. entirely user-object method
  calls that happen to also use the `CallMethod` opcode) — this was
  explicitly flagged as not yet done in both this audit and the prior
  real-game-corpus phase; should not be skipped again.
- **Ghidra references:** `avm1_builtin_prototypes_init` (0x2d228c) —
  confirms which `Object.prototype`/`String.prototype` methods a real
  shipped 3DS Flash port installed (`docs/reverse-engineering-map.md`) —
  use as a checklist of plausible targets, not as proof any specific one
  is called by this corpus.
- **JPEXS references:** Not available this session.
- **Expected implementation:** Reuse the existing `nativeImpl` binding
  pattern already used for `Key`/`Mouse`/`Sound`/`ExternalInterface`
  (`MovieClipInstance::handleNativeGet()`) — `Math` as a plain object
  with native methods (`floor`/`ceil`/`random`/`abs`/`min`/`max`/`sqrt`/
  `pow` at minimum, per common AS2 usage patterns, refined against the
  disassembly evidence above); `String`/`Number`/`Boolean` as callable
  conversion functions; `Date` only if evidence shows it's used (not
  assumed).
- **Tests:** Per-builtin unit tests (`Math.floor(-1.5) == -2`,
  `Math.random()` in `[0,1)`, `String.fromCharCode`, etc.) plus, if Phase
  7/this phase's disassembly pass identifies a specific real call site,
  a targeted integration test exercising that exact site against real
  corpus bytecode.
- **Real-game validation:** Render harness MD5 comparison; specific
  attention to whether previously-`undefined`-returning calls now
  produce different (hopefully correct-per-spec) values that change
  rendered output in a way that suggests progress, not regression.
- **3DS validation:** Cross-compile clean.
- **RAM impact:** Expected negligible (a handful of native function
  objects, not corpus-scale data).
- **Regression criteria:** No test count decrease; any render-harness
  MD5 change must be explained (expected: some frames may now render
  differently if a previously-undefined call now does something real —
  this is a case where an MD5 diff needs manual visual review, not an
  automatic fail, unlike every prior phase's zero-diff bar).
- **Completion criteria:** `docs/known-limitations.md` L2 updated/closed
  or narrowed; `docs/avm1-compatibility.md`/`avm1-support.md` updated to
  reflect the newly-implemented globals.

---

## Phase 9 — `DefineMorphShape`/`2` parsing + rendering — **DONE (`DefineMorphShape` v1 only, by evidence) 2026-08-25**

**Completed exactly as specified below, scoped by the evidence-gathering
step this section itself requires** — see `docs/known-limitations.md` L7,
`docs/compatibility-matrix.md` §2, and `docs/swf-support.md`'s
disambiguated "Roadmap Phase 9" section for full detail. Summary:
`swf_diagnostic`'s tag histogram, re-run against all 8 corpus games, found
`DefineMorphShape` (tag 46, v1) present but **zero** occurrences of
`DefineMorphShape2` (tag 84) anywhere — scoped implementation to v1 only.
Implemented parsing (`src/swf/DefineMorphShapeTag.h/.cpp`),
`CharacterDictionary` resolution (new `MorphShapeDef` variant arm), and
rendering (`SceneRenderer::renderMorphShapeCharacter`, synthesizing a
plain `swf::Shape` from the morph's START-side fill/line styles and start
edges and reusing the existing tessellation path — the same posture as
this project's gradient-as-flat-average simplification, clearly flagged
rather than silently declared full support). A new evidence tool,
`tools/real_game_harness/morph_ratio_scan.cpp`, confirmed this
simplification is exactly correct for the real corpus, not just
convenient: every `PlaceObject2` record targeting a morph character
across all 7 Hobo files uses `ratio=0` (explicit or absent) — zero
non-zero ratios anywhere. 7 new tests (368/368 total passing, up from
361), byte-identical render-harness MD5s (frames 1-5, all 8 games,
verified via `git stash`) — the real corpus's morph placements sit in
gameplay content this harness's frame 1-5 render never reaches, so this
phase adds real, tested, evidence-verified capability with zero visible
change to the harness's existing coverage.

---

## Phase 9 (original spec) — `DefineMorphShape`/`2` parsing + rendering

- **Objective:** Resolve `DefineMorphShape`(46)/`DefineMorphShape2`(84)
  into `CharacterDictionary` and render them — highest-value rendering
  gap by corpus-wide reach (all 7 Hobo files, 16–27 characters each; 0
  effect on Extreme Pamplona's loader).
- **Exact files:** New `src/swf/DefineMorphShapeTag.{h,cpp}` (parser,
  mirroring `DefineShapeTag.cpp`'s existing structure but handling the
  dual start/end shape + `MorphFillStyle`/`MorphLineStyle` records per
  spec); `src/runtime/CharacterDictionary.{h,cpp}` (new `MorphShapeDef`
  variant arm); `src/renderer/SceneRenderer.cpp`/`ShapeTessellator.cpp`
  (rendering — real Flash morphs interpolate between start/end shape by
  a `ratio` parameter typically driven by the containing sprite's own
  playhead position; simplest correct-enough first implementation could
  render only the start shape, same posture as this project's existing
  gradient-as-flat-average simplification, clearly flagged as such
  rather than silently declared "done" if full interpolation isn't
  built).
- **Evidence to gather:** Confirm via the real corpus (already known:
  19-27 morph shape characters per Hobo file) what `ratio` range/usage
  pattern these files actually exercise, if any beyond a static
  single-ratio placement — informs whether the "start-shape-only"
  simplification is visually acceptable or whether animated morphing is
  load-bearing for these specific files.
- **Ghidra references:** None specific to morph shapes exist in the
  current Shift-DX findings (`docs/reverse-engineering-map.md`).
- **JPEXS references:** Would help confirm actual ratio-usage patterns
  visually — not available this session.
- **Expected implementation:** See "Exact files" above.
- **Tests:** Parser unit tests against a synthetic `DefineMorphShape`
  fixture (both v1/46 and v2/84); a rendering test confirming a morph
  character now produces non-placeholder pixels (vs. the current "simply
  absent" behavior).
- **Real-game validation:** Render harness — expect MD5 changes for
  every Hobo file (morph shapes currently render as nothing; this is an
  intentional, expected, reviewed diff, not a silent regression).
- **3DS validation:** Cross-compile clean.
- **RAM impact:** Additive — new `MorphShapeDef` character data; should
  be measured and folded into Phase 5's per-character-kind accounting if
  Phase 5 lands first (sequencing note: since Phase 5 is scheduled
  before this phase, its lazy-parse design should already account for a
  future new character kind being added, i.e. don't hardcode the
  variant list assumption anywhere that would make adding this phase's
  new arm awkward).
- **Regression criteria:** Reviewed (not zero) MD5 diff, explicitly
  approved as "morph shapes now visible" rather than treated as a
  failure.
- **Completion criteria:** `docs/compatibility-matrix.md` §2 and
  `docs/known-limitations.md` L7 updated.

---

## Phase 10 — Bitmap tag family (`DefineBits*`) parsing + rendering

- **Objective:** Parse `DefineBits`(6)/`DefineBitsJPEG2/3/4`(21/35/90)/
  `DefineBitsLossless/2`(20/36) and render them as real bitmaps instead
  of the current unreachable flat-gray placeholder — second-highest
  rendering gap by corpus reach (Hobo 2/3/4/6/7 partial, Extreme
  Pamplona's loader has 10).
- **Exact files:** New `src/swf/DefineBitsTag.{h,cpp}` family (likely
  split JPEG-family vs. lossless-family given genuinely different
  codecs — JPEG needs a real JPEG decoder dependency, lossless needs
  zlib, already linked); `src/runtime/CharacterDictionary.{h,cpp}` (new
  `BitmapDef` variant arm); `src/renderer/SceneRenderer.cpp` (bitmap-fill
  rendering path, currently the flat-gray placeholder).
- **Evidence to gather:** Confirm which specific bitmap tag variants the
  corpus actually uses before picking a decoder dependency (already
  partially known from `docs/real-game-compatibility.md`'s histograms —
  Hobo2 has `DefineBitsLossless`x9 + `DefineBitsJpeg3`x1; re-confirm no
  corpus file uses `DefineBitsJPEG4`, which would need a more complex
  decoder, before scoping the dependency choice).
- **Ghidra references:** None specific.
- **JPEXS references:** Would help visually verify decoded bitmap
  correctness — not available this session.
- **Expected implementation:** A vendored, license-compatible (matching
  the `minimp3`/CC0-style precedent set by Phase 3's audio work — must
  not introduce an LGPL static-link concern on the 3DS `.3dsx` target,
  per that same reasoning) JPEG baseline decoder for the JPEG-family
  tags; direct zlib-based decode (already linked) for the lossless
  family; a new `BitmapDef` character kind holding decoded RGBA pixel
  data; `SceneRenderer`'s bitmap-fill path updated to sample real pixel
  data instead of the flat-gray placeholder.
- **Tests:** Parser + decode unit tests per tag variant against small
  synthetic fixtures; a rendering test confirming a bitmap-filled shape
  now produces non-placeholder, non-gray pixels.
- **Real-game validation:** Render harness MD5 diff, reviewed and
  expected for every file with bitmap content.
- **3DS validation:** Cross-compile clean; flag decode cost (JPEG
  decoding is nontrivially CPU-heavier than the current zero-cost
  placeholder) as a per-frame-time concern worth profiling, not just a
  RAM concern.
- **RAM impact:** Additive, potentially significant — decoded RGBA
  bitmap data is typically much larger than compressed source bytes;
  must be measured (this is exactly the "N/A currently" gap flagged in
  the audit's §D.1 GPU/texture row — this phase is what turns that row
  from N/A into a real, measurable cost, and should budget engineering
  time for that measurement, not treat it as an afterthought).
- **Regression criteria:** Reviewed (not zero) MD5 diff.
- **Completion criteria:** `docs/compatibility-matrix.md` §2/§8 and
  `docs/known-limitations.md` updated; new RAM figures folded into
  `docs/memory-audit.md`.

---

## Phase 11 — Extreme-Pamplona-specific rendering (`DefineShape4`/`PlaceObject3`/`CsmTextSettings`/`DefineFont3`)

- **Objective:** Close the four Extreme-Pamplona-only tag gaps (135/345/
  37/5 occurrences respectively, 0 effect on any Hobo file).
- **Exact files:** `src/swf/DefineShapeTag.cpp` (remove the v4 early-out,
  add v4 line-style-2/fill-scaling-flags support per spec); new
  `src/swf/PlaceObject3Tag.{h,cpp}` (extends `PlaceObject2`'s fields with
  blend mode/filter list/class-name/cache-as-bitmap flags);
  `src/swf/DefineFontTag.cpp` (v3, currently rejected on the "20x
  em-square mismatch" per `docs/compatibility-matrix.md` §2 — needs the
  actual v3 em-square-scale handling, not just accepting the rejected
  path); new `src/swf/CsmTextSettingsTag.{h,cpp}` (text-rendering-quality
  hint only — lowest implementation cost of the four, likely closer to a
  no-op/metadata-only parse than a rendering change).
- **Evidence to gather:** Whether Extreme Pamplona's 24 content sub-SWFs
  (not just the loader) also use these tags, once Phase-4's `loadMovie`
  makes them directly testable — the loader's own counts may
  undercount real usage across the whole title.
- **Ghidra/JPEXS references:** None specific; JPEXS would help visually
  confirm blend-mode/filter correctness once implemented.
- **Expected implementation:** Four largely-independent additions,
  sequenced by whichever sub-feature is cheapest first (`CsmTextSettings`
  likely first — smallest scope) unless Extreme Pamplona's own content-
  sub-SWF reachability work (deferred to tooling, per §J of the audit)
  surfaces a reason to reorder.
- **Tests:** Per-tag parser unit tests; `DefineShape4` needs a rendering
  test confirming v4-specific fields (e.g. non-scaling strokes) actually
  affect output, not just parse without crashing.
- **Real-game validation:** Extreme Pamplona render harness run (loader
  only, since sub-SWF loading is a separate, tooling-level concern per
  §J) — reviewed MD5 diff expected.
- **3DS validation:** Cross-compile clean.
- **RAM impact:** Expected small (structural fields, not bulk data like
  bitmaps).
- **Regression criteria:** Reviewed (not zero) MD5 diff for Extreme
  Pamplona specifically; zero diff for every Hobo file (none use these
  tags, so a Hobo-file MD5 change here would indicate a bug).
- **Completion criteria:** `docs/compatibility-matrix.md`/`docs/
  known-limitations.md` L7 updated.

---

## Phase 12 — Dual-screen (top-SWF + bottom-SWF) engine support

- **Objective:** Replace `nintendo3ds_main.cpp`'s hardcoded single-SWF-
  plus-diagnostic-picture structure with genuine support for two
  independent root movies, each rendered to its own screen target —
  prerequisite engine work for the VC-container proposal (§VC of the
  audit), not a file-format change alone.
- **Exact files:** `src/platform/nintendo3ds_main.cpp` (main loop —
  needs to drive two independent `MovieClipInstance` roots/`Timeline`s/
  `SceneRenderer` calls per tick instead of one); possibly a new shared
  `RuntimeSession`-style type wrapping "one loaded movie + its renderer
  target" so the main loop can hold two of them symmetrically rather
  than hardcoding "primary" vs. "diagnostic" as today.
- **Evidence to gather:** None beyond confirming today's exact
  single-root assumption boundaries via a full read of
  `nintendo3ds_main.cpp` and `SceneRenderer`'s public API (partially
  done this audit — full re-read recommended before starting, since this
  phase's scope depends on exactly how deep the single-root assumption
  goes).
- **Ghidra/JPEXS references:** None applicable — this is new engine
  architecture, not corpus-content-driven.
- **Expected implementation:** A `RuntimeSession` (or similarly-named)
  type owning one `MovieClipInstance` root + its `ScriptEnvironment` +
  its `SceneRenderer` target; `nintendo3ds_main.cpp` holds
  `top: RuntimeSession`, `bottom: std::optional<RuntimeSession>`
  (optional, since a bottom SWF is optional per the VC-container
  proposal); each ticks and renders independently.
- **Tests:** Desktop-side test (the existing CLI harness likely needs a
  two-SWF invocation mode) confirming two independently-loaded SWFs
  produce independent, non-interfering render output and independent
  AVM1 state (no accidental global-state bleed between the two —
  `GlobalObject`/`ScriptEnvironment` singletons must be duplicated, not
  shared, between top and bottom sessions).
- **Real-game validation:** Run any two corpus files simultaneously
  (e.g. Hobo1 top + a second Hobo file bottom) and confirm both render
  correctly and independently via the harness.
- **3DS validation:** Cross-compile clean; this phase doubles per-frame
  CPU/render cost in the worst case (two full SWF pipelines instead of
  one) — flag as a performance concern to profile, not just a
  compile-clean check.
- **RAM impact:** Potentially doubles peak RSS if both screens load
  substantial content — should be measured with two real corpus files
  loaded simultaneously, not assumed to simply add linearly (shared
  library/renderer-code cost is fixed, but per-movie
  `CharacterDictionary` cost is genuinely additive).
- **Regression criteria:** Existing single-SWF demo behavior must be
  exactly preserved when no bottom SWF is configured (backward
  compatibility with today's hardcoded demo, per the VC-proposal's own
  "absent = current behavior" design goal).
- **Completion criteria:** `docs/architecture.md` updated to describe
  the new dual-session structure; a new `docs/dual-screen.md` documenting
  the design.

---

## Phase 13 — `config.ini` parsing + input-capability layer

- **Objective:** Implement the `[input]`/`[display]` `config.ini` format
  proposed in the audit's VC-proposal section, plus the capability∩
  config-join input-mapping logic proposed in §G.1.
- **Exact files:** New `src/platform/ConfigIni.{h,cpp}` (flat key=value-
  per-section parser — deliberately minimal, not a general INI library);
  `src/platform/Nintendo3DSInput.{h,cpp}` (add `Platform3DSCapabilities`
  detection via libctru's New3DS-detection calls, currently entirely
  unused in this codebase); `src/platform/nintendo3ds_main.cpp` (wire
  config load + capability detection + mapping join into boot).
- **Evidence to gather:** None beyond confirming libctru's exact
  New3DS-detection API shape (`APT_CheckNew3DS` or equivalent) against
  whatever libctru version this project's toolchain vendors (`docs/
  3ds-toolchain.md`) — not independently re-verified this audit.
- **Ghidra/JPEXS references:** None applicable.
- **Expected implementation:** See "Exact files" above; the `[display]`
  section (`bottom_swf =`) depends on Phase 12 being done first (its
  parsed value feeds directly into Phase 12's `RuntimeSession::bottom`
  construction) — the `[input]` section does not depend on Phase 12 and
  could be built independently/first if sequencing needs to shift.
- **Tests:** Parser unit tests for `ConfigIni` (valid file, missing
  file → all-defaults, malformed line handling — must not crash, per
  this project's never-crash principle); a capability∩config-join test
  with synthetic capability flags (Old3DS vs. New3DS) confirming
  fallback-chain resolution picks the right physical mapping.
- **Real-game validation:** N/A directly (this is platform/config
  infrastructure, not corpus-content-driven) — indirectly validated by
  confirming a real corpus game still receives correct key/button input
  under the new indirection layer (regression, not new capability).
- **3DS validation:** Cross-compile clean; New3DS-capability-detection
  code path specifically cannot be verified without New3DS hardware or
  an emulator configured to report New3DS — flag as unverified the same
  way every other 3DS-specific code path in this project already is.
- **RAM impact:** Negligible (small config struct, not corpus-scale).
- **Regression criteria:** Existing hardcoded input mapping must remain
  the default behavior when no `config.ini` is present (backward
  compatible with today's demo).
- **Completion criteria:** `docs/input.md` updated with the new
  capability-layer design; a sample `config.ini` checked into `tests/` or
  `docs/` as a reference format example.

---

## Phase 14 — VC/CIA packaging (RomFS boot flow)

- **Objective:** Package the runtime + a `romfs/` directory (game.swf,
  optional game_bottom.swf, optional config.ini) into a real bootable
  `.cia`, per the audit's VC-container proposal.
- **Exact files:** Build/packaging scripts (new, under e.g. `tools/
  packaging/` or wherever `docs/3ds-toolchain.md`'s existing build
  process lives) using the 3DS homebrew toolchain's standard
  `makerom`/`3dstool`-equivalent RomFS-embedding process (not yet used
  anywhere in this project — `nintendo3ds_main.cpp`'s embedded demo SWF
  today is compiled directly into the binary, not loaded from RomFS at
  runtime — confirm this via a fresh read of the build process before
  starting, since this phase changes that fundamentally).
- **Evidence to gather:** Re-confirm exactly how the current embedded
  demo SWF reaches the binary (compile-time embed vs. any existing
  RomFS usage) — not independently re-verified this specific audit
  beyond the summary-level "embedded-demo-SWF-only, no SD/RomFS loading"
  finding already recorded.
- **Ghidra references:** The four unlisted VC titles (§F.2 of the
  audit) would be directly relevant here if ever loaded — real
  `exefs`/`romfs` boundary conventions from an actual shipped VC core
  would validate or correct this phase's packaging approach. **Flagged
  as the single most valuable piece of missing Ghidra evidence for this
  specific phase** — worth prompting the user to switch Ghidra's active
  program before this phase starts, if feasible.
- **JPEXS references:** None applicable.
- **Expected implementation:** Depends heavily on Phase 13's
  `IFileLoader`-with-write-extension work (if save support, Phase 15, is
  sequenced first) or can proceed read-only initially (RomFS read access
  via `IFileLoader`'s existing seam, extended with a
  `Nintendo3DSFileLoader` implementation — currently explicitly deferred
  per Phase 4's own scoping, and this phase is the natural point to
  finally build it).
- **Tests:** Build-process verification (does the packaged `.cia`
  actually contain the expected RomFS structure — inspectable via
  standard 3DS homebrew tooling, not this project's own test suite).
- **Real-game validation:** Package a real corpus file (e.g. `hobo.swf`)
  as `game.swf` inside a test `.cia` and confirm the boot flow reaches
  it — first genuine end-to-end "arbitrary SWF in, no rebuild" proof for
  this project.
- **3DS validation:** This phase is the first one where genuine on-
  device or Azahar-emulator testing becomes highly valuable — everything
  before it has been cross-compile-only; strongly recommend requesting
  actual hardware/emulator access before or during this phase rather
  than continuing to defer it.
- **RAM impact:** None beyond what Phases 5–12 already established (this
  phase is packaging, not new runtime cost).
- **Regression criteria:** The existing embedded-demo build target
  should remain available/working (don't remove the simple compile-time-
  embed path entirely, in case RomFS loading has 3DS-environment-
  specific issues the demo path doesn't).
- **Completion criteria:** A new `docs/vc-packaging.md` documenting the
  build process; the packaged `.cia` delivered as a build artifact.

---

## Phase 15 — Save system (Flash `SharedObject` + 3DS/VC container)

- **Objective:** Implement both save concepts proposed in the audit's
  §F.4, contingent on real evidence a target title needs
  `SharedObject` specifically (currently 0 corpus games do) or on the
  VC-container save layer becoming useful once Phase 14's packaging
  exists.
- **Exact files:** New `src/runtime/SharedObject.{h,cpp}` (AS2-level, if
  needed); a save-layer extension to `IFileLoader` (or a new sibling
  interface, `IPersistentStore`, read+write, mirroring the read-only
  `IFileLoader` pattern) shared between `SharedObject` and the VC-
  container layer per the audit's "one underlying primitive, two key
  namespaces" design.
- **Evidence to gather:** Re-scan the AS2 corpus for `SharedObject` usage
  before starting (currently confirmed absent, but re-confirm against
  whatever new corpus content may be available by this phase, including
  Extreme Pamplona's now-loadable content sub-SWFs, not yet individually
  AS2-scanned for this specific string).
- **Ghidra references:** Revisit §F.2 — if the user has by this point
  opened one of the four VC titles in Ghidra, a real save-container
  structure becomes available as ground truth; if not, proceed on the
  "inferred, not confirmed" basis documented in the audit, labeled
  honestly in whatever doc this phase produces.
- **JPEXS references:** Not available this session; would help confirm
  whether Hobo's `ExternalInterface` callback names
  (`SetUnlockedBonusIndex`, etc.) are the actual extent of what a host
  wrapper needs to persist, or whether there's more state referenced
  elsewhere in the bytecode not yet surfaced by the existing string
  scans.
- **Expected implementation:** See audit §F.4.
- **Tests:** `SharedObject` unit tests (`getLocal`, property read/write,
  `flush`, persistence across a simulated "session end/restart" in the
  test harness); persistent-store round-trip tests (write, close,
  reopen, read back identical data).
- **Real-game validation:** If Hobo's real `ExternalInterface` callback
  pattern is targeted, a test exercising `SetUnlockedBonusIndex` →
  simulated restart → confirm the value persists.
- **3DS validation:** Extradata read/write is 3DS-specific and cannot be
  verified without hardware/emulator access — flag as unverified, same
  standing caveat as audio.
- **RAM impact:** Expected negligible (small key/value blobs, not
  corpus-scale).
- **Regression criteria:** No corpus file currently calls `SharedObject`,
  so zero render/behavior change expected for the existing corpus;
  regression bar is "existing tests still pass."
- **Completion criteria:** `docs/known-limitations.md` updated; a new
  `docs/save-system.md` documenting both layers and their shared
  primitive.

---

## Phase 16 — Achievement system

- **Objective:** Implement the platform-independent achievement
  architecture proposed in the audit's §H, contingent on a concrete
  target title/trigger-set — explicitly the lowest-priority phase, not
  scheduled against any specific evidence today.
- **Exact files:** New `src/achievement/` directory (`Definition.h`,
  `Trigger.h`, `Store.{h,cpp}` with `InMemoryStore`/`PersistentStore`,
  `Dispatcher.{h,cpp}`, `UiPresentation.h`).
- **Evidence to gather:** A concrete achievement-definition set for a
  real target title — does not exist today for any corpus game; this
  phase should not start without one, per the audit's own explicit
  "must not block real-game-execution work" framing.
- **Ghidra/JPEXS references:** None currently known to exist for any
  loaded program.
- **Expected implementation:** See audit §H.
- **Tests:** Trigger-condition unit tests against synthetic
  `ExternalInterface`/frame-label events; persistence round-trip tests
  (shared pattern with Phase 15's store, if built by then).
- **Real-game validation:** Contingent on having a real trigger set to
  test against — none currently available.
- **3DS validation:** UI-presentation backend (`UiPresentation`) would
  need a real 3DS overlay-rendering implementation, deferred same as
  every other backend seam in this project until a concrete need exists.
- **RAM impact:** Expected negligible.
- **Regression criteria:** Purely additive — zero effect on existing
  corpus behavior if no triggers are defined.
- **Completion criteria:** A new `docs/achievement-system.md`.

---

**Standing instruction for whoever picks this roadmap up:** implement one
phase at a time, following this project's established 16-step per-phase
checklist (inspect current code → inspect Ghidra where applicable →
inspect JPEXS where applicable/available → implement the smallest
architecturally-correct change → add regression tests → run all tests →
run the real-game harness → cross-compile 3DS → profile RAM where
relevant → update docs → report exact files/APIs/test-count/real-game-
results/3DS-build-result/what-remains-unverified). Never claim hardware
functionality that hasn't actually been tested on hardware or a real
emulator — this project's own honesty about that gap, maintained
consistently across every phase to date, is itself a project asset worth
preserving.
