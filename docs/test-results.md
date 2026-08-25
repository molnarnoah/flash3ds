# Test Results

**Compatibility-audit phase (2026-08-18).** What was actually run this
phase, and the outcome — kept separate from `docs/compatibility.md` (the
older, still-valid Phase 9 Hobo report) and `docs/known-limitations.md`
(prioritized findings) so this doc can stay a plain run log.

## Automated test suite

- **189/189 `TEST_CASE`s passing** (`ctest` / `build/tests/flash3ds_tests`),
  up from 187 before this phase's two new regression tests. Zero
  regressions from any change made this phase.
- New this phase: `SceneRenderer_MovieClipInstanceAlpha_
  BlendsRenderedColorWithBackground`,
  `SceneRenderer_MovieClipInstanceAlphaZero_RendersFullyTransparent`
  (`tests/test_scene_renderer.cpp`) — regression coverage for the
  ColorTransform/`_alpha` rendering fix, see `docs/known-limitations.md`
  priority #1.

## Desktop CLI — new standalone test files

All three files in `tests/swf/` (see that directory's `README.md`) load
and render correctly via `flash_runtime`:

| File | `--quiet` tag trace | `--render` result |
|---|---|---|
| `001_empty.swf` | 2 tags (ShowFrame, End), no warnings | not applicable (no visible content) |
| `002_single_shape.swf` | `DefineShape3`/`PlaceObject2`/`ShowFrame`/`End`, no warnings | center pixel (100,60 device px) = `(224,48,48)` — exact match for the authored solid fill color, fully opaque |
| `003_placeobject2_colortransform_alpha50.swf` | same tag shape, `PlaceObject2` body 6 bytes longer (carries `CXFORMWITHALPHA`) | center pixel = `(239,151,151)` — matches the expected 50%-alpha-over-white blend of `(224,48,48)` almost exactly (`(224+255)/2≈239.5`, `(48+255)/2≈151.5`) |

## Real-content verification — `hobo.swf` frames 1-5, before/after the priority #1 fix

Rendered via `flash_runtime --render <N> <out.ppm> hobo.swf` for N=1..5,
once on the pre-fix commit (`git stash`) and once with the fix applied
(`git stash pop`), then compared byte-for-byte (`md5sum`):

```
frame 1: IDENTICAL
frame 2: IDENTICAL
frame 3: IDENTICAL
frame 4: IDENTICAL
frame 5: IDENTICAL
```

**Interpretation:** the fix (making `ColorTransform`/`_alpha` actually
affect rendering, for the first time) produces byte-for-byte identical
output for `hobo.swf`'s first 5 frames — meaning no placed character in
those frames carries a non-identity `ColorTransform`/`_alpha` change this
runtime's execution of the file's scripts ever triggers. This directly
informed the correction to `docs/compatibility.md`'s Phase 9 "PLAY! button
fading in by frame 5" claim — see `docs/compatibility-matrix.md`'s
"Corrections to prior docs" section for the full writeup, including the
separate confirmation that frame 1 and frame 5 DO differ visually
(4189 changed device pixels, bbox ~(167,308)-(301,355)) for some other,
uncharacterized reason.

## 3DS build

`cmake --build build_3ds` — clean rebuild after the fix, succeeds. Output
`.3dsx` regenerated (`build_3ds/flash3ds_3ds.3dsx`, 385528 bytes, up from
the prior 384480-byte build — confirms the new code is actually linked in,
not a stale/cached artifact). Only pre-existing, unrelated warnings
(newlib ABI parameter-passing notes, `_close`/`_read`/etc.
"not implemented" linker notes — all expected per `docs/3ds-toolchain.md`,
none new this phase).

**Not yet run this phase:** on Azahar or real hardware — no emulator/device
access from this environment; delivered to the user for that confirmation,
consistent with how every prior 3DS-facing change in this project has been
handled.

## ExternalInterface — Hobo pattern

**Not tested this phase.** `docs/compatibility-matrix.md` §7 and
`docs/known-limitations.md` note the mechanism is real (generically
unit-tested) but the EXACT Hobo call shape
(`addCallback("SetUnlockedBonusIndex", this, SetUnlockedBonusIndex)`,
`call("OnBonusCancel")`, `call("OnBonusOk")`, `call("color", 1)`) has not
been independently reproduced end-to-end from a real `.swf` yet — tracked
as `tests/swf/025_externalinterface_hobo_pattern.swf` (PLANNED, not yet
created — see `tests/swf/README.md`).

## Interactivity phase (2026-08-18) — `_width`/`_height` fix

- **194/194 tests passing** (up from 189) — 5 new regression tests in
  `tests/test_movieclip_instance.cpp` (`MovieClipInstance_WidthHeight_*`,
  `MovieClipInstance_Width_*`), all passing, zero regressions elsewhere.
- `hobo.swf` frame 1 rendered before/after this fix, byte-for-byte
  identical (`md5sum` match) — confirms zero rendering-side regression, as
  expected for a pure property-computation addition.
- 3DS build (`build_3ds`) rebuilds clean; new `.3dsx` (387556 bytes) not
  yet tested on Azahar/hardware.
- Full audit trace (input pipeline, coordinate system, Button2, hit-testing,
  event representation, `onClipEvent`) recorded in
  `docs/interactivity-audit.md`; a real, previously-undocumented gap was
  found in the process (`_xmouse`/`_ymouse` reads device-pixel coordinates
  with no stage-pixel conversion — see `docs/input.md`), flagged but not
  yet fixed (next in the dependency chain, per
  `docs/known-limitations.md`).

## Interactivity phase (2026-08-18) — `_xmouse`/`_ymouse` coordinate-space fix

- **199/199 tests passing** (up from 194) — 5 new regression tests in
  `tests/test_movieclip_instance.cpp`
  (`MovieClipInstance_XMouse_ViewportMatchesStage_CoordinatesUnchanged`,
  `_600x450StageDifferentViewport_ScalesNonUniformly`,
  `_TopLeftViewportCorner_MapsToStageOrigin`,
  `_BottomRightViewportCorner_MapsToStageWidthHeight`,
  `_BareMemberAccess_AppliesStageConversionToo`), all passing, zero
  regressions elsewhere — both PRE-existing `_xmouse`/`_ymouse` tests
  (`MovieClipInstance_XMouseYMouse_GetProperty_ReadsFromInputState`,
  `MovieClipInstance_XMouse_BareMemberAccess_ReadsFromInputState`) pass
  completely unchanged, confirming the default (no-viewport-set) behavior
  is preserved exactly.
- `hobo.swf` (real stage: 600x450) frames 1-5 rendered before/after this
  fix via `flash_runtime --render`, byte-for-byte identical (`md5sum`
  match) — expected, since this is a pure property-read change with zero
  rendering-path modification.
- 3DS build (`build_3ds`) rebuilds clean; new `.3dsx` (387780 bytes, up
  from 387556 — confirms the new code linked in) not yet tested on
  Azahar/hardware.
- Full writeup: `docs/known-limitations.md`'s "Sub-fix 2/N", corrected
  coordinate-flow diagram in `docs/input.md`.

## Input-transitions phase (2026-08-19) — edge-detected input state

- **217/217 tests passing** (up from 199) — 18 new regression tests in
  `tests/test_input_state.cpp`: 6 covering the base UP/DOWN/HELD/RELEASE/
  RELEASED matrix (`InputState_KeyEdge_InitialUp_NoTransition` through
  `_BeforeAnyCommit_ReportsNoTransition`), 8 covering edge cases (held for
  many frames, very-short sub-tick press, release-without-another-press,
  repeated press/release, simultaneous different buttons, simultaneous
  touch+button, no-setter-calls-between-commits, aliased key codes), 4
  covering the touch matrix (up, press-with-coordinates, held-while-
  moving, release-after-movement). All passing, zero regressions
  elsewhere — every pre-existing `_xmouse`/`_ymouse`/`InputState_*` test
  (viewport conversion, key-down tracking, mouse-position round-trip)
  passes completely unchanged.
- `hobo.swf` (real stage: 600x450) frames 1-5 rendered before/after this
  phase, byte-for-byte identical (`md5sum` match) — expected, since this
  phase touches only `InputState`/`Nintendo3DSInput`, neither of which is
  in the rendering or MovieClipInstance code path.
- 3DS build (`build_3ds`) rebuilds clean; new `.3dsx` (390048 bytes, up
  from 387780 — confirms the new code linked in). **Not yet tested on
  Azahar/hardware** — no emulator/device access from this environment (see
  `docs/3ds-limitations.md`'s new entry for this phase).
- Full writeup: `docs/known-limitations.md`'s "Sub-fix 3/N", full audit +
  model + semantics in `docs/input.md`'s "Input-transitions phase" section.

## Interactivity phase (2026-08-19) — bounding-box hit-testing

- **237/237 tests passing** (up from 217) — 12 new tests in
  `tests/test_movieclip_instance.cpp` (point inside/outside a shape,
  inclusive-boundary corners, overlapping-shapes topmost-wins,
  invisible-clip exclusion, degenerate-`_xscale` exclusion, script-
  mutated-transform awareness, two-level nested-`MovieClip` recursion, and
  5 `hitTest()`-specific cases) + 8 new tests in `tests/test_swf_records.cpp`
  (`invertMatrix()`/`transformPoint()`/`rectContainsPoint()` in isolation).
  All passing, zero regressions elsewhere.
- `hobo.swf` (real stage: 600x450) frames 1-5 rendered before/after this
  phase, byte-for-byte identical (`md5sum` match) — expected, since this
  phase adds pure query APIs and touches zero rendering code.
- 3DS build (`build_3ds`) rebuilds clean; new `.3dsx` (392032 bytes, up
  from 390048 — confirms the new code linked in) not yet tested on
  Azahar/hardware.
- Full writeup: `docs/known-limitations.md`'s "Sub-fix 4/N", implementation
  summary + AS2-vs-internal-primitive distinction in `docs/hit-testing.md`.

## ButtonInstance phase (2026-08-19) — `ButtonDef` -> `ButtonInstance` runtime instance

- **253/253 tests passing** (up from 237) — 16 new tests in the new
  `tests/test_button_instance.cpp` (creation, independent instances,
  transforms including nested-in-MovieClip composition, depth-ordered
  hit testing, HitTest-vs-visual-state geometry, invisible-button
  exclusion, the full UP/OVER/DOWN transition table, multi-button state
  isolation, removal, replacement, duplicate placements). All passing,
  zero regressions elsewhere.
- `hobo.swf` (real stage: 600x450) frames 1-5 rendered before/after this
  phase, byte-for-byte identical (`md5sum` match) — expected:
  `src/renderer/SceneRenderer.cpp` was not modified at all this phase (see
  `docs/buttons.md`'s "Rendering — deliberately unchanged" section).
- 3DS build (`build_3ds`) rebuilds clean; new `.3dsx` (397216 bytes, up
  from 392032 — confirms the new code linked in). **Not yet tested on
  Azahar/hardware** — no emulator/device access from this environment.
- Full writeup: `docs/known-limitations.md`'s "Sub-fix 5/N",
  architecture/design in `docs/buttons.md`, real Hobo `DefineButton2`
  diagnostic findings in `docs/buttons.md`'s "Real Hobo `DefineButton2`
  findings" section.

## Real-game-corpus phase (2026-08-18) — Hobo 1–7 + Extreme Pamplona analysis

- **253/253 tests passing** (unchanged from the ButtonInstance phase —
  this phase made no changes to `src/`, only additive tooling: the new
  `tools/swf_diagnostic/main.cpp` analysis tool and
  `tools/real_game_harness/run_harness.sh` — zero regressions).
- New: `swf_diagnostic`, a read-only, analysis-only CLI (tag histogram,
  AVM1 opcode profile with correct `DefineFunction`/`DefineFunction2`/
  `With` body-extent handling, AS2 API/identifier scan, button/sound/
  rendering feature profile) — registered in `CMakeLists.txt` alongside
  `flash_runtime`, builds clean, run against all 8 corpus games plus all
  23 Extreme Pamplona content sub-SWFs (31 files total) with **zero
  parse failures, zero crashes**.
- New: `tools/real_game_harness/run_harness.sh` — loads each corpus game
  via `flash_runtime --quiet` (init check) then `--render`s frames 1-5,
  recording per-frame MD5 as a golden-output baseline
  (`tests/games/_harness_baseline/harness_summary_2026-08-18.txt`). All 8
  games: **INIT_OK**, zero runtime exceptions/crashes. Every Hobo file
  (13 declared frames) renders all 5 requested frames cleanly; Extreme
  Pamplona (2 declared frames) correctly rejects frames 3-5 as
  out-of-range (`--render: frame 3 out of range [1, 2]`) — expected CLI
  behavior, not a bug.
- New real-content corpus established at `tests/games/` (manifests +
  MD5 checksums + diagnostic output only — binaries stay external per
  the project's existing convention, see `tests/games/README.md`):
  Hobo 1 (= the pre-existing `hobo.swf` baseline, confirmed unchanged),
  Hobo 2–7 (staged this phase from the user's device, ~5-8 MB each), and
  the full 24-file Extreme Pamplona package (1 main loader + 23
  `loadMovie`-style content sub-SWFs).
- Full per-game statistics, the cross-game compatibility matrix, and the
  "if we implement feature X, which games does it help?" analysis:
  `docs/real-game-compatibility.md`.
- **Not yet tested this phase:** dynamic/runtime behavior of any of the
  new games (this phase is static analysis + init/render-only per its
  explicit scope — no gameplay, no button-click interactivity, since
  that runtime feature still doesn't exist); Extreme Pamplona's own
  gameplay frames (only 2 frames exist in the main loader — its actual
  levels are loaded from the 9 separate `level-*.swf` content files,
  not analyzed for playability this phase); real Nintendo 3DS hardware
  (see `docs/3ds-limitations.md`).

## Not yet tested (carried over from `docs/compatibility.md`, still open)

- `hobo.swf`'s own gameplay frames beyond the title screen — still
  unreachable, since button-click interactivity doesn't exist yet (see
  `docs/known-limitations.md` priority #2). Confirmed this phase: the
  same is true for all 6 Hobo sequels (identical native-`condActionsV2`
  button-interactivity model, see `docs/real-game-compatibility.md`).
- Extreme Pamplona's actual gameplay (loader confirmed working; button
  event dispatch — a second, different mechanism than Hobo's, see
  `docs/real-game-compatibility.md` — still missing).
- Real Nintendo 3DS hardware, for anything (see `docs/3ds-limitations.md`).

## M2 RAM-validation phase (2026-08-24)

- **311/311 tests passing** (up from 304 pre-phase) — 7 new tests: 4 lazy
  CharacterDictionary-parsing regressions
  (`tests/test_character_dictionary.cpp`) + 3 `MemoryDiagnostics` unit
  tests (`tests/test_memory_diagnostics.cpp`). Zero regressions.
- **9-game render harness** (`tools/real_game_harness/run_harness.sh`,
  Cat Ninja added to the `GAMES` array this phase): byte-identical frame
  MD5s before/after Phase 5 (Option B lazy parsing) for every game,
  including Extreme Pamplona's and Cat Ninja's pre-existing out-of-range-
  frame failures (both reproduced identically — Cat Ninja's `FrameCount=2`
  makes `--render 3` correctly fail, not a bug).
- **3DS cross-build:** clean rebuild after Phase 5 + `MemoryDiagnostics`
  landed. Required one CMakeLists.txt fix (`flash3ds_core` needed
  `FLASH3DS_LIBCTRU_INCLUDE` added to its own include path for the first
  time, since `MemoryDiagnostics.cpp` is the first `flash3ds_core`
  translation unit that needs `<3ds.h>` when built for `__3DS__` — see
  `docs/memory-audit.md` §11). Zero undefined non-weak symbols
  (`arm-none-eabi-nm -u build_3ds/flash3ds_3ds | grep -v " w \| W "`
  prints nothing). New `.3dsx`: 443552 bytes, SHA-256
  `53aa3fefdb9fc232253ddeed96c2c7477d7c6a74a6d2a38448ba07e61f6db7b9`.
- **Not run this phase, same standing limitation as every prior phase:**
  on Azahar or real hardware — no emulator/device access from this
  environment.

**Re-verification (2026-08-24, later same-day session):** the Phase 5 code
above was lost to a second sandbox reset before this session began (only
this doc's own entries and `CLAUDE.md`/`docs/memory-audit.md`'s prose
survived — see `docs/memory-audit.md` §12) and was independently
re-implemented from scratch. **352/352 tests passing** (0 regressions; 3
new tests this time: `CharacterDictionary_Phase5_UnreferencedCharacter_NeverParsed`,
`_FirstFind_ProducesFullyParsedGoldenValue`, `_RepeatedFind_ReusesCachedParseNotReparsed`).
8-game render harness (Cat Ninja not available in this sandbox — not
staged here) byte-identical MD5s before/after, zero regressions. 3DS
cross-build clean, zero non-weak undefined symbols, RomFS-packaged
`.3dsx` produced. See `docs/memory-audit.md` §12 for the full re-measured
RAM figures.
