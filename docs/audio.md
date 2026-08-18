# Audio

**Status: Phase 6 built the `IAudioBackend` seam + `NullAudioBackend`
(structural `DefineSound`/`StartSound` parsing, no codec decode). Phase 10
added `Nintendo3DSAudioBackend`, a real ndsp-backed implementation — but see
below, it still can't actually play anything, for the same reason
`NullAudioBackend` never could.**

`IAudioBackend` (`src/audio/IAudioBackend.h`) is the abstract seam
`ScriptEnvironment` dispatches `StartSound` tags and AS2
`Sound.start()`/`stop()`/`setVolume()`/`getVolume()` through, mirroring
`IRenderer`'s design. It is deliberately structural: this project parses
`DefineSound`'s header fields (format/rate/size/type/sample count) and
`StartSound`'s `SOUNDINFO` record, but does **not** decode any compressed
audio codec (ADPCM/MP3/uncompressed-PCM framing) into actual samples — see
`src/swf/DefineSoundTag.h`. Without that decode step, no backend — however
real its underlying platform plumbing — has an actual sample buffer to
play.

## Phase 10 — Nintendo3DSAudioBackend

`src/audio/Nintendo3DSAudioBackend.h/.cpp` is a genuine libctru `ndsp`
integration: `ndspInit()`/`ndspExit()` lifecycle, per-`soundId` channel
reservation (`ndspChnReset`/`ndspChnInitParams`) across ndsp's 24 hardware
channels, `ndspChnSetPaused()` for play/pause, and
`ndspChnWaveBufClear()`/`ndspChnReset()` for stop — all real, all
exercised by this session's from-source toolchain link (see
[3ds-toolchain.md](3ds-toolchain.md)). What it does *not* do is queue an
actual `ndspWaveBuf` with sample data in `playSound()`, because — as
above — there is no decoded PCM/ADPCM buffer for any `soundId` anywhere in
this codebase yet. `playSound()` reserves and un-pauses a channel and logs
that fact explicitly rather than silently no-op'ing, so the gap is visible
in logs rather than hidden. A future codec-decode phase plugs in at
exactly the point the header comment marks.

This is not a Phase 10 regression or shortcut — `NullAudioBackend` has the
exact same "nothing is actually audible" limitation today, for the same
underlying reason. Phase 10's contribution is that the platform-specific
channel/hardware plumbing now genuinely exists and is real-toolchain-
verified to compile and link against libctru, ready for whichever future
phase adds codec decode.

See `docs/shift-dx-behavior.md`'s "Open items" section for what Shift-DX RE
could and couldn't confirm about its own sound-tag handling, and
`docs/avm1-support.md`'s carry-over list for `Sound.attachSound(name:
String)` (the linkage-name form, still unimplemented — only numeric
`attachSound(id)` resolves).
