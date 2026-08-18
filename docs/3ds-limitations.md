# Nintendo 3DS Platform-Specific Limitations

**Compatibility-audit phase (2026-08-18) deliverable.** Per the audit
charter: test desktop-vs-3DS *after* desktop behavior is correct, and never
assume a desktop failure is automatically a 3DS-specific problem (or vice
versa). Everything in `docs/compatibility-matrix.md`/`docs/known-
limitations.md` applies equally on 3DS, since `src/swf/`, `src/avm1/`, and
`src/runtime/` are all part of `flash3ds_core`, cross-compiled unchanged —
this doc covers only what's genuinely 3DS-specific: the platform backend
layer (`src/platform/`, `src/renderer/Nintendo3DSRenderer.*`,
`src/audio/Nintendo3DSAudioBackend.*`) and anything that behaves differently
on real/emulated hardware than on the desktop CLI.

## Confirmed working (real hardware/emulator signal)

- **Boots and runs in Azahar** (a Citra-based 3DS emulator) — user-confirmed
  this session's earlier work (Phase 10). This is the only actual
  hardware/emulator confirmation this project has received to date.
- **Real hardware: never tested.** No physical Nintendo 3DS was available in
  any session so far. Every "works on 3DS" claim in this project prior to
  the Azahar confirmation was "compiles and links against libctru" only —
  see `docs/3ds-toolchain.md` for the full from-source toolchain bootstrap
  story.

## Confirmed compiles/links, not yet visually/behaviorally confirmed

- **Framebuffer blit pixel format/orientation.** `Nintendo3DSRenderer`
  writes `GSP_BGR8_OES` (3 bytes/pixel, B-G-R order) into a framebuffer
  documented (both in libctru's own headers and this project's code
  comments) as physically rotated 90° and stored column-major. The exact
  rotated-index formula
  (`(x*fbWidth + (fbWidth-1-y)) * bytesPerPixel`) is implemented per public
  documentation and produces a plausible/booting image in Azahar, but has
  **not been independently pixel-verified** (e.g. rendering a known
  test pattern and confirming exact pixel positions match on real
  hardware). Flagged in-code, not just in docs (`Nintendo3DSRenderer.h:17-36`).
- **Top/bottom screen dual-render pipeline.** Real, exercised code
  (`nintendo3ds_main.cpp`'s dual-screen test app, Phase 10 follow-up):
  separate `Nintendo3DSRenderer` instances for `GFX_TOP`/`GFX_BOTTOM`, a
  single global `presentFrame()` call per real frame (correctly NOT calling
  `gfxFlushBuffers()`/`gfxSwapBuffers()` per-screen, since those are global
  libctru operations — a real bug caught and fixed during Phase 10, see
  `docs/renderer.md`). Compiles/links; boots in Azahar per user report;
  visual correctness of BOTH screens simultaneously not independently
  confirmed.
- **Input mapping (D-Pad/Circle Pad/A/B/X/Y/L/R/touch -> `Key`/`Mouse`).**
  Real `hid`-polling code (`Nintendo3DSInput.cpp`), but the specific
  mapping choices (documented as "judgment calls, not spec-derived facts"
  in `docs/input.md`) have not been confirmed against actual button
  presses on hardware or in Azahar this session. One specific open
  question, flagged in-code: `KEY_TOUCH` is documented by libctru's own
  header as "not actually provided by HID" — this implementation gates
  touch reads on it anyway, unconfirmed.
- **Edge-detected input state (input-transitions phase, 2026-08-19,
  `InputState::commitFrame()`/`isKeyPressed()`/`isKeyReleased()`/
  `isTouchPressed()`/`isTouchReleased()`).** Desktop-side logic is fully
  unit-tested (18 new tests, `tests/test_input_state.cpp`) and the model's
  correctness (exactly one edge per real transition, given `poll()` fires
  once per real hardware frame) is verified by trace/reasoning against the
  confirmed `poll()`/`hidScanInput()`/main-loop call pattern — but, like
  everything else input-related in this project, has **not been confirmed
  against an actual physical button press or touch on real hardware or in
  Azahar**. In particular: whether one real physical press genuinely
  produces exactly one `isKeyPressed()`-true tick (not zero, not more than
  one) has only been reasoned about and desktop-simulated, never observed
  on the actual polling cadence a real device produces.
- **Audio (`Nintendo3DSAudioBackend`).** The diagnostic `playTestTone()`
  path (real ndsp channel + waveform buffer submission) is the ONLY
  confirmed-plausible-audible code path — it was specifically built and
  exercised via the Phase 10 dual-screen test app's A/B/X/Y sound test, and
  the user separately confirmed the resulting `.3dsx` boots (though did not
  separately confirm audio was actually heard). The real SWF-audio path
  (`playSound()`) has zero decoded PCM to play regardless of platform — see
  `docs/known-limitations.md` priority #5; this is a shared (non-3DS-
  specific) gap, not a 3DS platform issue.

## Genuinely 3DS-specific known constraints (design-level, not bugs)

- **No `consoleDebugInit()`** — deliberately excluded from the libctru
  build (needs `sys/iosupport.h`'s full device-table framework, unavailable
  with this project's stock-newlib bootstrap). No on-screen debug console;
  all diagnostics are `LOG_*` calls, which have no visible sink on real
  hardware without a debugger/homebrew logging tool attached.
- **No filesystem I/O implemented.** `flash3ds_syscalls.c` deliberately
  doesn't define `_read`/`_write`/`_open`/etc. (`-specs=nosys.specs` stubs
  them to "always fail" instead). This means **loading an external SWF file
  from an SD card at runtime is not implemented** — the current 3DS build
  only runs the embedded demo SWF (`EmbeddedDemoSwf.h`, baked in at compile
  time via `tools/gen_3ds_demo_swf.py`). This is a significant, currently
  undocumented-elsewhere gap for the stated long-term goal ("a standalone
  3DS 'Flash Virtual Console' runtime... that can load external SWF
  files") — flagged here as a real, confirmed blocker for that specific
  goal, distinct from SWF-format/AVM1 compatibility.
- **Memory**: no measurement taken this phase (per the audit charter,
  performance/memory work is explicitly deferred until functional
  compatibility work is further along). `hobo.swf` alone is ~4.97 MB; this
  runtime loads a movie fully into memory synchronously (no streaming) — 3DS
  has limited RAM (128MB total, much less usable) — this is a plausible
  future constraint, not measured or confirmed as a problem yet.
- **Floating point / timing**: ARM11's VFP (`-mfpu=vfp -mfloat-abi=hard`)
  vs. desktop x86_64 FPU differences not investigated this phase. The two
  confirmed real ARM-cross-compile-only bugs found in Phase 10
  (`std::clamp`/`uint32_t` vs `unsigned int` type-identity mismatches, see
  `docs/3ds-toolchain.md`) are a proof this class of issue is real and worth
  continued vigilance for, not a closed/exhausted list.

## Explicitly deferred per audit charter

Per the compatibility-audit charter ("test the same SWF on real 3DS
hardware and compare against desktop... do not assume a desktop failure is
a 3DS problem"): this comparison has not been performed yet for ANY specific
SWF feature, because no feature has both (a) been confirmed correct on
desktop AND (b) been run on real hardware. The very first candidate for
this comparison, once real hardware is available, should be Priority #1's
fix (`_alpha`/ColorTransform rendering) — it is desktop-confirmed-correct
via regression tests as of this phase, and simple/visual enough to be a good
first real-hardware smoke test.
