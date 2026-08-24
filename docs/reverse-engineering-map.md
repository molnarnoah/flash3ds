# Reverse-Engineering Map — Ghidra/Shift-DX ↔ flash3ds-runtime

**Read this first:** `flash3ds-runtime` is a **clean-room, independently
implemented** SWF/AVM1 runtime (`CLAUDE.md`'s hard rules: never copy code
from Shift-DX or gameswf; implement against the public SWF spec). It is
**not** a decompiled or ported version of Shift-DX. Consequently there is
**no `FUN_xxxxx` → runtime-function 1:1 mapping to produce** — that
premise, present in the audit request, does not apply to this codebase.
What legitimately exists is a set of **behavioral cross-checks**: places
where a Ghidra finding about the real, shipped Shift-DX runtime was used
to validate or scope an independent design decision here. All of it is
already recorded in `docs/shift-dx-behavior.md`; this document summarizes
it against the current runtime's actual state and adds the correlation
the audit asked for.

## Ghidra connectivity this session

- **Live and connected**, pointed at **Shift-DX only**
  (`list_segments` → `ram: 0x0-0x44ffff`, ~4.5MB, matches Shift-DX's known
  size).
- The four titles the audit wanted for VC/save/achievement research
  (Pokemon Red VC, Super Mario World VC, Miitopia, Tetris Axis) are **not
  loaded** — switching to any of them requires the user to open a
  different Ghidra project. **No findings below or in `docs/
  implementation-roadmap.md`'s deferred sections are sourced from those
  four titles** — none were fabricated.
- **JPEXS/ffdecmcp: not connected in this session** (`ToolSearch` for
  jpexs/ffdecmcp tools returns zero matches, checked twice). Every
  "JPEXS-evidence" field in this audit's other documents is explicitly
  "not available this session."

## Confirmed Shift-DX findings and what they map to here

| Ghidra finding (Shift-DX) | Confirmed via | flash3ds-runtime correlation |
|---|---|---|
| `gameswf_stream_open_tag`/`close_tag` (0x2dcb48/0x2dcc34) — 10-bit-type/6-bit-length tag header, `0x3F` extended-length escape | String-xref + decompile, prior RE session | Matches `TagDispatcher`/`SwfReader` exactly — **implemented**, confirmed identical shape |
| `gameswf_create_movie_instance`/`gameswf_movie_def_impl_read_header` (0x2dc0b4/0x2b45b8) — signature/compression check → RECT → 8.8-fixed frame rate → frame count → tag loop | String-xref + decompile | Matches `SwfLoader` exactly — **implemented** |
| `sprite::add_display_object()` vs. `sprite::replace_display_object()` (two distinct logged code paths) | String evidence | Confirms `DisplayList::applyPlaceObject()`'s Move/HasCharacter-flag-driven add-vs-replace-vs-update split is real shipped behavior, not spec-only — **implemented** |
| `ExternalInterface` built as a 3-native-function AS object (`addCallback`/`removeCallback`/`call`) | Decompile of `as_global_externalinterface_init` (0x27d584) | Validates this project's `HostInterface`-style boundary — **implemented** (Phase 7, `ScriptEnvironment::registerHostFunction`/`callHostFunction`/`hasCallback`/`invokeCallback`) |
| `avm1_builtin_prototypes_init` (0x2d228c) — ~55 native bindings: `Object.prototype` (`addProperty`/`registerClass`/`hasOwnProperty`/`watch`/`unwatch`/`addEventListener`), `String.prototype` (`toString`/`fromCharCode`/`charCodeAt`/…), **`MovieClip.prototype`** (`gotoAndStop`/`gotoAndPlay`/`nextFrame`/`prevFrame`/`getBytesLoaded`/`getBytesTotal`/**`swapDepths`**/**`duplicateMovieClip`**/`getDepth`/**`createEmptyMovieClip`**/`removeMovieClip`/`hitTest`/`startDrag`/`stopDrag`/**`loadMovie`**/**`unloadMovie`**/`getNextHighestDepth`/`createTextField`/**`attachMovie`**/`localToGlobal`/`globalToLocal`/`getRect`/`getBounds`/`setMask`/drawing-API methods/`setFPS`/`getPlayState`/`addFrameScript` | Decompile, 55-method-name match | **Direct evidence for current-state-audit.md §3's gap list.** Of this real, shipped, 55-method API surface: `gotoAndStop`/`gotoAndPlay`/`nextFrame`/`prevFrame`/`getBytesLoaded`/`getBytesTotal`/`removeMovieClip`/`hitTest`/`startDrag`/`stopDrag` are **implemented** here; **`swapDepths`, `duplicateMovieClip`, `createEmptyMovieClip`, `loadMovie`, `unloadMovie`, `attachMovie`, `getNextHighestDepth`, the entire drawing API, `Object.prototype`'s methods, and all of `String.prototype`'s methods are confirmed-absent, confirmed-real-need gaps** — not speculative; this Ghidra evidence is exactly what a real shipped 3DS Flash port actually called |
| Touch → mouse (`onMouseDown`/`onMouseUp`/`onMouseMove`, `_xmouse`/`_ymouse`) + Up/Over/Down button state machine (`gameswf_button_register_mouse_state_names`, 0x27eed8) | Decompile + string evidence | Validates routing 3DS touch through the same input path as mouse — **implemented** (`Nintendo3DSInput`, `ButtonInstance` state machine) |

## Explicitly unresolved in the Ghidra RE work itself (not this project's gap — the RE's own limitation)

Per `docs/shift-dx-behavior.md`'s own "Open items" section, **never
successfully resolved even against Shift-DX**:

- `Key.isDown()`'s native implementation.
- Exact `DefineSound`/`StartSound` tag-loader boundaries.
- The AVM1 bytecode opcode dispatcher itself (Ghidra's function-boundary
  analysis around ~0x2ab2d0 was inconsistent — a still-open item per the
  top-level `/home/claude/CLAUDE.md`'s own "known anomaly" section).
- Renderer transform-matrix/clipping/dual-screen-target-selection detail.

None of these can be used to validate or scope this project's own AVM1
opcode dispatcher (which is instead built straight from the public SWF
spec, per `CLAUDE.md`) or its `Key`/sound bindings — this was already true
before this audit and remains true. **If resolving these matters to a
future phase**, it requires further Ghidra GUI work (manual `DAT_` →
pointer conversion, `Clear Code Bytes` + `Create Function` around the
0x2ab2d0-0x3ae4bb boundary anomaly) — tracked as still-open in the
top-level project's own `CLAUDE.md`, not duplicated here.

## What this means for the roadmap

The single highest-value, already-available piece of Ghidra evidence is
the `avm1_builtin_prototypes_init` method list above — it turns "we're
missing some MovieClip methods" into a concrete, RE-evidenced priority
list, cross-referenced against which real corpus games actually need which
missing method (see `docs/real-game-compatibility.md`: Extreme Pamplona's
AS2 scan positively confirms `createEmptyMovieClip`/`duplicateMovieClip`/
`attachMovie` usage; no Hobo file uses any of them). This is folded into
`docs/implementation-roadmap.md`'s phase ordering.
