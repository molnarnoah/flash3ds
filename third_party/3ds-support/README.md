# third_party/3ds-support

Small startup/linker-script files needed to produce a 3DS `.3dsx` executable
with a plain (non-devkitPro) `arm-none-eabi-gcc` toolchain. These are NOT
clean-room — they are the canonical files every devkitARM/3DS homebrew
project uses, and are vendored here verbatim rather than reimplemented,
since they are boilerplate startup/link glue with no game logic in them
whatsoever (equivalent to a libc's `crt0.o`/default linker script) and
reimplementing them clean-room would add risk (a subtly wrong linker script
or startup sequence) for zero benefit. This is consistent with the
project's existing stance on `flash3ds_syscalls.c` (see
`docs/3ds-toolchain.md`): legitimate public-SDK glue reuse is fine; the
line the project does not cross is copying anything from Shift-DX/
gameswf/code.bin.

## Provenance

- **Source repository:** https://github.com/devkitPro/devkitarm-crtls
- **Commit:** `1c0c10257c44bbb5a433453bb6bba91582825492` ("Fix gba_cart.ld
  support for overlays (#4)", 2025-05-02)
- **License:** Mozilla Public License 2.0 (MPL-2.0) — see the header
  comment retained in `3dsx_crt0.s` and `3dsx.ld`, and
  https://mozilla.org/MPL/2.0/. `3dsx.specs` has no letterable header of
  its own (it's a 5-line text file) but is part of the same MPL-2.0
  repository and carries the same license.

## Files copied verbatim (unmodified)

- `3dsx_crt0.s` — the `.3dsx` homebrew entry point / startup code (sets up
  `_start`, calls `__libctru_init`, calls `main`).
- `3dsx.ld` — the linker script defining the `.3dsx` ELF layout (load
  address `0x100000`, section placement, PHDRs).
- `3dsx.specs` — the GCC `-specs=` file wiring the above into a normal
  `arm-none-eabi-gcc` link (`*link:`/`*startfile:` sections; see
  `cmake/Toolchain-3DS.cmake`).

## File added by this project (not from devkitarm-crtls)

- `sync-dmb.specs` — an intentionally EMPTY stand-in file. `3dsx.specs`
  does `%include <sync-dmb.specs>`; in real devkitARM this pulls in a
  target-specific memory-barrier snippet used by devkitARM's own patched
  GCC build. Stock Ubuntu `arm-none-eabi-gcc` doesn't have (or need) this
  snippet — its generic ARMv6K codegen already emits correct barriers
  without extra spec-file assistance — so an empty file satisfies the
  `%include` directive as a safe no-op. Verified empirically: linking
  against it produces correct, working `.3dsx` binaries (see
  `docs/3ds-toolchain.md`'s smoke-test section).
