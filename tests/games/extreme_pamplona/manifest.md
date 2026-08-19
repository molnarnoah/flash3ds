# Extreme Pamplona — manifest

**Multi-SWF, `loadMovie`-style package** — one main loader plus 23 separate
content sub-SWFs (9 levels, 4 music tracks, 2 player-character sprites, 9
sound banks). Confirmed via direct analysis (see "Content sub-SWF findings"
below) that the main loader alone carries essentially all interactivity
(buttons, `Key`, `onPress`/`onRelease`, `MovieClip` manipulation, user-
defined AVM1 functions) — the level/player/sound/music sub-SWFs are pure
asset/animation content with no `DefineButton2`, no `Key`/`Mouse` string
references, and (for the level files) `has_actionscript_tag: no` or only
trivial per-instance `DoAction` (`gotoAndPlay`/`stop`-class control flow).
Source device path root: `G:\3DS\extreme-pamplona\extreme-pamplona\`. One
file present on the device under `content/extreme-pamplona.swf` (apparent
duplicate of the main loader) was **not** staged/analyzed — 24 files
total were staged, not 25.

## Main loader

| Field | Value |
|---|---|
| External path | `/home/claude/game-corpus/extreme_pamplona/extreme-pamplona.swf` |
| File size | 1,000,458 bytes |
| MD5 | `e36aac73914ec8672218317e000615d7` |
| SWF version | 8 |
| Compression | zlib (`CWS`) |
| Stage | 800.0 x 400.0 px |
| Frame rate | 24.00 fps |
| Declared frame count | 2 |
| ActionScript | AVM1 (`DoAction` x25, `DoInitAction` x117 — 126 `DefineFunction` bodies detected) |
| Role | primary loader/menu/interactivity movie |

Full diagnostic output: [`diagnostic_main_loader.txt`](diagnostic_main_loader.txt)
(generated via `swf_diagnostic`, 2026-08-18).

## Content sub-SWFs (`content/`)

All 24 files below live under
`/home/claude/game-corpus/extreme_pamplona/content/`. Per-file diagnostic
output: `content/<name>.txt` (same basename as the `.swf`, generated the
same way).

| File | Size (bytes) | MD5 |
|---|---|---|
| `content/level-amsterdam.swf` | 180174 | `5ed32ecf9fdac379f729a3132bc4d601` |
| `content/level-geneva.swf` | 153654 | `1b40c4c152d771bf5636d54e794f1831` |
| `content/level-london.swf` | 250019 | `e7ff7de039d7c90f6675d4a8d9a4f308` |
| `content/level-moscow.swf` | 151736 | `d57bcf02e5225972fe4dd85c636502dd` |
| `content/level-munich.swf` | 226851 | `05c24443a3359c21a6ee46b3e0e98e4b` |
| `content/level-pamp1.swf` | 438803 | `a0c5119078b410f80c04d96e590bf839` |
| `content/level-pamp2.swf` | 392268 | `12c0a09b8c98a2a58cb8050b9b6fa03d` |
| `content/level-paris.swf` | 156039 | `03b210ee95b3305688c9d2d6924976dc` |
| `content/level-stockholm.swf` | 203725 | `2e12956afd88055e8333f7afb95f56da` |
| `content/music_funk.swf` | 556129 | `3b41499ea982acb7713337a8dae804a8` |
| `content/music_pamp.swf` | 590979 | `6a391d8a01b680b46a22a949c92b3f31` |
| `content/music_perc.swf` | 580676 | `4f7cbb69bf91b1129af46b44aa765302` |
| `content/music_ska.swf` | 345105 | `7724b7e1c541274b429e7c3c79b26e8c` |
| `content/player_player.swf` | 644205 | `4c73ae74a75464b7560785e9fbbd5dfc` |
| `content/playersnow_playersnow.swf` | 714241 | `31f36d27ef819f96e4db534fa0e445a2` |
| `content/sounds_all.swf` | 192684 | `57bc8a6ad4f780ea91a0e75fe4d51d95` |
| `content/sounds_amsterdam.swf` | 167003 | `0face7ce128eead65bbe6ba123ad58fc` |
| `content/sounds_geneva.swf` | 220736 | `3feaf1ada6a38f86e6a92f62907e141b` |
| `content/sounds_london.swf` | 75123 | `d08b4c9786a7f5712a7fc73e1645c493` |
| `content/sounds_moscow.swf` | 82794 | `f64fc39627ce7c1993f60396f4648e1c` |
| `content/sounds_munich.swf` | 126261 | `5b343edb18feb2450667322bcc76fa66` |
| `content/sounds_pamplona.swf` | 343395 | `eb57eb6af39fc02fc30a3f92cb55d7b6` |
| `content/sounds_paris.swf` | 194783 | `40f5a7abe3102734631147a5f802562e` |
| `content/sounds_stockholm.swf` | 193347 | `8f4c1cc978be240ed2e841aa0be9ab0e` |

## Content sub-SWF findings (informs the "which blocker helps Extreme
Pamplona" question)

- **`player_player.swf`** (SWF v8, stage 550x400, 1 frame): `has_actionscript_tag: yes`
  but trivial — 31 tiny `DoAction` buffers, 88 total opcodes, dominated by
  `Push`/`SetVariable`/`GetVariable`/`SetMember`/`GotoLabel`/`Play`. No
  `DefineButton2`, no `Key`/`Mouse`/`MovieClip`/`Sound` string hits. Reads
  as a self-contained sprite-sheet/label-driven animation clip, not
  interactive logic.
- **`level-pamp1.swf`** (representative level file, SWF v8, stage 800x400,
  1 frame): `has_actionscript_tag: no` at the top level, though 225 small
  `DoAction` buffers exist nested inside `DefineSprite` instances (985
  total opcodes — `Push`/`GetVariable`/`SetMember`/`Stop`/`CallMethod`
  dominate). No `DefineButton2`, no `Key`/`Mouse` identifiers found. Reads
  as animated scenery/background content.
- **music_*.swf / sounds_*.swf**: all show `Button2_present: no`, all
  `Key`/`Mouse`/`onPress`/`onRelease`/`onClipEvent` identifier scans
  negative — pure asset containers (sound/music data + minimal wrapper
  timeline).
- **Conclusion**: the main loader (`extreme-pamplona.swf`) is where
  Extreme-Pamplona-specific event-dispatch requirements must be evaluated
  against — the sub-SWFs contribute assets, not interactivity, to this
  game's compatibility profile. See `docs/real-game-compatibility.md`'s
  Extreme Pamplona section and section 17 analysis for the full writeup.
