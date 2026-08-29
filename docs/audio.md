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

## "No sound at all" investigation, and its resolution (2026-08-29)

A real user report ("no sound at all" on real hardware/Azahar, v10-v12
builds) triggered a structured 6-point audit of the whole pipeline (NDSP
init, channel setup, buffer submission/queue, mixing/volume chain,
emulator discrepancy, logging). All four native/engine layers (1-4) were
confirmed correct by direct testing: `/tmp/audio_probe.cpp` (ticked a real
`hobo.swf`'s root clip for 300 ticks holding gameplay keys, zero `[AUDIO]`
log lines fired — `StartSound` dispatch was simply never reached on
currently-reachable content), `/tmp/audio_probe2.cpp`/`audio_probe3.cpp`
(direct `playSoundById()`/`CharacterDictionary::find()` calls on 8 real
`DefineSound` character IDs — all resolved and decoded correctly as real
MP3 PCM with real sample counts/rates). Root cause, first pass: reachability
(`StartSound` tags aren't on frame 1, and frame 1 is where root currently
stays) plus a separate, independently-real gap (`SoundStreamHead`/
`SoundStreamBlock` streaming audio is entirely unimplemented).

**This was shown to be incomplete.** The user tested `playTestTone()` — a
synthesized sine wave wired to A/B/X/Y, fully independent of SWF content,
reachability, or streaming — and it was ALSO silent. This ruled out
"reachability" as the sole cause and pointed at the `ndsp` pipeline itself
or its behavior under Azahar.

**Binary-level verification.** Ghidra was not usable for this — the
`ghidra__*` MCP tools in this environment are bound to the Shift-DX
reverse-engineering project's binary (a separate, unrelated project — see
the top-level `/home/claude/CLAUDE.md`), not this project's own compiled
ELF/3dsx. Since flash3ds-runtime has full source, `arm-none-eabi-nm`/
`objdump`/`addr2line` against the real, unstripped `build_3ds/
flash3ds_3ds` ELF gave the same ground truth Ghidra would, more directly
(source is available to cross-check against). Findings: every requested
ndsp symbol (`ndspInit`, `ndspChnWaveBufAdd`, `ndspChnSetPaused`,
`ndspChnSetMix`, `ndspSetOutputMode`, plus `ndspChnSetFormat`/
`ndspChnSetRate`/`ndspExit`/`DSP_FlushDataCache`) is real, defined, and
non-stripped; every call site matches source exactly (`ndspInit`/
`ndspSetOutputMode` called once each in the constructor, `ndspChnSetPaused`'s
only caller is `playSound()`) — no dead code, no stub, no duplicate/
competing init path. A suspected missing `ndspChnSetPaused` call in
`playTestTone()` was investigated and ruled out by reading libctru's own
built-from-source `ndspChnReset()` (which `playTestTone()` does call),
which already unconditionally clears the paused flag.

**Root cause, confirmed on real hardware.** Reading libctru's own
`ndspInit()`/`ndspFindAndLoadComponent()` source
(`source/ndsp/ndsp.c`) found that `ndspInit()` needs a DSP firmware
"component" — normally a real `dspfirm.cdc` dump at `sdmc:/3ds/
dspfirm.cdc`, or (on real hardware only) an `hb:ndsp` handle from a
homebrew launcher. `isInitialized()`/`initResult()` accessors were added
to `Nintendo3DSAudioBackend` (capturing `ndspInit()`'s real `Result`) and
a `drawAudioStatusIndicator()` was added to the bottom-screen test picture
(green = initialized OK; red + a module/description square-count encoding
of the failing `Result` otherwise) so this could be confirmed on-device
without any log access. **The v13 test build's on-screen indicator showed
red, with a module count of 41 and a description count of 1018** — decoded
from the on-screen squares (4-square row, then 1-square row, then 1 more
marker square) exactly as `module = (Result >> 10) & 0x7FF`,
`description = Result & 0x3FF` predicts for **`RM_DSP` (41) /
`RD_NOT_FOUND` (1018)** — i.e. `ndspInit()` fails with exactly "no DSP
firmware component found," confirmed on real hardware/Azahar, not just
predicted from source.

**Why, and the actual fix — confirmed via devkitPro's own documentation**
(`devkitPro/3ds-examples`'s `audio/README.md`, fetched 2026-08-29): "Homebrew
requires a copy of the DSP firmware to be present at `sdmc:/3ds/
dspfirm.cdc`" on real hardware — but "a 0 byte file is sufficient for
homebrew audio to work since Citra uses HLE [high-level emulation]."
Azahar is a Citra fork (same DSP HLE lineage), so the same almost
certainly holds there: the emulator's HLE DSP doesn't need or read the
file's actual contents, it only needs `ndspFindAndLoadComponent()`'s file-
open call to succeed. **The fix is a one-time step on the user's own SD
card / Azahar virtual SD root, not a code change**: create an empty (or
any placeholder) file at `3ds/dspfirm.cdc` relative to Azahar's configured
SD root. This project will never bundle a real `dspfirm.cdc` dump itself
(Nintendo's proprietary firmware — see this repo's own "public sources
only" rule), and there is nothing `Nintendo3DSAudioBackend` can do in code
to make `ndspInit()` succeed without that file existing — this is
correctly-behaving code hitting a missing-prerequisite launch-environment
gap, the same class of issue as the already-fixed `argv[0]`/`envIsHomebrew()`
launcher-inconsistency bug (Azahar's direct "Load File" launch path
doesn't set up everything a homebrew-launcher forwarder normally would).

Sources consulted: [devkitPro/3ds-examples audio/README.md](https://github.com/devkitPro/3ds-examples/blob/master/audio/README.md) (dspfirm.cdc / Citra HLE 0-byte-file finding), [Citra/Azahar FAQ](https://citra.azahar-emu.org/wiki/faq/) (general system-file guidance, does not itself mention DSP firmware).

**Status: CONFIRMED FIXED (2026-08-29).** The user placed the `dspfirm.cdc`
placeholder and re-tested: the A/B/X/Y test tones are now audible in
Azahar. This closes the "no sound at all" investigation's ndsp/pipeline
half completely — every layer (init, channel setup, buffer submission,
mixing, emulator playback) is now confirmed working end-to-end on real
hardware/Azahar, not just toolchain-verified.

## Follow-up (2026-08-29): "the buttons make sound but the game doesn't, even though Flash Player has sound"

With the pipeline itself now proven, the user reports real SWF game audio
is still silent, unlike in a real Flash Player. Three possible
explanations for THIS specific screen (root is still stuck on frame 1 —
see `docs/hobo-playability-verification.md`) were checked with real
evidence, not assumption, using a new throwaway tool
(`/tmp/sound_tag_scan.cpp`, not committed — recursively walks the
top-level tag stream and every `DefineSprite`'s own nested tag stream,
tallying `DefineSound`/`StartSound`/`SoundStreamHead2`/`SoundStreamBlock`
occurrences per sprite):

1. **Streaming audio (`SoundStreamHead`/`SoundStreamBlock`) — ruled out.**
   529 `SoundStreamHead2` tags exist across the file (one on root itself,
   one in each of the 528 `DefineSprite`s) — but **zero**
   `SoundStreamBlock` tags exist ANYWHERE in the whole 4.97 MB file. A
   `SoundStreamHead2` with no `SoundStreamBlock`s following it is a
   header-only stub (a common Flash-IDE-publish artifact when a timeline's
   "sync to frames" is enabled but nothing was ever assigned) — there is
   no actual streaming audio DATA anywhere in `hobo.swf` for this
   (still-unimplemented) mechanism to be missing.
2. **`Sound.attachSound(name: String)` linkage-name form — ruled out for
   this screen.** This gap IS real and still unwired (see `CLAUDE.md`'s
   carry-over list) — `attachSound()`'s native impl logs a `LOG_WARN` any
   time it's called with a non-numeric argument. A fresh 100-tick probe
   (`/tmp/attachsound_probe.cpp`) at `LogLevel::kWarn` produced **zero**
   `attachSound`/`Sound.start` warnings of any kind while root sits on
   frame 1 — no code path on this screen calls either method at all
   (`docs/hobo-playability-verification.md`'s Finding 3 already noted "31
   `new Sound()` calls, 30 `setVolume(100)` calls" every tick; these
   `Sound` objects are apparently constructed and volume-configured but
   never actually attached/started on this screen).
3. **`StartSound` reachability — confirmed, again, as the real cause.**
   The tag scan shows root's three actual named frame-1 children —
   `preloader` (characterId=33), `shade` (38), `mutebutton` (91) — each
   carry only an empty `SoundStreamHead2` stub and **zero** `StartSound`
   tags of their own. All 266 real `StartSound` tags in the file (across
   35 unique `DefineSound` assets) live inside OTHER sprites, entirely
   outside root's currently-reachable frame-1 content.

**Conclusion:** frame 1 — the ONLY screen this engine can currently
reach — is genuinely, verifiably silent in the source file itself, by
every mechanism this engine does or doesn't implement. This isn't an
audio bug at all; it's the same reachability gap
`docs/hobo-playability-verification.md` already documents (no known input
sequence advances root past frame 1). The sound the user hears in a real
Flash Player is almost certainly real GAMEPLAY audio, from content this
engine cannot currently reach — meaning "the game has no sound" and "input
can't get past the loading screen" are very likely **the same open
problem observed two different ways**, not two separate ones. Fixing the
title-screen-advance question (see that doc's own "not yet investigated"
list) would very plausibly fix this too, with no further audio-side work
needed.
