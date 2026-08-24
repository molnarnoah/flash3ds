# minimp3 (vendored)

**File:** `minimp3.h`
**Upstream project:** [minimp3](https://github.com/lieff/minimp3) by lieff
**License:** CC0 1.0 Universal (public domain dedication) — see the
notice at the top of `minimp3.h` itself and
<http://creativecommons.org/publicdomain/zero/1.0/>.

## Why this library

Roadmap Phase 3 (2026-08-21) needed a real MP3 decoder to make
`DefineSound`'s embedded MP3 audio actually playable (see
`docs/known-limitations.md` L1 and `docs/implementation-roadmap.md`
Phase 3). minimp3 was chosen because:

- **License-compatible with a 3DS homebrew target.** It's public domain
  (CC0) — no attribution requirement, no copyleft, no dynamic-linking
  constraint. `libmpg123` (the obvious system-library alternative) is
  LGPL, which is awkward for a statically-linked `.3dsx` binary (no
  dynamic linking on 3DS homebrew — LGPL compliance there needs either
  dynamic linking, which doesn't exist, or shipping relinkable object
  files).
- **Single header, no dependencies.** Fits this project's existing
  "vendor what you need directly" pattern (`third_party/3ds-support/`)
  without pulling in a build-system dependency or needing to be built as
  a separate library for both the desktop and 3DS cross-compile targets.
- **No dynamic allocation of its own** beyond the small
  `mp3dec_t`/`mp3dec_frame_info_t` state structs the caller owns — decode
  output goes into a caller-supplied buffer, which suits this project's
  "caller controls the memory" pattern (relevant given the ongoing
  memory-audit work — see `docs/memory-audit.md`).
- **Scalar fallback with no SIMD requirement.** The 3DS's ARM11 CPU is
  ARMv6 with no NEON; minimp3.h's SIMD paths are gated behind
  `__SSE2__`/`__ARM_NEON`/`__aarch64__`/`_M_ARM64` macros and fall back to
  plain C otherwise — no `MINIMP3_NO_SIMD` override needed for a correct
  build, though `src/audio/Mp3Decoder.cpp` defines it anyway for the 3DS
  cross-compile as explicit belt-and-suspenders (see that file).

## How it was obtained

This environment could not reach `raw.githubusercontent.com` directly
(the WebFetch/WebSearch content-restriction policy — see this project's
own `CLAUDE.md`-external tooling notes). The file was instead obtained
via `npm pack minimp3@1.0.0` (the `node-minimp3` package, MIT-licensed as
a *wrapper*, but its `lib/minimp3.h` is an unmodified vendor copy of the
real upstream `minimp3.h`, carrying its own original CC0 notice — that
notice, not node-minimp3's MIT wrapper license, is what governs this
file's actual content). The only edit made here: removed an unused
`#include <iostream>` (replaced with `#include <stdint.h>`, which the
header actually needs for `uint8_t`/`int16_t` and was previously pulled
in transitively) — no functional/decode-logic change. This is an older
minimp3 snapshot (~1,800 lines, the classic `mp3dec_init`/
`mp3dec_decode_frame` two-function API) rather than the newer upstream
version that also ships an `mp3dec_ex_*` higher-level helper API; this
project only needs (and only uses) the core two-function API, so the
snapshot's age doesn't matter here.

## API surface actually used

Only two functions, both declared in `minimp3.h` and defined when
`MINIMP3_IMPLEMENTATION` is `#define`d before including it exactly once
(the classic single-header-library pattern — see
`src/audio/Mp3Decoder.cpp`, the only translation unit that defines that
macro):

```c
void mp3dec_init(mp3dec_t *dec);
int mp3dec_decode_frame(mp3dec_t *dec, const uint8_t *mp3, int mp3_bytes,
                         mp3d_sample_t *pcm, mp3dec_frame_info_t *info);
```

`mp3d_sample_t` is `int16_t` by default (matches this project's PCM16
convention throughout `src/audio/` — see `IAudioBackend.h`), since
`MINIMP3_FLOAT_OUTPUT` is never defined.

## Not modified beyond the one include swap noted above

No decode logic, constants, or algorithm code in this file was changed.
Do not hand-edit `minimp3.h` to fix a bug — if a real decode bug is ever
found, re-vendor a newer upstream snapshot instead, so this file stays a
clean, auditable copy of a known third-party version.
