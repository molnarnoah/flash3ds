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

**`playTestTone()` — diagnostic-only, proves the pipeline is actually
audible.** Since `playSound()` has nothing real to play, a separate method,
`Nintendo3DSAudioBackend::playTestTone(frequencyHz, durationSeconds)`,
synthesizes a mono PCM16 sine wave (with a short linear fade-out to avoid
an end-of-buffer click) directly into libctru's "linear" heap
(`linearAlloc` — required for DSP-DMA-accessible memory; a plain
heap-allocated buffer is not guaranteed reachable the same way), flushes
the data cache (`DSP_FlushDataCache` — the ARM11 side writes through cache,
but the DSP reads physical memory directly via DMA), and queues it via
`ndspChnWaveBufAdd` on a dedicated channel reserved separately from the
per-`soundId` channel pool (so it never collides with real SWF-sound
bookkeeping). This is **not** part of the SWF audio pipeline — it's not
reachable from `playSound()`/`StartSound`/AS2 `Sound.*` — it exists purely
so the dual-screen test app (`nintendo3ds_main.cpp`) can trigger a real,
audible tone on A/B/X/Y press and prove the ndsp integration actually
works end to end, independent of the still-missing codec-decode step. When
that future phase does add codec decode, the real fix goes into
`playSound()` itself; `playTestTone()` stays a diagnostic, not something
SWF content ever triggers.

See `docs/shift-dx-behavior.md`'s "Open items" section for what Shift-DX RE
could and couldn't confirm about its own sound-tag handling, and
`docs/avm1-support.md`'s carry-over list for `Sound.attachSound(name:
String)` (the linkage-name form, still unimplemented — only numeric
`attachSound(id)` resolves).
