# Compatibility Targets

## Hobo series (primary target)

Confirmed characteristics (per project spec section 11):

- SWF 6, CWS compression, AVM1, ~25 fps
- Tags used: `DoAction`, `DefineSound`, `DefineSprite`, `DefineShape3`,
  `DefineFont3`, `DefineButton2`, `DefineEditText`, `DefineText`,
  `PlaceObject2`
- ActionScript features used: `Key.isDown`, `gotoAndStop`, `gotoAndPlay`,
  `removeMovieClip`, `new Sound()`, `stopAllSounds`, `_root`,
  `onClipEvent(enterFrame)`

Phase-by-phase readiness for Hobo:

| Requirement | Phase | Status |
|---|---|---|
| CWS decompression | 1 | ✅ |
| SWF 6 header parse | 1 | ✅ |
| `DefineSprite`/`DefineShape3`/etc. tag *recognition* (name only) | 1 | ✅ |
| `DefineSprite`/`DefineShape3`/etc. tag *body parsing* | 3 | ❌ not started |
| `PlaceObject2` / DisplayList | 2 | ❌ not started |
| `DoAction` bytecode execution | 4 | ❌ not started |
| `gotoAndStop`/`gotoAndPlay`/`removeMovieClip` | 5 | ❌ not started |
| `Key.isDown` | 6 | ❌ not started |
| `new Sound()` / `stopAllSounds` | 6 | ❌ not started |
| `onClipEvent(enterFrame)` | 5 | ❌ not started |

## Extreme Pamplona (secondary target, mentioned in spec section 24)

Not yet analyzed. Add findings here once a copy is available for testing.
