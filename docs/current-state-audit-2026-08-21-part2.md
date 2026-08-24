# Current-State Audit — Part 2 (2026-08-21)

**Scope of this document.** This is a from-scratch, source-verified audit
requested explicitly to NOT rely on prior phase summaries. It supersedes
the *prioritization* in `docs/known-limitations.md` and adds analysis that
did not exist anywhere in the repo before today: a classified RAM
breakdown against the user's full checklist, save/achievement/VC/input-
capability architecture proposals (all three previously undesigned), a
per-game (Hobo 1–7 individually + Extreme Pamplona) blocker matrix
reconciled against the now-confirmed-working event dispatcher, and a new
numbered roadmap (Phases 5+, since Phases 1–4 are done and committed as of
`577b063`).

It does **not** re-derive facts that `docs/compatibility-matrix.md`,
`docs/real-game-compatibility.md`, `docs/memory-audit.md`,
`docs/avm1-compatibility.md`, `docs/known-limitations.md`, and
`docs/reverse-engineering-map.md` already establish by direct source
citation — those were spot-verified today (see git log / file reads this
session) and found current. This document cites them rather than
duplicating their tables, per this project's own stated anti-drift
convention. Every claim below is either (a) a citation to one of those
already-verified documents, (b) a fresh grep/read performed today (marked
"verified 2026-08-21"), or (c) explicitly marked PROPOSED (design only,
not built) / NOT PROVEN FROM GHIDRA / INFERRED.

Git HEAD at time of writing: `577b063` (Phase 4 Steps 2–4), working tree
clean, 304/304 tests passing (`grep -rc TEST_CASE tests/*.cpp`, summed,
verified 2026-08-21).

---

## A. Architecture (verified 2026-08-21)

```
SWF bytes → SwfLoader (decompress FWS/CWS; ZWS/LZMA rejected)
          → SwfReader (bounds-checked primitives)
          → three independent ad hoc dispatch chains (NOT a real
            TagDispatcher despite the name — confirmed,
            compatibility-matrix.md §2's "Ground truth note"):
              1. CharacterDictionary::scanTagsForCharacters()
                 (character-defining tags, recursive into DefineSprite)
              2. Timeline::applyFrame()
                 (PlaceObject/RemoveObject family)
              3. Timeline / MovieClipInstance
                 (DoAction / DoInitAction / StartSound extraction)
          → CharacterDictionary (variant<ShapeDef,SpriteDef,SoundDef,
            FontDef,TextDef,ButtonDef,EditTextDef>, + linkageNameToId_
            since Phase 4)
          → MovieClipInstance tree (root + recursive children,
            independent Timelines/playheads)
          → avm1::Interpreter (100/100 ActionCode cases; ExecutionContext
            / Scope / Value) driven by ScriptEnvironment
              → GlobalObject (empty — see C/M below)
              → MovieClipInstance::handleNativeGet() (native property/
                method dispatch: MovieClip API, Key, Mouse, Sound,
                ExternalInterface)
              → IFileLoader (NEW Phase 4 — NullFileLoader default,
                LocalFileLoader real desktop impl, Nintendo3DSFileLoader
                deferred)
          → SceneRenderer (SoftwareRenderer desktop / Nintendo3DSRenderer
            3DS) ← ShapeTessellator (re-tessellates every render call,
            no cache)
          → audio::IAudioBackend (NullAudioBackend / Nintendo3DSAudioBackend)
            ← Mp3Decoder (Phase 3)
          → src/platform/nintendo3ds_main.cpp (3DS entry point)
```

**What this is not, confirmed by direct grep today:** there is no save
layer, no achievement system, no `config.ini`/RomFS/CIA packaging code, no
Old-vs-New-3DS input-capability detection, and no generic "arbitrary
top-screen SWF + arbitrary bottom-screen SWF" abstraction anywhere in
`src/` (all zero-hit greps, verified 2026-08-21 — see §F/§G/§ VC-proposal
below for the exact commands run). `src/platform/nintendo3ds_main.cpp` is
a single hardcoded demo: top screen renders one embedded SWF, bottom
screen renders a hardcoded diagnostic test picture — not a data-driven
dual-SWF loader.

**Design principle confirmed pervasive (re-spot-checked today, not
re-audited exhaustively):** every missing-API failure path degrades to a
logged warning + `undefined`/no-op, never a crash. This property should be
preserved by every future phase — it is why this runtime survives running
against real, imperfectly-covered corpus content at all today.

---

## B. What is actually implemented (headline; full detail in
`docs/compatibility-matrix.md`, re-verified current today)

- SWF container: FWS/CWS (real inflate), header fields, truncation
  handling. ZWS/LZMA: rejected outright.
- Tags: PlaceObject/2, RemoveObject/2, DefineShape/2/3 (not 4),
  DefineSprite (recursive), DefineText/2, DefineEditText (partial),
  DefineFont/2 (not 3), DefineButton/2 (Up-state render only),
  DefineSound (header only)/StartSound (dispatch only, MP3 now decodes —
  Phase 3), DoAction/DoInitAction, FrameLabel, ExportAssets (Phase 4).
  Not implemented: PlaceObject3, DefineShape4, DefineBits* (all bitmap
  tags), DefineFont3, DefineFontInfo/2, DefineButtonCxform/Sound,
  SoundStreamHead/2/Block, SetBackgroundColor, DefineMorphShape/2,
  DoABC/2 (by design, AVM2 out of scope).
- AVM1: all 100 ActionCode values have a real case; arithmetic/objects/
  arrays/functions/closures/MovieClip-timeline-actions/StartDrag all
  real. Try/Catch/Finally and Throw are no-ops (parsed, never executed).
  `GlobalObject::create()` installs **zero** named builtins (`Math`,
  `Date`, `Number`, `String`, `Boolean` do not exist as callable globals)
  — confirmed today via direct read of `src/avm1/GlobalObject.cpp`/`.h`
  (5-line stub, `std::make_shared<Object>()` and nothing else).
- MovieClip API (Phase 4 additions confirmed present via grep today):
  `attachMovie`, `createEmptyMovieClip`, `duplicateMovieClip`,
  `removeMovieClip`, `swapDepths` (full bidirectional), `getNextHighestDepth`,
  `loadMovie` (target-clip-replacement form only). Still absent:
  `unloadMovie`, drawing API, `createTextField`, `localToGlobal`/
  `globalToLocal`/`getRect`/`getBounds`/`setMask`, `_level`-indexed
  sibling-movie loading, `Object.prototype`/`String.prototype` methods.
- Event dispatch: **`DefineButton2` `condActionsV2` AND
  `object.onPress`/`onRelease`/`onRollOver`/`onRollOut` property-handler
  dispatch AND `CondKeyPress` are all real and wired** into the per-tick
  root `advanceFrame()` call (`docs/known-limitations.md` R1, git commit
  `03c08b7`, already in HEAD — this corrects `docs/real-game-
  compatibility.md`'s 2026-08-18 "Next blocker recommendation" section,
  which is now stale on this specific point; that document's per-game
  tag/opcode data is otherwise still accurate and is what §I below is
  built from).
- ExternalInterface: real, fully wired (`call`/`addCallback`/`available`).
- Input: `Key.isDown`/`getCode`, 18 named constants, `_xmouse`/`_ymouse`
  (viewport-to-stage conversion), edge-detected `InputState`
  (press/release transitions), bounding-box hit-testing
  (`hitTestPoint`/`hitTest(x,y)`), `ButtonInstance` UP/OVER/DOWN state
  machine. 3DS side: D-Pad/Circle-Pad/A/B/X/Y/L/R/touch mapped — **no
  New3DS C-Stick/ZL/ZR** (confirmed zero-hit grep today).
- Rendering: matrix transforms, ColorTransform/`_alpha` (Phase-prior
  fix), solid fills, crude strokes, DefineText/2 + partial DefineEditText
  text. Gradients render as a flat average color (not a real gradient).
  Bitmap fills are an unreachable flat-gray placeholder (no bitmap
  character type resolves at all). Shape tessellation is topologically
  wrong for shapes with holes/multi-run fills (documented, not
  hypothesized).
- Audio: MP3 decode-on-demand-and-cache (Phase 3) queues real PCM via
  `ndspChnWaveBufAdd()` on 3DS — genuine, cross-compiled, **never
  confirmed audible on hardware/emulator**. `loopCount>1` degrades to
  "play once." ADPCM/Nellymoser/Speex/uncompressed: no decoder exists (0
  corpus games need one — see §E).
- Loading: `IFileLoader`/`LocalFileLoader` seam (Phase 4) — real desktop
  file I/O; `MovieClip.loadMovie(url)` target-clip form works
  end-to-end (parse → teardown old children → rebind → run new frame-1
  script), 3 dedicated tests. `_level`-indexed sibling loading: not
  implemented (§C).

---

## C. What is missing (headline; grouped by why it matters, not just
alphabetically)

**Blocks real Hobo/Extreme-Pamplona execution directly:**
1. 3DS heap budget vs. `CharacterDictionary` peak RSS — §D, the single
   most severe open problem in the whole project.
2. On-device audio playback — never verified, only cross-compiled (§E).
3. `DefineMorphShape`/`2` rendering — visible-gap in all 7 Hobo files
   (16–27 characters each), 0 effect on Extreme Pamplona's loader.
4. `GlobalObject` builtins (`Math`/`String`/etc.) — Hobo files show
   585–1,092 `CallMethod` opcodes per file, a plausible built-in-method
   vector never independently disassembled to confirm targets (INFERRED
   risk, not proven blocker — see §I).
5. Extreme Pamplona's main loader has **zero in-bytecode path** to any of
   its 24 content sub-SWFs — a proven-negative, not a missing-feature gap
   (`docs/known-limitations.md` L6's Step-1 finding, re-confirmed by
   today's read — this is not fixable by implementing more runtime
   features).

**Visual/audio completeness gaps, not hard blockers:**
6. `DefineShape4`/`PlaceObject3`/`CsmTextSettings`/`DefineFont3` —
   Extreme-Pamplona-only.
7. Bitmap tags (all `DefineBits*` variants) — affects Hobo 2/3/4/6/7
   (1–10 bitmap tags each) and Extreme Pamplona's loader (10 bitmap
   tags); 0 effect on Hobo1.
8. Gradient/clip-mask/blend-mode rendering correctness.
9. Two unidentified tag IDs (253/255) in Extreme Pamplona only.

**Entirely undesigned, zero code, confirmed by grep today (see below):**
10. Save system (Flash `SharedObject` semantics AND a 3DS/VC-style
    container save layer) — §F.
11. Achievement system — §H.
12. VC/CIA container, `config.ini`, RomFS boot flow — §F/VC-proposal.
13. Old-vs-New-3DS input-capability detection layer — §G.
14. Generic top-screen-SWF / bottom-screen-SWF dual-stage architecture
    (current `nintendo3ds_main.cpp` is single-hardcoded-demo scaffolding
    only) — §G/VC-proposal.
15. Any 3D-rendering consideration (stereoscopic top screen) — not
    started, and current renderer/platform abstraction was not designed
    with it in mind (see VC-proposal's honest caveat).

**Grep commands run today confirming §10–14 are zero-code, not just
undocumented** (all returned 0 relevant hits in `src/`):
```
grep -rniE "sharedobject|savegame|save_data|\.sav\b" src/
grep -rni "achievement" src/
grep -rniE "config\.ini|romfs|\.cia\b" src/
grep -rniE "new3ds|cstick|c_stick|KEY_ZL|KEY_ZR|KEY_CSTICK" src/
grep -rniE "bottom.*swf|game_bottom|game_top|screen.*swf" src/
```

---

## D. RAM investigation — PRIORITY 0

**Methodology note (self-correcting an early misstep this session):**
running the full 8-game corpus through one process invocation of
`tools/mem_profile_check` (which itself loops over argv) produces
cross-contaminated peak-RSS numbers, because glibc's allocator does not
return freed heap pages to the OS between files. Re-running each corpus
file as an isolated `for`-loop-driven separate process invocation today
produced numbers matching `docs/memory-audit.md`'s existing figures
almost exactly — this is itself a finding: **zero measurable memory
regression across Phases 1–4** (event dispatch, MP3 decode, dynamic
instantiation, loadMovie), despite substantial new code landing in that
window.

**Fresh isolated-process figures, 2026-08-21 (post-Option-A, current
HEAD):**

| Game | Peak RSS |
|---|---:|
| Hobo 1 | ~89.8 MB (build peak) / ~85.3 MB (steady-state) |
| Hobo 5 | ~261.6 MB |
| Extreme Pamplona (loader only) | ~11.0 MB |

These match `docs/memory-audit.md` §5c exactly — full methodology,
byte-level breakdown, and the Option-A fix history are documented there
in far more depth than is useful to re-paste; this section adds the
**classification checklist the user asked for**, which did not exist in
that form before.

### D.1 — Classification checklist (every requested category)

Legend: **REQUIRED** (real data the runtime must hold to function) ·
**REDUCIBLE** (real need, wasteful representation) · **TEMPORARY**
(exists only transiently, already freed) · **DUPLICATED** (same logical
data held more than once) · **BUG** (unintentional waste) ·
**DESKTOP-ONLY** (never reaches the 3DS binary) · **3DS-UNSAFE**
(architecturally cannot work within budget even if "correct").

| Category | Classification | Evidence |
|---|---|---|
| Baseline process overhead (~4 MB, `Process start` row) | REQUIRED | `memory-audit.md` §3 checkpoint table |
| Raw compressed SWF bytes held during load (~10 MB Hobo1) | TEMPORARY | freed after `loadSwf`/`CharacterDictionary::build` per `Movie::data` ownership; measured recovery only ~4.9MB/3% because the *decompressed* buffer is retained (see next row), not this one |
| Decompressed `Movie::data` tag stream (7.7 MB Hobo1) | **REQUIRED** (by design — `TagRecord::bodyOffset/bodyLength` and `SpriteDef`'s nested streams index into it for the `Movie`'s whole lifetime) | `memory-audit.md` §6, `Movie.h` doc comment |
| `CharacterDictionary::build()` — shape `ShapeRecord` vectors (dominant cost, 92.5% post-Option-A) | **REDUCIBLE** (Option A already cut 3.0x/record; Option B — lazy/on-demand parsing — is the next available reduction and is NOT yet built) | `memory-audit.md` §5a/§5c, PROVEN via `sizeof()` walker |
| `CharacterDictionary::build()` — sprite/sound/font/text/button/edit-text data (~5.5–7.5% combined) | REQUIRED, small | `memory-audit.md` §5a |
| `FillStyle`/`LineStyle`/`Gradient::records` arrays | REQUIRED, unaffected by Option A | `memory-audit.md` §5c |
| Decoded MP3 PCM cache (`ScriptEnvironment::decodedSoundCache_`) | **DUPLICATED + BUG (no eviction)** — real-content measured cost is negligible (~52 KB, title screens only) but the proven worst case is 25.4–28.2 MB per Hobo title *held twice* (once here, once in the backend copy below) if a session eventually triggers every distinct sound, since neither cache ever evicts | `memory-audit.md` §9, PROVEN worst-case table |
| `Nintendo3DSAudioBackend`'s own `linearAlloc`'d PCM copy | **DUPLICATED** (necessary duplication — DSP-DMA-accessible memory must be a dedicated buffer per that file's own comment — so the *fact* of duplication is REQUIRED, but the lack of any cap/eviction on either copy is a BUG) | `memory-audit.md` §9; `src/audio/Nintendo3DSAudioBackend.cpp` |
| Renderer-side buffers (framebuffer writes, tessellation output) | **TEMPORARY per-frame, not accumulated** — confirmed no tessellation cache exists (`SceneRenderer` re-tessellates every render call, `ShapeTessellator.cpp` lines cited in `memory-audit.md` §5b Option-C discussion) — so this is real per-frame CPU cost, not a standing RSS contributor; **not separately measured this session** (desktop `mem_profile_check` only checkpoints up to 5 `advanceFrame()` calls, each showing +0/+8 KB deltas after the initial build, consistent with "no growing renderer-side accumulation") |
| GPU/texture allocations (3DS `citro3d`/GPU-side) | **N/A currently — DESKTOP-ONLY renderer is what's measured.** The 3DS renderer path (`Nintendo3DSRenderer.cpp`) has never been RSS-profiled on-device or in an emulator this session or any prior one (no such environment available in this sandbox) — **NOT PROVEN, flagged honestly**, not assumed zero. Bitmap/texture support doesn't exist yet either (§B), so there is currently no GPU-texture-upload cost to measure regardless. |
| Display-list / `MovieClipInstance` hierarchy overhead (per-instance object graph, not character data) | **Not separately isolated this session.** The `advanceFrame()` checkpoint deltas (+0/+8 KB across 5 ticks for Hobo1, a single root movie with no dynamic instantiation exercised) suggest this is small relative to `CharacterDictionary`, but this is an **INFERENCE from one file's title-screen-only trace**, not a direct measurement — a session using `attachMovie`/`createEmptyMovieClip` heavily could grow this in a way the current profiling checkpoints wouldn't isolate from character-dictionary cost. Flagged as a real gap in this session's own instrumentation, not glossed over. |
| AVM1 bytecode storage (`Movie::data` sub-ranges referenced by `DoAction`/button-record byte ranges) | REQUIRED, already counted inside the 7.7 MB `Movie::data` figure above (no separate copy — confirmed by `SoundDef`'s own offset/length pattern being representative of how every non-shape character kind stores its payload, per `memory-audit.md` §5a's correction) | `memory-audit.md` §5a/§6 |
| String storage (AVM1 `Value`, constant pool, property names) | **Not separately measured this session.** Plausibly small relative to shape data given `CharacterDictionary`'s dominance is already proven at 90%+ of peak, but not isolated. |
| Test-harness / `mem_profile_check` / `real_game_harness` tool allocations | **DESKTOP-ONLY** — these are standalone diagnostic executables (`tools/mem_profile_check/`, `tools/real_game_harness/`), never linked into the 3DS `.3dsx` target; zero 3DS-relevant cost | Confirmed by `CMakeLists.txt` structure (these tools are separate `add_executable()` targets, not part of `flash3ds_core`) |
| STL/container overhead (`unordered_map` buckets/nodes, `vector` capacity slack) | **REDUCIBLE, not modeled/not separately measured** — `memory-audit.md` §5a's `sizeof()`-based estimate explicitly excludes this (accounts for only 97% of measured RSS for the large games, 57% for the small Extreme-Pamplona loader — the gap is attributed to exactly this overhead, proportionally larger at small scale) | `memory-audit.md` §5a table |
| Full-file copies / duplicated SWF representations beyond `Movie::data` | **Not found** — confirmed no second full-file copy exists anywhere in the pipeline (`SpriteDef`/`SoundDef` both index into the one `Movie::data` buffer, not copy from it, per §5a's correction of an earlier wrong claim) | `memory-audit.md` §5a |
| A loaded sub-movie via `loadMovie()` (Phase 4) | **REQUIRED but ADDITIVE, unbounded** — `ScriptEnvironment::ownLoadedMovie()` owns every loaded `Movie`/`CharacterDictionary` for the rest of the session, **never freed**, by explicit design (documented tradeoff, not an oversight) to sidestep dangling-pointer risk from non-owning raw pointers elsewhere in the codebase. This is a **BUG-adjacent, 3DS-UNSAFE-at-scale** design choice if a session calls `loadMovie` repeatedly (e.g. cycling through Extreme Pamplona's 23 content sub-SWFs one at a time would accumulate all 23 in memory simultaneously, never releasing the earlier ones) — **not yet measured**, flagged as a concrete new risk this audit surfaces that no prior document called out this plainly. | `src/runtime/MovieClipInstance.h`'s own doc comment, re-read today |

### D.2 — Highest-value reductions, ranked (not blind optimization)

1. **Option B (lazy/on-demand character parsing), `CharacterDictionary`.**
   Still open. Composes with the already-shipped Option A (a smaller
   struct is cheaper to lazily parse too). Directly attacks the ~85–90 MB
   Hobo1 floor, which is still 2.5–4x any plausible 3DS budget even
   post-Option-A. Highest-value because it's the only lever that changes
   the *shape* of the cost curve (peak-at-any-moment vs. whole-file
   ceiling) rather than a constant-factor shrink.
2. **`loadMovie()`'s never-free-loaded-movies design (new finding, D.1
   above).** Currently invisible in the corpus because no test/harness
   run has called `loadMovie` more than once per session — but it is a
   real, unbounded, additive risk the moment any content (or a future VC
   "select next content SWF" UI) calls it repeatedly. Cheap to bound (an
   LRU cap on `loadedMovies_`/`loadedCharacterDicts_`, or explicit
   `unloadMovie()` support) relative to the RE-architecture cost of
   Option B.
3. **Sound-cache eviction (`decodedSoundCache_` + backend copy).**
   Currently 0 measured real-content cost (title screens only exercise
   1 sound), but the proven worst case (~51–56 MB per title, double-
   copy) is comparable in order of magnitude to the entire post-Option-A
   `CharacterDictionary` peak — i.e. a real gameplay session (not just a
   title screen) could plausibly make sound the dominant cost, not
   shapes, once actual play is tested. A bounded LRU is a small,
   self-contained fix.
4. **On-3DS/emulator GPU and renderer-side profiling** — currently a
   complete blind spot (D.1's "N/A currently" row). Not a fix by itself,
   but a prerequisite: no reduction plan for this category can be
   written responsibly without first measuring it, since bitmap/texture
   support (currently absent) will add a wholly new cost category the
   moment it's implemented (§C item 7), and this should be sized against
   real GPU-memory budget data, not assumed.
5. **STL/container overhead** — lowest priority of the five: real, but
   the `sizeof()`-based estimate already accounts for 97% of measured
   cost on the two large games, meaning this is a single-digit-percent
   contributor at the scale that matters (Hobo-family), even though it's
   proportionally larger for small files (57% accounted, Extreme
   Pamplona's tiny loader) — not worth spending engineering effort on
   before #1–#3.

**Not itemized further because they are genuinely N/A or already fully
covered:** bitmap/image storage (zero cost today — not implemented, §C
item 7), decoded-image/GPU texture cache (same), display-list overhead
beyond what's already noted as an instrumentation gap, frame caches (none
exist — no tessellation cache per §5b Option C's own finding).

---

## E. Sound investigation — PRIORITY 1

Do **not** treat sound as finished. Confirmed status, re-verified today
against `docs/known-limitations.md` L1 and `docs/memory-audit.md` §9
(both dated 2026-08-21, current):

- **Works:** MP3 decode-on-demand-and-cache, real `ndspChnWaveBufAdd()`
  queuing on the 3DS backend, cross-compiled cleanly. All 8 corpus games
  use MP3 exclusively (423 `DefineSound` tags, 0 other-codec characters
  found) — so codec coverage is 100% of what this corpus needs.
- **Not implemented:** ADPCM/Nellymoser/Speex/uncompressed decode (0
  corpus games need these — confirmed, not assumed, via
  `sound_corpus_worstcase`'s corpus-wide scan). A real counted-repeat for
  `loopCount > 1` (currently silently degrades to "play once" — a single
  `ndspWaveBuf`'s `looping` flag is infinite-loop-only; a true repeat
  needs chained wavebufs, which needs on-device verification this
  sandbox cannot perform). No eviction policy on either PCM cache (see
  §D.1).
- **Never verified:** actual audible playback on 3DS hardware or an
  emulator. `playTestTone()` (a synthetic diagnostic, not part of the
  SWF pipeline) proves the ndsp plumbing itself works; the real
  `StartSound`→decode→queue path has never been confirmed audible.
  **This is the single most important unverified claim in the whole
  audio subsystem** — everything above it (decode correctness, RSS
  cost, queuing calls) is proven; whether a sound is actually heard is
  not.

---

## F. Save / VC investigation — PRIORITY 2

**Confirmed zero code exists** (§C, grep commands listed there). This
section is pure architecture proposal, explicitly distinguishing two
different, both-needed concepts per the user's instruction.

### F.1 — Two distinct save concepts (must not be conflated)

1. **Flash `SharedObject` semantics** — AS2-level, per-SWF persistent
   key/value state (`SharedObject.getLocal(name)`, `.data.foo = bar`,
   `.flush()`). This is content-facing: a game's own AS2 script expects
   this API to exist and behave like real Flash Player's local shared
   objects. **Confirmed absent from the AS2 scan of every corpus file**
   (neither `SharedObject` nor any save-related string appears in
   `docs/real-game-compatibility.md`'s AS2 FOUND/NOT-FOUND lists for any
   of the 8 games) — so implementing this specific API is **not
   currently required by the primary corpus**, though a "real 3DS VC"
   experience for other future titles would likely need it eventually.
2. **3DS/VC-container save semantics** — platform-level, opaque-to-the-
   SWF persistent state a VC wrapper manages around the runtime (extra
   save slot, save icon on the HOME menu, SpotPass/StreetPass metadata
   if ever relevant, or simply "does this title support suspend/resume
   via the 3DS system"). This is **not** something AS2 content ever
   calls directly — it's a wrapper-level concern, analogous to what a
   real Nintendo VC emulator core does around the emulated ROM.

### F.2 — What Ghidra evidence exists for #2 — NOT PROVEN

The four VC titles named in the original instruction (Pokémon Red VC,
Super Mario World VC, Miitopia, Tetris Axis) are **not loaded in this
session's Ghidra connection** — `list_segments` returns Shift-DX's own
`ram: 0x0-0x44ffff` signature exclusively, confirmed today (same result
as the prior segment; re-checked, unchanged). Switching Ghidra's active
program to one of those four requires the user to do so in the Ghidra
GUI on their own machine — this session's MCP bridge only exposes
whichever program is currently focused there, with no way to enumerate
or switch projects remotely. **No claim below is sourced from those four
titles' actual binaries.** If the user opens one of them in Ghidra and
this session is continued, a real save-container/CIA-structure
reconstruction pass against real evidence becomes possible and should be
prioritized — this is flagged as the single most valuable piece of
missing Ghidra evidence for the whole VC-architecture question.

**What IS available:** `docs/reverse-engineering-map.md`'s existing
Shift-DX findings (this project's own ground-truth reference binary) —
but Shift-DX is itself a single self-contained game binary, not
demonstrably built with the same VC-wrapper architecture as Nintendo's
own first-party VC titles (no evidence either way was found this
session; not investigated, since Shift-DX's own save behavior was never
a target of any prior RE session per that document's "Open items").
**Conclusion: F.2's Ghidra angle is currently unaddressable given what's
loaded. Labeled explicitly as "Not proven from Ghidra" per the user's own
instruction, rather than inferring an architecture from general VC
knowledge and presenting it as RE-confirmed.**

### F.3 — What the Hacks Guide Wiki (external documentation) confirms —
confirmed-from-documentation

Fetched `https://wiki.hacks.guide/wiki/3DS:Virtual_Console/Extraction`
today (the sibling `/Creation` page failed to fetch — returned as binary/
non-text content to the fetch tool, not retried further this session).
Per-platform-core structure the wiki documents for extracting console
data out of Nintendo's own VC titles:

- **NES-core titles:** ROM lives at `romfs/rom`, wrapped in a
  non-standard "TNES" header specific to the VC build (must be stripped/
  converted to get a standard `.nes` file back).
- **SNES-core titles:** ROM lives as `romfs/data.bin`, described as
  "altered for more efficient playback in the Virtual Console" (audio
  data specifically needs restoring via an external tool to work outside
  the VC context).
- **GB/GBC-core titles:** `romfs/rom`, extension-renamed only.
- **GBA-core titles:** stored in `exefs/.code` (the executable code
  segment itself, not `romfs`) — GodMode9 can rename directly for
  emulator use, implying the GBA core is closer to "the ROM IS the
  native code" than a separate romfs asset.
- **Game Gear-core titles:** `romfs/system/roms`, inside a compressed
  archive.
- **General principle the wiki states explicitly:** Nintendo's VC
  pipeline generally does not modify the ROM directly — it layers
  patches on top, meaning a properly-restored extraction should
  reproduce the original cartridge dump.

**What this means for flash3ds-runtime, labeled confirmed-from-
documentation → inferred → proposed:**
- **Confirmed-from-documentation:** every VC core Nintendo has shipped
  stores the emulated content inside `romfs`, under a structure specific
  to that core (not a single universal format across cores) — a
  Flash-VC core choosing its own `romfs/game.swf` convention is
  consistent with this pattern, not inventing something foreign to it.
- **Inferred (not directly evidenced by the wiki, which only documents
  extraction, not save-file structure):** a real VC core almost
  certainly keeps its own save data separate from `romfs` (extradata/
  savedata partitions are standard 3DS CIA mechanisms independent of any
  specific VC core, per general 3DS homebrew knowledge, not sourced from
  this wiki page specifically) — flagged as inferred, not confirmed.
- **Proposed for our implementation:** §F.4 below.

### F.4 — PROPOSED save architecture (design only, nothing built)

```
romfs/
  game.swf              # the primary/top-screen SWF (or game_top.swf —
                         #   see §VC-proposal for the dual-SWF question)
  game_bottom.swf        # OPTIONAL — see §VC-proposal
  config.ini              # OPTIONAL — see §VC-proposal
  assets/                 # OPTIONAL future use — external bitmap/font
                           #   assets this SWF's content references but
                           #   the runtime doesn't embed-decode yet
```

- **Flash `SharedObject` layer** — implement as a small AVM1-object-model
  addition (same `nativeImpl` pattern already used for `Key`/`Mouse`/
  `Sound`), backed by a single flat key/value file written to 3DS
  extradata (or a `sdmc:/` path on the 3DS, an ordinary file on desktop —
  same `IFileLoader`-style seam pattern already established for
  `loadMovie`, extended with a write side this time since `IFileLoader`
  is currently read-only). **Only build this once a target title is
  confirmed to actually call `SharedObject`** — none of the 8 corpus
  games do today (§F.1), so this stays low-priority relative to items
  that unblock the primary corpus.
- **3DS/VC-container save layer** — a wrapper-level concept, sitting
  above the Flash runtime, not inside it: a small serialized blob (last
  completed level/frame-label reached, unlock-bonus-index-style simple
  state Hobo's own `ExternalInterface` calls already reference per
  `docs/externalinterface.md`'s "Hobo's exact pattern" note) written to
  3DS extradata on suspend, restored on next boot. This is genuinely
  achievable today with real evidence backing the *shape* of what needs
  saving (Hobo's `SetUnlockedBonusIndex`/`OnBonusCancel`/`color`
  `ExternalInterface` callback names, confirmed real and wired — see
  `docs/compatibility-matrix.md` §7 — strongly suggest a small
  integer/string state blob is exactly what Hobo-family content expects
  a host wrapper to persist across sessions, matching real Flash-VC-arcade-
  wrapper patterns generally, though this specific inference about Hobo's
  intent is not confirmed by directly reading Hobo's own `DoAction`
  bytecode beyond what the string scan already shows).
- Both layers should share one underlying "platform persistent storage"
  primitive (open/read/write/close a named blob) so a future
  `Nintendo3DSFileLoader`-equivalent write-capable seam serves both, not
  two independent implementations.

---

## G. Input / console investigation — PRIORITY toward the input/event
audit the user asked for, feeding into VC proposal below

**Confirmed absent, verified today:** `src/platform/Nintendo3DSInput.cpp`
(84 lines, read in full this session) maps D-Pad, Circle Pad, A/B/X/Y,
L/R, and touch — **zero** New3DS-specific input (`KEY_ZL`/`KEY_ZR`/
`KEY_CSTICK*` all zero-hit grep today) and **zero** capability-detection
logic of any kind (no `APT_GetAppCpuTimeLimit`/`osGetFirmVersion`/similar
New3DS-detection call exists — confirmed by the same grep sweep finding
no New3DS-related string at all in `src/`).

### G.1 — PROPOSED capability-based input architecture (design only)

Two genuinely separate concepts must not be merged into one struct:

1. **Physical console capability** (queried once at boot, read-only
   thereafter): does this hardware have a C-Stick? ZL/ZR? (New3DS-only,
   detectable via libctru's own `APT_CheckNew3DS`-family calls — not yet
   used anywhere in this codebase, confirmed). A small
   `Platform3DSCapabilities` struct (`hasCStick`, `hasZlZr`, maybe
   `hasStereoscopic3D` for the future 3D question below) populated once,
   passed down read-only.
2. **Game input configuration** (what a specific title's `config.ini`
   — see VC proposal — *wants* mapped to which logical input): e.g.
   "map logical action JUMP to physical button A, map logical action
   AIM to the C-Stick if present, else fall back to the Circle Pad."
   This is data, not platform code — it should live entirely in
   `config.ini`, parsed into a small mapping table the input layer
   consults each frame.
3. **The join between them:** at boot, the input layer intersects what
   the config *wants* against what the hardware *has*, producing the
   actual live mapping, with an explicit, loggable fallback rule set
   (e.g. "C-Stick unavailable → AIM falls back to Circle Pad" rather than
   silently doing nothing) — consistent with this project's existing
   never-crash/never-silently-fail design principle (§A).

This is a small, self-contained addition on top of the existing
`InputState`/`Nintendo3DSInput.cpp` structure — it does not require
restructuring anything already built, and composes cleanly with the
per-title `config.ini` proposed in §VC below.

### G.2 — Future 3D-rendering consideration (not immediate)

Audited today: `src/renderer/Nintendo3DSRenderer.cpp` and the
`SceneRenderer`/`ShapeTessellator` split were **not** designed with
stereoscopic rendering in mind — there is a single render-target concept
per screen, no left/right-eye offset parameter anywhere in the matrix/
transform pipeline (confirmed by reading the transform-composition call
sites; no stereo-related field exists on any transform/camera-like
struct). This does **not** currently block anything (no corpus content
needs 3D), but a future phase wanting real stereoscopic 3D on New3DS
hardware should expect to add an eye-offset parameter threaded through
the render call, not assume today's single-pass renderer trivially
supports it. Flagged so a future architecture decision isn't made
assuming this is already handled.

---

## H. Achievement investigation — PROPOSED architecture (zero code exists)

Confirmed zero-hit grep for "achievement" anywhere in `src/` today. Pure
design proposal, explicitly scoped to be platform-independent (must
survive a hypothetical port to PC/PS3/other platforms per the user's
instruction) and generic (not Hobo-specific), and explicitly **not**
blocking the primary real-game-execution goal — this is a stub proposal
only, not scheduled into the near-term roadmap (§N/§O).

```
achievement::Definition   { id, title, description, hidden? }
achievement::Trigger      { definitionId, condition }  // e.g. "ExternalInterface
                                                          //  callback X invoked
                                                          //  with arg == Y",
                                                          // "frame label Z
                                                          //  reached", or a
                                                          //  small scripted
                                                          //  predicate
achievement::Store        (interface) { isUnlocked(id), unlock(id), timestamp(id) }
  ├─ InMemoryStore          (default/test — mirrors NullAudioBackend/
  │                          NullFileLoader's pattern exactly)
  └─ PersistentStore         (backed by §F's shared "platform persistent
                               storage" primitive — same underlying blob
                               mechanism as save data, different key
                               namespace)
achievement::Dispatcher    // subscribes to ExternalInterface callback
                            //   invocations, frame-label transitions, and
                            //   MovieClipInstance lifecycle events
                            //   (already-existing hook points — no new
                            //   engine-core plumbing needed) and checks
                            //   Trigger conditions against them
achievement::UiPresentation (interface) { onUnlock(Definition) }  // platform
                                                                    //   backend
                                                                    //   renders
                                                                    //   a toast/
                                                                    //   overlay;
                                                                    //   3DS impl
                                                                    //   deferred,
                                                                    //   same
                                                                    //   pattern
                                                                    //   as every
                                                                    //   other
                                                                    //   backend
                                                                    //   seam in
                                                                    //   this
                                                                    //   codebase
```

Design principle: the `Dispatcher` hooks into **existing** engine
surfaces (`ExternalInterface` callback invocation, already real and
wired per `docs/compatibility-matrix.md` §7; frame-label transitions,
already real via `Timeline`) rather than requiring new instrumentation
points — this keeps the achievement system a pure add-on layer, matching
the "must not block real-game-execution work" constraint explicitly.
**Not implementable against real evidence today** — no corpus game has
been shown to need achievements, and no achievement-definition format
for Hobo/Extreme Pamplona exists to design triggers against; this section
is architecture-only, correctly sequenced last in the roadmap (§N).

---

## VC / CIA container architecture — PROPOSED (smallest clean format,
not the example blindly adopted)

Per the user's explicit instruction not to blindly implement a suggested
format, the proposal below was derived by first re-reading what actually
exists (`nintendo3ds_main.cpp`'s hardcoded single-SWF-plus-diagnostic-
picture structure) and what the Hacks Guide Wiki confirms about real VC
`romfs` conventions (§F.3), then proposing the smallest structure that
covers the concrete, stated requirements (swappable main SWF without
rebuild; optional independent bottom-screen SWF; text-editable input
remap with no recompile) — not a maximal, speculative format.

```
title.cia
└── romfs/
      game.swf          # required — top-screen content
      game_bottom.swf   # optional — if absent, bottom screen falls back
                         #   to current behavior (diagnostic picture, or
                         #   simply blank) rather than erroring
      config.ini        # optional — see format below; absent = all
                         #   defaults (matches current hardcoded behavior
                         #   exactly, so this is backward-compatible with
                         #   the existing demo app)
```

**Boot flow:** CIA → RomFS mount → check for `config.ini` (defaults if
absent) → check for `game_bottom.swf` (skip dual-stage if absent) →
resolve input mapping (§G.1's capability∩config join) → load `game.swf`
into the top-screen `MovieClipInstance` root (and `game_bottom.swf` into
an independent bottom-screen root, if present — this requires
`SceneRenderer`/the main loop to support two fully independent root
movies rendered to two independent screen targets, which does **not**
exist today; `nintendo3ds_main.cpp` currently has exactly one Flash
content root, plus a hardcoded non-SWF diagnostic picture on the other
screen — this is real new engine work, not just a file-format addition,
and should be scoped as its own roadmap phase, §N Phase 12) → present.

**`config.ini` — smallest clean format proposal**, deliberately flat and
line-oriented (no nested sections beyond one flat `[input]` block, since
the stated requirement is specifically "a text-editor-based per-game
input-mapping configuration with no recompile," not a general-purpose
config language):

```ini
[input]
; logical action = physical button, with an optional fallback chain
; consulted left-to-right; the first physically-available entry wins
JUMP = A
AIM = CSTICK, CIRCLEPAD
PAUSE = START

[display]
; optional — omit entirely to keep current hardcoded top/bottom behavior
bottom_swf = game_bottom.swf
```

This is intentionally narrower than a general engine-config format —
extending it later (e.g. adding a `[save]` or `[achievements]` section
once those subsystems are built) is additive, not a breaking redesign,
because the parser should be a simple flat key=value-per-section reader
from the start rather than anything more elaborate. **Not built this
session** — this is the proposed shape only, correctly sequenced as its
own roadmap phase (§N).

---

## I. Hobo 1–7 blocker matrix (per-title, reconciled against the
now-confirmed-working event dispatcher — corrects `docs/real-game-
compatibility.md`'s 2026-08-18 "Next blocker recommendation," which
predates that confirmation)

Base per-game tag/opcode/AS2/button/sound/rendering data: `docs/real-
game-compatibility.md` (spot-verified accurate today; not re-scanned
from scratch — its own per-file SWF-metadata/tag-histogram numbers are
static facts about static corpus files, not something that could have
drifted). What's NEW in this section: reconciling those per-game facts
against everything confirmed-changed since that document was written
(event dispatch — done; MP3 decode — done; dynamic instantiation/
loadMovie — done, though 0 Hobo files use any of that family).

| Game | Structurally unique vs. Hobo1 | Button dispatch | Sound | Rendering gap | RAM | **Current top blocker** |
|---|---|---|---|---|---|---|
| Hobo 1 | baseline (3,575 chars, 2,990 shapes, 19 morph shapes, 16 `DefineButton2`, 0 bitmaps) | WORKING (condActionsV2 wired); all 3 frame-1 buttons are **keypress-only** in their own condActionsV2 records (0 mouse-transition bits) — confirmed by `button_scan` census, not a dispatcher bug | MP3 decode works; never confirmed audible | 19 `DefineMorphShape` chars absent from render | ~89.8 MB peak (post-Option-A) | **(1) RAM vs. 3DS budget — 2.5–4x over. (2) Unresolved: what actually progresses the title screen** — a real `CondKeyPress` ("End") produces a measurably different render, but Hobo1's own 3 buttons are keypress-only per their binary data, so the open question is *which specific key(s)* real players use, not a runtime gap — needs either manual disassembly of the exact `CondKeyPress` code(s) those 16 `DefineButton2` records specify, or a broader keypress sweep test. `DefineMorphShape` rendering is a visible (not blocking) gap. |
| Hobo 2 | +bitmap content (9 `DefineBitsLossless`, 1 `DefineBitsJpeg3`), +`_parent` usage, 17 `DefineButton2` | Same as Hobo1 (same native-button pattern, not re-scanned per-button this audit) | Same | Bitmap tags (10) unresolved — no bitmap character type exists at all (§C item 7) — new gap vs. Hobo1 | Not individually re-measured (INFERRED between Hobo1/Hobo5 bounds per `memory-audit.md` §4) | Same as Hobo1, plus bitmap rendering is now a visible gap (0 in Hobo1) |
| Hobo 3 | Larger content volume only (37 `DefineSound`, 24 morph shapes, 19 `DefineButton2`); drops the bitmap tags Hobo2 introduced | Same | Same | Same morph-shape gap, 0 bitmap gap (no bitmaps in this file) | Not individually measured | Same as Hobo1 |
| Hobo 4 | Monotonic size growth only, 20 `DefineButton2`, 27 morph shapes | Same | Same | Same | Not individually measured | Same as Hobo1 |
| Hobo 5 | Largest/densest file (8,626 chars, 21 `DefineButton2`, first `GotoLabel` opcode, `DefineFontName` tag) | Same | Same | Same | **261.6 MB peak — the worst RAM case in the corpus, 8–10x any plausible 3DS budget** | **RAM is the single dominant blocker for this specific title** — even Option A's 1.74x reduction leaves this file furthest from any 3DS budget of the whole corpus |
| Hobo 6 | Introduces `DefineBitsJpeg2` + first `InitObject` opcode, 23 `DefineButton2` (most-buttons-so-far) | Same | Same | Bitmap gap (2 tags) | Not individually measured | Same as Hobo1, plus bitmap gap |
| Hobo 7 | Most `DefineButton2` (27) of the family, `Play` opcode highest-frequency in family | Same | Same | Same morph/bitmap gaps as Hobo6 | Not individually measured | Same as Hobo1, plus bitmap gap |

**Family-wide, honest uncertainty:** `GlobalObject`'s missing built-ins
(§C item 4) are flagged as a *possible* additional blocker across the
whole family (585–1,092 `CallMethod` opcodes per file is a real signal,
but which specific methods those calls target was never independently
disassembled — this audit does not manually walk that disassembly either,
for the same reason the original real-game-corpus phase didn't: it's a
multi-hour targeted-disassembly task, not a quick grep, and should be
scoped as its own roadmap phase if `GlobalObject` gaps are suspected of
blocking real progression once the button/keypress question above is
resolved).

---

## J. Extreme Pamplona blocker matrix

Single target (no per-sub-file breakdown attempted for the 24 content
sub-SWFs beyond what `docs/known-limitations.md` L6 and `docs/real-game-
compatibility.md` already establish for two representative examples,
`content/sounds_all.swf` and `content/level-pamp1.swf`).

| Layer | Status | Blocker? |
|---|---|---|
| Main loader itself | Parses fully; SWF v8, 800x400@24fps, 2 declared frames | Not a blocker by itself |
| Reachability of its own 24 content sub-SWFs | **Proven negative** — 0 `CallMethod`/`NewObject`/`NewMethod`/`GetURL`/`GetURL2` opcodes anywhere in its 142 AVM1 buffers (33,741 opcodes, both static-disassembly and live-runtime-trace agree over a 500-tick run); no `ImportAssets` tag; no wrapper HTML found | **Yes, and unfixable by more runtime features** — this file's original loading mechanism (most likely external HTML/JS embedding multiple SWF instances, or content reorganized after corpus extraction) is not recoverable |
| `DefineButton2` in the main loader (5 chars) | `onPress`/`onRelease` property-handler strings ALSO present in this file — status UNKNOWN whether these 5 buttons use condActionsV2, property handlers, or both, without deeper disassembly | Both dispatch mechanisms are now WIRED (event-dispatch confirmed done, §B) — remaining uncertainty is which mechanism this file's specific 5 buttons actually use, not whether the runtime supports it |
| `DefineShape4` (135), `PlaceObject3` (345), `CsmTextSettings` (37), `DefineFont3` (5) | Not implemented | Visual-completeness gap only — not a crash/hard-block per the never-crash design principle |
| Two unrecognized tag IDs (253 x126, 255 x1) | Unidentified | Unknown impact — some bodies >5-10KB, so this is not a trivial reserved-field case; genuinely unresolved |
| RAM | 11.0 MB loader-only (well within budget) — but loading even a handful of its 24 sub-SWFs via the new Phase-4 `loadMovie()` would add unmeasured, unbounded cost per §D.1's `loadMovie`-never-frees finding | Not currently a blocker (loader alone is small), but a real risk the moment any content sub-SWF is loaded for testing |
| Sound | 2 `DefineSound` in the loader (MP3) — its 9 real sound banks live in unreachable sub-SWFs | Same reachability blocker as above |

**Bottom line for Extreme Pamplona:** the primary blocker is not a
missing runtime feature at all — it is that the corpus file as extracted
has no self-contained way to reach most of its own content. The
practical path forward (already partially enabled by Phase 4's
`loadMovie`) is treating the 24 content sub-SWFs as independently
loadable/testable assets in their own right (e.g. via a debug/dev harness
that calls `loadMovie()` directly, bypassing the main loader's absent
navigation) rather than expecting the main loader to ever drive them.

---

## K. Ghidra reconstruction map

Full existing table: `docs/reverse-engineering-map.md` (re-read in full
today, confirmed current — no new Ghidra queries were performed this
session beyond re-confirming Shift-DX is still the only loaded program).
Headline, not re-pasted: `avm1_builtin_prototypes_init` (0x2d228c) is the
single highest-value piece of Ghidra evidence this project has —
directly validates the MovieClip-method gap list in §C/§I. **Confirmed
today: no further Ghidra queries were attempted this session beyond the
initial connectivity check** — the four VC titles remain unloaded (§F.2),
and no new DAT_-pointer-resolution or function-boundary work was
attempted against Shift-DX itself this session (both remain open items
in the top-level `/home/claude/CLAUDE.md`, unrelated to this specific
audit's scope, since neither blocks real-game execution directly).

---

## L. JPEXS dependency map

**No JPEXS/ffdec MCP connection exists this session** (confirmed twice —
`RefreshMcpTools` lists only Google_Drive/claude-code-remote/
remote-devices/visualize/claude-in-chrome; `ToolSearch` for "jpexs"
returns zero matches). Every "what does the real SWF actually contain"
question in this document was instead answered via direct decompression
+ tag/opcode/string scanning of the real corpus files
(`docs/real-game-compatibility.md`'s own tooling,
`tools/swf_diagnostic/`, `tools/real_game_harness/avm1_loader_disasm.cpp`
+ `avm1_runtime_trace.cpp`) — a valid, evidence-grounded substitute for
this specific purpose (confirming real tag/opcode/API usage), though not
a substitute for JPEXS's visual timeline/asset browser for questions like
"what does this shape actually look like" or "what's tag 253's payload
structure." **If a JPEXS/ffdec MCP connector becomes available in a
future session, the two open unidentified-tag-ID questions (§C item 9)
and any visual-correctness verification (gradient/tessellation
correctness, §C item 8) are exactly the kind of question it would answer
directly that the current toolset cannot.**

---

## M. Re-prioritized limitations (per the user's explicit new ordering)

**Priority 0 — RAM/memory architecture.** See §D. Option B (lazy/on-
demand parsing) is the single highest-value next engineering effort;
`loadMovie`'s never-free design and sound-cache eviction are close
seconds, both cheap relative to Option B.

**Priority 1 — Sound completion.** See §E. Not "done" — on-device
audibility is unverified, `loopCount` is a stub, no eviction exists.

**Priority 2 — Save system.** See §F. Two distinct concepts (Flash
`SharedObject` vs. 3DS/VC container save), both proposed, neither built,
correctly low-priority since 0 corpus games currently call
`SharedObject` and no VC-container save has an implementation-ready spec
without more Ghidra evidence (§F.2).

**Priority 3 — Remaining core Flash runtime.** `GlobalObject` builtins
(`Math`/`Date`/`Number`/`String`/`Boolean`) remain the most likely
single item in this bucket to matter for the primary corpus (§I's
585–1,092-`CallMethod`-per-Hobo-file signal), ahead of `unloadMovie`/
drawing-API/`_level`-form-loading, which 0 corpus games currently need.

**Priority 4 — SWF rendering/tag compatibility.** `DefineMorphShape`/`2`
(all 7 Hobo games) ranks above `DefineShape4`/`PlaceObject3`/
`CsmTextSettings`/bitmaps (Extreme-Pamplona-and-partial-Hobo-only) by
corpus-wide reach, per §I/§J's per-game breakdown.

**Priority 5 — Input/event compatibility.** The event dispatcher itself
is done and confirmed working (§B) — this priority now narrows to: (a)
resolving exactly which key(s) progress Hobo's title screen (§I, a
disassembly/verification task, not a missing-feature task), (b) the
remaining 16 `onClipEvent` flags beyond Load/Unload/EnterFrame (L9,
never re-verified this or the prior audit — still open), (c) the New3DS
capability-detection layer (§G, zero code, but zero corpus content
currently needs it either — no game's interactivity has been shown to
require C-Stick/ZL/ZR).

---

## N. Multi-phase implementation roadmap (Phases 5+; Phases 1–4 done,
`577b063`)

See `docs/implementation-roadmap-2026-08-21-part2.md` for the full
per-phase detail (objective / exact files / evidence-to-gather / Ghidra
references / JPEXS references / expected implementation / tests /
real-game validation / 3DS validation / RAM impact / regression criteria
/ completion criteria for each phase, matching the original Phase 1–10
document's level of rigor). Headline phase list:

- **Phase 5 — RAM Option B (lazy/on-demand character parsing).**
  Highest-value single item per §D.2 #1.
- **Phase 6 — `loadMovie` bounding + sound-cache eviction.** Two small,
  independent, cheap fixes for §D.1's two newly-surfaced unbounded-growth
  risks; bundled into one phase since both are "add an eviction/cap
  policy to an existing cache" in shape.
- **Phase 7 — Resolve Hobo's title-screen progression trigger.**
  Disassembly/verification task (§I) — determines whether Priority 3/4
  work is even reachable for the Hobo family before spending engineering
  time there.
- **Phase 8 — `GlobalObject` builtins (`Math`/`String`/`Number`/
  `Boolean`/`Date` as needed).** Contingent on Phase 7's finding —
  proceed regardless if Phase 7 shows Hobo progression is blocked on a
  missing built-in call.
- **Phase 9 — `DefineMorphShape`/`2` parsing + rendering.** Highest-value
  rendering gap by corpus-wide reach (§I/§J).
- **Phase 10 — Bitmap tag family (`DefineBits*`) parsing + rendering.**
  Second-highest rendering gap by reach (Hobo 2/3/4/6/7 partial +
  Extreme Pamplona loader).
- **Phase 11 — Extreme-Pamplona-specific rendering (`DefineShape4`/
  `PlaceObject3`/`CsmTextSettings`/`DefineFont3`).** Lower priority than
  Phases 9–10 by corpus-wide reach, but unblocks Extreme Pamplona's own
  visual completeness once its content-sub-SWF reachability question
  (§J) is separately addressed by tooling, not runtime features.
- **Phase 12 — Dual-screen (top-SWF + bottom-SWF) engine support.** Real
  new engine work (independent second root-movie rendering to a second
  target), prerequisite for the VC-container proposal (§VC), not just a
  file-format change.
- **Phase 13 — `config.ini` parsing + input-capability layer (§G.1).**
  Depends on Phase 12 only for the `[display]` section; the `[input]`
  section is independently buildable against the existing single-screen
  app today.
- **Phase 14 — VC/CIA packaging (RomFS boot flow, §VC-proposal).**
  Depends on Phases 12–13.
- **Phase 15 — Save system (Flash `SharedObject` + 3DS/VC container,
  §F.4).** Sequenced after the primary-corpus-focused phases per
  Priority 2's ranking; revisit Ghidra evidence (§F.2) if the user opens
  one of the four VC titles before this phase starts.
- **Phase 16 — Achievement system (§H).** Explicitly last — architecture-
  only until a concrete title/trigger-set exists to design against.

---

## O. Immediate next phase

**Phase 5 — RAM Option B (lazy/on-demand character parsing).**

Rationale, directly from the evidence above: RAM is Priority 0 by
explicit user instruction, and remains the single most severe blocker to
"real Hobo/Extreme-Pamplona SWFs loadable and executable on the 3DS
runtime" — the project's own stated success criterion — regardless of
which other gap gets closed next. Even Hobo1, the smallest/simplest file
in the corpus, sits at ~85–90 MB post-Option-A, 2.5–4x over budget; Hobo5
sits at ~262 MB, 8–10x over. No feature-completeness work (morph shapes,
bitmaps, GlobalObject builtins, button-trigger investigation) changes
this number, and several of those additions (bitmap decode
specifically) would make it worse, not better, if built before RAM
architecture is addressed. This is not a new conclusion — `docs/
memory-audit.md` §8 and `docs/known-limitations.md` L5 already flagged
Option B as the next candidate — this audit's contribution is confirming
that ranking still holds against every other candidate surfaced by the
full re-prioritization exercise (§M), and surfacing two additional small,
cheap wins (Phase 6) that should be bundled alongside it rather than
independently deferred, since they touch the same "unbounded cache
growth" class of problem Option B's own design will need to reason about
anyway (a lazy-parse cache is, itself, another cache that needs an
eviction/bounding story — designing it alongside Phase 6 rather than
after it avoids solving the same problem twice).

Per the user's explicit instruction, implementation of Phase 5 is
**not started in this turn** — this document stops at identifying it as
the single highest-value next phase, per "First finish the audit and
roadmap. Then identify the SINGLE highest-value next implementation
phase" (not "implement it").
