# Event Dispatch Architecture

**Interactivity phase (2026-08-18). Status: DESIGN ONLY — not implemented.**
Per the task charter's §11 ("do not create a collection of hardcoded
special cases... create a small generic event-dispatch mechanism... keep
platform-specific input out of the AVM1 VM"). Written now, ahead of
implementation, so hit-testing/button-state work (next in the dependency
chain — see `docs/interactivity-audit.md` §8) can be built against an
agreed shape rather than improvised per-feature.

## Current state (no dispatcher exists)

Confirmed in `docs/interactivity-audit.md` §6: there is no `InputEvent`
type, no dispatcher class, nothing resembling the conceptual diagram the
task spec sketches. The only existing "event-like" mechanism is
`MovieClipInstance::runClipEvent(swf::ClipEventFlag)` — a direct,
special-cased function that looks up matching `ClipActionRecord`s and runs
their bytecode, called from exactly 4 hardcoded sites
(`initializeNewlyCreated`/`syncChildren`/`advanceFrame`/`removeFromParent`).
This is fine for `Load`/`Unload`/`EnterFrame` (lifecycle events with a
single, unambiguous trigger point each) but does not generalize to
mouse/button events (which need a hit-test result, previous-state
comparison, and potentially fan out to a `Button`'s AS2 `onRelease`-style
property AND an overlapping `MovieClip`'s `onClipEvent(release)` AND a
`Button`'s `condActionsV2` bytecode — real Flash fires more than one of
these for the SAME physical click in general).

## Planned shape (matches the task's own diagram, adapted to this codebase's existing seams)

```
Nintendo3DSInput::poll() (per-tick, existing, unchanged)
    -> InputState (existing, unchanged — still the passive position/key bag
       Key.isDown()/_xmouse/_ymouse read from directly; this doesn't change)
    -> NEW: MouseEventSource (owns previous-tick InputState snapshot;
       diffs against current to produce zero or more of:
       MouseMoved(stageX, stageY), MousePressed, MouseReleased)
       -- this is the "edge detection" layer from
       docs/interactivity-audit.md §8 item 3, and where the device-px ->
       stage-twips conversion (§8 item 2) happens ONCE per tick, not
       per-hit-test-call
    -> NEW: RuntimeEventDispatcher::dispatchMouseTick(root, mouseEvents)
       -- called once per tick, likely from the SAME place
       ScriptEnvironment::updateDrag() already gets called from today
       (root's own advanceFrame()) -- runs hitTestPoint() (docs/hit-testing.md)
       against the CURRENT stage point, compares to LAST tick's hit target
       (tracked by the dispatcher, not by InputState) to derive
       rollOver/rollOut, and combines with MousePressed/Released to derive
       press/release/releaseOutside
    -> fans out to, for whatever was hit (a Button leaf, a MovieClip, or
       both if a MovieClip is an ancestor of a hit Button):
         - Button: condActionsV2 bytecode matching the transition's
           ButtonCondition bitmask (already fully parsed and matched
           trivially -- see docs/interactivity-audit.md §3)
         - Button/MovieClip: onClipEvent(press/release/rollOver/rollOut/...)
           via the EXISTING runClipEvent() mechanism -- no change needed
           there, just new call sites with real flags instead of the
           current 3
         - a to-be-added AS2-settable `onPress`/`onRelease`/`onRollOver`/
           `onRollOut` property lookup+call (real Flash also supports this
           SEPARATELY from onClipEvent -- see the task's own example,
           `button.onRelease = function() {...}`), which needs the
           per-placement Button "instance" object from
           docs/interactivity-audit.md §3/§8 item 5 to exist first, since
           there's currently nowhere to SET `onRelease` on
    -> AVM1 Interpreter::callFunction() (existing, unchanged -- same
       mechanism ExternalInterface.addCallback's native->AS2 direction
       already uses, see docs/avm1-support.md)
```

## Design principles carried over from this codebase's existing seams (not new)

- **Keep `avm1::` host-agnostic.** Exactly like `HostBindings`,
  `nativeGet`/`nativeSet` hooks, and native (Phase 6/7) functions — the
  event dispatcher lives in `runtime/`, never in `avm1/`. `avm1/` gains
  zero new dependencies from this work; it only ever gets called INTO via
  `Interpreter::callFunction()`, exactly as ExternalInterface's
  `invokeCallback()` already does today.
- **One shared dispatch path for Button AND MovieClip**, per the task's own
  instruction ("do not duplicate event logic across Button and MovieClip if
  a shared event system can handle both") — the design above already
  achieves this: `runClipEvent()`-style dispatch is identical machinery for
  both; only the ADDITIONAL `onPress`/`onRelease`-property mechanism is
  Button-specific in real Flash (though nothing above prevents adding the
  same property-style handlers to `MovieClip` too, which real Flash
  actually also supports as of a certain SWF version — not investigated
  this phase, out of scope for the current priority).
- **Touch-as-mouse mapping stays exactly where it already is** — no changes
  needed to `Nintendo3DSInput`/`InputState` for this design; per the task's
  §12, this is already the existing behavior (touch down/up/move already
  map to mouse down/up/move) and the new `MouseEventSource` layer sits
  cleanly ABOVE `InputState`, not inside the 3DS-specific input code.

## Explicitly not decided yet

- Whether `MouseEventSource`/`RuntimeEventDispatcher` should be new classes
  owned by `ScriptEnvironment` (matching how `InputState`/drag-state are
  already owned there) or a separate top-level concept — leaning toward
  the former for consistency, not yet committed.
- Exact `ButtonCondition` bitmask -> dispatch-flag mapping table (9 bits
  already enumerated in `swf::ButtonCondition`, see
  `docs/interactivity-audit.md` §3) — mechanical work, deferred until the
  dispatcher itself is being built.
