# `onClipEvent()` Compatibility

**Interactivity phase (2026-08-18) deliverable.** All 19 `swf::
ClipEventFlag` bits (`src/swf/PlaceObjectTag.h:30-50`), traced against
`MovieClipInstance::runClipEvent()`'s actual call sites (not assumed).

Status legend: **Parsed?** — is the bit read out of `PlaceObject2`'s
`ClipActionRecord` flags word at all (`swf::PlaceObjectTag.cpp`'s
`readClipActionRecord`-equivalent parsing). **Stored?** — does the parsed
record survive into `MovieClipInstance::clipActions_`
(`DisplayListEntry::clipActions` -> `syncChildren()` -> `clipActions_`).
**Dispatched?** — is `runClipEvent(flag)` ever actually CALLED with this
flag anywhere in the codebase (confirmed by exhaustive grep of every
`runClipEvent(` call site). **Tested?** — does any `tests/test_*.cpp` case
exercise it.

| Event | Parsed? | Stored? | Dispatched? | Tested? | Status |
|---|---|---|---|---|---|
| `Load` | yes | yes | **yes** — `initializeNewlyCreated()`, `MovieClipInstance.cpp:626` | yes — `MovieClipInstance_ClipActions_LoadOnCreation_*` | WORKING |
| `EnterFrame` | yes | yes | **yes** — `advanceFrame()`, `MovieClipInstance.cpp:736` | yes — same test | WORKING |
| `Unload` | yes | yes | **yes** — `syncChildren()` (display-list-driven removal, `:678`) AND `removeFromParent()` (explicit RemoveSprite, `:929`) | yes — same test | WORKING |
| `MouseMove` | yes | yes | no | no | NOT IMPLEMENTED — needs hit-testing (see `docs/hit-testing.md`) |
| `MouseDown` | yes | yes | no | no | NOT IMPLEMENTED |
| `MouseUp` | yes | yes | no | no | NOT IMPLEMENTED |
| `KeyDown` | yes | yes | no | no | NOT IMPLEMENTED — no keyboard-event dispatcher exists (only polled `Key.isDown()`) |
| `KeyUp` | yes | yes | no | no | NOT IMPLEMENTED |
| `Data` | yes | yes | no | no | NOT IMPLEMENTED — fires on `loadVariables`/`loadMovie` completion in real Flash; neither exists in this runtime at all, so this flag has no trigger source yet regardless of dispatch |
| `Initialize` | yes | yes | no | no | NOT IMPLEMENTED — real Flash fires this before `Load`, exactly once, even before the clip's own first frame script runs; not distinguished from `Load` anywhere in this codebase currently |
| `Press` | yes | yes | no | no | NOT IMPLEMENTED — needs hit-testing AND edge-detected input state (see `docs/interactivity-audit.md` §8) |
| `Release` | yes | yes | no | no | NOT IMPLEMENTED |
| `ReleaseOutside` | yes | yes | no | no | NOT IMPLEMENTED |
| `RollOver` | yes | yes | no | no | NOT IMPLEMENTED |
| `RollOut` | yes | yes | no | no | NOT IMPLEMENTED |
| `DragOver` | yes | yes | no | no | NOT IMPLEMENTED |
| `DragOut` | yes | yes | no | no | NOT IMPLEMENTED |
| `KeyPress` | yes (incl. the associated `KeyCode` byte — `swf::ClipActionRecord::keyCode`) | yes | no | parsing tested (`test_place_object_tag.cpp`'s `KeyPress record's KeyCode byte`), dispatch not tested | NOT IMPLEMENTED (dispatch) |
| `Construct` | yes | yes | no | no | NOT IMPLEMENTED — SWF7+ only (class-based construction order), lowest priority of the 19: no class model exists in this runtime at all (see `docs/avm1-compatibility.md`'s `Extends`/`InstanceOf` notes) |

## Priority for Hobo (per the task spec's explicit ask)

Of the 16 not-yet-dispatched flags, ranked by what `hobo.swf`'s title
screen plausibly needs (not independently confirmed against the file's
actual `ClipActionRecord` content this phase — a natural next audit step
once hit-testing exists to make any of these actionable):

1. `Press`/`Release` — the most common interactive pattern (button/clip
   click-to-act), and Hobo's "PLAY!" button almost certainly uses one of
   these or the button-`on()` equivalent.
2. `RollOver`/`RollOut` — common for hover-highlight UI feedback.
3. `MouseDown`/`MouseUp` — global (non-target-specific) mouse state,
   less commonly used directly by simple game logic than `Press`/`Release`.
4. `ReleaseOutside`/`DragOver`/`DragOut` — edge-case press/drag variants,
   lower priority.
5. `KeyDown`/`KeyUp`/`KeyPress` — Hobo's control scheme is presumably
   `Key.isDown()`-polled (per `docs/avm1-support.md`'s existing Key
   coverage) rather than event-driven; not yet confirmed either way.
6. `Data`/`Initialize`/`Construct` — no trigger source exists yet
   (`Data`), redundant with `Load` for this runtime's purposes so far
   (`Initialize`), or blocked on a class model that doesn't exist
   (`Construct`) — lowest priority of all 19.
