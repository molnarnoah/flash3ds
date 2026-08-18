# Input

**Status: Phase 6 built `InputState` (host-settable keyboard/mouse bag)
backing `Key.isDown()`/`_xmouse`/`_ymouse`/`StartDrag`/`EndDrag`. Phase 10
added `Nintendo3DSInput`, a real libctru `hid`-polling implementation that
feeds it.**

`InputState` (`src/runtime/InputState.h/.cpp`) is deliberately a plain
host-settable data struct, not an abstract interface like `IRenderer`/
`IAudioBackend` — see that file's header comment for why that's the right
shape specifically for input (there's no meaningful "input backend"
polymorphism the way there is for pixel output or sound playback; every
platform just needs to set the same handful of fields once per tick).
`docs/shift-dx-behavior.md`'s "Input mapping" section confirms Shift-DX
routed 3DS touch through the same code path as Flash "mouse" events — the
same design this project uses.

## Phase 10 — Nintendo3DSInput

`src/platform/Nintendo3DSInput.h/.cpp` polls libctru's `hid` service once
per frame (`hidKeysHeld()`/`hidTouchRead()`) and writes the result into an
`InputState`. Mapping decisions (all documented, judgment-call
conventions, not spec-derived facts, since AS2/Flash's Key/Mouse model was
never designed for a handheld's button layout):

- **Touch screen -> mouse.** Touch position feeds `_xmouse`/`_ymouse`;
  touch-held feeds `Mouse` "down" state. This is the closest 3DS analog to
  a pointer device, but means mouse coordinates only update while the
  bottom screen is actually being touched (there is no cursor-based
  pointer on real hardware). One specific detail is flagged as an open
  question rather than resolved with certainty: libctru's own `hid.h`
  documents `KEY_TOUCH` as "Not actually provided by HID" — this
  implementation currently gates touch reads on that bit anyway, since no
  hardware/emulator was available in this session to observe the actual
  runtime behavior and confirm or replace that approach.
- **D-Pad / Circle Pad -> arrow keys** (`Key.LEFT`/`RIGHT`/`UP`/`DOWN`),
  merging both input methods per libctru's own `KEY_LEFT`/etc. convenience
  masks (each already ORs the D-Pad and Circle Pad bits together).
- **A/START -> `Key.ENTER`, B/SELECT -> `Key.ESCAPE`** (a common
  confirm/cancel convention), **X/Y -> printable ASCII `'X'`/`'Y'`** (no
  natural AS2 equivalent exists for these buttons at all).

None of this has been exercised against real 3DS button input in this
session (no hardware/emulator available) — it's real code that compiles
and links against libctru (see [3ds-toolchain.md](3ds-toolchain.md)), with
a documented, reasonable-effort mapping a target title can override if its
own control needs differ.
