# Audio

**Status: Phase 6 built the `IAudioBackend` seam + `NullAudioBackend`
(structural `DefineSound`/`StartSound` parsing, no codec decode). Phase 10
added `Nintendo3DSAudioBackend`, a real ndsp-backed implementation.
Roadmap Phase 3 (2026-08-21) closed the remaining gap for MP3 content —
the format every one of the 8 corpus games actually uses — end to end:
real decode, real caching, real `ndsp` PCM playback. Other codecs
(ADPCM/Nellymoser/Speex/uncompressed) still aren't decoded; see
`docs/known-limitations.md` L1 for the exact remaining scope.**

`IAudioBackend` (`src/audio/IAudioBackend.h`) is the abstract seam
`ScriptEnvironment` dispatches `StartSound` tags and AS2
`Sound.start()`/`stop()`/`setVolume()`/`getVolume()` through, mirroring
`IRenderer`'s design. `DefineSound`'s header fields (format/rate/size/
type/sample count) and `StartSound`'s `SOUNDINFO` record have been parsed
structurally since Phase 6 — see `src/swf/DefineSoundTag.h`. As of Phase
3, MP3-format audio is also actually **decoded** into PCM samples (see
below); other codecs are still structural-only, so a backend has real
samples to play only for MP3 content.

## Phase 3 — MP3 decode, decode-on-demand-and-cache, real playback

**`src/audio/Mp3Decoder.h/.cpp`** wraps `third_party/minimp3` (a
public-domain/CC0, single-header decoder — chosen specifically because a
3DS `.3dsx` has no dynamic linking, so an LGPL library like libmpg123
would need static linking, which LGPL treats unfavorably; CC0 has no such
concern at all — see `third_party/minimp3/README.md` for the full
rationale and how it was obtained). Two entry points: `decodeMp3()`
decodes a raw, container-less MP3 frame stream (scanning forward for the
first real frame sync, since real files often carry leading junk/ID3
data before it), and `decodeSwfMp3Sound()` is the SWF-aware wrapper —
per the public SWF spec, `DefineSound`'s `SoundData` for `SoundFormat::
kMp3` begins with a 2-byte little-endian `SI16` "SeekSamples" field
*before* the actual MP3 frame data, which `decodeSwfMp3Sound()` skips
before delegating to `decodeMp3()`.

**`runtime::ScriptEnvironment::playSoundById()`/`ensureSoundDecoded()`**
(`src/runtime/MovieClipInstance.h/.cpp`) is the decode-on-demand-and-cache
layer sitting between `StartSound`/`Sound.start()` and the audio backend.
`ensureSoundDecoded(soundId)` decodes a given soundId's audio **at most
once** — the result (success *or* failure/unsupported-format) is cached in
`decodedSoundCache_`, so a second reference to the same soundId is a cache
lookup, not a re-decode or a repeated failed attempt. `playSoundById()`
calls `IAudioBackend::loadSound()` only on the *first* reference to a
soundId (a real bug where it fired on cache hits too was caught by a
failing integration test and fixed before this shipped — see
`docs/known-limitations.md` L1), then always calls `playSound()`. This
design was deliberately chosen over eager whole-`CharacterDictionary`
decode specifically because of the memory-audit findings (`docs/
memory-audit.md` §5-8): shape data, not sound, is this project's dominant
memory cost, so only paying decode cost for sounds a session actually
triggers is the right tradeoff — see `docs/memory-audit.md` §9 for the
measured real-content and worst-case memory numbers this choice produces.

**`IAudioBackend::loadSound(soundId, samples, sampleCount, sampleRate,
channels)`** (new virtual on the existing seam, default no-op) is where
decoded PCM is handed to a backend, before `playSound()` starts it.
`NullAudioBackend::loadSound()` just logs and discards (desktop testing
doesn't need real audio output). Every corpus game's real `StartSound`
firing during its title/menu screen was confirmed to produce genuinely
non-silent PCM (RMS ~1888.4) with sample counts matching each `SoundDef::
sampleCount` header field exactly — see `docs/known-limitations.md` L1 for
the full evidence and `tests/test_mp3_decoder.cpp`/`test_movieclip_
instance.cpp` for the automated coverage (287/287 tests passing).

## Phase 10 / Phase 3 — Nintendo3DSAudioBackend

`src/audio/Nintendo3DSAudioBackend.h/.cpp` is a genuine libctru `ndsp`
integration: `ndspInit()`/`ndspExit()` lifecycle, per-`soundId` channel
reservation (`ndspChnReset`/`ndspChnInitParams`) across ndsp's 24 hardware
channels, `ndspChnSetPaused()` for play/pause, and
`ndspChnWaveBufClear()`/`ndspChnReset()` for stop — all real, all
exercised by this session's from-source toolchain link (see
[3ds-toolchain.md](3ds-toolchain.md)). As of Phase 3, `loadSound()` really
`memcpy`s decoded PCM into its own `linearAlloc`'d buffer (DSP-DMA-
accessible memory needs a dedicated buffer, not a view into another
allocation) and flushes the data cache (`DSP_FlushDataCache` — the ARM11
side writes through cache, but the DSP reads physical memory directly via
DMA); `playSound()` builds a real `ndspWaveBuf` pointing at that buffer
and queues it via `ndspChnWaveBufAdd()`. For a soundId this backend has no
loaded PCM for (an undecoded codec, or a decode failure), `playSound()`
falls back to the original Phase 10 "channel reserved, nothing to queue,
logged" path — the gap stays visible in logs rather than hidden, same
convention as before.

**Known gap, not attempted this phase:** a real SWF `StartSound`
`loopCount > 1` means "repeat this sound N times," but a single
`ndspWaveBuf`'s `looping` flag means "loop forever," not a counted
repeat — a real fix means queuing N separate wavebufs (same underlying
PCM), which needs on-device verification this sandbox cannot do. Rather
than guess at that without being able to test it, `loopCount` is honored
only as "play once" — logged, not silently dropped. This backend's code
compiles cleanly against the real bootstrapped libctru headers and links
into a real `.3dsx`, but (like the rest of Phase 10) has never actually
run on 3DS hardware or an emulator — the audio behavior described here is
real code, verified to the extent a non-hardware sandbox can, not yet
confirmed audible.

**`playTestTone()` — diagnostic-only, proves the pipeline is actually
audible.** A separate method, `Nintendo3DSAudioBackend::
playTestTone(frequencyHz, durationSeconds)`, synthesizes a mono PCM16 sine
wave (with a short linear fade-out to avoid an end-of-buffer click)
directly into libctru's "linear" heap, flushes the data cache, and queues
it via `ndspChnWaveBufAdd` on a dedicated channel reserved separately from
the per-`soundId` channel pool (so it never collides with real SWF-sound
bookkeeping). This is **not** part of the SWF audio pipeline — it's not
reachable from `playSound()`/`StartSound`/AS2 `Sound.*` — it exists purely
so the dual-screen test app (`nintendo3ds_main.cpp`) can trigger a real,
audible tone on A/B/X/Y press and prove the ndsp integration actually
works end to end, independent of the SWF-content decode path. It stays a
diagnostic, not something SWF content ever triggers, even now that real
codec decode exists.

See `docs/shift-dx-behavior.md`'s "Open items" section for what Shift-DX RE
could and couldn't confirm about its own sound-tag handling, and
`docs/avm1-support.md`'s carry-over list for `Sound.attachSound(name:
String)` (the linkage-name form, still unimplemented — only numeric
`attachSound(id)` resolves).
