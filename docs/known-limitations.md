# Known Limitations — flash3ds-runtime

**Rebuilt 2026-08-21 as part of a complete current-state audit** (see
`docs/current-state-audit.md`). Every item below was checked against the
actual current source tree this turn — not carried forward from this
file's own prior text uncritically. The prior version of this file (before
this rebuild) is preserved in `git log`/`git diff` for full historical
narrative (STEP-by-STEP writeups for each interactivity sub-fix, etc.);
this rebuild keeps that context brief and pointers explicit rather than
re-pasting it.

**Schema per item:** ID · Subsystem · Status · Source location · Evidence
· Affected SWFs (from the real corpus, `docs/real-game-compatibility.md`)
· Severity · Dependency · Proposed fix · Ghidra evidence · JPEXS evidence
· Test required.

---

## L1 — Sound codec decode (MP3)

- **Subsystem:** Audio.
- **Status:** **Implemented (Roadmap Phase 3, 2026-08-21).** MP3-format
  `DefineSound` audio now decodes to real PCM and is queued for real
  playback. Structural-only parsing for other codecs (ADPCM, Nellymoser,
  Speex, uncompressed) remains — see "Still not implemented" below; this
  item stays open (not closed/removed) for that reason.
- **Source location:** `src/audio/Mp3Decoder.{h,cpp}` (vendors
  `third_party/minimp3` — public-domain/CC0, chosen specifically to avoid
  LGPL static-linking concerns on a 3DS `.3dsx` target with no dynamic
  linking; see `third_party/minimp3/README.md`); `decodeSwfMp3Sound()`
  skips the 2-byte `SeekSamples` prefix the SWF spec's `MP3SOUNDDATA`
  puts before MP3 frame data. `runtime::ScriptEnvironment::
  playSoundById()`/`ensureSoundDecoded()` (`src/runtime/
  MovieClipInstance.{h,cpp}`) decode **on demand, once per distinct
  soundId, then cache** (both successful decodes and failures/unsupported
  formats are cached, so a repeat `StartSound`/`Sound.start()` for the
  same soundId never re-decodes or re-attempts). `IAudioBackend::
  loadSound()` (new virtual, default no-op — `NullAudioBackend` overrides
  it only to log) is the seam that hands decoded PCM to a backend before
  `playSound()`. `Nintendo3DSAudioBackend::loadSound()`/`playSound()` now
  really `memcpy` PCM into a `linearAlloc`'d (DSP-DMA-accessible) buffer
  and queue it via `ndspChnWaveBufAdd()` — genuine, not stubbed, and
  verified via a real 3DS cross-compile (`docs/3ds-toolchain.md`), but
  never run on real hardware/an emulator (same standing gap as the rest of
  this backend).
- **Evidence:** Real-corpus verification (`hobo.swf`, `hobo5.swf`,
  `hobo2.swf`, `hobo3.swf`, first 13 frames — their title/menu screens):
  every real `StartSound` tag that fires produces genuinely non-silent
  decoded PCM (RMS ~1888.4, confirmed via a standalone smoke tool), with
  decoded sample counts matching each `SoundDef::sampleCount` header field
  exactly (e.g. mono soundId=21: 26,496 decoded samples = 26,496
  `sampleCount`; stereo soundId=148: 20,736 total samples / 2 channels =
  10,368 = `sampleCount`). Exactly one `loadSound()`+`playSound()` call
  pair per distinct soundId, confirming the decode-once-cache design
  works as intended (a caching bug where `loadSound()` fired on cache
  HITS too, not just fresh decodes, was found via a failing integration
  test and fixed before this shipped — see `test_movieclip_instance.cpp`'s
  `MovieClipInstance_StartSoundTag_Mp3Payload_DecodesAndLoadsPcmBeforePlay`).
  Zero rendering/behavioral regression: full 8-game render harness
  produced byte-identical MD5s versus the pre-Phase-3 baseline for every
  frame, including Extreme Pamplona's pre-existing `exit=3` frame 3-5
  failure reproduced identically. 7 new decoder unit tests
  (`test_mp3_decoder.cpp`) + 1 new integration test; 287/287 total tests
  pass, zero compiler warnings (desktop and 3DS cross-compile).
- **Memory implications — MEASURED this phase** (see `docs/memory-audit.md`
  §9 for the full corpus table): real-content cost is negligible (one
  ~52KB decode per 13-frame title screen, immaterial against Hobo1's
  ~85MB post-Option-A baseline) — but that reflects only title-screen
  content, not full gameplay. The **worst-case** bound (every distinct MP3
  soundId in a file eventually triggered once, since
  `decodedSoundCache_` never evicts, and PCM is stored **twice** — once in
  `ScriptEnvironment`'s cache, once in `Nintendo3DSAudioBackend`'s own
  `memcpy`'d copy) is **~51-56MB per Hobo title** — a real, non-trivial
  addition, comparable in order of magnitude to Hobo1's entire
  `CharacterDictionary` peak. Flagged honestly, not glossed over; no
  eviction policy was built this phase since no real corpus content
  available in this environment exercises enough distinct sounds to prove
  the cache actually growing that large in practice.
- **Still not implemented (honest carry-over, not attempted this phase):**
  ADPCM/Nellymoser/Speex/uncompressed `DefineSound` formats still don't
  decode (a soundId in one of those formats never reaches `loadSound()` —
  `ScriptEnvironment` only ever attempts MP3 — so `playSound()` falls back
  to its original "channel reserved, nothing to queue, logged" path for
  those, unchanged from before this phase); a real finite `StartSound`
  `loopCount > 1` is **not** implemented as a true repeat on
  `Nintendo3DSAudioBackend` — a single `ndspWaveBuf`'s `looping` flag means
  infinite loop, not a counted repeat, and building a real counted repeat
  needs chaining multiple queued wavebufs, which needs on-device
  verification this environment cannot do — so `loopCount` is honored only
  as "play once," logged rather than silently dropped (see that file's own
  comment); no on-device/emulator audible confirmation has been done, per
  this project's own established "3DS code compiles and is real, but
  unverified beyond that" precedent (`docs/3ds-toolchain.md`).
- **Affected SWFs:** All 8 corpus games use MP3-format `DefineSound`
  exclusively (423 `DefineSound` tags total across the corpus, all
  confirmed MP3 via `sound_corpus_worstcase` this phase — 0 other-codec
  characters found in any corpus file).
- **Severity:** Downgraded from High to **Low-Medium** — decode now works
  for the format every corpus game actually uses; remaining severity is
  the loop-count gap (cosmetic for most content) and the still-unverified
  on-device audible confirmation.
- **Dependency:** Sequenced with awareness of the memory-audit findings
  per this project's own stated plan (decode-on-demand-and-cache was
  chosen specifically because it's cheaper than eager whole-dictionary
  decode, given shape data — not sound — is this project's dominant
  memory problem per §5-8).
- **Proposed fix (remaining):** ADPCM/other-codec decoders if a target
  title needs them (none of the 8 corpus games do); a counted-repeat
  `ndspWaveBuf`-chaining implementation for `loopCount > 1`, once
  on-device testing is possible to verify it; a bounded LRU/eviction
  policy for `decodedSoundCache_`/`Nintendo3DSAudioBackend::loadedSounds_`
  if a real gameplay session is ever shown to actually grow the cache
  toward the measured worst case.
- **Ghidra evidence:** None resolved — Shift-DX's own sound tag-loader
  boundaries were never successfully identified in the prior RE session
  (`docs/shift-dx-behavior.md`'s "Open items"). Not needed for this
  fix — implemented against the public SWF spec, not Shift-DX internals,
  per `CLAUDE.md`'s hard rules.
- **JPEXS evidence:** Not available this session (JPEXS/ffdecmcp not
  connected).
- **Test required:** Done — see "Evidence" above. Still open: on-device/
  emulator audible confirmation (blocked on hardware/emulator access, not
  on anything in this codebase).

## L2 — `GlobalObject` has zero named built-ins

- **Subsystem:** AVM1/AS2 object model.
- **Status:** **`Math` implemented (Roadmap Phase 8, 2026-08-25)** —
  `floor`/`ceil`/`round`/`abs`/`sqrt`/`pow`/`min`/`max`/`random`/`PI`/`E`,
  via the same `nativeImpl` pattern as Phase 6's `Key`/`Mouse`/`Sound`. A
  static disassembly pass (`tools/real_game_harness/avm1_loader_disasm.cpp`,
  keyword-filtered) across all 8 real corpus games plus a standalone
  hobo.swf copy found `Math.random()`/`Math.ceil()` calls (the classic
  `Math.ceil(Math.random() * n)` random-integer idiom) in hobo2/hobo3/
  hobo5/hobo6/hobo7 — 66 call sites each for `random`/`ceil` combined
  across the corpus — and ZERO `CallFunction`/`NewObject`/`CallMethod`
  hits for `String`/`Number`/`Boolean`/`Date` anywhere in the corpus.
  **`String`/`Number`/`Boolean`/`Date` remain DELIBERATELY NOT
  implemented** — same "don't build against a hypothetical" reasoning as
  L5/Phase 6 — pending real evidence. `floor`/`round`/`abs`/`sqrt`/`pow`/
  `min`/`max`/`PI`/`E` were added alongside the two evidenced methods
  anyway since they're the same trivial/stateless/zero-RAM shape and match
  the roadmap's own "at minimum" baseline — but only `ceil`/`random` have
  an actual traced call site in this corpus.
- **Source location:** `src/avm1/GlobalObject.{h,cpp}`.
- **Evidence:** `avm1_loader_disasm` run against
  `/home/claude/game-corpus/*/*.swf` + `/home/claude/hobo-testing/hobo.swf`,
  filtered for `Math`/`String`/`Number`/`Boolean`/`Date` — see this file's
  git history / `docs/avm1-compatibility.md`'s Phase 8 section for the
  full per-game breakdown. This is the disassembly pass this entry
  previously flagged as "not yet done, should not be skipped again" —
  it has now been done.
- **Affected SWFs:** hobo2/hobo3/hobo5/hobo6/hobo7 (confirmed `Math.ceil`/
  `Math.random` call sites); hobo1/hobo4/extreme_pamplona show none in the
  DoAction/DoInitAction streams this pass could statically resolve (a
  linear, non-control-flow-following disassembler — see its own header
  comment — so absence here is not proof of absence, only "not found by
  this pass").
- **Severity:** Downgraded from Medium-High to Low for the confirmed-used
  subset (`Math.ceil`/`Math.random` now resolve correctly instead of
  silently returning `undefined`); unchanged (Medium, unconfirmed) for
  `String`/`Number`/`Boolean`/`Date` since no evidence either way exists
  yet that any corpus content needs them.
- **Dependency:** None.
- **Real-game validation:** `tools/real_game_harness/run_harness.sh`
  frames 1-5 across all 8 corpus games: byte-identical MD5s before/after
  (verified via `git stash` on `GlobalObject.{h,cpp}` and re-running) —
  expected, since the confirmed `Math.ceil`/`Math.random` call sites sit
  deep in nested-sprite/gameplay ticks (tag offsets well past the title-
  screen frames this harness renders), not in frames 1-5's own scripts.
- **Ghidra evidence:** `avm1_builtin_prototypes_init` (0x2d228c) confirms
  `Object.prototype`/`String.prototype` method lists Shift-DX actually
  installs — see `docs/reverse-engineering-map.md`. Still a plausible-
  targets checklist for a future String/Number/Boolean/Date phase, not
  proof this corpus calls any of them.
- **JPEXS evidence:** Not available this session.
- **Test required:** Done — 9 new unit tests in `tests/
  test_avm1_interpreter.cpp` (`Math_Floor_*`/`Math_Ceil_*`/`Math_Round_*`/
  `Math_Abs_*`/`Math_Sqrt_And_Pow`/`Math_Min_And_Max_*`/`Math_PI_And_E_*`/
  `Math_Random_*`/`Math_UnknownGlobal_*`), all exercised via real AS2
  bytecode (`ActionCallMethod`/`ActionGetMember`) through
  `GlobalObject::create()`, not by calling the C++ lambdas directly.

## L3 — `MovieClip` OOP method surface gaps (dynamic instantiation family)

- **Subsystem:** AVM1/AS2 `MovieClip` API.
- **Status:** **`createEmptyMovieClip`, `duplicateMovieClip` (OOP form —
  the bare `CloneSprite` action-code form already existed),
  `removeMovieClip` (OOP form — likewise, `RemoveSprite`'s bare form
  already existed), `attachMovie`, `swapDepths`, `getNextHighestDepth`,
  and `loadMovie` (target-clip-replacement form only) implemented
  (Roadmap Phase 4 Steps 2-4, 2026-08-21 — see L4 and L6 below for
  `attachMovie`'s `ExportAssets` dependency and `loadMovie`'s own writeup
  respectively).** Still NOT implemented: `unloadMovie`, drawing API
  (`beginFill`/`lineTo`/`moveTo`/`curveTo`/`endFill`/`clear`/`lineStyle`),
  `createTextField`, `localToGlobal`/`globalToLocal`/`getRect`/
  `getBounds`/`setMask`.
- **Source location:** `src/runtime/MovieClipInstance.{h,cpp}` — the new
  primitives (`attachCharacter()`, `createEmptyChild()`,
  `swapDepthsWith()`, `nextHighestDepth()`, `loadMovie()`) plus their AS2-
  visible `handleNativeGet()` dispatch block (`attachMovie`/
  `createEmptyMovieClip`/`duplicateMovieClip`/`removeMovieClip`/
  `swapDepths`/`getNextHighestDepth`/`loadMovie`).
- **Evidence:** Direct `grep` this turn; corroborated by `docs/real-game-
  compatibility.md`'s AS2 string scans. **Update (Roadmap Phase 4,
  2026-08-21):** 9 new passing unit tests (`tests/test_movieclip_instance.
  cpp`) cover `attachMovie` (success + unresolved-linkage-name failure),
  `createEmptyMovieClip`, `duplicateMovieClip`, `removeMovieClip`,
  `swapDepths` (full bidirectional swap, not a one-sided move),
  `getNextHighestDepth` (empty and non-empty cases), plus
  `CharacterDictionary::findByLinkageName()` itself.
- **Affected SWFs:** **Extreme Pamplona positively confirmed** (AS2 scan
  finds `MovieClip`, `_global`, `createEmptyMovieClip`,
  `duplicateMovieClip`, `attachMovie` as literal strings — none of the 7
  Hobo files use any of them). This is an Extreme-Pamplona-specific gap,
  not a Hobo one.
- **Severity:** Downgraded from "High for Extreme Pamplona" now that L6's
  Step 1 finding disproved the original "Extreme Pamplona's own main file
  dynamically builds its UI via `attachMovie`" theory (see Dependency
  below) — **implemented anyway per explicit user direction** ("Build
  generic loading capability anyway"), since real corpus content (e.g.
  `content/sounds_all.swf`, `content/level-pamp1.swf`) genuinely uses this
  API family, just not reachable from Extreme Pamplona's own main-file
  script. Zero severity for the Hobo family either way (none of the 7
  files use any of these methods).
- **Dependency:** `attachMovie`/dynamic-linkage instantiation needs
  `ExportAssets` tag parsing — **L4 below, now also implemented.** **Update
  (Phase 4 Step 1, 2026-08-21):** the earlier assumption that Extreme
  Pamplona's *own main file* dynamically builds its UI via `attachMovie`
  is disproven by direct evidence — see L6's rewrite: the main file's
  entire AVM1 payload contains zero `CallMethod`/`NewObject`/`NewMethod`
  opcodes of any kind, so it cannot call `attachMovie` (named or
  otherwise) — implementing this item does NOT unblock Extreme Pamplona's
  own UI, and isn't expected to; it's forward-looking capability for other
  content/future titles, and makes the content sub-SWFs themselves
  directly loadable/testable as standalone assets.
- **Proposed fix (done):** Added each method as a native `nativeImpl`
  binding on `MovieClipInstance`'s scriptObject (via `handleNativeGet()`),
  reusing the existing scene-graph mutation primitives (`DisplayList`,
  `syncChildren()`) that already back `CloneSprite`/`RemoveSprite`, plus
  four new lower-level primitives (`attachCharacter()`,
  `createEmptyChild()`, `swapDepthsWith()`, `nextHighestDepth()`) those
  native bindings sit on top of.
- **Ghidra evidence:** `avm1_builtin_prototypes_init` — see
  `docs/reverse-engineering-map.md`'s table; confirms this exact method
  set is what a real shipped 3DS Flash port installs.
- **JPEXS evidence:** Not available this session.
- **Test required (done):** Per-method unit tests against synthetic
  fixtures — see the Evidence bullet above for the full list (9 tests). An
  Extreme-Pamplona-specific integration test was NOT pursued: Step 1's own
  finding (see L6) means Extreme Pamplona's main file has no in-bytecode
  path to call any of these methods, so such a test would only ever
  exercise this runtime's own synthetic call sites, not real corpus
  content driving them end-to-end.

## L4 — `ExportAssets` tag parsing (linkage-name resolution) — DONE (Roadmap Phase 4, 2026-08-21)

- **Subsystem:** SWF tag parsing / AVM1 dynamic instantiation.
- **Status:** **Implemented.** `CharacterDictionary::parseExportAssets()`
  (an anonymous-namespace helper in `CharacterDictionary.cpp`) parses the
  tag per the public spec (`Count: UI16`, then `Count` x `{characterId:
  UI16, name: STRING}`), populating a new `linkageNameToId_` map alongside
  the existing `characters_` map — scanned by the same recursive
  `scanTagsForCharacters()` pass as every other character-defining tag
  (including inside nested `DefineSprite` streams, same "SWF's character
  dictionary is global, not just top-level" reasoning that pass already
  applied to shapes/sounds/fonts/etc). `CharacterDictionary::
  findByLinkageName(name)` exposes the lookup; wired into `attachMovie`
  (L3 above) — `Sound.attachSound(name: String)`'s real linkage-name form
  (mentioned as a related carry-over) was NOT wired this phase, remains
  open.
- **Source location:** `src/runtime/CharacterDictionary.{h,cpp}`
  (`parseExportAssets()`, `findByLinkageName()`, `linkageNameToId_`).
- **Evidence:** `CharacterDictionary_FindByLinkageName_ResolvesExportedSprite`
  (`tests/test_movieclip_instance.cpp`) — a synthetic `ExportAssets` tag
  exporting a `DefineSprite` character under a linkage name, resolved back
  to the correct character ID; a query for an unexported name confirmed to
  return `nullptr`. `MovieClipInstance_AttachMovie_CreatesNamedChildAtDepth`
  covers the full parse → lookup → instantiate path end-to-end.
- **Affected SWFs:** Extreme Pamplona (131 `ExportAssets` entries in the
  loader — a real, heavily-used mechanism there, though see L3/L6 for why
  this doesn't by itself unblock Extreme Pamplona's own UI); 0 in every
  Hobo file.
- **Severity:** Resolved as forward-looking engine capability — see L3's
  Severity note on why this doesn't change Extreme Pamplona's own
  reachability picture.
- **Dependency:** Unblocks L3's `attachMovie` (done together this phase).
- **Ghidra evidence:** None specific to `ExportAssets` resolved.
- **JPEXS evidence:** Not available this session.
- **Test required (done):** Parser unit test against a synthetic
  `ExportAssets` tag; integration test resolving a linkage name via
  `attachMovie` — both present (see Evidence above).

## L5 — `CharacterDictionary::build()` / session peak memory (~7.9-43.6MB typical-session peak, was ~85-262MB post-Option-A / ~149-454MB pre-Option-A)

- **Subsystem:** Memory / core parsing.
- **Status:** Option A (compact `ShapeRecord`, 2026-08-21) AND Option B
  (lazy/on-demand `CharacterDictionary` parsing, 2026-08-24) are both now
  implemented — see `docs/memory-audit.md` §5c/§10 for full detail. This
  is a real, large, measured additional win on top of Option A, not just a
  smaller one: Hobo1's typical-session peak dropped a further ~3.5x (25.2MB
  from 87.7MB), Hobo5's ~5.9x (43.6MB from 255.5MB). **Important nuance —
  this lowers *peak-per-session*, not the *worst-case ceiling*:** a session
  that eventually references every character in a file still pays the full
  Option-A-reduced cost eventually (confirmed via `memory_breakdown`, which
  force-parses everything: Hobo1 46.16MB / Hobo5 143.16MB estimated
  total, unchanged from before Option B, exactly as
  `docs/implementation-roadmap-2026-08-21-part2.md` Phase 5 predicted).
- **Source location:** `src/runtime/CharacterDictionary.{h,cpp}`,
  `src/swf/ShapeRecords.h/.cpp`.
- **Evidence (Option A, 2026-08-21):** `hobo.swf` → 89.8MB peak / 85.3MB
  steady-state (was 153.6MB/149.2MB); `hobo5.swf` → 261.6MB peak (was
  453.9MB); Extreme Pamplona loader alone → 11.0MB (was 14.8MB).
- **Evidence (Option B, 2026-08-24 — isolated-process `mem_profile_check`,
  same methodology as Option A's measurement, 9-game corpus incl. Cat
  Ninja added this phase):**

  | Game | Peak before Option B | Peak after Option B | Reduction |
  |---|---:|---:|---:|
  | Hobo 1 | 87.70 MB | **25.17 MB** | 3.48x |
  | Hobo 2 | 93.24 MB | **26.98 MB** | 3.46x |
  | Hobo 3 | 116.98 MB | **30.09 MB** | 3.89x |
  | Hobo 4 | 140.27 MB | **33.23 MB** | 4.22x |
  | Hobo 5 | 255.48 MB | **43.58 MB** | 5.86x |
  | Hobo 6 | 147.96 MB | **35.52 MB** | 4.17x |
  | Hobo 7 | 155.84 MB | **37.41 MB** | 4.17x |
  | Extreme Pamplona (loader) | 10.76 MB | **7.88 MB** | 1.37x |
  | Cat Ninja | 13.33 MB | **13.31 MB** | ~1.0x (expected — its 63 bitmap tags aren't resolved into characters at all yet, so Option B has nothing to defer for them; see L-bitmap/roadmap Phase 10) |

  Zero render-harness regression: all 9 games' frame MD5s byte-identical
  before/after (Extreme Pamplona's/Cat Ninja's pre-existing out-of-range
  frame failures reproduced identically). 311/311 tests pass (was 304 —
  4 new lazy-parsing regression tests + this phase's memory-diagnostics
  tests).
- **Affected SWFs:** All 7 Hobo files (scales with size); Extreme
  Pamplona's loader is comparatively small, but would grow substantially
  once `loadMovie` (L6) pulls in its 23 content sub-SWFs (not measured —
  those files were never loaded by this runtime). Cat Ninja is NOT
  meaningfully affected by either memory option yet — its cost is
  dominated by 63 still-unresolved `DefineBitsLossless2` bitmap
  characters (roadmap Phase 10, not started), not by anything
  `CharacterDictionary::build()` currently parses.
- **Severity:** Downgraded from critical to moderate for Hobo1 specifically
  (21.2MB final/25.2MB peak — now UNDER the ~24MB Old-3DS app-heap target
  at steady-state, though peak briefly exceeds it during load) and Extreme
  Pamplona (7.9MB, comfortably under). **Still critical for Hobo5** (43.6MB
  peak — still ~1.8x over the ~24MB target) and by extension Hobo3/4/6/7
  (27-37MB). Still real, still not measured on actual 3DS hardware (see
  §5 of `docs/memory-audit.md` for the standing host-RSS-vs-3DS-heap
  caveat) — `src/platform/MemoryDiagnostics.{h,cpp}` (new this phase) is
  the first mechanism that could produce a REAL on-device number rather
  than a desktop proxy, but that on-device run itself has not happened in
  this sandbox (no 3DS/emulator access here — delivered for the user to
  run).
- **Dependency:** Both Option A and Option B are now implemented. The
  worst-case ceiling (`memory_breakdown`'s force-everything numbers,
  unchanged at 46.16MB Hobo1 / 143.16MB Hobo5) is the next thing that
  would need a different kind of fix (actual eviction, not just deferral)
  if a real play session's character-reference pattern turns out to
  approach it — not yet evidenced as a real (vs. theoretical) problem for
  this corpus's available content (title screens only).
- **Proposed fix — Option A (compact `ShapeRecord`): IMPLEMENTED
  2026-08-21.** Phase 2's byte-level breakdown
  (`docs/memory-audit.md` §5a) root-caused the cost to `swf::ShapeRecord`
  being a flat 120-byte struct carrying every SHAPERECORD sub-type's
  fields on every record regardless of actual type. Fixed by splitting
  the mutually-exclusive straight/curved-edge fields into a real C++
  union and moving the rarer style-change fields out-of-line behind a
  `std::shared_ptr` (chosen over a raw pointer / `unique_ptr` to keep
  `ShapeRecord` implicitly copyable, since `SceneRenderer.cpp`'s
  font-glyph-scaling path copies them). **Measured result: `sizeof(
  ShapeRecord)` 120→40 bytes (3.0x), not the ~8-10x/~35-40MB originally
  estimated when this was only a design** — see `docs/memory-audit.md`
  §5c for why the single-struct ratio doesn't carry straight through to
  the ~1.7x whole-pipeline reduction actually measured. All 279 tests
  pass; the 8-game render harness produced byte-identical output versus
  the pre-fix baseline (zero regression).
- **Proposed fix — Option B (lazy/on-demand character parsing): IMPLEMENTED
  2026-08-24.** `CharacterDictionary::build()` now only peeks each
  character-defining tag's leading CharacterId (2 bytes) and records
  {tag code, offset, length} in a `pending_` index; the real
  `swf::parseDefineX()` call happens on first `find()`, cached in
  `parsed_` thereafter. `DefineSprite` stays eager (already cheap — see
  `src/runtime/CharacterDictionary.h`'s file header for the full
  rationale). `find()`'s public signature/constness is unchanged, so every
  existing call site (`SceneRenderer`, `MovieClipInstance`, `ButtonInstance`,
  `CharacterBounds`) needed zero changes. See the Evidence table above for
  measured results; helps peak-per-session, not the worst-case ceiling
  (by design — see `docs/implementation-roadmap-2026-08-21-part2.md`
  Phase 5's own framing of this tradeoff, confirmed correct by the
  `memory_breakdown` ceiling numbers being unchanged).
  **Re-verified 2026-08-24 (same day, later session):** the code behind
  this "IMPLEMENTED" line was itself lost to a second sandbox reset before
  this later session began — see `docs/memory-audit.md` §12 for the full
  account — and was independently re-implemented from scratch against
  the actual (eager) source found on disk. Freshly re-measured this time:
  Hobo1 87.63→21.78MB peak / 17.66MB steady-state, Hobo5 255.48→38.02MB
  peak / 31.34MB steady-state, typically only 0.5-1.4% of a file's
  registered characters actually get parsed in a 5-frame session,
  worst-case-everything-touched converges back to within ~5% of the eager
  baseline (confirms deferral, not data loss). 352/352 tests pass, 8-game
  render harness byte-identical MD5s, 3DS cross-build clean. This line's
  "IMPLEMENTED" status is accurate again as of this re-verification, not
  merely carried over from the earlier (lost) work. Option C
  (tessellate-once-and-cache) — **checked and found NOT viable as
  originally conceived**, since `SceneRenderer` currently re-tessellates
  from raw records on every render call with no cache.
- **Resource eviction (task03's "M2" investigation, 2026-08-24) —
  investigated, deliberately NOT implemented this phase.** Checked
  `ScriptEnvironment::decodedSoundCache_` (never evicts, worst case
  ~51-56MB per Hobo title if a session eventually triggers every distinct
  sound — see `docs/memory-audit.md` §9) and `loadedMovies_`/
  `loadedCharacterDicts_` (`ownLoadedMovie()`, also never evicts). Real
  corpus content available in this environment (title/menu screens only)
  exercises at most 1 sound and 0 `loadMovie` calls per session, so there
  is no *measured* evidence either cache actually grows large in practice
  today — building eviction now would be tuning against a hypothetical,
  not a demonstrated problem, which this project's own stated principle
  (`docs/memory-audit.md` §9's own reasoning, restated by task03.txt's own
  "do NOT introduce unsafe eviction... reference-count or otherwise prove
  ownership before releasing anything") argues against. Flagged here as an
  open, worth-watching risk for a real (non-title-screen) play session,
  not silently declared fine.
- **Ghidra evidence:** None directly — Shift-DX's own tag-loader hash
  table (`DAT_002b4e40`) and its construction function were never
  resolved (top-level `CLAUDE.md`'s open item #2); no memory-footprint
  comparison available from Shift-DX RE.
- **JPEXS evidence:** Not available this session.
- **Test required:** A regression test asserting peak RSS for a fixed
  synthetic fixture stays under a defined ceiling, once any fix is
  designed (none exists yet — nothing to regress against today beyond the
  ad hoc `mem_profile_check` tool itself, which is not currently wired
  into CI/`ctest`).

## L6 — `loadMovie`/`_level`/multi-SWF loading — target-clip form DONE (Roadmap Phase 4, 2026-08-21); `_level` form still open

- **Subsystem:** Loading / multi-SWF.
- **Status:** **`MovieClip.loadMovie(url)`'s target-clip-replacement form
  implemented** (Roadmap Phase 4 Steps 2-4, 2026-08-21, per the user's
  explicit "build generic loading capability anyway" decision following
  Step 1's negative finding below) — see the new "What was implemented"
  section further down for exactly what this does and doesn't cover. The
  full `_level`-indexed sibling-movie form (`_levelN.loadMovie(...)`)
  remains NOT implemented — see that same section for why it was
  deliberately scoped out this phase. **Roadmap Phase 4, Step 1
  (2026-08-21) — targeted disassembly completed, with a real,
  evidence-based, negative finding that changes this item's whole
  picture — see below.**
- **Source location:** N/A (absent).
- **Evidence (original):** `grep` this turn, zero hits for `loadMovie`/
  `_level` anywhere in `src/` (i.e. this runtime never implemented it —
  says nothing about whether the *content* calls it).
- **Evidence (Phase 4, Step 1 — real disassembly + real runtime trace of
  the actual corpus content, not just a string grep):** Two independent
  tools were built and cross-checked against each other:
  `tools/real_game_harness/avm1_loader_disasm.cpp` (a static, best-effort
  AVM1 symbolic disassembler resolving `ActionPush`/`ActionConstantPool`
  string literals through `CallFunction`/`CallMethod`/`NewMethod`/
  `NewObject`/`GetURL`/`GetURL2`) and
  `tools/real_game_harness/avm1_runtime_trace.cpp` (which instead RUNS the
  real `avm1::Interpreter` against the real movie via the normal
  `MovieClipInstance::createRoot()`/`advanceFrame()` boot path, with a new
  optional `ExecutionContext::callTraceSink`/`ScriptEnvironment::
  callTraceSink` hook — see those files, `src/avm1/ExecutionContext.h`,
  and `src/avm1/Interpreter.cpp` — reporting every such action with its
  REAL runtime-resolved values, immune to the string/name obfuscation
  that defeated the static pass). Both agree, and a 500-tick (~20
  real-seconds-at-24fps) live run confirms it holds regardless of how
  long the movie runs: **`extreme-pamplona.swf`'s entire AVM1 payload
  (142 bytecode buffers — every `DoAction`/`DoInitAction`/button
  `actionsV1`/`condActionsV2`, 33,741 opcodes total, including all 126
  nested `DefineFunction` bodies) contains ZERO `CallMethod`, `NewObject`,
  `NewMethod`, `GetURL`, or `GetURL2` opcodes anywhere.** All 126
  `DefineFunction`s are anonymous (empty name, empty params) and each is
  called exactly once via `CallFunction` with an empty resolved name (an
  immediately-invoked-function pattern, live-confirmed by the runtime
  trace, not just guessed from bytecode shape) — consistent with a
  config/constant-table decode step, not interactive game logic. No
  `ImportAssets`/`ImportAssets2` tag exists either (only `ExportAssets`,
  131x), ruling out the SWF-native declarative "shared library" import
  mechanism as well. **By contrast, several of the 24 real content
  sub-SWFs DO contain normal, unobfuscated-looking interactive AS2**
  (e.g. `content/sounds_all.swf`: `CallMethod`=58, `NewObject`=17,
  `GetMember`=302, `SetMember`=74, `DefineFunction2`=24;
  `content/level-pamp1.swf`: `CallMethod`=13, `SetMember`=205,
  `GotoFrame`=4 across 225 small per-sprite frame scripts) — the "hollow
  shell, real content elsewhere" split is specific to the OUTER/loader
  file, not a property of the whole game.
- **What this means:** `extreme-pamplona.swf`, as it exists in this
  corpus, contains **no AVM1-bytecode-expressible mechanism** to load,
  reference, or navigate to any of its own sub-SWF content files — not
  `loadMovie`/`loadMovieNum` (would need a resolvable non-empty
  `CallFunction` name; none exist), not `MovieClipLoader`/`attachMovie`/
  any other `CallMethod`/`NewMethod`/`NewObject`-based API (none exist at
  all, of any kind, anywhere), not `getURL` (0 occurrences), and not a
  declarative `ImportAssets` tag (absent). This is a **proven negative**,
  not an unresolved search — comprehensive (every action-bytecode buffer
  in the file, cross-checked by two independently-written tools, plus a
  long live run of the real interpreter) rather than partial. Whatever
  originally tied the main file to its 24 content siblings (most
  plausibly an external HTML/JS page embedding multiple SWF instances
  side by side, or a Projector/AIR-specific mechanism outside plain AVM1,
  or content simply reorganized/cut after these files were extracted) is
  **not recoverable by implementing more AVM1 runtime features** — it
  isn't there to find. No wrapper HTML/config file exists alongside the
  corpus files in this environment either (checked).
- **Affected SWFs:** Extreme Pamplona specifically — its 9 levels, 2
  player sprites, 4 music tracks, and 9 sound banks remain **100%
  external content**, unreachable from the main file's own script, for
  the reason above (not because this runtime is missing a feature it
  needs to implement, but because the content itself has no in-bytecode
  caller). Zero effect on any Hobo file (single self-contained movie).
- **Severity:** Confirmed by implementation — Extreme Pamplona's own main
  file still cannot reach any of its content sub-SWFs, exactly as Step 1
  predicted (nothing in this phase's work changes that; it was never
  expected to — see the user's own chosen option below). The value
  delivered is forward-looking engine capability for other/future titles,
  plus making Extreme Pamplona's OWN content sub-SWFs (several of which
  contain normal interactive AS2, per Step 1's evidence above) directly
  loadable/testable as standalone assets in their own right.
- **Dependency:** L3/L4 (both done alongside this) are prerequisites for
  `attachMovie` specifically, not for `loadMovie` — `loadMovie` needed
  only the new `IFileLoader` seam and `Movie`/`CharacterDictionary`
  ownership plumbing below.
- **What was implemented (Roadmap Phase 4 Steps 2-4, 2026-08-21):** per
  the user's explicit choice at the Step 1 decision point — "Build generic
  loading capability anyway" — real, working `MovieClip.loadMovie(url)`
  was added, deliberately scoped to the SIMPLER of real Flash's two
  `loadMovie` forms:
  - **Implemented — target-clip replacement:** calling `loadMovie(url)` on
    an EXISTING clip fetches `url` via a new `runtime::IFileLoader` seam
    (`src/runtime/IFileLoader.h` — mirrors `audio::IAudioBackend`'s
    injectable-platform-seam pattern exactly; default `NullFileLoader`
    always fails/logs, so every existing test and the CLI need zero setup
    changes), parses it as a fresh SWF, builds a fresh
    `CharacterDictionary` for it, and REPLACES the target clip's own
    content with the loaded movie's — tearing down every existing
    child/button first (same `runClipEvent(kUnload)`/`notifyRemoved()`/
    `notifyButtonRemoved()` path `syncChildren()`'s own prune loop uses),
    then rebinding the clip's `movie_`/`characters_` pointers and swapping
    in a freshly-built `Timeline`, then running the new content's frame-1
    script. The target clip's own depth/position/name/parent are left
    untouched — matches real Flash's documented "loaded content replaces
    the target clip's own content, keeping its depth/position/name"
    behavior. A real desktop `runtime::LocalFileLoader` (`src/runtime/
    LocalFileLoader.{h,cpp}`, plain `ifstream`-based) is provided as a
    genuinely usable implementation, not just a test stub — a
    `Nintendo3DSFileLoader` (cartridge/SD filesystem I/O) is explicitly
    deferred, matching this project's established precedent of not
    building 3DS backends ahead of verified on-device need.
  - **NOT implemented — `_level`-indexed sibling movies:** `_level0`/
    `_level1`/... (`_levelN.loadMovie(...)` loading a genuinely independent
    SIBLING root movie alongside the existing one, both rendered
    simultaneously) was explicitly judged too large a lift for this
    phase — it would require `SceneRenderer` to walk multiple independent
    root movies (currently assumes exactly one), plus non-trivial
    `Movie`/`CharacterDictionary` ownership-lifetime questions the
    target-clip form sidesteps (every `MovieClipInstance` holds
    non-owning raw `const Movie*`/`const CharacterDictionary*` pointers
    throughout the codebase — see `ScriptEnvironment::ownLoadedMovie()`'s
    own doc comment in `MovieClipInstance.h` for how the target-clip form
    solved this by having `ScriptEnvironment` itself own every loaded
    `Movie`/`CharacterDictionary` for the rest of the session, never
    freed). A future phase wanting the `_level` form should expect to
    revisit both of these, not just add a thin wrapper.
  - **Known narrow gap (documented, not glossed over):** a loaded
    sub-movie's DoInitAction bodies ARE (re)scanned so its own sprites
    instantiate correctly, but `ScriptEnvironment`'s single env-wide
    `movie_`/`characters_` binding (used by `Sound.attachSound(id)`/
    `playSoundById()`'s numeric-ID resolution) is NOT rebound to the
    loaded sub-movie — a loaded sub-movie's OWN sound characters won't
    resolve by numeric ID through that specific path. Widening that
    binding to be per-`MovieClipInstance` rather than env-wide was judged
    out of scope for this phase; see `loadMovie()`'s own doc comment in
    `MovieClipInstance.h` for the full detail.
- **Ghidra evidence:** None Extreme-Pamplona-specific (different Ghidra
  project, not loaded).
- **JPEXS evidence:** Not available this session.
- **Test required (done):** `tests/test_movieclip_instance.cpp` —
  `MovieClipInstance_LoadMovie_ReplacesTargetTimelineAndRunsNewScript`
  (full parse → teardown → rebind → new-frame-1-script-runs path, via an
  in-memory `MapFileLoader` test double, checking the target clip's
  timeline/frame-count/script-set-variable AND that its depth/name/parent
  are preserved), `..._NoFileLoaderWired_ReturnsFalseAndLeavesClipUnchanged`,
  `..._FetchedBytesNotAValidSwf_ReturnsFalseAndLeavesClipUnchanged` (both
  failure paths leave the target clip provably untouched). `tests/
  test_local_file_loader.cpp` — 5 tests covering `LocalFileLoader` itself
  against REAL temp-file I/O (not a synthetic fixture, deliberately, since
  it's the one genuinely OS-facing piece of this phase's work): absolute
  path, missing file, empty file, base-dir-relative joining, and
  absolute-URL-ignores-base-dir. The previously-envisioned "integration
  test against a real Extreme Pamplona content sub-SWF, loaded via the
  actual in-game path" remains not achievable as scoped — no such in-game
  path exists in the main file (Step 1's finding, unchanged) — but the
  content sub-SWFs are now directly loadable via `loadMovie()` as
  standalone assets, if a future session wants to test one that way
  manually/via the CLI rather than through Extreme Pamplona's own script.

### L6 addendum — 2026-08-27 re-verification with a REAL simulated click (Track B B1)

New evidence (found by direct inspection of the actual corpus files)
prompted a re-check of the "proven negative" above before writing any new
engine code, per two specific concerns about how that finding was
produced: (1) the static disassembly scanned top-level `DoAction`/
`DoInitAction`/button buffers, not the method bodies of 117 real
`#initclip`-registered AS2 classes (`__Packages.*`, from a licensed AS2
platform-game framework, `uk.co.flexiblefactory.platformgameframework`,
including a `LevelShell` class and `loadLevel`/`loadPlayer`/`loadClip`/
`MovieClipLoader`/`onLoadInit`/`onLoadProgress`/`getLevelsXML` method
names) that this file's content actually carries; (2) the live runtime
trace only ran idle ticks with no simulated click-through, so it could
never reach a code path gated behind clicking "Play" on the embedded
menu-state XML's `FrontPage`.

**Method:** a new tool, `tools/real_game_harness/pamplona_click_trace.cpp`,
combines `click_probe.cpp`'s real hover→press→release mouse simulation
with `avm1_runtime_trace.cpp`'s full `ScriptEnvironment::callTraceSink`
capture, run at every point of a grid sweep across the entire 800×400
stage (253 points, ~35px spacing), each preceded by idle setup ticks and
followed by idle settle ticks.

**A real, reproducible crash was found and fixed first** (this blocked
the investigation outright at some coordinates): several grid points
segfaulted deterministically (confirmed via `gdb`, backtrace below)
inside `ScriptEnvironment::firePropertyHandler()` →
`Object::getMember()`, called on a dangling `hoverClip_` pointer.
Root cause: `ScriptEnvironment::notifyRemoved()` (`MovieClipInstance.cpp`)
only cleared `hoverClip_`/`pressedClip_`/`hoverButton_`/`pressedButton_`
when they pointed AT the exact clip being removed (or, for buttons, at a
button whose immediate parent was that clip) — but all three real removal
call sites (`syncChildren()`'s display-list-driven prune,
`removeFromParent()`'s explicit `RemoveSprite`, and `loadMovie()`'s
target-clip teardown) destroy a clip's ENTIRE subtree at once. A
hover/press pointer into a GRANDCHILD (or deeper descendant) of the node
actually being removed survived the single-node check and dangled once
the subtree was destroyed. Fixed with a new
`ScriptEnvironment::notifyRemovedRecursive()` that walks the full subtree
(see its doc comment in `MovieClipInstance.h`), used at all three call
sites. Confirmed via AddressSanitizer: a synthetic regression test
(`EventDispatch_RemovingAncestor_ClearsGrandchildHoverClip_
NoDanglingPointerCrash`, `tests/test_event_dispatch.cpp`) reproduces a
clean `heap-use-after-free` under ASan against the unfixed code and is
clean against the fixed code; the original real-corpus crash (gdb
backtrace: `Object::getMember` ← `firePropertyHandler` ←
`dispatchPointerEvents` ← `MovieClipInstance::advanceFrame`) no longer
reproduces anywhere in a 253-point re-sweep of the same file. 369/369
tests passing, zero new compiler warnings.

**B1's actual finding, now with a working click simulation:** the
re-verification **confirms, rather than overturns, the proven-negative
finding above** — every one of the 253 grid points (hover→press→release,
full call trace captured) produces the exact same trace, with **zero**
`CallMethod`/`NewObject`/`NewMethod`/`GetURL`/`GetURL2` anywhere, and no
click-position-dependent difference of any kind. This directly answers
B1's question: **no, clicking does not make `LevelShell`/`MovieClipLoader`
calls fire** — not because clicking doesn't reach the interpreter (the
crash fix and the ASan-verified test prove clicks and hover *do* reach
real dispatch code), but because nothing on this file's frame-1 display
list has a mouse handler wired to anything beyond what Step 1 already
found (an anonymous-IIFE decode step).

**A plausible (evidence-backed, not fully confirmed) explanation for
*why*:** the only AS2 activity ever traced, click or no click, is a
repeating `CallFunction` with an EMPTY resolved name (runtime-resolved,
per `avm1_runtime_trace.cpp`'s own rationale — this isn't a static-guess
artifact), firing roughly once per tick regardless of input — consistent
with Step 1's "anonymous IIFE, immediately-invoked" finding, just
re-confirmed dynamically. `grep -rn "fromCharCode\|charCodeAt\|charAt\|
substr\|split\|join\|indexOf" src/avm1/` returns **zero hits** anywhere in
this runtime: **no AS2 String-value prototype methods are implemented at
all** (matches L2's "only `Math`, no `String`/`Number`/`Boolean`/`Date`"
finding, extended here to string *methods*, not just the constructors).
A dynamically-computed function/member name built via
`String.fromCharCode(...)`/`.charAt()`/`.substr()`-style decode logic —
exactly the shape real-world AS2 obfuscators use, and exactly what would
produce a legitimately-empty resolved name if those calls silently
resolve to `undefined` in this interpreter — is a plausible root cause,
not a confirmed one. Confirming it would need either JPEXS/FFDec
decompilation (unavailable this session — the `ffdecmcp` MCP server
disconnected partway through this investigation) or manual disassembly of
one of the 126 `DefineFunction` bodies to read its actual bytecode.

**Updated recommendation:** do not implement a native `MovieClipLoader`/
`loadClip` (B2) yet — B1's re-verification shows no code path reaches that
API at all, clicking or not, so implementing it now would be speculative.
The better-evidenced next step, if this file's own interactivity is worth
pursuing further, is implementing AS2 `String` prototype methods
(`fromCharCode`/`charAt`/`charCodeAt`/`substr`/`length`, the ones an
obfuscated name-builder would plausibly need) and re-running this same
click-trace sweep to see whether real, non-empty `CallMethod`/`NewObject`
calls appear once name resolution can actually succeed — a concrete,
falsifiable test of the hypothesis above, rather than another guess.
B3 (path resolution through `LocalFileLoader`) and B4 (the bespoke
external-XML-driven driver, a deliberate last resort) remain not started;
B4 in particular should stay a last resort until the String-methods
hypothesis above has actually been tried and found insufficient, not
skipped to preemptively.

### L6 addendum — 2026-08-27 (Track B B3, task #63): content/ path resolution confirmed, independently reinforces the "unreachable via AVM1" finding

B3 asked specifically whether the infrastructure a `loadMovie("content/
...")` call would need — `runtime::LocalFileLoader`'s relative-path
joining against the real, on-disk `content/` layout, then
`swf::SwfLoader` parsing the fetched bytes — actually works, independent
of whether Extreme Pamplona's own bytecode can ever reach it (the B1
addendum above already answered that: no).

**Method:** a new read-only tool,
`tools/real_game_harness/pamplona_content_loadmovie_probe.cpp` (no
copyrighted bytes committed — every path is discovered from the
filesystem at runtime, given a base directory on the command line, same
discipline as every other tool in that directory), run against the real
corpus (`extreme-pamplona.swf` + its real 24-file `content/` sibling
directory).

**Finding 1 (new, independent angle on the same conclusion):** the
literal substring `"loadMovie"` does not exist ANYWHERE in
`extreme-pamplona.swf`'s decompressed body. Since AVM1's
`ActionPush`/`ActionConstantPool` can only ever push a string that
literally exists in the file's own bytes, this means no script in this
file — however deeply obfuscated, however it resolves method names —
can call `MovieClip.loadMovie()` by name. This isn't a new discovery (the
B1 addendum's "zero `CallMethod` opcodes anywhere" finding already
implies it), but it's a genuinely independent check (string presence vs.
opcode-histogram absence — two different failure-proof angles on the
same file), and it rules out one theoretical loophole the opcode count
alone didn't: a `CallMethod` reached through some AVM1 mechanism this
runtime doesn't yet interpret would still need the string `"loadMovie"`
to exist somewhere to be usable, and it doesn't.

**Finding 2 (the actual B3 question — infrastructure verification):**
`LocalFileLoader(baseDir).loadFile("content/<name>")` was run against
all 24 real `content/*.swf` siblings. Every single one: (a) resolved to
the correct file (byte-for-byte identical to a direct absolute-path
read), and (b) parsed successfully via `swf::SwfLoader::loadSwf()` —
24/24, zero failures. `MovieClipInstance::loadMovie()`'s own C++ API
(the exact method, called directly rather than through AVM1 dispatch)
was also exercised end-to-end against a real content file
(`content/sounds_pamplona.swf`) and succeeded.

**What this confirms and doesn't:** the `LocalFileLoader`/`loadMovie()`
infrastructure itself has no defect — it is fully ready for a future B4
bespoke driver (or any other future title whose main file DOES reach
`loadMovie()` via real AVM1 bytecode) to use as-is, with zero further
plumbing work. It does **not** change B1/B2's conclusion that Extreme
Pamplona's own main file cannot reach this infrastructure today — B3 was
never expected to (see L6 addendum above's own recommendation, which
this doesn't override): the next well-evidenced step for Extreme
Pamplona's own interactivity remains task #67 (AS2 `String` prototype
methods + a re-run of the click-trace sweep), with B4 staying a last
resort until that's been tried.

**Regression / build:** read-only tool, no runtime behavior modified. Not
CMake-registered (matches this directory's established convention for
most tools — built standalone via `g++`, see the tool's own usage
comment). No new unit tests needed — `tests/test_local_file_loader.cpp`'s
existing 5 tests already cover `LocalFileLoader`'s own logic against
real (synthetic, temp-file) I/O; this tool's contribution is confirming
that logic against the REAL corpus's specific directory shape, which a
synthetic fixture can't stand in for.

### L6 addendum — 2026-08-27 (Track B, task #67): AS2 `String` prototype methods implemented — click-trace re-run is a confirmed NEGATIVE result

**What was implemented:** exactly the surface the L6 addendum above named,
nothing broader — `String.fromCharCode(...)` (a static method on a new
`String` global constructor object, `src/avm1/GlobalObject.cpp`) plus
`someString.charAt(pos)`/`.charCodeAt(pos)`/`.substr(start[, length])` as
new autoboxed instance methods on bare string PRIMITIVES
(`tryStringPrimitiveMethod()`, `src/avm1/Interpreter.cpp`'s
`ActionCode::CallMethod` handler — a bare AS2 string value has no
`avm1::Object`/prototype to attach a method to, so these are special-cased
directly against the raw `std::string`, the same pattern
`ActionCode::GetMember` already used for `someString.length`). 7 new
regression tests (`String_*`, `tests/test_avm1_interpreter.cpp`); the
existing `Math_UnknownGlobal_StringNumberBooleanDate_AreNotDefined` test
was renamed to `Math_UnknownGlobal_NumberBooleanDate_AreNotDefined` and had
its `String` assertion removed, per that test's own stated update
criteria. 382/382 tests passing (up from 375), zero new compiler warnings.

**Re-run of the click-trace sweep (the actual point of this task):**
`tools/real_game_harness/pamplona_click_trace.cpp`, rebuilt against the new
interpreter, re-run against the real `extreme-pamplona.swf` at a 32-point
grid sample (coarser than B1's original 253-point sweep, but spanning the
full stage) with the same hover→press→release→settle shape. **Result: byte-
identical to before this task.** Every trace line at every sampled point is
still exactly `CallFunction ()` — an empty resolved name — and a
grep across the full sweep output for `CallMethod`/`NewObject`/
`NewMethod`/`GetURL` still returns **zero** matches, unchanged from the B1
re-verification's finding.

**Why this is a clean (not inconclusive) negative result:** the L6
addendum's hypothesis was specifically that the empty `CallFunction` names
were the OUTPUT of an obfuscated name-builder using
`String.fromCharCode`/`.charAt`/`.charCodeAt`/`.substr` that this
interpreter previously couldn't evaluate (so any such expression would
have silently coerced to `undefined`, i.e. an empty string once
`Value::toString()`'d). That hypothesis makes a concrete, falsifiable
prediction: once those methods actually work, a script that depends on
them should start producing a non-empty, meaningfully different resolved
name. It didn't — the trace is identical down to the exact call count and
shape at every sampled point. This rules out the specific mechanism the
hypothesis proposed; it does not (and cannot) rule out some other,
unidentified obfuscation technique, but there is no remaining
evidence-backed reason to suspect one. Combined with L6's original Step 1
finding (all 126 `DefineFunction`s are anonymous and each is invoked
exactly once via `CallFunction`, live-confirmed), the simplest explanation
consistent with every piece of evidence gathered so far (Step 1's static
disassembly, the B1 re-verification's click-driven live trace, and this
task's String-methods re-run) remains the original one: a genuinely
anonymous, immediately-invoked config/constant-table decode step running
once per tick, not a name-resolution failure this runtime was causing.

**Updated recommendation:** per L6's own addendum, B4 (a bespoke
external-XML-driven driver) was to stay a last resort until the
String-methods hypothesis had "actually been tried and found
insufficient" — it now has been. Extreme Pamplona's own main-file
interactivity has exhausted the well-evidenced AVM1-runtime-feature
avenues this investigation identified (B1 disassembly+live-trace,
B3 infrastructure verification, this task's String methods); B4 is now the
only remaining path to Extreme Pamplona's own content specifically, should
a future session want to pursue it. This does not affect any Hobo file
(zero relationship to Extreme Pamplona's specific obfuscation/loading
situation), and the `String.fromCharCode`/`charAt`/`charCodeAt`/`substr`
methods themselves remain real, permanent, evidence-motivated engine
capability regardless of this negative result — they were a reasonable,
falsifiable thing to try, and having tried them narrows the remaining
hypothesis space for anyone picking this up later.

**Regression / build:** `cmake --build build -j` clean, zero warnings;
`ctest`/`./build/tests/flash3ds_tests` 382/382 passing. Click-trace re-run
is read-only (no runtime behavior recorded/changed by the tool itself);
raw sweep output not committed (matches this directory's real-corpus-output
discipline — no copyrighted trace content, only this summary).

## L7 — `DefineShape4`/`DefineMorphShape`/`2`/`PlaceObject3`/`CsmTextSettings` not resolved

- **Subsystem:** Rendering / SWF tag parsing.
- **Status:** `DefineShape4`/`PlaceObject3`/`CsmTextSettings`: still not
  recognized/parsed at all. **`DefineMorphShape` (v1, tag 46): DONE**
  (Roadmap Phase 9, 2026-08-25) — parsed (`src/swf/DefineMorphShapeTag.h/
  .cpp`), resolved into `CharacterDictionary`, and rendered
  (`SceneRenderer::renderMorphShapeCharacter`) using **START-side geometry/
  colors only (ratio=0)** — a deliberate, roadmap-pre-approved
  simplification, not full morph-tween support. `DefineMorphShape2` (tag
  84) remains explicitly unsupported — see below.
- **Source location:** `src/swf/DefineShapeTag.cpp` (early-outs on v4);
  `src/swf/DefineMorphShapeTag.h/.cpp` (new, tag 46 only — early-outs on
  tag 84); no `PlaceObject3Tag`/`CsmTextSettingsTag` files exist in
  `src/swf/`.
- **Evidence (original L7):** `docs/compatibility-matrix.md` §2, `docs/
  real-game-compatibility.md`'s per-game tag histograms.
- **Evidence (Phase 9 `DefineMorphShape` implementation):**
  1. `swf_diagnostic`'s tag histogram, re-run against all 8 corpus games:
     only tag 46 (`DefineMorphShape` v1) appears anywhere in the real
     corpus — **zero** hits for tag 84 (`DefineMorphShape2`). Scoped the
     implementation to v1 only, matching the existing `DefineShape4`/
     `LineStyle2` explicit-non-support precedent.
  2. A new evidence tool, `tools/real_game_harness/morph_ratio_scan.cpp`
     (walks every `DefineMorphShape` character ID and every `PlaceObject2`
     record targeting one, including inside nested `DefineSprite` tag
     streams), run against all 7 Hobo files: **every** morph placement
     uses `ratio=0` (explicit or absent) — zero non-zero ratios anywhere.
     This confirms the "start-shape-only" rendering simplification is not
     an approximation of convenience for this corpus — it is exactly
     correct, since no real placement ever asks for a mid-tween frame.
  3. Real-game render harness (`tools/real_game_harness/run_harness.sh`,
     frames 1-5, all 8 corpus games): MD5s are **byte-identical**
     before/after this phase (verified via `git stash` before/after
     rebuild+re-run) — the morph characters present in the real corpus
     are not placed anywhere within the title-screen frames this harness
     renders, so this phase has zero observable effect on the harness's
     existing coverage (same pattern Phase 8 found for `Math.random`/
     `Math.ceil`: real corpus usage sits deep in gameplay ticks the
     harness doesn't reach, not on frame 1-5).
- **Affected SWFs:** `DefineMorphShape` — all 7 Hobo files (16-27
  characters each; now rendered, start-side only), 0 effect on Extreme
  Pamplona's loader. `DefineShape4`/`PlaceObject3`/`CsmTextSettings` —
  Extreme Pamplona only (135/345/37 occurrences respectively), still not
  resolved, 0 effect on any Hobo file.
- **Severity:** Medium → **Low for `DefineMorphShape`** (now rendered,
  correctly for every real placement in the corpus); unchanged (Medium)
  for the three still-unresolved tag families — visual-completeness gaps,
  not crashes or hard blockers (unresolved characters are simply absent
  from rendering, confirmed not to error).
- **Dependency:** None.
- **Proposed fix:** `DefineMorphShape` (v1) — DONE. Three remaining
  independent, separable parser+renderer additions (`DefineShape4`,
  `PlaceObject3`, `CsmTextSettings`, plus `DefineMorphShape2` if future
  evidence ever surfaces it); can be sequenced by whichever game is being
  prioritized (see `docs/real-game-readiness.md`'s ranked list).
- **Ghidra evidence:** None specific.
- **JPEXS evidence:** Not available this session.
- **Test required:** `DefineMorphShape` — DONE:
  `tests/test_define_morph_shape_tag.cpp` (parser, 5 tests),
  `CharacterDictionary_Build_ResolvesDefineMorphShape` (dictionary
  integration), `SceneRenderer_DefineMorphShape_RendersStartSideGeometryAndColorOnly`
  (rendering — a synthetic morph with a small green START rect and a much
  larger red END rect, confirming only the START side's geometry/color
  paints). Remaining tags: per-tag parser unit tests against synthetic
  fixtures; visual/MD5-checksum regression tests against the real corpus
  render harness once implemented.

## L8 — Two genuinely unrecognized tag IDs (253, 255) in Extreme Pamplona

- **Subsystem:** SWF tag parsing.
- **Status:** Unidentified — `TagCode` enum has no entry for either;
  generic scanner logs and skips their bodies.
- **Source location:** N/A.
- **Evidence:** `docs/real-game-compatibility.md`'s Extreme Pamplona
  section (126 occurrences of 253, 1 of 255; some bodies >5-10KB).
- **Affected SWFs:** Extreme Pamplona only.
- **Severity:** Unknown — impact not determinable until the tag's purpose
  is identified.
- **Dependency:** None.
- **Proposed fix:** Investigate against the official SWF spec's reserved/
  unused ID ranges, or check whether it's a compiler/tool-specific
  extension (worth a targeted JPEXS/ffdec query **once that tool is
  actually connected** — currently cannot be investigated this way).
- **Ghidra evidence:** None.
- **JPEXS evidence:** Not available this session — this is exactly the
  kind of question JPEXS/ffdec would be well-suited to answer once
  connected.
- **Test required:** N/A until identified.

## L9 — `onClipEvent`'s remaining mouse/key flags beyond Load/Unload/EnterFrame

- **Subsystem:** AVM1 event model.
- **Status:** Not independently re-verified this audit beyond confirming
  the *property-handler* (`onPress`/`onRelease`/`onRollOver`/`onRollOut`)
  and `condActionsV2`/`CondKeyPress` mechanisms are real and tested (see
  L10 below, and `docs/current-state-audit.md` §3/§5). `docs/onclipevent-
  compatibility.md` remains the authority for the full 19-flag table;
  treat its content as current unless re-verified.
- **Source location:** Not re-read line-by-line this audit.
- **Evidence:** Deferred to existing doc.
- **Affected SWFs:** Unknown without re-verification.
- **Severity:** Unknown.
- **Dependency:** Related to but distinct from L10.
- **Proposed fix:** N/A — re-verification task, not a fix.
- **Ghidra evidence:** None specific.
- **JPEXS evidence:** Not available this session.
- **Test required:** Re-read `docs/onclipevent-compatibility.md` against
  current source as a Phase-1-adjacent verification task.

---

## L11 — Bare `ActionGotoFrame`/`ActionGotoLabel` incorrectly force-stopped the target timeline — FIXED (task #68, 2026-08-27)

- **Subsystem:** AVM1 interpreter / `Timeline`/`MovieClipHostBindings`
  integration (`src/runtime/Timeline.h/.cpp`,
  `src/runtime/MovieClipInstance.cpp`).
- **Status:** **Fixed and regression-tested.** `MovieClipHostBindings::
  gotoFrame(uint32_t)`/`gotoLabel(const std::string&)` — the interpreter's
  handlers for the bare `ActionGotoFrame` (0x81) and `ActionGotoLabel`
  (0x8C) action codes (`Interpreter.cpp`'s `GotoFrame`/`GotoLabel` cases)
  — were calling `Timeline::gotoAndStop()` directly, which unconditionally
  sets `playing_ = false`. Per the standard AS2-compiler convention (and
  this codebase's own already-correct `ActionGotoFrame2` handling, which
  explicitly calls `play()`/`stop()` itself right after repositioning
  based on its own flag bit — see `Interpreter.cpp` ~L377-393), a bare
  `gotoAndPlay(literalFrame)` compiles to `ActionGotoFrame` ALONE with no
  accompanying `ActionPlay`, relying on the target simply not having been
  stopped; `gotoAndStop(literalFrame)` compiles to `ActionGotoFrame`
  followed by a SEPARATE `ActionStop`. Calling `gotoAndStop()` from inside
  the bare-`GotoFrame` handler silently converted every
  `gotoAndPlay(literalFrame)`/`gotoAndPlay(label)` in the corpus into a
  `gotoAndStop`, with zero prior test coverage catching it (the one
  existing test using bare `GotoFrame`,
  `EventDispatch_ActionChangesTimeline_GotoAndStopOnParent` in
  `tests/test_event_dispatch.cpp`, only asserted `currentFrame()`, never
  `isPlaying()`).
- **Source location:** `src/runtime/Timeline.h/.cpp` (new neutral
  `Timeline::gotoFrame(uint32_t)`/`gotoFrame(const std::string&)` pair —
  repositions + rebuilds the display list exactly like
  `gotoAndStop`/`gotoAndPlay`, but deliberately leaves `playing_`
  untouched); `src/runtime/MovieClipInstance.cpp`
  (`MovieClipHostBindings::gotoFrame()`/`gotoLabel()` now call these
  instead of `Timeline::gotoAndStop()`).
- **Evidence:** Found via real-corpus investigation (task #68) into why
  `hobo.swf`'s "preloader" sprite (characterId=33, root frame 1, a real
  unmodified corpus file, named `"preloader"` in the SWF itself) appeared
  permanently stuck at its own local frame 1: its frame-1 script is
  exactly `gotoAndPlay(3)`, which compiles to bare `ActionGotoFrame(2)`
  with no trailing `ActionStop` — before the fix, this silently force-
  stopped preloader's own timeline the instant it ran, so
  `Timeline::advanceOneFrame()`'s `if (!playing_ ...) return;` guard then
  permanently blocked any further auto-advance. Confirmed via a
  pointer-identity debug-tracing pass (temporary `fprintf` instrumentation
  in `Timeline::gotoAndStop()`, reverted before the real fix landed) that
  cross-referenced live `Timeline*` addresses against a live probe run —
  this also caught and corrected a real self-made misattribution earlier
  in the same investigation, where a recurring `gotoAndStop(2)` call
  visible in `hobo_end_key_probe`'s call trace had been assumed to be a
  specific button's (`characterId=32`, nested in preloader, targeting
  `_root` directly) real effect, when pointer-address correlation proved
  every occurrence of that specific trace line actually landed on a
  completely different, unrelated object (`"mutebutton"`, depth 313 —
  root's own pause/mute overlay, matching `docs/hobo-title-progression.md`
  Phase 7's independently-documented finding about that same button).
- **Post-fix real-corpus re-verification:** with the fix applied,
  preloader's own local timeline now correctly reaches local frame 4
  (previously unreachable) and cycles as a normal two-frame loading-spinner
  animation (`3 -> 4 -> 3 -> 4 ...`), exactly as its own bare-GotoFrame
  bytecode intends. Character 32's real, byte-verified
  `_root.gotoAndStop(2)` button — nested inside preloader at its local
  frame 4 — was confirmed to genuinely fire and move ROOT's own
  `currentFrame()` from 1 to 2 for the first time in this investigation's
  history, once the End-key press edge was timed to land on a tick where
  preloader had already reached local frame 4 (see the narrower open note
  below). This closes task #68's original question with a definitive,
  demonstrated **yes** — the interpreter fix is both correct per spec and
  practically necessary for this exact corpus content, independent of
  whether it "unblocks Hobo1" in the broader roadmap sense (see the
  Track-A note below).
- **A narrower, separate note (not fixed, not claimed to be a bug):**
  `MovieClipInstance::advanceFrame()`'s per-tick order runs
  `dispatchButtonKeyPressesRecursive()` (root-only, using every child's
  CURRENT, pre-this-tick display-list/button state) BEFORE recursing into
  each child's own `advanceFrame()` for this same tick. This means a
  child's own newly-placed button (like character 32, which only exists in
  preloader's display list once preloader's own timeline reaches local
  frame 4) cannot be dispatched on the very tick that child first reaches
  the frame that places it — only from the following tick onward. Combined
  with `CondKeyPress` dispatch being press-edge-triggered
  (`InputState::isKeyPressed()`, not level-triggered), a single-tick
  End-key **tap** synchronized to tick 0 (the pattern every prior probe in
  this corpus's investigation history has used, including
  `hobo_end_key_probe.cpp`'s default) will systematically miss this
  button's press edge, because the button doesn't exist in the display
  list yet at tick 0's dispatch. This is a real, demonstrated timing
  interaction, not a hypothesis — but whether real Flash Player orders
  per-tick child-advance vs. button-dispatch the same way (and thus
  whether this is spec-accurate rather than an engine quirk) has not been
  investigated, so per this project's no-speculative-fix rule it is
  documented here rather than "fixed." It does explain why every prior
  probe run in this corpus's history (tap-on-tick-0) saw `root.currentFrame()`
  stuck at 1 even after this task's real fix landed.
- **Affected SWFs:** Systemic — any corpus content using literal-frame or
  frame-label `gotoAndPlay()` calls compiled to bare `ActionGotoFrame`/
  `ActionGotoLabel` (the standard AS2 compiler form), not specific to
  `hobo.swf`.
- **Severity:** Was high (silently broke a common, spec-mandated AVM1
  primitive with zero test coverage); now fixed.
- **Dependency:** None.
- **Proposed fix:** Done — see source locations above.
- **Ghidra evidence:** None (clean-room project; Shift-DX/gameswf are
  never consulted for implementation, per `CLAUDE.md`).
- **JPEXS evidence:** Not used.
- **Test required / done:** Three new regression tests in
  `tests/test_movieclip_instance.cpp`:
  `MovieClipInstance_BareActionGotoFrame_MovesPlayheadWithoutStopping`,
  `MovieClipInstance_BareActionGotoLabel_MovesPlayheadWithoutStopping`,
  and the contrast case
  `MovieClipInstance_BareActionGotoFramePlusSeparateStop_MovesPlayheadAndStops`
  (proving the OTHER real compiled form — bare `GotoFrame` immediately
  followed by a separate `ActionStop`, i.e. `gotoAndStop(literalFrame)` —
  still stops correctly, since the fix must not regress that path). Full
  clean rebuild + `ctest`: 374/374 passing (up from 371), zero
  regressions.

---

## L12 — Bitmap tag support (`DefineBits*`) — DONE (Priority Fix List item #2, 2026-08-31)

- **Subsystem:** SWF tag parsing / rendering (`src/swf/DefineBitsTag.h/
  .cpp`, new; `src/runtime/CharacterDictionary.h/.cpp`;
  `src/renderer/ShapeTessellator.h/.cpp`, `src/renderer/IRenderer.h`,
  `src/renderer/SoftwareRenderer.h/.cpp`,
  `src/renderer/Nintendo3DSRenderer.h/.cpp`,
  `src/renderer/SceneRenderer.cpp`).
- **Status:** **Fixed and regression-tested.** Every bitmap fill
  previously rendered as `ShapeTessellator`'s flat gray (160,160,160,255)
  placeholder — a hard visual ceiling for any title (Extreme Pamplona
  named specifically) leaning on bitmap art rather than vector shapes.
  `DefineBitsLossless`(20)/`DefineBitsLossless2`(36)/
  `DefineBitsJpeg2`(21)/`DefineBitsJpeg3`(35) — the only variants with
  real-corpus evidence (`tools/swf_diagnostic` tag histograms across all
  8 Hobo titles + Extreme Pamplona's loader and content sub-SWFs; zero
  `DefineBits`(6)/`JPEGTables`(8)/`DefineBitsJpeg4`(90) anywhere) — are
  now fully decoded to normalized RGBA8 (`swf::BitmapDef`) and rendered
  with real per-pixel nearest-neighbor sampling through the full
  `ShapeTessellator` -> `SceneRenderer` -> `SoftwareRenderer`/
  `Nintendo3DSRenderer` pipeline, mirroring the existing (2026-08-28)
  gradient-fill architecture end to end. See `docs/renderer.md`'s
  "Bitmap rendering" section for the full pipeline writeup and
  `docs/memory-audit.md` §13 for measured RAM cost.
- **New dependency:** `third_party/jpgd` (Rich Geldreich's
  Public-Domain/Apache-2.0 `jpeg-compressor` JPEG decoder), vendored via
  Ubuntu's `libjpeg-compressor-cpp-dev` apt package (raw.githubusercontent.com
  is blocked in this project's sandbox — same limitation
  `third_party/minimp3`'s own README already documents), built as an
  isolated CMake target (`jpgd_vendor`, `-w` to suppress the vendored
  code's own warnings without weakening this project's `-Wall -Wextra`
  everywhere else) and linked `PRIVATE` into `flash3ds_core`.
- **Real-corpus finding — a genuinely surprising second bug found during
  verification, not just the headline placeholder fix:** initial
  real-corpus testing (`tools/real_game_harness/bitmap_ram_probe.cpp`)
  found 6 of 7 real `DefineBitsJpeg2`/`3` tags across the corpus failed
  to decode at all. Investigation found the raw tag bytes are not always
  one self-contained JPEG stream as the SWF spec describes for these two
  tag types — several Flash-authoring-tool encoders instead embed a
  shared quantization/Huffman-tables segment, then a spurious 4-byte
  `0xFF 0xD9 0xFF 0xD8` ("EOI SOI") marker pair, then the real per-image
  segment which references the first segment's tables by ID without
  redefining them. `stripErroneousEoiSoiMarkers()` (`DefineBitsTag.cpp`)
  splices out every occurrence of that exact 4-byte sequence before
  handing bytes to `jpgd`, verified via a standalone probe against every
  failing corpus sample: real-corpus JPEG bitmap decode success went from
  ~14% (1 of 7 tags) to 100%, zero regression on the one sample that
  never had the quirk. A simpler, earlier theory (naively trim to the
  LAST SOI marker in the stream) was tried first and disproved with the
  same evidence — it discards the shared tables the second segment
  actually depends on, failing differently (`JPGD_UNDEFINED_HUFF_TABLE`/
  `JPGD_UNDEFINED_QUANT_TABLE`-class errors) rather than fixing anything.
  Regression-tested:
  `DefineBitsJpeg2_ErroneousEoiSoiMarkerPairMidStream_StillDecodesCorrectly`
  (`tests/test_define_bits_tag.cpp`).
- **Verification:** 446/446 desktop tests passing (up from 434 before
  this task — 11 new unit tests covering every supported BitmapFormat/
  colormap/JPEG-alpha/no-alpha/out-of-scope-tag path, plus one end-to-end
  `SceneRenderer` integration test,
  `SceneRenderer_BitmapFill_SamplesRealBitmapPixelsNotGrayPlaceholder`,
  confirming the full parse->tessellate->matrix-invert->rasterize pipeline
  samples real bitmap pixels rather than the gray placeholder), zero
  regressions, zero new compiler warnings. Real-corpus render-harness
  frames (Hobo2 all 13 frames, Extreme Pamplona's 2-frame loader) are
  byte-identical before/after this change — **not a red flag**: the same
  pattern this project's Roadmap Phase 8/9 entries already document (real
  bitmap-filled content in this corpus sits in gameplay/timeline frames
  outside the render harness's reachable frame range, same as
  `Math`/`DefineMorphShape` before it), confirmed by the dedicated
  end-to-end unit test above actually exercising the rendering pipeline
  directly rather than relying on the harness alone. 3DS cross-build
  (`build_3ds/flash3ds_3ds.3dsx`, RomFS-packaged) compiles clean, zero
  non-weak undefined symbols, `jpgd_vendor`/`DefineBitsTag.cpp`
  cross-compile without incident.
- **Explicitly not implemented:** `smoothed` (bilinear) filtering —
  sampling is nearest-neighbor only, same "no established
  texture-filtering precedent to extend" reasoning as other deferred
  render-quality work; a bitmap-cache eviction/streaming strategy — no
  evidence of real unbounded growth, same discipline as the pre-existing
  `loadMovie`/sound-cache eviction decision (§5b Option C /
  Roadmap Phase 6); `DefineBits`(6)/`JPEGTables`(8)/
  `DefineBitsJpeg4`(90) — zero real-corpus evidence.

---

## RESOLVED this audit (previously listed as open/uncertain — confirmed done)

### R1 — Button/clip event dispatch (`condActionsV2` + `onPress`/`onRelease`/`onRollOver`/`onRollOut` property handlers + `CondKeyPress`)

**Previously this file's own priority #2 (and `docs/real-game-
compatibility.md`'s "MISSING" verdict for every game).** Confirmed this
turn: **fully implemented, wired into the per-tick root `advanceFrame()`
call, 18 dedicated passing tests** — but **uncommitted**, and every doc
that referenced it as missing (including this file's own prior text) was
stale. See `docs/current-state-audit.md` §3/§5 for full evidence.
**Confirmed end-to-end against real game content, 2026-08-21 (Phase 1 of
`docs/implementation-roadmap.md`, now done):** the dispatcher itself works
correctly — a real `CondKeyPress` ("End" key) trigger against `hobo.swf`
produces a measurably different render than a no-input control. Simulated
mouse clicks on Hobo1's 3 documented frame-1 buttons produced no effect,
but a corpus-wide census (`tools/real_game_harness/button_scan.cpp`)
found this is because those specific buttons' own `condActionsV2` records
are genuinely keypress-only in the SWF binary (0 mouse-transition bits
set) — not a dispatcher bug. See `docs/real-game-readiness.md`'s "Phase 1
results" section for full detail, including the still-open question of
what actually triggers Hobo's title-screen progression.

### R2 — `ColorTransform`/`_alpha` rendering

Confirmed fixed and tested (compatibility-audit-phase priority #1,
committed). Not re-verified in depth this audit beyond confirming it's
still present in source; no evidence of regression found.

### R3 — `_width`/`_height`, `_xmouse`/`_ymouse` coordinate conversion, edge-detected input, bounding-box hit-testing, `ButtonInstance` runtime

All five "interactivity phase" sub-fixes confirmed present in source and
covered by passing tests this turn (part of the 279/279 total).

---

## Explicitly out of scope for this rebuild

Per-opcode AVM1 tables (`docs/avm1-compatibility.md`), the full SWF tag
support matrix (`docs/swf-support.md`), and the 3DS-specific limitation
list (`docs/3ds-limitations.md`) were **not** independently re-verified
line-by-line this audit — they were spot-checked where directly relevant
(GlobalObject, MovieClip methods, audio, loadMovie) and found accurate
where checked. Treat those three documents as still-authoritative unless
a future audit re-verifies them, and prioritize re-verifying
`docs/onclipevent-compatibility.md` first (L9) since it's the one most
likely to have drifted given how much changed in the uncommitted
event-dispatch phase.
