# Shift-DX Behavioral Reference

This document records what our Ghidra reverse-engineering of the Shift-DX
(Nintendo 3DS) executable established about its embedded Flash/SWF runtime,
for use as a **behavioral cross-check only**. No code from that binary (or
from the gameswf library it embeds) is copied into this project — this file
exists so that when `flash3ds-runtime`'s independent implementation makes a
design choice, we can check it against how a real, shipped 3DS Flash port
actually behaved.

Source: two Ghidra RE sessions against the Shift-DX ELF reconstruction,
summarized in the companion reports `shift2_3ds_swf_engine_reconstruction.md`
and `shift-dx_swf_runtime_reconstruction.md` (delivered separately, not part
of this repo). 11 functions were confirmed and renamed in that Ghidra
project via string-literal evidence + decompilation; see those reports for
full detail and confidence levels.

## Confirmed findings relevant to Phase 1

### Container identity

Shift-DX embeds the open-source **gameswf** SWF player (Thatcher Ulrich),
ported to 3DS and extended with AS3/ABC (`DoABC` tag) support alongside the
classic AVM1 interpreter. This is evidence the runtime we're reproducing is
**AVM1-first with optional AS3**, matching this project's stated priority
(section 3 of the spec: AVM1/AS2 first, do not prioritize AVM2).

### Tag stream format — matches our Phase 1 implementation

Ghidra function `gameswf_stream_open_tag` (0x0002dcb48) decompiles to
exactly the tag-header format `flash3ds-runtime`'s `TagDispatcher` also
implements: a 16-bit little-endian word, top 10 bits = tag type, bottom 6
bits = length, with `0x3F` as an escape to a following 32-bit extended
length. `gameswf_stream_close_tag` (0x0002dcc34) verifies the stream
position against the tag's declared end and logs a mismatch rather than
crashing — our `SwfReader`/`SwfLoader` follow the same "log and continue
gracefully" philosophy for malformed input.

### Movie creation flow — matches our SwfLoader shape

`gameswf_create_movie_instance` (0x002dc0b4) and
`gameswf_movie_def_impl_read_header` (0x002b45b8) together implement
exactly the `SWF_Load → SWF_ParseHeader → SWF_ParseTags` sequence this
project's architecture calls for: signature/compression check, CWS zlib
decompression (only entered when the signature's 3rd byte is `'C'`),
version + declared length, stage RECT, frame rate (confirmed 8.8
fixed-point, matching `Movie::frameRateFps()`), frame count, then straight
into the tag-reading loop.

### Tag dispatch is table-driven

The Shift-DX tag loop looks up a `(tag_type -> handler)` hash table per
tag. Our Phase 1 doesn't implement per-tag handlers yet (see
`swf-support.md`), but the architecture is already shaped for that: Phase 3+
should register per-tag-code handlers into `TagDispatcher` (or a
successor), mirroring this table-driven approach rather than a giant
`switch`.

### ExternalInterface — confirmed native-binding shape (informs Phase 7)

Shift-DX builds `flash.external.ExternalInterface` as an AS object with
three members (`addCallback`, `removeCallback`, and a third slot
positionally consistent with `call`), each wrapping a **native C++
function pointer** via a generic "wrap native fn as AS value" helper. The
exact native function addresses could not be resolved in our RE pass (no
raw-memory-read tool was available against the Ghidra MCP bridge at the
time), but the *structure* — an abstract host boundary that ActionScript
calls into, and that native code can call back through — directly
validates this project's planned `HostInterface` abstraction (section 10 of
the spec): `externalCall(name, args)` / `registerCallback(name, callback)`.

### AVM1 built-in API surface — confirms Phase 4/5/9 scope

`avm1_builtin_prototypes_init` (0x002d228c) enumerates, via ~55 sequential
native-binding registrations, the full built-in method set actually used by
a real AS2 Flash port: `Object.prototype` (`addProperty`, `registerClass`,
`hasOwnProperty`, `watch`, `unwatch`, `addEventListener`),
`String.prototype` (`toString`, `valueOf`, `fromCharCode`, `charCodeAt`,
`concat`, `indexOf`, `lastIndexOf`, `slice`, `split`, `substring`,
`substr`, `toLowerCase`, `toUpperCase`, `charAt`, `length`), and
`MovieClip.prototype` (`gotoAndStop`, `gotoAndPlay`, `nextFrame`,
`prevFrame`, `getBytesLoaded`, `getBytesTotal`, `swapDepths`,
`duplicateMovieClip`, `getDepth`, `createEmptyMovieClip`,
`removeMovieClip`, `hitTest`, `startDrag`, `stopDrag`, `loadMovie`,
`unloadMovie`, `getNextHighestDepth`, `createTextField`, `attachMovie`,
`localToGlobal`, `globalToLocal`, `getRect`, `getBounds`, `setMask`,
`beginFill`, `endFill`, `lineTo`, `moveTo`, `curveTo`, `clear`,
`lineStyle`, `setFPS`, `getPlayState`, `addFrameScript`). This is a
concrete, evidence-based target list for Phase 5's MovieClip API — it's
what a real target game (Shift, and presumably Hobo-family games since they
share the same SWF6-era AS2 idioms) actually calls.

### Input mapping — informs Phase 12

The 3DS touch screen was mapped to the Flash "mouse" API
(`onMouseDown`/`onMouseUp`/`onMouseMove`, `_xmouse`/`_ymouse`,
`MouseEvent`), and button hit-testing used the standard AS2 Up/Over/Down
state machine (`gameswf_button_register_mouse_state_names`,
0x0027eed8). This validates routing 3DS touch input through the same
`InputManager` abstraction as desktop mouse input rather than modeling it
as a separate device type.

## Open items from the RE work (not yet cross-checked)

- Exact `Key.isDown()` native implementation — not resolved in Ghidra RE.
- Precise `DefineSound`/`StartSound` tag-loader boundaries — not resolved.
- AVM1 bytecode opcode dispatcher — Ghidra's function-boundary analysis was
  inconsistent around the suspected address range (~0x2ab2d0), so the
  interpreter's exact opcode `switch` was not extracted. Phase 4's opcode
  list therefore comes from the public SWF spec, not from Shift-DX RE.
- Renderer transform-matrix / clipping / dual-screen target-selection
  details — not resolved.

None of these block Phase 1–3 work; they matter starting around Phase 4
(AVM1) and Phase 6/10 (audio, 3DS backend).
