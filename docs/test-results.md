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

## Not yet tested (carried over from `docs/compatibility.md`, still open)

- `hobo 2 - prison brawl.swf` through `hobo 7 - heaven.swf` (sequels).
- `hobo.swf`'s own gameplay frames beyond the title screen — still
  unreachable, since button-click interactivity doesn't exist yet (see
  `docs/known-limitations.md` priority #2).
- Extreme Pamplona.
- Real Nintendo 3DS hardware, for anything (see `docs/3ds-limitations.md`).
