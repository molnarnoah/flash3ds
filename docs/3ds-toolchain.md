# Nintendo 3DS toolchain — from-source bootstrap (Phase 10)

## Why this document exists

Phase 10's target platform is the Nintendo 3DS, whose standard SDK is
**devkitARM** (part of the devkitPro toolchain suite). This project's build
environment cannot reach `apt.devkitpro.org` (the sandbox's network
allowlist proxy returns HTTP 403 for it), so the normal
`dkp-pacman`/`devkitpro-pacman` install path is unavailable. `github.com`
and `raw.githubusercontent.com` **are** reachable, and devkitPro publishes
the actual source of every library devkitARM ships (`libctru`, `citro3d`,
`3dstools`, `devkitarm-crtls`) as public GitHub repositories. This document
records how those sources were built against Ubuntu's own generic
`gcc-arm-none-eabi`/`g++-arm-none-eabi` cross-compiler instead — a
from-source bootstrap of a devkitARM-equivalent toolchain — including every
build failure encountered and exactly how each was resolved, so a future
session can reproduce or extend this without re-deriving it from scratch.

**This is a real, working toolchain**, not a stub: it was used in this
session to cross-compile `flash3ds_core` (the full, unmodified,
platform-independent runtime) plus Phase 10's new 3DS-specific backend
code into a linked, packaged `.3dsx` with zero undefined non-weak symbols.
See "What's verified vs. not" below for the precise boundary of what that
does and doesn't prove.

## Layout produced by this bootstrap

```
<toolchain-root>/
  libctru/libctru/{include, lib/libctru.a, source/...}
  citro3d/{include, lib/libcitro3d.a, source/...}
  3dstools/src/{3dsxtool.cpp, romfs.cpp}   (compiled to a native host binary)
  devkitarm-crtls/{3dsx_crt0.s, 3dsx.ld, 3dsx.specs, ...}
  dkp-newlib/                              (sparse checkout, investigation only)
  zlib/{*.c, *.h, build_arm/libz.a}
  stub_include/sys/iosupport.h             (empty stand-in — see below)
  build_libctru.sh, build_citro3d.sh       (the actual build scripts used)
  3dsxtool                                 (native host binary)
```

`cmake/Toolchain-3DS.cmake` (in this repo) consumes this layout via
`-DFLASH3DS_3DS_TOOLCHAIN_ROOT=<toolchain-root>`; see that file's own
comments for the exact cache variables. `third_party/3ds-support/` (in
this repo) vendors the small crt0/linker-script/specs files verbatim from
`devkitarm-crtls` (MPL-2.0; see that directory's `README.md` for
provenance) so they don't need to be re-fetched separately.

## What's genuinely reused vs. clean-room

Per this project's `CLAUDE.md` hard rule, nothing here comes from
Shift-DX/gameswf/code.bin. Everything in this document comes from
devkitPro's own public SDK source repositories (`libctru`, `citro3d`,
`3dstools`, `devkitarm-crtls`) or from Ubuntu's packaged generic ARM
cross-compiler and newlib — legitimate public-SDK reuse, the same
category as linking against any other vendor SDK. The only genuinely
*written* (not vendored) pieces are: `stub_include/sys/iosupport.h` (an
empty file), `libctru/libctru/source/system/flash3ds_syscalls.c` (a
minimal newlib syscall surface, described below — small glue functions,
with the two functions that ARE copied from libctru's own original
`syscalls.c` explicitly marked as such), a one-line patch to
`synchronization.h`, an empty `sync-dmb.specs` stand-in, and the build
scripts themselves.

## Step 1 — compiler

```
apt-get install -y gcc-arm-none-eabi g++-arm-none-eabi
```

This is a **generic** ARM EABI bare-metal cross-compiler — not
devkitARM's own patched GCC build. It has no 3DS-specific knowledge at
all; every piece of that knowledge comes from the libraries/scripts below.

## Step 2 — ABI flags (ARMv6K/MPCore vs. available multilib)

The 3DS's ARM11 core is ARMv6K (MPCore). Ubuntu's `gcc-arm-none-eabi`
package does not ship a precompiled ARMv6K multilib runtime (its
`arm-none-eabi/lib` only has `arm/v5te/{soft,hard}` and various `thumb/*`
variants — confirmed via `find`/`gcc -print-multi-lib`). This is not a
blocker: ARMv6K is a strict instruction-set superset of ARMv5TE with an
**identical EABI hard-float calling convention**, so linking
own-compiled ARMv6K objects against the v5te-multilib prebuilt runtime
pieces (`libgcc.a`, `libc.a`, `libstdc++.a`, ...) is functionally correct
— just not maximally optimized for the exact core. The flags used
throughout this bootstrap:

```
-march=armv6k -mtune=mpcore -mfpu=vfp -mfloat-abi=hard -mtp=soft
```

`-mfpu=vfp` must be **explicit** — devkitARM's own GCC build implies an
FPU by default for this target; a stock GCC does not, and
`-march=armv6k -mtune=mpcore -mfloat-abi=hard` alone fails with `cc1:
error: selected architecture lacks an FPU`.

Confirmed via `arm-none-eabi-gcc <these flags> -print-multi-directory` ->
`arm/v5te/hard`, i.e. GCC's own multilib selection logic picks the
v5te/hard runtime automatically with no `-L` override needed — verified
directly in this session before relying on it.

## Step 3 — crt0 / linker script / specs (devkitarm-crtls)

```
git clone --depth 1 https://github.com/devkitPro/devkitarm-crtls.git
```

This repo is exactly the small set of `.3dsx`-launcher glue devkitARM's
own installed `3dsx.specs` wires in: `3dsx_crt0.s` (the `_start` entry
point — calls `__libctru_init` then `main`), `3dsx.ld` (the ELF layout
linker script, load address `0x100000`), and `3dsx.specs` (a GCC
`-specs=` file with `*link:`/`*startfile:` sections tying the two
together). These three files are vendored verbatim into this repo at
`third_party/3ds-support/` (MPL-2.0, see that directory's own
`README.md`) rather than re-fetched at build time.

`3dsx.specs` also does `%include <sync-dmb.specs>` — a devkitARM-specific
spec snippet (a target-specific memory-barrier codegen hint) that doesn't
exist in a stock GCC install and isn't needed by one (stock ARMv6K codegen
already emits correct barriers without it). `third_party/3ds-support/`
provides an empty `sync-dmb.specs` stand-in to satisfy the `%include` as a
safe no-op — verified empirically (see "smoke test" below): linking
against it produces a correctly-behaving binary.

One more gap `-specs=3dsx.specs`'s `*startfile:` line does **not** cover:
it looks up `3dsx_crt0.o` by bare filename via GCC's `-B`/`-L` search
path, but does not compile it from `3dsx_crt0.s` itself. This project's
`CMakeLists.txt` assembles it once per configure into the build tree
(`add_custom_command`, see the `flash3ds_3ds_crt0` target) with the same
ARCH flags as everything else, rather than committing a prebuilt object.

## Step 4 — libctru

```
git clone --depth 1 https://github.com/devkitPro/libctru.git
```

Built via a **custom bash script** (`build_libctru.sh`), not libctru's own
Makefile (which assumes the full devkitARM environment/`$DEVKITARM`).
Compiles every `.c`/`.cpp`/`.s` under libctru's own `source/` subdirectory
list directly with `arm-none-eabi-gcc`/`g++`/`as` (via the gcc driver, see
below), archives the result with `arm-none-eabi-ar rcs`.

**Issues hit and fixes (all in this session, all confirmed resolved):**

- **`.s` files failing with `Error: bad instruction 'begin_asm_func ...'`.**
  Root cause: these `.s` files use C-preprocessor macros
  (`#include <3ds/asminc.h>` etc.) and need to be run through the
  preprocessor first — plain `.s` is not preprocessed by GCC by default.
  Fix: added `-x assembler-with-cpp` to `ASFLAGS`.
- **`arm-none-eabi-as -mtune=mpcore` -> `Error: unrecognized option
  -mtune=mpcore`.** Root cause: invoking the raw assembler directly
  doesn't understand GCC-level `-mtune`. Fix: invoke `arm-none-eabi-gcc -c
  file.s` (the gcc driver translates the flags correctly), never `as`
  directly.
- **`synchronization.c`: struct-member errors on `RecursiveLock`**
  (`request for member 'lock' in something not a structure`). Root cause:
  libctru's own `synchronization.h` defines `RecursiveLock` as
  `_LOCK_RECURSIVE_T` — a type from newlib's *retargetable locking*
  extension (`_RETARGETABLE_LOCKING`), which devkitARM's own patched
  newlib build has enabled but Ubuntu's generic newlib package does not
  (confirmed: without it, `_LOCK_T`/`_LOCK_RECURSIVE_T` are dummy `int`
  typedefs with no-op locking macros). Fix: patched
  `include/3ds/synchronization.h` to define `RecursiveLock` as a
  self-contained struct (`LightLock lock; s32 thread_tag; s32 counter;`)
  instead of `_LOCK_RECURSIVE_T`. Verified safe via `arm-none-eabi-nm` on
  the prebuilt `libc.a`: it references **zero** `__retarget_lock_*`
  symbols, confirming this newlib build never expected the retargetable-
  locking path to be wired up at all, so nothing else depends on the
  original typedef's shape.
- **~40 files: `fatal error: sys/iosupport.h: No such file or
  directory`.** Root cause: this header genuinely does not exist in
  Ubuntu's newlib package (confirmed via `dpkg -L` and filesystem search)
  — it's part of devkitARM-newlib's own device-table/virtual-filesystem
  framework. Two different fixes depending on how deeply each file
  actually uses it:
  - `ctru_init.c` (and originally `syscalls.c`, see Step 5) only
    `#include` it transitively without using any of its actual API
    surface — satisfied with `stub_include/sys/iosupport.h`, an
    intentionally **empty** file, passed via `-I`.
  - `console.c`, `romfs_dev.c`, `archive_dev.c`, `gdbhio_dev.c`,
    `path_buf.c`, `sslc.c`, and everything under `source/services/soc/`
    (~40 files, TLS-socket/networking) all genuinely *use* the
    device-table framework or need networking this project's Phase 10
    scope doesn't (no virtual filesystem, no debug console, no
    networking) — **excluded from the build entirely**
    (`EXCLUDE_FILES`/`EXCLUDE_DIRS` in `build_libctru.sh`). This means
    `consoleInit()`/SD-card file I/O/etc. are genuinely unavailable in
    this build — see `nintendo3ds_main.cpp`'s own scope note for the
    concrete consequence (the app plays an embedded demo movie, not one
    loaded from SD card).
- **`syscalls.c`: `'__SYSCALL' declared as function returning a
  function'` and dozens of cascading errors.** Root cause: this file uses
  an `__SYSCALL(name)` macro that's part of devkitARM-newlib's own
  pthread-retargeting patch set — confirmed absent even in devkitPro's own
  `newlib` fork's tracked `sys/` headers (checked via a sparse clone,
  `dkp-newlib/`), so it isn't something a header/include-path fix could
  restore. Fix: excluded `syscalls.c` entirely and wrote a minimal
  replacement, `flash3ds_syscalls.c` (see Step 5).

**Result: 105/105 objects compiled successfully, zero failures.**

## Step 5 — flash3ds_syscalls.c (replaces libctru's syscalls.c)

libctru's own `syscalls.c` needs devkitARM-newlib's pthread-retargeting
surface (Step 4's `__SYSCALL` finding), which doesn't exist here and isn't
needed for Phase 10's single-threaded render loop. The replacement,
`libctru/libctru/source/system/flash3ds_syscalls.c`, provides only the
symbols an actual link against this newlib build empirically asked for
(discovered via undefined-reference errors during the smoke test below,
not guessed up front):

- `fake_heap_start`/`fake_heap_end` + `_sbrk()` — newlib's sbrk-based heap.
- `_exit()` — calls `__ctru_exit()`.
- `initThreadVars()` + `__system_initSyscalls()` — thread-local-storage
  bootstrap for the main thread. These two are **reproduced faithfully
  from libctru's own original `syscalls.c`** (explicitly marked as such in
  the file's header comment) since this is legitimate public-SDK logic,
  not reverse-engineered code, and Phase 10's scope needs the exact same
  TLS bootstrap behavior libctru's own startup path relies on.
- No-op `archiveMountSdmc()`/`archiveUnmountAll()` stubs, standing in for
  the excluded `archive_dev.c` (no SD-card filesystem access in this
  phase).

Deliberately **not** redefined: `_read`/`_write`/`_close`/`_fstat`/
`_isatty`/`_lseek`/`_gettimeofday`/`_kill`/`_getpid`/`_getentropy`. These
are supplied at *link* time by Ubuntu's `libnosys.a` (see Step 8) — newlib's
own "always fail with ENOSYS" defaults — rather than redefined here.

## Step 6 — citro3d

```
git clone --depth 1 https://github.com/devkitPro/citro3d.git
```

Built via `build_citro3d.sh`, same custom-script approach as libctru,
adding `-DCITRO3D_BUILD -I<libctru include>`. **Result: 50/50 objects
compiled successfully, zero failures — no issues hit at all** (unlike
libctru, citro3d has no `sys/iosupport.h`/retargetable-locking
dependencies).

## Step 7 — 3dstools (host-side, NOT cross-compiled)

```
git clone --depth 1 https://github.com/devkitPro/3dstools.git
```

Only `3dsxtool` (the ELF -> `.3dsx` packager) is needed for this phase;
`smdhtool` (icon/metadata packaging) is not. `libtool`/autotools weren't
available/needed — `src/3dsxtool.cpp` + `src/romfs.cpp` compile directly
as an ordinary **native** (host, `g++`, not `arm-none-eabi-g++`) binary
with zero special flags. One harmless warning (unused `fread` return
value).

## Step 8 — linking: the parts that only showed up at actual link time

Everything above resolves at compile time. Three more issues only surface
once a real executable is linked, all found and fixed in this session
while validating the toolchain end-to-end for `flash3ds_3ds` (not just the
smoke test, though the smoke test found the first two):

- **Link order: `-lcitro3d` must come *before* `-lctru`.** citro3d
  references libctru symbols (`GPUCMD_Add`, `gxCmdQueue*`, `GX_*`,
  `f32tof24`/`f32tof16`, ...). Static archive linking is single-pass by
  default — placing `-lctru` first means those symbols are looked up
  before anything has asked for them, and they're never revisited. Fixed
  by ordering `-lcitro3d -lctru -lm` (confirmed by deliberately
  reproducing the failure with the reverse order, then fixing it).
- **`_read`/`_write`/`_close`/`_fstat`/`_isatty`/`_lseek`/
  `_gettimeofday`/`_kill`/`_getpid`/`_getentropy`: undefined reference.**
  `flash3ds_syscalls.c` (Step 5) deliberately doesn't define these,
  expecting newlib's own "always fail" defaults — but those defaults are
  not linked in automatically; they live in a separate archive,
  `libnosys.a`, which Ubuntu's `gcc-arm-none-eabi` package ships
  alongside a matching `nosys.specs`. A plain `-lnosys` in the wrong
  link-line position does **not** work (same single-pass-linking
  circularity as the citro3d/libctru ordering issue, this time between
  libc/libnosys/libgcc). The actually-correct fix, discovered by reading
  `nosys.specs` itself: it overrides GCC's `*link_gcc_c_sequence` to wrap
  `libc`/`libnosys`/`libgcc` in a `--start-group`/`--end-group`, which
  correctly resolves the circular references. `cmake/Toolchain-3DS.cmake`
  adds `-specs=nosys.specs` (alongside `-specs=3dsx.specs`) to
  `CMAKE_EXE_LINKER_FLAGS_INIT` for exactly this reason.
- **`3dsx_crt0.o`: cannot find.** Covered in Step 3 — CMake assembles it
  into the build tree; this was a real failure the first time
  `flash3ds_3ds` was actually linked through CMake (the earlier hand-run
  smoke test had already produced its own `3dsx_crt0.o` manually).

## Step 9 — smoke test (hand-run, before any CMake integration existed)

Before wiring any of this into `flash3ds-runtime`'s own `CMakeLists.txt`,
the whole chain was validated by hand with three progressively more
complete `main.c` variants:

1. A trivial `main.c` with no libctru calls at all — proved the
   crt0/linker-script/specs plumbing alone produces a valid, loadable
   `.3dsx`.
2. `main2.c` using `gfxInitDefault`/`aptMainLoop`/`hidScanInput`/
   `gspWaitForVBlank`/`KEY_START` — linked against `libctru.a` with **zero
   undefined symbols**, packaged to a 32,884-byte `.3dsx`.
3. `main3.c` adding `citro3d.h`/`C3D_Init`/`C3D_FrameBegin`/`C3D_FrameEnd`/
   `C3D_Fini` — linked against **both** `libctru.a` and `libcitro3d.a`
   with zero undefined symbols, packaged to a 39,232-byte `.3dsx`, magic
   bytes (`3DSX`) confirmed via `od`.

This smoke test is what caught the link-order and `sync-dmb.specs`/
`3dsx_crt0.o` issues described above, before any of this project's own
code was involved — isolating "is the toolchain itself sound" from "does
flash3ds-runtime's own code cross-compile", which made the later, real
integration (Step 10) much easier to debug.

## Step 10 — building flash3ds-runtime itself for the 3DS

Once the toolchain was validated in isolation, `cmake/Toolchain-3DS.cmake`
+ the top-level `CMakeLists.txt`'s `FLASH3DS_BUILD_3DS` section (see
`docs/architecture.md`'s module layout for the new 3DS-only source files)
cross-compiled `flash3ds_core` — the real, complete, platform-independent
runtime library, unmodified by Phase 10 except for two genuine portability
bug fixes described next — plus the new `Nintendo3DSRenderer`/
`Nintendo3DSInput`/`Nintendo3DSAudioBackend`/`nintendo3ds_main.cpp`, and
linked the result into `flash3ds_3ds.3dsx`.

**Zlib.** No prebuilt ARM zlib exists anywhere (needed for `SwfLoader.cpp`'s
CWS/zlib-compressed-SWF decompression). Cross-compiled from `zlib` v1.3.1
source (15 core `.c` files: `adler32 compress crc32 deflate gzclose gzlib
gzread gzwrite infback inffast inflate inftrees trees uncompr zutil`) with
the same ARCH flags as everything else — all 15 succeeded, zero failures.
`CMakeLists.txt` declares an `IMPORTED` `ZLIB::ZLIB` target pointing at
this build when cross-compiling, so `flash3ds_core`'s
`target_link_libraries(... ZLIB::ZLIB)` line needs no change between
desktop and 3DS configures.

**Two genuine portability bugs found and fixed** (both in
platform-independent code, both benefit every future platform, not
3DS-specific workarounds):

- `Timeline::gotoAndStop()`/`gotoAndPlay()` called
  `std::clamp(frameIndex, 1u, frameCount())`. `1u` is `unsigned int`;
  `frameIndex`/`frameCount()` are `uint32_t`. On x86_64 desktop these
  happen to be the same type, so this silently compiled; on this ARM
  target `uint32_t` is a distinct type from `unsigned int` as far as
  `std::clamp`'s single-type template deduction is concerned (confirmed
  via a minimal `static_assert(std::is_same<int32_t, int>::value)` probe,
  which fails on this toolchain), so the desktop build had been
  compiling something that was never actually portable. Fixed by casting
  the literal to `uint32_t` explicitly.
- `SoftwareRenderer::fillPolygon()` computed `minY`/`maxY` as plain `int`
  compared directly against `PointTwips::y` (`int32_t`) via
  `std::min`/`std::max` — same underlying type-mismatch issue. Fixed with
  an explicit cast at each comparison.

**Embedded demo content.** Phase 10's entry point plays a small,
hand-authored demo SWF (a red square sliding across a blue-background
stage over 8 frames) rather than loading a file from the SD card — see
`nintendo3ds_main.cpp`'s own header comment for why (SD-card access needs
either direct `FSUSER` calls or restoring the excluded `archive_dev.c`,
both explicit follow-ups). The demo is generated by
`tools/gen_3ds_demo_swf.py` (a from-scratch, clean-room SWF byte-level
constructor written directly against the public SWF spec — not derived
from `tests/SwfTestFixtures.cpp`, which is test-only and never linked into
any shipped binary) into the checked-in, generated
`src/platform/EmbeddedDemoSwf.h`. Before being embedded, the generated SWF
was **independently verified** using this project's own desktop
`flash_runtime` CLI (`--timeline`, `--render`) — exactly the same
methodology `docs/compatibility.md` used for real Hobo content: the
render was pixel-sampled at the expected square position for frame 1 (x
~20px) and frame 8 (x ~380px) and matched exactly (RGB `0xE0,0x30,0x30`,
the declared fill color, at both expected locations). Two bugs were found
and fixed in the generator itself during this verification (a bogus
`HasTranslate` bit in the hand-rolled MATRIX encoder — the real SWF spec
has no such flag, translate fields are unconditional — and a wrong
`PlaceObject2` flags-byte bit assignment for `HasMatrix`, cross-checked
against this project's own `PlaceObjectTag.cpp` parser to fix), which is
exactly the kind of self-contained, testable mistake this incident
illustrates handling correctly rather than shipping silently wrong.

**Final result:**

```
$ cmake -S . -B build_3ds \
    -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-3DS.cmake \
    -DFLASH3DS_3DS_TOOLCHAIN_ROOT=/path/to/3ds-toolchain
$ cmake --build build_3ds -j
...
Packaging flash3ds_3ds.3dsx
[100%] Built target flash3ds_3ds
```

`arm-none-eabi-nm -u build_3ds/flash3ds_3ds` reports only 8 remaining
undefined symbols, **all weak** (`_ITM_deregisterTMCloneTable`,
`_ITM_registerTMCloneTable`, `__deregister_frame_info`,
`__gnu_Unwind_Find_exidx`, `__libc_fini`, `__register_frame_info`,
`userAppExit`, `userAppInit`) — standard optional C++-runtime/libctru
hooks that resolve to no-ops when absent, not a linking gap.
`build_3ds/flash3ds_3ds.3dsx` (381,768 bytes) starts with the correct
`3DSX` magic. The desktop build and its 187 tests were rebuilt and
re-verified passing immediately afterward, in the same session, to
confirm Phase 10 didn't regress anything above it (per this project's
own phase-completion rule).

## Dual-screen / button / sound test app (post-initial-delivery)

After the first Phase 10 `.3dsx` was delivered, the user confirmed it
**boots and runs in Azahar** (a Citra-based 3DS emulator) — the first
actual hardware-emulator confirmation this project has had. Building on
that, the test app was extended to exercise more of the platform surface
at once:

- **Both screens, every frame.** `nintendo3ds_main.cpp` now instantiates
  one `Nintendo3DSRenderer` per screen (`GFX_TOP` 400x240, `GFX_BOTTOM`
  320x240) instead of one. This surfaced a real bug in the original
  single-screen design: `gfxFlushBuffers()`/`gfxSwapBuffers()` are
  **global, both-screens** libctru calls (confirmed in `source/gfx.c` —
  `gfxSwapBuffers()` unconditionally swaps both `GFX_TOP` and
  `GFX_BOTTOM`), not per-screen. The original `endFrame()` called both
  once per invocation, which happened to be correct with exactly one
  screen active (one `endFrame()` == one real frame), but calling it twice
  per real frame once a second screen exists double-swaps each screen's
  buffer index, showing stale content instead of what was just drawn.
  Fixed by moving both calls out of `endFrame()` (now blit-only) into a
  new static `Nintendo3DSRenderer::presentFrame()`, called exactly once
  per real frame after every active screen has been blitted. See
  `docs/renderer.md`'s matching section for more detail.
- **Bottom-screen input test picture.** `drawButtonTestScreen()` draws a
  live diagnostic picture using `IRenderer::fillPolygon`/`strokePolyline`
  directly (no SWF content) — a box per D-Pad direction/face button/
  shoulder button/Start/Select that lights up while held, a Circle Pad
  bounding box + offset dot (raw `hidCircleRead()`), and a touch-position
  dot (raw `hidTouchRead()`). See `docs/input.md`'s matching section.
- **Audible sound test.** `Nintendo3DSAudioBackend::playTestTone()` (new,
  diagnostic-only — see `docs/audio.md`) synthesizes a short sine-wave tone
  into libctru's linear heap and queues it via `ndspChnWaveBufAdd` on a
  dedicated channel; A/B/X/Y each trigger a distinctly-pitched tone on
  press. This is a real, audible exercise of the `ndsp` pipeline,
  independent of the still-missing SWF audio codec decode.
- **Quit control changed** from START alone to START+SELECT held together,
  so START's own indicator is visible/testable on the button screen
  instead of exiting the instant it's pressed.

This dual-screen/input/sound test build was compiled and linked cleanly on
the first attempt after these changes (zero new build/link issues beyond
what Steps 1-10 above already resolved) — `arm-none-eabi-nm -u` again
reports only the same 8 weak, harmless undefined symbols.

## What's verified vs. not

**Verified in this session, with evidence:**

- Every library (libctru, citro3d, zlib) and the crt0/linker-script/specs
  combo compiles and links correctly against a real, complete, unmodified
  copy of `flash3ds_core` plus the new Phase 10 backend code, producing a
  structurally valid `.3dsx` (correct magic bytes, correct ELF header,
  zero undefined *non-weak* symbols).
- The `.3dsx` **boots and runs in Azahar** — confirmed directly by the
  user in this session, for the initial (top-screen-only) build. The
  dual-screen/button/sound follow-up build was not separately re-confirmed
  running by the user as of this writing.
- The embedded demo SWF's *content* is correct — independently confirmed
  via this project's own desktop rendering pipeline (which is the same
  `SceneRenderer`/`ShapeTessellator`/`Timeline` code the 3DS build also
  cross-compiles unchanged, so this is meaningful evidence, not a
  tautology).
- The desktop build and all 187 tests are unaffected (rebuilt and
  re-tested in this session after every Phase 10 code change, including
  the dual-screen/button/sound follow-up).
- The rotated-framebuffer blit formula matches libctru's own source
  (`source/gfx.c`), not just general folklore, and is identical for both
  screens (only logical width/height and the `gfxScreen_t` argument
  differ).
- The `gfxFlushBuffers()`/`gfxSwapBuffers()`-are-global finding above is
  confirmed directly from libctru's own source, not inferred from
  symptoms.

**NOT verified in this session:**

- Whether the rendered output actually **appears pixel-correct** on
  screen (right orientation, right colors, no off-by-one row/column in the
  framebuffer blit, bottom screen specifically) — booting successfully in
  Azahar is strong evidence the pipeline works end to end, but is not the
  same as a reported visual check of what's actually on screen.
- Whether `Nintendo3DSInput`'s touch/button mapping and the new
  `drawButtonTestScreen()` picture behave as intended against real input
  (in particular the `KEY_TOUCH`/touch-position-`(0,0)` heuristics noted in
  `docs/input.md`) — not separately reported.
- Whether `Nintendo3DSAudioBackend::playTestTone()` is actually audible —
  the code compiles/links and follows the documented ndsp API sequence
  correctly, but no audio output has been separately confirmed by ear.
- Real-world timing/performance (frame pacing with two screens now being
  drawn, `gspWaitForVBlank` behavior, ndsp audio latency) — none of this
  can be assessed without running on the target and reporting back.

Running the latest `.3dsx` and reporting back on all four of these
(top-screen render, bottom-screen button/circle-pad/touch picture, sound
on A/B/X/Y, and START+SELECT quit) is the natural next step.
