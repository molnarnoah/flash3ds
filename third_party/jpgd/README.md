# jpgd (vendored)

**Files:** `jpgd.h`, `jpgd.cpp`
**Upstream project:** [jpeg-compressor](https://github.com/richgel999/jpeg-compressor)
by Rich Geldreich — specifically its JPEG *decompressor* half (`jpgd.h`/
`jpgd.cpp`; the sibling `jpge.h`/`jpge.cpp` compressor is not vendored
here, since this project only ever needs to decode JPEG data embedded in
`DefineBitsJpeg2`/`DefineBitsJpeg3` tags, never encode any).
**License:** Public Domain, or (at the vendor's option) Apache-2.0 — see
the notice at the top of `jpgd.h` itself. No attribution requirement, no
copyleft, no dynamic-linking constraint — same license shape as
`third_party/minimp3` (see that directory's own README.md), chosen for
the identical reason: a statically-linked `.3dsx` binary has no dynamic
linking on 3DS homebrew, which makes an LGPL dependency (e.g. the system
`libjpeg`) awkward, while public-domain/permissive code has no such
concern at all.

## Why this library

Priority Fix List item #2 (`docs/known-limitations.md`) needed a real
JPEG decoder to make `DefineBitsJpeg2`/`DefineBitsJpeg3` tags render
actual bitmap art instead of the flat-gray placeholder every bitmap fill
used before this. jpgd was chosen because:

- **License-compatible with a 3DS homebrew target** — see above.
- **Self-contained, no external dependencies beyond the C++ standard
  library.** No SIMD intrinsics anywhere in the source (confirmed by
  grepping for `__SSE`/`_mm_`/`intrin`/`neon` before vendoring — zero
  hits), so it needs no `MINIMP3_NO_SIMD`-style override for the 3DS
  ARM11 cross-compile: it's already plain, portable C++.
- **Failure handling is contained** (no `exit()`/`abort()` on malformed
  input; a corrupt/truncated JPEG stream unwinds via `setjmp`/`longjmp`
  back to `decompress_jpeg_image_from_memory()`, which returns `nullptr`
  — safe to call directly on untrusted tag data, matching this project's
  existing "never trust the input" posture for every other parser here).
- **Simple in-memory API.** `decompress_jpeg_image_from_memory()` takes a
  raw byte buffer and returns a malloc'd, fully-decoded RGB (or RGBA)
  pixel buffer plus width/height — exactly the shape
  `src/swf/DefineBitsTag.cpp` needs, no stream/callback plumbing required.
- **Handles both baseline and progressive JPEG** (`m_progressive_flag` —
  see `jpgd.cpp`'s SOF marker handling), so it isn't a source of "which
  JPEG variant does the corpus actually use" scoping risk the way the
  bitmap *tag* family itself needed real-corpus evidence for (see
  `docs/renderer.md`'s "Bitmap rendering" section) — one dependency
  covers whatever baseline-vs-progressive mix real encoders produced.

## How it was obtained

This environment could not reach `raw.githubusercontent.com` directly
(the WebFetch/WebSearch content-restriction policy — see this project's
own `CLAUDE.md`-external tooling notes and `third_party/minimp3/README.md`
for the same limitation hit during the audio-decode phase). The files
were instead obtained via Ubuntu's `libjpeg-compressor-cpp-dev` archive
package (`apt-get install libjpeg-compressor-cpp-dev`, universe
component), which packages upstream jpeg-compressor's `jpgd.h`/`jpgd.cpp`
(alongside `jpge.h`/`jpge.cpp`, not vendored here — see above) verbatim
under `/usr/share/include/`; Debian's own `copyright` file for that
package (`/usr/share/doc/libjpeg-compressor-cpp-dev/copyright`) records
the same upstream source URL and Public-Domain/Apache-2.0 license cited
above. Copied byte-for-byte, no edits.

## API surface actually used

Exactly one function, declared in `jpgd.h`:

```cpp
unsigned char *jpgd::decompress_jpeg_image_from_memory(
    const unsigned char *pSrc_data, int src_data_size,
    int *width, int *height, int *actual_comps, int req_comps);
```

Called with `req_comps = 3` (request RGB — `DefineBitsJpeg2` has no
separate alpha channel of its own, and `DefineBitsJpeg3`'s alpha channel
is a *separate* zlib-compressed record this library never sees, decoded
independently by `src/swf/DefineBitsTag.cpp` and combined with this
function's RGB output afterwards). Returns `nullptr` on any decode
failure (truncated/corrupt/non-JPEG data); the caller is responsible for
`free()`-ing the returned buffer once its pixels have been copied into a
`swf::BitmapDef` (see `DefineBitsTag.cpp`).

## Not modified

No decode logic, constants, or algorithm code in these files was changed
from what the Debian package ships. `jpgd.cpp` compiles with real
warnings under this project's own `-Wall -Wextra` (old-style C-cast/
signed-shift/fallthrough warnings typical of a ~2010s C++ codebase) — it
is built as its own small static library target (`jpgd_vendor` in the top
level `CMakeLists.txt`) with warnings suppressed for exactly that target,
the same "don't hold vendored code to this project's own zero-warnings
bar, but don't silently let its warnings leak into the rest of the build
either" principle `src/audio/Mp3Decoder.cpp` already established for
`minimp3.h` (there via a `#pragma GCC diagnostic push/pop` around the
`#include`, since minimp3 is header-only; here via a separate CMake
target, since jpgd is a real `.cpp` translation unit rather than a
single-header library). Do not hand-edit `jpgd.h`/`jpgd.cpp` to silence
those warnings or fix a decode bug — if a real decode bug is ever found,
re-vendor a newer upstream snapshot instead, so these files stay a clean,
auditable copy of a known third-party version.
