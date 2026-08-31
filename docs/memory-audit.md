# Memory Audit — flash3ds-runtime

**Date:** 2026-08-21. Fresh measurement against the actual current source
tree (see `docs/current-state-audit.md` §2 for exact git state). The
previously-reported "145MB" figure existed only in a now-lost session
checkpoint and was **not** reused uncritically — it was re-derived from
scratch below, and happens to match closely, which is itself a finding
(the bug is real and stable across at least two different points in this
project's history, not an artifact of one specific past build).

## 1. What "145MB" actually means — precisely classified

- **Peak, not steady-state.** The number is the highest `VmRSS` observed
  during a synthetic load-and-tick sequence (load bytes → decompress/scan
  tags → `CharacterDictionary::build()` → create root clip → construct
  renderer → advance 5 frames), not a resting/idle figure.
- **Desktop x86_64 process, not 3DS, not an emulator.** Measured via
  `/proc/self/status`'s `VmRSS` line in a native Linux build of
  `flash3ds_core`. This is a **proxy for likely 3DS heap pressure**, not a
  direct 3DS measurement — see §5 for why the two numbers won't match
  1:1 and in which direction the difference likely runs.
- **A single game's load path**, not the whole engine's footprint over a
  play session (audio buffers, render targets, and any future dynamic
  content — currently absent, see `docs/current-state-audit.md` — would
  add to this, not replace it).

## 2. Instrumentation

`tools/mem_profile_check/main.cpp` (rewritten from scratch this turn — the
version described in prior session notes no longer exists in this
environment and was not trusted; see `docs/current-state-audit.md` §0).
Compiled standalone against the real build output:

```
g++ -std=c++20 -O2 -I src -o /tmp/mem_profile_check \
    tools/mem_profile_check/main.cpp build/libflash3ds_core.a -lz
```

Checkpoints, in order, per file: process start → after reading raw SWF
bytes → after `SwfLoader::loadSwf` (decompress + tag scan) → after
`CharacterDictionary::build` → after freeing the raw compressed-file
buffer → after `MovieClipInstance::createRoot` → after `SceneRenderer`
construction → after each of 5 `advanceFrame()` calls.

## 3. Pipeline checkpoint table — `hobo.swf` (baseline reference file)

| Checkpoint | RSS (KB) | Δ from previous | Cumulative Δ |
|---|---:|---:|---:|
| Process start | 3,952 | +328 | +328 |
| After reading raw SWF bytes (4,967,978 bytes) | 13,848 | +9,896 | +10,224 |
| After `SwfLoader::loadSwf` (→ 7,729,004 decompressed bytes, 3,745 tags) | 21,904 | +8,056 | +18,280 |
| **After `CharacterDictionary::build` (3,575 characters resolved)** | **153,612** | **+131,708** | **+149,988** |
| After freeing the raw compressed-file buffer | 148,760 | −4,852 | +145,136 |
| After `MovieClipInstance::createRoot` | 149,188 | +428 | +145,564 |
| After `SceneRenderer` construction | 149,188 | +0 | +145,564 |
| After `advanceFrame()` #1-5 | 149,196 | +8 (once, then flat) | +145,572 |

**Peak RSS: 149.2MB. Steady-state after 5 ticks: 149.2MB (i.e. peak ≈
steady-state for this file — nothing frees back down again).**
`CharacterDictionary::build()` alone accounts for **~90%** of total
growth. Freeing the now-unneeded raw compressed-file buffer recovers only
~4.9MB (3% of the peak) — the cost is not "we're holding onto the
compressed bytes too long," it's the **parsed, in-memory representation**
itself.

## 4. Per-game comparison (PROVEN — measured this turn, not extrapolated)

| Game | File size | Characters resolved | Peak RSS | Peak vs. Hobo1 |
|---|---:|---:|---:|---:|
| Hobo 1 (`hobo.swf`) | 4.97 MB | 3,575 | **149.2 MB** | 1.0x (baseline) |
| Hobo 5 (`hobo5.swf`, largest/densest Hobo file) | 8.11 MB | 8,626 | **453.9 MB** | 3.04x |
| Extreme Pamplona (main loader only — no sub-SWF loading exists to pull in the other 23 files) | 1.00 MB | 441 | **14.8 MB** | 0.10x |

Character count and RSS do not scale exactly linearly (8,626/3,575 = 2.41x
characters but 3.04x memory) — plausibly allocator overhead/fragmentation
effects at larger heap sizes, or a nonlinear mix of character *kinds*
(e.g. proportionally more/larger shapes vs. sprites in Hobo5) — **not
independently isolated this turn; flagged as INFERRED, not PROVEN, and a
good target for the byte-level-by-character-kind breakdown noted in §7.**

Hobo 2/3/4/6/7 were not individually re-measured this turn (time-boxed);
given the monotonic size/character-count growth already documented in
`docs/real-game-compatibility.md` (Hobo1: 3,575 chars → Hobo7: implied
highest button/morph-shape counts of the family) they are expected to fall
between the Hobo1 and Hobo5 figures above. **INFERRED, not measured —
do not cite as a specific number.**

## 5. Desktop vs. 3DS — known differences (not independently measured this session)

- The 3DS's actual usable heap for a homebrew/`.3dsx` app is on the order
  of tens of MB (libctru's `HEAP_SIZE`/`LINEAR_HEAP_SIZE` split, soft-capped
  around 24MB app-heap / 32MB linear-heap by default, with the real ceiling
  depending on the process's resource-limit "remaining commit" — see prior
  session research, not re-verified this turn). **149MB for Hobo1 alone is
  already 4-6x any plausible 3DS heap budget before touching rendering,
  audio, or input state.**
- Desktop `malloc`/glibc and 3DS's `newlib`+`libctru` allocator have
  different overhead-per-allocation characteristics; the *absolute* 3DS
  number could be somewhat lower or higher than the desktop figure for the
  same logical data, but the **order of magnitude gap (149MB vs. a
  <32MB realistic budget) is not going to close from allocator differences
  alone.** This is a PROVEN structural problem, not a measurement artifact.
- This was **not measured on real 3DS hardware or an emulator this
  session** (no 3DS/Azahar execution environment available in this cloud
  sandbox) — the desktop figure is the best available proxy, not a
  substitute for an eventual on-device measurement (see roadmap Phase 2).

## 5a. Phase 2 (2026-08-21): byte-level breakdown — PROVEN, not INFERRED

`tools/real_game_harness/memory_breakdown.cpp` walks every resolved
character via the same public `CharacterDictionary::find()` API real code
uses, summing `sizeof()` of each `CharacterDef` variant arm's struct plus
its heap-owned vector element counts (recursing into `ShapeRecord`'s own
nested `newFillStyles`/`newLineStyles` for mid-shape style changes, and
into each `FillStyle`'s `Gradient::records`). This is a `sizeof()`-based
**estimate** (does not model glibc malloc's own per-allocation bookkeeping,
`unordered_map` bucket/node overhead, or vector capacity-vs-size slack —
all real, all additive, not included below) — but it closely tracks the
independently-measured RSS delta:

| Game | Estimated bytes | Measured `CharacterDictionary::build()` RSS delta | Estimate accounts for |
|---|---:|---:|---:|
| Hobo 1 | 127.83 MB | 131.7 MB | **97%** |
| Hobo 5 | 404.43 MB | 415.1 MB | **97%** |
| Extreme Pamplona (loader) | 6.38 MB | 11.2 MB (14.8MB peak − 3.6MB pre-dict) | 57% (small-scale: fixed per-allocation overhead the estimate doesn't model matters proportionally more at this size) |

**The dominant cost is confirmed, not inferred: shapes account for
94.5–98.5% of the estimated total in all three games measured.** Sprites,
sounds, fonts, text, buttons, and edit-text combined never exceed ~5.5%.

**Root cause, PROVEN via direct `sizeof()` measurement:**
`swf::ShapeRecord` (`src/swf/ShapeRecords.h`) is a **flat, non-tagged
struct carrying every SHAPERECORD sub-type's fields simultaneously** —
`StyleChangeRecord` fields (move-to coordinates, 3 `std::optional<uint32_t>`
style indices, `hasNewStyles`, and **two full `std::vector`s** for
`newFillStyles`/`newLineStyles`), `StraightEdgeRecord` fields (2 deltas),
and `CurvedEdgeRecord` fields (4 deltas) all exist on **every** record
regardless of which `type` is actually in use. Measured: **`sizeof(swf::
ShapeRecord) == 120 bytes`**, even though the overwhelmingly common case
(a straight or curved edge) needs only 8–16 bytes of real data. Hobo1's
2,990 shape characters contain **1,069,989 total `ShapeRecord`s** — at 120
bytes each, that's **124.3MB by itself**, matching the measured total
almost exactly.

**This also resolves memory-audit.md's previously-UNKNOWN nonlinearity**
(§7 below, corrected): Hobo1→Hobo5 character count grows 2.41x but memory
grows 3.04x. The byte breakdown shows why: `ShapeRecord` count grows
**3.20x** (1,069,989 → 3,423,729) — Hobo5's shapes are not just more
numerous, they're individually more geometrically complex (records/shape
rises from ~358 to ~444). Memory tracks record count almost exactly, not
character count — the earlier "UNKNOWN cause" is now PROVEN, not merely
better-explained.

**Correction to this document's own earlier §6:** the original version of
this section (written before this breakdown) claimed `SoundDef` "retains a
full copy of each sound's raw MP3 byte stream." **This was wrong — checked
directly against `src/swf/DefineSoundTag.h` this phase.** `SoundDef` only
stores a `dataOffset`/`dataLength` pair indexing into `Movie::data`
(exactly like `SpriteDef`'s tag records) — there is no byte copy. Sound's
measured contribution above (0.0% in all three games) confirms this.

## 5b. Fix options (design only — NOT implemented, per this phase's own scope)

Three options, informed directly by §5a's evidence:

### Option A — Compact/tagged `ShapeRecord` representation — **IMPLEMENTED 2026-08-21, see §5c for real measured results**

Replace the flat 120-byte-per-record struct with a packed representation
that only stores the fields each record's actual type needs (e.g. a
`std::variant`-of-smaller-structs, or parallel typed arrays, or a
variable-length packed byte encoding). A straight-edge record needs ~9
bytes of real data (1 type tag + 2×`int32`); a curved edge needs ~17; a
style-change record (the rare case — most of a shape's records are edges)
still needs the two style vectors when `hasNewStyles` is set, but that
cost is already legitimate and small in aggregate (18,833 fill styles +
3,508 line styles across all of Hobo1, vs. 1,069,989 records — style
changes are a small minority of records).

~~**Estimated savings:** if the average record shrinks from 120 bytes to
roughly 12-16 bytes (an 8-10x reduction...), Hobo1's shape cost would drop
...to roughly **35-40MB**~~ — **this was a pre-implementation estimate and
did not hold.** The design actually built (a real C++ union for the
mutually-exclusive edge cases + a `std::shared_ptr`-indirected side struct
for the rarer style-change case, chosen for copy-safety over maximum
compression) measured at **40 bytes/record (3.0x), not 12-16 bytes
(8-10x)**, and the whole-pipeline peak RSS reduction was **~1.7x**
(Hobo1: ~149MB → ~85-90MB), not the ~35-40MB figure above. See §5c for
the full measured breakdown and why the single-struct ratio doesn't carry
straight through to the whole-pipeline ratio.
**Cost:** a real, non-trivial representation change touching
`ShapeRecords.h/.cpp`, `ShapeTessellator` (the only other consumer of
`ShapeRecord`), `SceneRenderer.cpp` (glyph-scaling loop, rewritten
type-aware), and every test that constructs one directly. Highest
engineering cost of the three — **now paid**, real win banked, but a
smaller win than originally estimated.

### Option B — Lazy/on-demand character parsing

Instead of `CharacterDictionary::build()` eagerly parsing all 3,575 (or
8,626, for Hobo5) characters up front, store just each character's
`TagRecord` offset/length (as `SpriteDef` already effectively does via its
nested tag stream) and parse a character's shape/font/etc. data on first
reference (first placement or render), caching the result.
**Estimated savings:** highly workload-dependent — does NOT lower the
*ceiling* (a play session that eventually references every character
still hits the same total), but substantially lowers *peak-at-any-given-
moment* for a typical session that only touches a fraction of a large
file's characters at once (e.g. a title screen referencing ~20 immediate
characters plus whatever their nested sprites reference, not all 3,575
up front). Untested this phase whether real Hobo/Extreme-Pamplona play
patterns would actually realize a large peak reduction — flagged as
INFERRED benefit, not measured.
**Cost:** moderate — needs a parse-on-demand + cache layer in
`CharacterDictionary`, and needs to be fast enough not to cause a frame
hitch on first reference to a complex character. Composes with Option A
(a compact format is still faster/smaller to lazily parse).

### Option C — Tessellate once, discard raw records (flagged low-confidence, NOT recommended without further measurement)

**Checked this phase and found NOT currently viable as stated:**
`SceneRenderer::renderShapeCharacter`/`renderClip` call
`tessellateShape(shapeDef.shape)` **fresh on every single render call** —
`src/renderer/SceneRenderer.cpp` lines 159/213, confirmed via direct
read — there is no tessellation cache anywhere. Discarding raw
`ShapeRecord`s after "first render" would break every subsequent render.
**A real version of this option would need to ADD a tessellated-output
cache** (replacing raw records with cached polygons keyed by character
ID), which could be smaller OR larger than the raw records depending on
tessellation output size — genuinely not known without building and
measuring it. **Recommendation: do not pursue this option before A/B are
evaluated**, and only revisit it with real tessellated-size measurements
in hand, not assumed savings.

## 5c. Phase 2 implementation (2026-08-21): Option A built and measured — real numbers, corrected estimate

**Option A (compact `ShapeRecord`) is now implemented**, not just
designed. See `git log` for the commit. Summary of what changed and what
it actually achieved, measured — not assumed:

**What changed:** `swf::ShapeRecord` (`src/swf/ShapeRecords.h`) is no
longer a flat struct carrying every SHAPERECORD sub-type's fields on every
record. `kStraightEdge`/`kCurvedEdge` (mutually exclusive per spec) now
share storage via a real C++ `union EdgeData`. `kStyleChange`'s fields
(the two style vectors + three optionals — much larger, and only live on a
small minority of records) moved out-of-line behind a
`std::shared_ptr<ShapeStyleChange>`, non-null iff `type == kStyleChange`.
`shared_ptr` (not `unique_ptr`) was chosen deliberately to preserve
`ShapeRecord`'s implicit copyability, since `SceneRenderer.cpp`'s
font-glyph-scaling path copies `ShapeRecord`s. Every consumer
(`ShapeRecords.cpp`'s parser, `ShapeTessellator.cpp`, `SceneRenderer.cpp`'s
glyph-scaling loop — rewritten to be type-aware, since a blind
whole-struct field copy is no longer safe once `styleChange` can be null)
and every test constructing a `ShapeRecord` directly
(`test_shape_records.cpp`, `test_shape_tessellator.cpp`) was updated to
match.

**Measured `sizeof()` result:**

| Struct | Before | After | Ratio |
|---|---:|---:|---:|
| `swf::ShapeRecord` | 120 bytes | **40 bytes** | **3.0x** |

This is **not** the ~8-10x reduction estimated in §5b below when this
option was only a design — that estimate assumed an aggressively packed
representation (e.g. ~12-16 bytes/record). The actual number, measured
after implementing with the safety constraints that turned out to matter
in practice (`shared_ptr` over a raw/`unique_ptr` scheme to preserve safe
implicit copyability, keeping the full `int32_t` twip range rather than
narrowing, avoiding non-standard anonymous-struct-in-union tricks to keep
the zero-warnings bar), is a real, measured **3.0x** per-record reduction
— smaller than hoped, still substantial, and worth stating plainly rather
than quietly reusing the earlier, higher estimate.

**Measured end-to-end peak RSS** (via `tools/mem_profile_check`, same
instrumentation/methodology as §3, same three games as §4):

| Game | Peak RSS before | Peak RSS after | Reduction |
|---|---:|---:|---:|
| Hobo 1 (`hobo.swf`) | 153.6 MB (build peak) / 149.2 MB (steady-state) | **89.8 MB (build peak) / 85.3 MB (steady-state)** | **1.71x / 1.75x** |
| Hobo 5 (`hobo5.swf`) | 453.9 MB | **261.6 MB** | **1.74x** |
| Extreme Pamplona (loader) | 14.8 MB | **11.0 MB** | **1.35x** |

So: Hobo1 moved from ~149MB toward **~85-90MB**, not the ~35-40MB
originally estimated when this fix was still a design (§5b). The gap
between the 3.0x per-record `sizeof()` win and the ~1.7x whole-pipeline
win is real and has an identifiable cause: shapes' *total* memory cost is
`ShapeRecord`s **plus** `FillStyle`/`LineStyle`/`Gradient` arrays (whose
size is unchanged by this fix) **plus** every non-shape character kind
(sprites, sounds, fonts, text, buttons — ~5.5-7.5% of the total per §5a,
also unchanged) **plus** `std::vector` capacity-vs-size slack and glibc
per-allocation bookkeeping (neither modeled by the `sizeof()`-based
estimate, both unaffected by shrinking one struct). Shrinking the single
largest sub-cost by 3x does not shrink the sum of everything else, so the
whole-pipeline ratio is necessarily smaller than the single-struct ratio
— an intuition this document did not fully account for when the ~8-10x
estimate was first written.

**Correctness verification:** all 279 unit tests pass (no regressions,
3 pre-existing shape-record tests updated for the new field-access
pattern, not behavior). `tools/real_game_harness/run_harness.sh` against
all 8 corpus games produced **byte-identical MD5s** for every
successfully-rendered frame versus the pre-fix baseline
(`tests/games/_harness_baseline/harness_summary_2026-08-18.txt`) —
including Extreme Pamplona's pre-existing frame 3-5 failure, reproduced
identically (`exit=3`), confirming that failure is unrelated to this
change. `button_scan` against `hobo.swf` reproduced Phase 1's exact
finding (1 button with mouse conditions, 12 keypress-only, 3 with no
cond-actions) unchanged.

**Updated estimate from `memory_breakdown`** (Hobo1): 46.16 MB estimated
total (was 127.83 MB), shapes still the dominant share at 92.5% (was
94.5-98.5% — the relative share dropped slightly since `ShapeRecord`
itself, not the fill/line style arrays, was what shrank).

**Still true, unchanged by this fix:** the 3DS's realistic app-heap budget
(~24-32MB per §5) is still well below even the post-fix ~85-90MB for
Hobo1 — this fix is real progress, not a complete solution. Option B
(lazy/on-demand parsing) remains open and would compose with this change
(a smaller struct is also faster/cheaper to lazily parse).

## 6. Ownership map (PROVEN via direct code read, `src/runtime/CharacterDictionary.h`)

```
CharacterDictionary::characters_
  : std::unordered_map<uint16_t, CharacterDef>
  where CharacterDef = std::variant<
      swf::ShapeDef,      // bounds + Shape{fillStyles, lineStyles, records}
      SpriteDef,          // characterId, frameCount, nested TagRecord vector
                           //   (bodyOffset/bodyLength index into Movie::data,
                           //    NOT a byte copy — see Movie.h's own doc comment)
      swf::SoundDef,       // header fields + offset/length into Movie::data
                           //   (NOT a byte copy — corrected in §5a, was
                           //    wrongly stated as a copy in an earlier
                           //    version of this document)
      swf::FontDef,        // glyph outlines, one Shape-shaped record set per glyph
      swf::TextDef,        // TEXTRECORD/GLYPHENTRY runs
      swf::ButtonDef,      // per-state character refs + condActionsV2 action bytes
      swf::EditTextDef>    // structural fields only
```

3,575 characters for `hobo.swf`, of which (per `docs/real-game-
compatibility.md`'s independently-derived tag histogram) **2,990 are
shape tags** — each holding a fully-expanded `std::vector<ShapeRecord>`
(one entry per SHAPERECORD — edges, style changes — with no compression),
plus fill/line style arrays. **§5a below proves this is the dominant
cost** (94.5-98.5% of the estimated total in every game measured) with a
direct byte-level breakdown, not just structural plausibility.

`Movie::data` (the fully-decompressed tag-stream buffer, 7.7MB for
Hobo1) is retained for the `Movie`'s entire lifetime (by design — see
`Movie.h`'s own doc comment: `TagRecord::bodyOffset`/`bodyLength` index
into it, and `SpriteDef`'s nested tag streams rely on this too). This is
a legitimate, bounded, PROVEN 7.7MB cost, clearly distinct from
`CharacterDictionary`'s 131.7MB.

## 7. PROVEN / INFERRED / UNKNOWN summary (updated 2026-08-21, Phase 2)

| Claim | Grade | Basis |
|---|---|---|
| `CharacterDictionary::build()` is the dominant memory cost for `hobo.swf` (~90% of peak growth) | **PROVEN** | Direct instrumented measurement |
| The same is true (qualitatively) for other real games, and scales with content volume | **PROVEN** for Hobo5 and Extreme Pamplona specifically (all three now measured); **INFERRED** for Hobo2-4/6-7 (not individually RSS-measured, though `button_scan`/tag-histogram data from Phase 1/the real-game-corpus phase shows the same shape-tag-dominant character mix) |
| Peak RSS is 4-6x any realistic 3DS heap budget | **PROVEN** as an order-of-magnitude statement; the exact 3DS-side number is **UNKNOWN** (not measured on-device) |
| Shape/`SHAPERECORD` expansion specifically (not sounds, not sprites, not `Movie::data`) is the sub-cost within `CharacterDictionary::build()` responsible for most of the growth | **PROVEN** — §5a's byte-level breakdown: 94.5-98.5% of estimated bytes across all three games measured, and `sizeof(ShapeRecord)==120` directly confirms the per-record overhead hypothesis |
| The 2.41x-characters → 3.04x-memory nonlinearity between Hobo1 and Hobo5 has a specific cause | **PROVEN, resolved** — §5a: `ShapeRecord` count grows 3.20x (closely tracking the 3.04x memory growth), not character count; Hobo5's shapes are individually more complex, not just more numerous |
| `SoundDef` retains a full copy of raw MP3 bytes | **PROVEN FALSE** — corrected this phase; it only stores an offset/length into `Movie::data`, confirmed by direct source read and by sound's 0.0% contribution in the byte breakdown |
| Freeing the compressed input buffer after `loadSwf()` meaningfully helps | **PROVEN false** — only recovers ~3% of peak |
| This is fixable without an architecture change (e.g. lazy/on-demand character parsing, a more compact `ShapeRecord` representation, or streaming tessellation instead of retaining full record vectors) | **PARTIALLY RESOLVED, Option A now PROVEN (not just designed)** — see §5c: Option A (compact `ShapeRecord`) is implemented and measured at a real 3.0x per-record / ~1.7x whole-pipeline reduction (the earlier ~8-10x/~35-40MB figure was an unbuilt estimate that did not hold); Option B (lazy parsing) is still INFERRED-moderate, not yet built (helps peak, not ceiling, and composes with Option A); Option C (tessellate-once) is **PROVEN NOT viable as originally stated** — no tessellation cache exists, `SceneRenderer` re-tessellates every render call |

## 8. Status as of 2026-08-21 (Phase 2 design + Option A implementation)

Phase 2's byte-level breakdown (§5a) and fix *options* (§5b) were done
first, followed by real implementation and measurement of Option A (§5c) —
per explicit user instruction to proceed from design into implementation.
`swf::ShapeRecord` went from 120 to 40 bytes (measured); Hobo1's peak RSS
went from ~149MB to ~85-90MB (measured); Hobo5 from ~454MB to ~262MB;
Extreme Pamplona's loader from ~14.8MB to ~11.0MB. All 279 tests pass, and
the full 8-game render harness produced byte-identical output versus the
pre-fix baseline — zero visual/behavioral regression. **This does not
close the gap to a realistic 3DS heap budget (~24-32MB) on its own** —
Option B (lazy/on-demand parsing) remains open as the next candidate, and
would compose with this change. No 3DS/Azahar on-device measurement was
attempted this phase either (no such environment available in this
sandbox) — all numbers above are desktop-proxy measurements, per §5.

## 9. Phase 3 (MP3 audio decode) — memory implications, MEASURED (2026-08-21)

Roadmap Phase 3's own "Definition of Done" requires this measurement, not
an assumption ("Memory implications: New — must be measured, not assumed;
feed back into `docs/memory-audit.md`") — this section is that
measurement, done against the real corpus with two complementary tools
(both `tools/mem_profile_check/`, same ad hoc/standalone convention as
§2's `mem_profile_check`): `audio_mem_check.cpp` measures the REAL RSS
cost of what actually happens during real gameplay content (a real
`StartSound` tag firing and going through the full decode-on-demand-and-
cache path), and `sound_corpus_worstcase.cpp` sums every `SoundDef`'s
declared `sampleCount` to bound what the cache could grow to if a session
eventually triggered every distinct sound in a file (since
`decodedSoundCache_` never evicts — see `src/runtime/MovieClipInstance.h`'s
design comment).

**Real, measured cost for the actual corpus content available (title/menu
screens only — see `docs/compatibility.md`'s "only title screens tested"
scope):**

| Game | Frames ticked | `loadSound()` calls | PCM bytes copied | RSS delta attributable to sound |
|---|---:|---:|---:|---:|
| Hobo 1 (`hobo.swf`) | 13 (full title-screen `FrameCount`) | 1 | 52,992 B (~52 KB) | Not separately resolvable above noise — smaller than one measurement step's granularity against an ~85MB baseline |
| Hobo 5 (`hobo5.swf`) | 13 | 1 | 52,992 B (~52 KB) | Same — negligible |
| Extreme Pamplona (main loader) | 20 | 0 | 0 | None — its 24 sub-SWF sound banks are unreachable without `loadMovie` (L6), so this run cannot even trigger them |

**On this evidence, real-content sound decode cost today is negligible** —
these are 13-frame title/menu screens with exactly one `StartSound` firing
each, and 52KB is immaterial against Hobo1/5's ~85MB/~262MB post-Option-A
baselines. This matches this project's own honest caveat every time it's
been stated: only title screens have ever been exercised, not real
gameplay, so this measurement is real but narrow.

**Worst case if a full playthrough eventually triggered every distinct
sound** (`sound_corpus_worstcase`, summing `SoundDef::sampleCount x
channels x 2 bytes` — PCM16 — over every `kMp3` character
`CharacterDictionary::build()` resolves; all sound characters in every
corpus file measured are MP3, none are another still-undecoded codec):

| Game | Distinct MP3 sounds | Worst-case single-copy PCM | Worst-case with a real backend's own copy too |
|---|---:|---:|---:|
| Hobo 1 | 35 | 25.4 MB | ~50.8 MB |
| Hobo 2 | 36 | 25.5 MB | ~51.0 MB |
| Hobo 3 | 37 | 25.7 MB | ~51.4 MB |
| Hobo 4 | 37 | 25.7 MB | ~51.4 MB |
| Hobo 5 | 43 | 27.0 MB | ~54.0 MB |
| Hobo 6 | 37 | 25.7 MB | ~51.3 MB |
| Hobo 7 | 39 | 28.2 MB | ~56.4 MB |
| Extreme Pamplona (main loader only — real content is in 24 unreachable sub-SWFs, not counted here) | 2 | 1.1 MB | ~2.2 MB |

The "with a real backend's own copy too" column reflects how this
project's actual Phase 3 design works, not a hypothetical: PCM is stored
**twice** by design — once in `ScriptEnvironment::decodedSoundCache_`
(decode-on-demand-and-cache, keyed by soundId) and again in whichever
`IAudioBackend` is active, because `Nintendo3DSAudioBackend::loadSound()`
`memcpy`s into its own `linearAlloc`'d buffer (DSP-DMA-accessible memory
must be a dedicated buffer, not a view into another allocation — see that
file's own comment). Neither cache ever evicts.

**Honest assessment — this is a real, non-trivial cost, not a rounding
error, if a session's play time is long enough to touch every sound in a
file:** ~51-56MB worst-case (double-copy) for a typical Hobo title is
comparable in order of magnitude to Hobo1's entire post-Option-A
`CharacterDictionary` peak (~85-90MB per §5c) — i.e. a long play session
could plausibly add *more* RAM pressure from accumulated sound cache than
this session was able to measure from the (title-screen-only) corpus
content actually available. This was NOT assumed away; it's flagged here
explicitly as the honest ceiling, not glossed over because the measured
real-content number happens to be tiny.

**What this does and doesn't call for right now:** per this project's own
stated Phase 3 dependency note ("should be sequenced with Phase 2's
memory findings in mind... decide decode-on-demand vs. decode-and-cache
with that context"), decode-on-demand-and-cache (rather than eager
whole-dictionary decode) was the deliberate choice made — it already caps
the cost at "distinct sounds actually triggered this session," not
"distinct sounds defined in the file," which is the right call given §5-8's
finding that shape data, not sound, is this project's dominant memory
problem. A bounded LRU/eviction policy for `decodedSoundCache_` would cap
the worst case further, but was not built this phase — no real corpus
content available in this environment exercises enough distinct sounds to
demonstrate the cache actually growing large in practice (see the 52KB/1-
sound real-measurement row above), so building eviction now would be
tuning against an assumption rather than an observed problem. Flagged in
`docs/known-limitations.md` as an open, worth-watching item rather than
silently declared fine.

## 10. M2 RAM phase (2026-08-24): Option B (lazy/on-demand parsing) — IMPLEMENTED, MEASURED

**Context note on this section's provenance:** a prior session's work
(referred to elsewhere as "M1", reaching a commit `0e31641` with 308 tests
and figures matching what this section independently re-derives — Hobo1
~89.8→21.7MB, Hobo5 ~261.6→37.9MB, Extreme Pamplona ~11.0→7.8MB) was lost
before this phase began: not recoverable from this repository's git
history (reachable or dangling), from any checked-in backup tarball, or
from the user's own machine. This section is a genuine from-scratch
reimplementation against the actual current source tree (`430585a`), not
a restoration — the close numeric match to the lost session's own figures
(see the table below) is independent corroboration that this is the
right design, not a copied result.

**What changed:** `CharacterDictionary::build()` (§5-§5c above) no longer
eagerly calls `swf::parseDefineShape`/`parseDefineFont`/etc. for every
character-defining tag it discovers. It now only peeks each such tag's
leading CharacterId (a 2-byte read — every DefineShape/2/3, DefineSound,
DefineFont, DefineFont2, DefineText/2, DefineButton/2, and DefineEditText
tag begins with exactly that field per the public SWF spec, confirmed by
direct source read of each parser) and records `{tag code, offset,
length}` in a `pending_` index. The real parse happens on first
`CharacterDictionary::find()`, cached in `parsed_` thereafter so a second
`find()` for the same character is a plain hash-map lookup. `DefineSprite`
is the one exception, kept eager (constructing a `SpriteDef` was already
cheap — offset/length only, no byte copy — and `build()` must recurse into
its nested tag stream regardless to discover characters defined only
inside it). `find()`'s public signature and `const`-ness are unchanged
(internal caching uses `mutable` members), so every existing call site
needed zero changes — see `src/runtime/CharacterDictionary.h`'s updated
file header for the full design writeup.

**Measured result — isolated-process `mem_profile_check`, same methodology
as §4/§5c, 9-game corpus (Cat Ninja added to the corpus this phase — see
below):**

| Game | Peak before (Option A only) | Peak after (Option A + B) | Reduction |
|---|---:|---:|---:|
| Hobo 1 | 87.70 MB | **25.17 MB** (21.24 MB steady-state) | 3.48x |
| Hobo 2 | 93.24 MB | **26.98 MB** | 3.46x |
| Hobo 3 | 116.98 MB | **30.09 MB** | 3.89x |
| Hobo 4 | 140.27 MB | **33.23 MB** | 4.22x |
| Hobo 5 | 255.48 MB | **43.58 MB** (37.02 MB steady-state) | 5.86x |
| Hobo 6 | 147.96 MB | **35.52 MB** | 4.17x |
| Hobo 7 | 155.84 MB | **37.41 MB** | 4.17x |
| Extreme Pamplona (loader) | 10.76 MB | **7.88 MB** | 1.37x |
| Cat Ninja | 13.33 MB | **13.31 MB** | ~1.0x (see below) |

Hobo1's post-Option-B steady-state (21.24MB) is now UNDER the ~24MB
Old-3DS application-heap target established in §5; its peak (25.17MB)
briefly exceeds it during load, before settling. Hobo5 (worst case in the
Hobo family) is still over budget at 43.58MB — a real, substantial
improvement (5.86x) but not a complete fix on its own.

**Cat Ninja is intentionally near-unchanged (13.33MB → 13.31MB):** its
`swf_diagnostic`-confirmed 63 `DefineBitsLossless2` bitmap tags (zero
shape/sprite/button tags) are not resolved into `CharacterDictionary`
characters AT ALL yet (bitmap tag parsing is roadmap Phase 10, not
started — see `docs/compatibility-matrix.md`) — so `characters resolved =
18` for Cat Ninja is just its 18 `DefineSound` characters; Option B has
nothing to defer for the 63 bitmaps because nothing parses them in the
first place. **Cat Ninja's real memory cost, once bitmap decode exists,
is a separate, not-yet-measured question** — do not read the near-zero
delta here as "Cat Ninja is memory-cheap."

**This does NOT lower the worst-case ceiling, only typical-session peak —
confirmed, not just theorized:** `tools/real_game_harness/memory_breakdown`
(which force-resolves every character via the same public `find()` API,
i.e. simulates "a session that eventually touches everything") reports
the exact same totals as before Option B — Hobo1 46.16MB, Hobo5 143.16MB
estimated. This is the expected, by-design outcome (Option B changes WHEN
parsing happens, not how much total data exists) — see
`docs/implementation-roadmap-2026-08-21-part2.md` Phase 5's own framing,
now confirmed correct by direct measurement rather than left as a
prediction.

**Testing:** 4 new regression tests in `tests/test_character_dictionary.cpp`
(`CharacterDictionary_LazyParse_RepeatedFindReturnsSamePointer`,
`_SizeCountsPendingAndParsedIdentically`, `_MalformedBodyFailsAtFindNotAtBuild`
[×2 assertions]) plus the pre-existing suite — 311/311 passing (up from
304; the other 3 new tests are `MemoryDiagnostics`' own, §11 below), zero
regressions. Render harness (`tools/real_game_harness/run_harness.sh`,
now 9 games) produced byte-identical frame MD5s before/after Option B
across every game, including both Extreme Pamplona's and Cat Ninja's
pre-existing out-of-range-frame failures reproduced identically.

**Cat Ninja corpus onboarding (2026-08-24):** staged into
`/home/claude/game-corpus/cat_ninja/cat_ninja.swf` from the user's device
(`G:\3DS\Új mappa\CatNinja.swf`) and added to `run_harness.sh`'s `GAMES`
array. `swf_diagnostic` confirms the previously-reported facts exactly:
SWF10, AVM2 (`DoABC2`x2), zero shape/sprite/button tags, 63
`DefineBitsLossless2` tags, 18 `DefineSound` (MP3) tags, `FrameCount=2`
(so `--render 3` correctly fails with "frame out of range", not a bug).

## 11. Real on-device memory instrumentation — mechanism added, not yet run on hardware

`src/platform/MemoryDiagnostics.{h,cpp}` (new this phase) is a small,
disabled-by-default checkpoint logger callable from the ACTUAL runtime
(not just an external desktop tool): `currentResidentKb()`/
`currentFreeKb()` read `/proc/self/status` VmRSS on desktop Linux (same
proxy source as `mem_profile_check`) or libctru's
`osGetMemRegionUsed(MEMREGION_APPLICATION)`/`osGetMemRegionFree(...)` on
the real 3DS build (`__3DS__`-gated, mirroring `Mp3Decoder.cpp`'s existing
platform-branch pattern) — the latter is a REAL on-device application-heap
number, not a proxy. Wired into `nintendo3ds_main.cpp` at exactly the six
checkpoints task01.txt asked for (startup, after SWF load, after
`CharacterDictionary::build`, after audio backend init, after first frame,
after first render) plus shutdown/peak, enabled by holding L at boot (no
persistent config file exists yet — that's a separate, later roadmap
phase). Also wired into the desktop `flash_runtime` CLI via a new
`--memdiag` flag, which is how the mechanism itself was verified working
this phase (see the sample output in this phase's report) before trusting
it on the 3DS build. 4 new unit tests in `tests/test_memory_diagnostics.cpp`.

**Honest limitation, stated plainly per this project's own standing
convention:** this mechanism has NOT been run on a real 3DS or an emulator
in this sandbox (no such environment available here) — the 3DS code path
(`osGetMemRegionUsed`) compiles cleanly in the cross-build (verified
2026-08-24, zero undefined symbols) but its actual reported numbers are
unverified against real hardware. This is the same standing caveat every
other 3DS-specific code path in this project carries, restated honestly
rather than glossed over.

## 12. Second code loss and re-implementation (2026-08-24, same day): §10's own pattern repeated

**§10 above already documents one lost-and-rebuilt cycle (a "M1" session's
Option B implementation lost, "M2" rebuilding it from scratch).** Before
the session that carried out this phase's own work began, the **M2**
implementation itself — the actual `pendingCharacters_`/`parsedCharacters_`
code in `CharacterDictionary.{h,cpp}` — was *also* gone: this sandbox's
git history had reset to commit `430585a` (predating M2, predating even
the Virtual Console layer M2's own commit `0e31641` sits on top of), and
`src/runtime/CharacterDictionary.{h,cpp}` on disk were back to the fully
eager, pre-Option-B form. Only §10's own prose and the `docs/`/`CLAUDE.md`
narrative describing M2 survived — apparently synced to the user's local
checkout independently of (and later than) the code that produced it, the
same asymmetry §10 itself flags for M1. This is now a confirmed *pattern*
in this project's environment, not a one-off: code changes and doc/CLAUDE.md
updates can desynchronize from each other across a sandbox reset, with docs
occasionally surviving when code does not.

This session (still 2026-08-24) re-implemented Option B a third time,
independently, against the actual current source — not by trying to
recover or diff against M2's lost code (not possible; nothing of it
persisted anywhere reachable). The design is the same in substance
(`pendingCharacters_`/`parsedCharacters_` split, lazy `find()`-triggered
parse, `DefineSprite` handled via a shared nested-tag-stream walk helper
used both at scan time for ID discovery and at parse time for the real
`SpriteDef`) — see `CharacterDictionary.h`'s own file header for the
current, authoritative design writeup rather than relying on this
history section.

**Re-measured, this implementation — isolated-process `mem_profile_check`,
same 8-game corpus §4/§10 used (Cat Ninja not available in this sandbox;
see below):**

| Game | Peak, eager (re-measured this session, fresh isolated-process run — not carried over from before this container's reset) | Peak, Option B (this session, at `build()`) | Steady-state after 5-frame session | Characters touched / registered |
|---|---:|---:|---:|---:|
| Hobo 1 | 87.63 MB | **21.78 MB** | 17.66 MB | 37 / 3575 (1.0%) |
| Hobo 2 | 93.22 MB | **23.49 MB** | 19.34 MB | 39 / 3930 (1.0%) |
| Hobo 3 | 116.95 MB | **25.87 MB** | 22.63 MB | 39 / 5073 (0.8%) |
| Hobo 4 | 140.27 MB | **28.72 MB** | 24.19 MB | 39 / 5813 (0.7%) |
| Hobo 5 | 255.48 MB | **38.02 MB** | 31.34 MB | 39 / 8626 (0.5%) |
| Hobo 6 | 148.00 MB | **30.61 MB** | 25.12 MB | 39 / 6350 (0.6%) |
| Hobo 7 | 155.79 MB | **32.22 MB** | 26.42 MB | 40 / 6547 (0.6%) |
| Extreme Pamplona (loader) | 10.77 MB | **7.71 MB** | n/a (only 2 frames exist) | 59 / 441 (13.4%) |

These figures line up closely with M2's own documented table above (Hobo1
25.17→21.78MB range, Hobo5 43.58→38.02MB range — the small remaining gap is
plausibly measurement-point differences, e.g. M2's table doesn't specify
whether its number is the `build()` checkpoint or a later steady-state
one, which this table reports as two separate columns). The close
agreement between two structurally-independent implementations, done on
different days by different context windows with zero shared code, is
itself reasonably strong evidence the ~20-30x fewer-bytes-touched design is
sound and not an artifact of one particular implementation's quirks.

**Worst-case ceiling re-confirmed unchanged, directly (not just by
citing §10's claim):** `tools/mem_profile_check` was extended this session
with a step that calls `find()` for every possible `uint16_t` character ID
after the 5-frame session (a cheap, harmless no-op for IDs the file never
defined), forcing every remaining pending character to parse. Result:

| Game | Worst-case peak (force every character) | vs. eager baseline (this session, fresh) |
|---|---:|---:|
| Hobo 1 | 83.48 MB | 87.63 MB (95% — same order, small variance from allocator/measurement noise) |
| Hobo 2 | 88.93 MB | 93.22 MB |
| Hobo 3 | 112.23 MB | 116.95 MB |
| Hobo 4 | 134.95 MB | 140.27 MB |
| Hobo 5 | 248.73 MB | 255.48 MB |
| Hobo 6 | 142.29 MB | 148.00 MB |
| Hobo 7 | 149.84 MB | 155.79 MB |
| Extreme Pamplona | 10.17 MB | 10.77 MB |

Confirms directly, not just by re-asserting §10's prediction: Option B
defers cost, it does not eliminate the worst case — a session that
genuinely references every character in a file still ends up paying
essentially the same total the old eager design always paid up front.

**Testing/build verification this session:** 352/352 desktop tests pass
(zero regressions; 3 new tests specifically for this phase —
`CharacterDictionary_Phase5_UnreferencedCharacter_NeverParsed`,
`_FirstFind_ProducesFullyParsedGoldenValue`, `_RepeatedFind_ReusesCachedParseNotReparsed`
— plus the pre-existing suite grown by the unrelated Virtual Console/MP3/
memory-diagnostics work this same reset also required re-merging from the
user's local checkout, see `CLAUDE.md`'s Virtual Console section for that
separate provenance story). `tools/real_game_harness/run_harness.sh`
produced byte-identical frame MD5s across all 8 available corpus games
before/after this change (Extreme Pamplona's pre-existing frame-3+ failure
reproduced identically, not a new regression). 3DS cross-build
(`build_3ds/flash3ds_3ds.3dsx`, RomFS-packaged) compiles clean, zero
non-weak undefined symbols.

**Process note for whoever picks this up next:** given this is now a
twice-observed pattern, treat any doc claiming a feature is "implemented"
with the same discipline `CLAUDE.md`'s audit methodology already
prescribes for everything else — verify against the actual current source,
not the doc's word alone, before building on top of it. This section's own
existence is the proof that doing so matters.

## 13. Priority Fix List item #2 (2026-08-31): decoded-bitmap RAM cost — MEASURED

Bitmap rendering (`docs/renderer.md`'s "Bitmap rendering" section)
decodes every `DefineBitsLossless`/`2`/`DefineBitsJpeg2`/`3` character to
straight RGBA8 (`swf::BitmapDef::pixels`, 4 bytes/pixel) EAGERLY on first
`CharacterDictionary::find()` — unlike audio's lazy-decode-and-cache
pattern (§9), a bitmap referenced by a placed shape's fill style is
essentially always about to be sampled, so there's no meaningful
"defined but never used" case worth optimizing for the way sound has (see
`src/swf/DefineBitsTag.h`'s own header comment for the full reasoning).
This section measures the actual cost, per this project's "measure, don't
assume negligible" discipline (mirrors §6 Option B's own "PROVEN not
INFERRED" framing) — via a new permanent tool,
`tools/real_game_harness/bitmap_ram_probe.cpp`, which parses every
`DefineBits*` character in a real movie (top-level and nested inside
`DefineSprite` streams) with the exact same `swf::parseDefineBits()` call
`CharacterDictionary` uses, and sums `width*height*4` bytes.

| File | Bitmap characters | Decoded RGBA8 total |
|---|---:|---:|
| Hobo1 (`hobo.swf`) | 0 | 0 |
| Hobo2 | 10 | 1,577,520 bytes (~1.50 MB) |
| Hobo3 | 1 | 22,320 bytes (~21.8 KB) |
| Hobo4 | 1 | 22,320 bytes (~21.8 KB) |
| Hobo5 | 1 | 22,320 bytes (~21.8 KB) |
| Hobo6 | 2 | 3,168,048 bytes (~3.02 MB) |
| Hobo7 | 2 | 3,168,048 bytes (~3.02 MB) |
| Extreme Pamplona (loader only) | 10 | 7,274,076 bytes (~6.94 MB) |

The two largest single contributors are Hobo6/Hobo7's `DefineBitsJpeg2`
characters (one each) — a 1024x768 photographic background, ~3.15 MB
decoded RGBA8 apiece, roughly the same order of magnitude as this
project's entire measured *session* RAM budget for some titles (§4).
Every bitmap character `CharacterDictionary::find()` is ever called for
(i.e. actually placed and reached, not just defined) stays resident for
the life of that `CharacterDictionary` — there is no eviction, matching
§6/Roadmap-Phase-6's own explicit "not implemented, no evidence of real
unbounded growth" decision for the sound/loadMovie caches. Extreme
Pamplona's loader alone (~6.9 MB, before even reaching any of its
`loadMovie`-loaded content sub-SWFs — see `docs/known-limitations.md` L6)
is the more RAM-relevant data point for a 3DS target than any Hobo file:
a real one-time budget line, not a hypothetical.

**Deliberately not implemented this phase:** no bitmap-cache
eviction/streaming/downsampling strategy. Consistent with §6 Option B's
own precedent (`loadMovie`/sound-cache eviction was investigated and
explicitly deferred for lack of real evidence of unbounded growth) — this
measurement instead gives a real, evidence-based number for a *future*
session to weigh against an actual 3DS RAM budget and a specific target
title's real total footprint (this bitmap cost stacks with every other
§4/§10 finding, not in isolation), rather than tuning against a
hypothetical now. `bitmap_ram_probe` is registered in `CMakeLists.txt`
(`add_executable(bitmap_ram_probe ...)`) so this measurement is
trivially re-run against any future corpus addition.
