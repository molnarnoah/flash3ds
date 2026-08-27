// MovieClipInstance.h
//
// Phase 5: the MovieClip API / scene-graph wiring. This is the integration
// point between the Phase 1-3 SWF/runtime model (Movie, CharacterDictionary,
// Timeline, DisplayList) and the Phase 4 AVM1 interpreter — it deliberately
// depends on both (unlike avm1/, which stays host-agnostic, and the rest of
// runtime/, which stays AVM1-agnostic).
//
// A MovieClipInstance wraps ONE placed MovieClip: either the root (the
// movie's own top-level timeline) or a placed DefineSprite character. Each
// instance owns its own Timeline — giving it a genuinely independent
// playhead, unlike Phase 3's SceneRenderer, which cached one shared Timeline
// per *character* — and a scripting Object (see scriptObject()) that AVM1
// bytecode running "on" this clip sees as `this`, exposing intrinsic
// properties (_x, _y, _currentframe, ...), `_root`/`_parent`, and named
// child clip references through the normal property-access path (see
// avm1::Object's nativeGet/nativeSet hooks).
//
// DOCUMENTED LIMITATIONS (see docs/avm1-support.md for the full writeup):
//   - A child's authored transform (from its parent's PlaceObject2 tag) is
//     only applied once, at the moment the child instance is first created.
//     A later frame's PlaceObject2 "update in place" tag targeting the same
//     depth is NOT re-applied to an already-existing instance — only a
//     genuine character replacement (Move=1,HasCharacter=1, or a brand-new
//     placement) creates a fresh instance and picks up a fresh transform.
//     This trades a rarer fidelity gap (an author re-authoring a clip's
//     position mid-timeline while also independently script-driving it) for
//     a much more common one it avoids (script-set _x/_y being silently
//     stomped back to the placement transform every single tick).
//   - _width/_height (interactivity-audit phase, 2026-08-18): NOW computed
//     — see width()/height()/computeBoundsInOwnSpace() below. Previously
//     hardcoded to 0 always; see docs/known-limitations.md priority #2 for
//     the audit finding and docs/hit-testing.md for how this bounding-box
//     machinery is meant to be reused by hit-testing (not yet implemented
//     — bounds computation is a prerequisite, not the same thing).
//   - onClipEvent handlers (Phase 6): Load/Unload/EnterFrame are wired
//     (see runClipEvent()/initializeNewlyCreated()/removeFromParent()/
//     advanceFrame()). Mouse*/Press/Release/RollOver*/DragOver*/KeyDown/
//     KeyUp are parsed (ClipActionRecord — swf/PlaceObjectTag.h) but still
//     NOT dispatched: hit-testing now exists (interactivity phase,
//     2026-08-19 — see hitTestPoint()/hitTestBounds() below) but nothing
//     yet DRIVES clip-event dispatch off of it — see docs/hit-testing.md.
//     Button `on()` handlers are parsed (DefineButton/2, Phase 8) and a
//     placed button now HAS a real runtime instance with UP/OVER/DOWN
//     state (ButtonInstance phase, 2026-08-19 — see buttonInstances_/
//     ButtonInstance.h/docs/buttons.md), but the parsed condActionsV1/
//     condActionsV2 bytecode is still NOT dispatched anywhere — that, and
//     onClipEvent(mouse*)/onPress/onRelease/onRollOver/onRollOut, are the
//     explicit next phase (see docs/buttons.md's "Not implemented yet").
//     DoAction/DoInitAction frame scripts ARE fully wired.
//   - StartDrag/EndDrag (Phase 6) are real: ScriptEnvironment tracks at
//     most one dragged clip and repositions it from InputState's mouse
//     position once per tick (see ScriptEnvironment::updateDrag()).
//     Constraint-rectangle clamping is applied; LockCenter is honored.
//   - _xscale/_yscale/_rotation use a standard decomposition of the
//     placement MATRIX that assumes no independent (non-rotational) skew —
//     exactly correct for any transform actually produced by _xscale/
//     _yscale/_rotation themselves, an approximation for a hand-authored
//     skewed matrix (rare in practice).
//   - Sound (Phase 6): DefineSound/StartSound tag parsing is structural
//     only (header fields, no codec decode — see swf/DefineSoundTag.h).
//     StartSound TAG dispatch (numeric SoundId, straight from the tag
//     stream) is fully wired to IAudioBackend. AVM1's `Sound` object only
//     resolves attachSound() when given a NUMBER (treated directly as a
//     character ID); the real AS2 form — a String linkage identifier —
//     can't be resolved without ExportAssets parsing, which doesn't exist
//     yet, and logs a warning instead of silently doing nothing.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "audio/IAudioBackend.h"
#include "audio/Mp3Decoder.h"
#include "audio/NullAudioBackend.h"
#include "avm1/HostBindings.h"
#include "avm1/Value.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/DisplayList.h"
#include "runtime/IFileLoader.h"
#include "runtime/InputState.h"
#include "runtime/Movie.h"
#include "runtime/Timeline.h"
#include "swf/DefineButtonTag.h"
#include "swf/SwfRecords.h"

namespace flash3ds::runtime {

class MovieClipInstance;
class ButtonInstance;  // runtime/ButtonInstance.h — see MovieClipInstance.cpp's #include

// Shared AVM1 scripting environment for one loaded movie's whole MovieClip
// tree: owns the global object (so a top-level user-defined function/var
// created by one clip's script is visible to another clip's script, as in
// real AS2 — everything shares one _global) and knows how to run a bytecode
// buffer against a specific MovieClipInstance as the executing clip. Also
// tracks DoInitAction (tag 59) bodies, which are associated with a
// character (not a frame) and run at most once per character per movie
// load, the first time any instance of that character is created.
class ScriptEnvironment {
public:
    ScriptEnvironment();

    // Runs `code` (length `length`) with `target` as `this` / the innermost
    // scope object / the natural HostBindings target. Returns whatever the
    // script's top-level ActionReturn produced (almost always undefined for
    // a frame script).
    avm1::Value run(MovieClipInstance& target, const uint8_t* code, size_t length);

    const std::shared_ptr<avm1::Object>& globalObject() const { return global_; }

    // Roadmap Phase 4 (2026-08-21) diagnostic hook — OPTIONAL, nullptr by
    // default (zero behavior change unless a caller explicitly sets it).
    // Mirrors avm1::ExecutionContext::callTraceSink's own doc comment:
    // threaded into every ExecutionContext this ScriptEnvironment creates
    // (run()/callHandler()) so a diagnostic tool can observe every real,
    // runtime-resolved CallMethod/NewObject/etc. a movie's scripts
    // actually execute, without needing a static (and, per
    // docs/known-limitations.md L6, defeatable-by-obfuscation) bytecode
    // disassembly.
    std::function<void(const std::string&)> callTraceSink;

    // Scans `movie`'s top-level tags for DoInitAction and records each
    // one's action bytes keyed by the character (SpriteId) they're
    // associated with. Called once by MovieClipInstance::createRoot().
    void scanInitActions(const Movie& movie);

    // Returns the init-action bytes for `characterId` and marks them
    // consumed, if present and not already run this movie load. Returns
    // nullptr if there's nothing to run (no DoInitAction tag for this
    // character, or it already ran).
    const std::vector<uint8_t>* takeInitActionsOnce(uint16_t characterId);

    // --- Phase 6: input / audio / drag ------------------------------------

    // Host-settable keyboard/mouse state — set this from whatever's driving
    // the runtime (a desktop test harness, later a 3DS input poll) BEFORE
    // calling MovieClipInstance::advanceFrame() for a tick, so Key.isDown()/
    // _xmouse/_ymouse/StartDrag all see up-to-date values during that tick.
    InputState& inputState() { return inputState_; }
    const InputState& inputState() const { return inputState_; }

    // Defaults to a NullAudioBackend (logs, plays nothing) so every
    // existing/new test and the CLI work with zero setup — see
    // audio/NullAudioBackend.h. Call setAudioBackend() to point sound
    // dispatch (StartSound tags, AVM1 Sound.start()/stop()) at a real
    // implementation instead. Non-owning: the caller must keep `backend`
    // alive for as long as this ScriptEnvironment (and anything built from
    // it) is used — same "outlives everything" convention as `movie`/
    // `characters` elsewhere in this file.
    void setAudioBackend(audio::IAudioBackend* backend) {
        audioBackend_ = backend ? backend : &nullAudioBackend_;
    }
    audio::IAudioBackend& audioBackend() { return *audioBackend_; }

    // Roadmap Phase 4 (2026-08-21, loadMovie — see docs/known-limitations.md
    // L4): defaults to a NullFileLoader (logs, loads nothing), same
    // "zero-setup-required" precedent as audioBackend_ above. Call
    // setFileLoader() to point MovieClip.loadMovie() at a real
    // implementation (LocalFileLoader for desktop/tests; see
    // runtime/LocalFileLoader.h). Non-owning — same lifetime convention as
    // setAudioBackend().
    void setFileLoader(IFileLoader* loader) {
        fileLoader_ = loader ? loader : &nullFileLoader_;
    }
    IFileLoader& fileLoader() { return *fileLoader_; }

    // Roadmap Phase 4: owns every Movie/CharacterDictionary loadMovie()
    // parses for the lifetime of this ScriptEnvironment (never freed mid-
    // session — MovieClipInstance holds non-owning raw pointers into these
    // throughout the codebase, see Movie::movie_/characters_'s own "outlives
    // everything" convention, so something has to keep a loaded sub-movie
    // alive for as long as any MovieClipInstance might still reference it;
    // simply never freeing it, matching this project's small-scope-homebrew
    // "don't build a GC for this" precedent, is the simplest safe answer).
    // Returns the (Movie*, CharacterDictionary*) pair now owned here.
    std::pair<const Movie*, const CharacterDictionary*> ownLoadedMovie(
        std::unique_ptr<Movie> movie, std::unique_ptr<CharacterDictionary> characters);

    // Lets AVM1's native Sound object resolve a numeric attachSound() ID
    // against real DefineSound character data (see Phase 6 limitations
    // note on MovieClipInstance's class header: attachSound(name:String) —
    // real AS2's actual linkage-name form — is NOT resolvable without
    // ExportAssets parsing, which doesn't exist yet). Called once by
    // MovieClipInstance::createRoot(); non-owning, same lifetime
    // assumption as everything else here.
    void bindCharacters(const CharacterDictionary& characters) { characters_ = &characters; }
    const CharacterDictionary* characters() const { return characters_; }

    // Non-owning, same "outlives everything" lifetime assumption as
    // bindCharacters() — needed so playSoundById() below can read a
    // SoundDef's raw (still-compressed) bytes out of Movie::data. Called
    // once by MovieClipInstance::createRoot(), alongside bindCharacters().
    void bindMovie(const Movie& movie) { movie_ = &movie; }

    // Roadmap Phase 3 (2026-08-21, MP3 audio decode — see
    // docs/known-limitations.md L1): the single entry point every StartSound
    // tag dispatch and AVM1 Sound.start() call goes through (replacing a
    // direct audioBackend().playSound() call). Decodes `soundId`'s audio
    // ON FIRST REFERENCE and caches the result (decode-on-demand, not
    // decode-all-up-front — deliberately, given docs/memory-audit.md's
    // still-open memory-cost findings: a game that never triggers a given
    // sound never pays to decode it), then hands the decoded PCM to the
    // current IAudioBackend via loadSound() before calling playSound() —
    // exactly once per distinct soundId, not once per trigger; a second
    // playSoundById() call for an already-decoded soundId skips straight to
    // playSound(). Only MP3-format DefineSound characters actually decode
    // right now (ADPCM/Nellymoser/Speex remain unimplemented, see L1); for
    // anything else (unresolvable soundId, non-MP3 format, or a decode
    // failure) this still calls playSound() — matching Phase 6's original
    // "the backend at least gets told this was triggered" behavior, now
    // just without any PCM behind it.
    void playSoundById(uint16_t soundId, int loopCount);

    // ActionStartDrag/ActionEndDrag's real (Phase 6) implementation: tracks
    // at most one dragged clip at a time (matches real Flash — starting a
    // new drag implicitly ends any previous one), and updateDrag() (called
    // once per full-tree tick, from the ROOT MovieClipInstance's
    // advanceFrame() only — see its implementation) repositions the
    // dragged clip from the current mouse position.
    void startDrag(MovieClipInstance* clip, const avm1::HostBindings::DragOptions& options);
    void endDrag();
    void updateDrag();

    // Called by MovieClipInstance::removeFromParent() (synchronously,
    // BEFORE the clip is actually erased/destroyed — see that method's own
    // comment) and by syncChildren()'s display-list-driven prune loop, so a
    // removed clip never lingers as a dangling drag/hover/press target.
    // Event-dispatch phase (2026-08-19) extension: also clears
    // hoverClip_/pressedClip_ (Phase I AS2 property-handler targets) and,
    // critically, hoverButton_/pressedButton_ whenever their OWNING clip
    // (`button->parent()`) is `clip` — a button's own lifetime is tied to
    // its parent's buttonInstances_ map (see MovieClipInstance.cpp), so if
    // the parent is destroyed, every button it owns is destroyed with it;
    // this call happens BEFORE that erase, while `button->parent()` is
    // still safe to read, which is what makes clearing the pointer HERE
    // (rather than trying to detect it after the fact) safe — see
    // docs/events.md's "Event target safety" section.
    void notifyRemoved(MovieClipInstance* clip);

    // Real-corpus crash fix (2026-08-27, Track B B1 re-verification of
    // Extreme Pamplona — see docs/known-limitations.md L6 and
    // tools/real_game_harness/pamplona_click_trace.cpp): `notifyRemoved()`
    // above only clears hoverClip_/pressedClip_/hoverButton_/pressedButton_
    // when they point AT `clip` itself (or, for buttons, at a button whose
    // immediate parent is `clip`). All three real removal call sites
    // (syncChildren()'s display-list-driven prune, removeFromParent()'s
    // explicit RemoveSprite, and loadMovie()'s target-clip teardown) tear
    // down `clip`'s entire subtree at once, destroying every descendant
    // MovieClipInstance/ButtonInstance along with it — but they only ever
    // called notifyRemoved() for the single node being removed, not
    // recursively for its descendants. If hoverClip_/pressedClip_/
    // hoverButton_/pressedButton_ pointed at a GRANDCHILD (or deeper) of
    // the node being removed, rather than the node itself, it survived the
    // single-node check and was left dangling once the subtree was
    // actually destroyed — a real, reproducibly crashing use-after-free
    // (confirmed via gdb against real corpus content: a mouse hover over a
    // nested clip, followed by removal of one of its ancestors, then any
    // later pointer-event dispatch touching the stale hoverClip_,
    // segfaults inside Object::getMember()). This walks `clip` and its
    // FULL descendant tree (children AND each level's own buttonInstances)
    // calling notifyRemoved()/notifyButtonRemoved() on every node, so no
    // hover/press pointer into any depth of a torn-down subtree can
    // survive it. Deliberately does NOT also make onClipEvent(kUnload)
    // fire recursively for descendants — that's a separate (real, Flash
    // Player does fire it recursively) but distinct semantic question this
    // fix doesn't address, to stay scoped to the crash.
    void notifyRemovedRecursive(MovieClipInstance* clip);

    // Called by syncChildren()'s buttonInstances_ prune loop (a button
    // itself is only ever removed via that DISPLAY-LIST-driven path —
    // never synchronously mid-dispatch, see docs/events.md — so this is
    // the one call site that needs it).
    void notifyButtonRemoved(ButtonInstance* button) {
        if (hoverButton_ == button) hoverButton_ = nullptr;
        if (pressedButton_ == button) pressedButton_ = nullptr;
    }

    // --- Phase 7: ExternalInterface (AS2 <-> host/native) -----------------
    //
    // Real Flash's ExternalInterface bridges AS2 to browser JavaScript,
    // with values crossing a JS<->XML<->AS2 marshalling boundary. This
    // runtime has no browser — the eventual target is native 3DS code in
    // the SAME process — so, as a deliberate documented simplification/
    // improvement over real ExternalInterface semantics, `avm1::Value`
    // crosses the boundary directly in both directions; no serialization.
    //
    // AS2 -> native ("ExternalInterface.call(name, ...args)"): native/host
    // code registers a name via registerHostFunction() ahead of time;
    // ExternalInterface.call's nativeImpl (built in the constructor) looks
    // it up and invokes it via callHostFunction(). Returns undefined (and
    // logs a warning) if `name` was never registered.
    using HostFunction = std::function<avm1::Value(const std::vector<avm1::Value>&)>;
    void registerHostFunction(const std::string& name, HostFunction fn) {
        hostFunctions_[name] = std::move(fn);
    }
    avm1::Value callHostFunction(const std::string& name, const std::vector<avm1::Value>& args);

    // native -> AS2 ("ExternalInterface.addCallback(name, instance,
    // function)"): AS2 script registers `function` (bound to `instance` as
    // `this`) under `name` via addCallback's nativeImpl (which calls
    // registerCallback() below); host/native code then calls
    // hasCallback()/invokeCallback() to check for and run it. invokeCallback
    // builds a FRESH top-level Scope/ExecutionContext with **no HostBindings
    // bound** (see docs/avm1-support.md's Known Phase 7 limitations —
    // matches Phase 4's "host-less no-op" precedent: MovieClip-affecting
    // actions called directly inside a callback body are no-ops, but
    // ordinary computation, global/this member access, and calling other
    // AS2 functions all work normally).
    bool hasCallback(const std::string& name) const { return callbacks_.count(name) != 0; }
    avm1::Value invokeCallback(const std::string& name, const std::vector<avm1::Value>& args);

    // --- Event-dispatch phase (2026-08-19): pointer-event dispatcher ------
    // design: docs/events.md.
    //
    // ONE dispatcher covers both of this phase's real-corpus-driven
    // mechanisms: DefineButton2 native condActionsV2 (Hobo 1-7's primary
    // mechanism) and AS2 onPress/onRelease/onRollOver/onRollOut PROPERTY
    // handlers (Extreme Pamplona's mechanism) — see docs/events.md's
    // "Two mechanisms, one dispatcher" section for the evidence this split
    // is based on (both this phase's corpus scan AND a fresh raw-byte
    // condActionsV2 inspection done before writing this code).
    //
    // Called exactly once per full-tree tick, from the ROOT
    // MovieClipInstance's advanceFrame() only (same "root-only, once per
    // tick" precedent as updateDrag()/updateButtonStatesRecursive()) —
    // `hitButton`/`hitClip` are the SAME single hitTestPoint() result
    // advanceFrame() already computed for updateButtonStatesRecursive(),
    // reused here rather than a second hit-test call (`hitClip` is only
    // set when `hitButton` is null — a button hit always takes priority
    // over a plain-clip hit, matching how hitTestPoint() itself already
    // returns AT MOST one topmost result).
    void dispatchPointerEvents(MovieClipInstance& root, ButtonInstance* hitButton,
                                 MovieClipInstance* hitClip);

    // Called once per tick (root-only, alongside dispatchPointerEvents) via
    // MovieClipInstance::dispatchButtonKeyPressesRecursive() — checks every
    // CURRENTLY PLACED button in the whole tree (keyboard triggers are not
    // mouse-position-dependent) for a condActionsV2 record whose CondKeyPress
    // field matches a key that was just pressed this tick (InputState edge
    // detection — see condKeyPressToInputKeyCode()'s doc comment in the
    // .cpp for the SWF-spec-specific key-code table this converts from).
    void fireButtonKeyPressIfMatched(ButtonInstance& button);

private:
    void registerCallback(const std::string& name, avm1::Value thisArg,
                           std::shared_ptr<avm1::Object> func) {
        callbacks_[name] = Callback{std::move(thisArg), std::move(func)};
    }

    struct Callback {
        avm1::Value thisArg;
        std::shared_ptr<avm1::Object> func;
    };

    // Runs `func` (must be a callable Function object; a no-op returning
    // undefined otherwise) as an AS2 event-handler invocation: `thisVal` is
    // the AS2 `this` binding (the PROPERTY HANDLER's owning object — a
    // button's or clip's own scriptObject, matching real AS2's "this
    // follows the object the property was invoked on" rule), while
    // `hostBindingsTarget` is the MovieClipInstance any UNQUALIFIED
    // MovieClip-affecting action (bare `gotoAndPlay()`, `stop()`, ...)
    // inside the handler body acts on — see docs/events.md's "Execution
    // target" section for exactly why these two are deliberately DIFFERENT
    // objects for a button's property-handler call (thisVal = the button,
    // hostBindingsTarget = the button's PARENT, since buttons have no
    // timeline of their own) but the SAME object for a plain MovieClip's
    // property-handler call (both = that clip). Mirrors run()'s own
    // MovieClipHostBindings/Scope/ExecutionContext construction exactly
    // (shared anonymous-namespace helper class, see the .cpp) — reuses
    // Interpreter::callFunction() (Phase 7's existing public entry point
    // for invoking an already-resolved Function value), NOT a new
    // interpreter or call path.
    avm1::Value callHandler(MovieClipInstance& hostBindingsTarget,
                              const std::shared_ptr<avm1::Object>& func, avm1::Value thisVal);

    // Looks up `name` (onPress/onRelease/onRollOver/onRollOut) as a
    // prototype-chain-aware member (avm1::Object::getMember — covers both
    // a direct `obj.onPress = fn` assignment AND a shared-prototype-based
    // one) on `owner`'s AS2 identity object; calls it via callHandler() if
    // it resolves to a callable Function, no-ops otherwise (including: not
    // a function, e.g. a plain value some content assigned to the same
    // name by coincidence — matches real AS2's "only invoke if truly
    // callable" behavior).
    void firePropertyHandler(MovieClipInstance& owner, const char* name);
    void firePropertyHandler(ButtonInstance& owner, const char* name);

    // Looks up every condActionsV2 record on `button.def()` whose
    // `conditions` bitmask includes `cond` and runs each one's actionBytes
    // via run() with `button.parent()` as the target (native condActionsV2
    // dispatch — see docs/events.md's "Execution target" section for why
    // this, unlike property-handler dispatch above, uses the SAME object
    // for both `this` and the HostBindings target: real Flash button
    // symbol on()-handler code has always executed in the timeline that
    // CONTAINS the button, with no separate "this is the button" binding
    // at all). A no-op if `button.parent()` is null (shouldn't happen in
    // practice — every button is placed inside some clip's display list,
    // including the root's).
    void fireButtonCondition(ButtonInstance& button, swf::ButtonCondition cond);

    std::shared_ptr<avm1::Object> global_;
    std::unordered_map<uint16_t, std::vector<uint8_t>> initActionsByCharacter_;
    std::unordered_set<uint16_t> initializedCharacters_;

    InputState inputState_;
    audio::NullAudioBackend nullAudioBackend_;
    audio::IAudioBackend* audioBackend_ = &nullAudioBackend_;
    const CharacterDictionary* characters_ = nullptr;
    const Movie* movie_ = nullptr;

    NullFileLoader nullFileLoader_;
    IFileLoader* fileLoader_ = &nullFileLoader_;
    // See ownLoadedMovie()'s own doc comment: these just accumulate for the
    // lifetime of this ScriptEnvironment, never pruned.
    std::vector<std::unique_ptr<Movie>> loadedMovies_;
    std::vector<std::unique_ptr<CharacterDictionary>> loadedCharacterDicts_;

    // Decode-on-demand cache keyed by soundId (see playSoundById()'s own
    // comment for why decode-on-demand rather than eager). std::nullopt
    // means "already attempted, decode failed or wasn't applicable" — so a
    // sound that can't be decoded is only ever attempted once, not on
    // every single trigger.
    std::unordered_map<uint16_t, std::optional<audio::DecodedAudio>> decodedSoundCache_;

    // Decodes soundId on first reference (see playSoundById()); returns the
    // cached result (which may itself be an absent std::optional, for "we
    // already tried and it didn't decode") on every call after the first.
    const std::optional<audio::DecodedAudio>& ensureSoundDecoded(uint16_t soundId);

    MovieClipInstance* dragTarget_ = nullptr;
    avm1::HostBindings::DragOptions dragOptions_;
    double dragGrabOffsetX_ = 0.0;  // clip._x minus mouseX at drag start (non-lockCenter mode)
    double dragGrabOffsetY_ = 0.0;

    std::unordered_map<std::string, HostFunction> hostFunctions_;
    std::unordered_map<std::string, Callback> callbacks_;

    // --- Event-dispatch phase (2026-08-19) per-tick pointer state ---------
    // Non-owning; kept in lockstep with reality via notifyRemoved()/
    // notifyButtonRemoved() (see those methods' doc comments) so these
    // never dangle across a tick boundary or a removal that happens
    // synchronously mid-dispatch (docs/events.md's "Event target safety").
    //
    // hoverButton_/hoverClip_: whichever ONE of the two (never both — a
    // button hit always takes priority, see dispatchPointerEvents()'s doc
    // comment) the pointer was over as of the LAST tick's dispatch — used
    // to detect roll-over/roll-out transitions this tick.
    ButtonInstance* hoverButton_ = nullptr;
    MovieClipInstance* hoverClip_ = nullptr;
    // pressedButton_/pressedClip_: the button/clip that "captured" the
    // CURRENT press (set on a mouse-pressed edge, cleared on the matching
    // mouse-released edge) — real Flash's mouse-capture model: once you
    // press down on a button, THAT SAME button keeps receiving Over/Out-
    // Down transitions as the pointer drags around, even onto a DIFFERENT
    // button, until release (see docs/events.md — `trackAsMenu`'s "pick up
    // whichever button is currently under the pointer while held" override
    // of this rule is deliberately NOT implemented: zero `trackAsMenu`
    // buttons exist anywhere in the real-game corpus, confirmed by a
    // dedicated probe before writing this code).
    ButtonInstance* pressedButton_ = nullptr;
    MovieClipInstance* pressedClip_ = nullptr;
};

class MovieClipInstance : public std::enable_shared_from_this<MovieClipInstance> {
public:
    // Builds the ROOT instance from `movie`'s own top-level timeline: runs
    // frame 1's DoAction and recursively creates+initializes child
    // instances for every sprite character it places, exactly like a real
    // player's first frame. `movie`, `characters`, and `env` must all
    // outlive the returned tree. Returns nullptr if `movie` isn't valid or
    // has no frames.
    static std::shared_ptr<MovieClipInstance> createRoot(const Movie& movie,
                                                           const CharacterDictionary& characters,
                                                           ScriptEnvironment& env);

    // One host "tick": advances this clip's OWN timeline by one frame (if
    // playing — see Timeline::advanceOneFrame), runs the newly-current
    // frame's DoAction scripts, resyncs children against the resulting
    // display list (creating newly-placed sprite instances, destroying
    // removed ones), then recurses into every child. Each child's own
    // timeline advances independently, exactly once per call, regardless of
    // this clip's own frame count — this is what gives every MovieClip
    // instance an independent playhead.
    void advanceFrame();

    Timeline& timeline() { return *timeline_; }
    const Timeline& timeline() const { return *timeline_; }

    const std::string& name() const { return name_; }

    // Slash-syntax AS2 target path: "/", "/child1", "/child1/grandchild".
    std::string targetPath() const;

    MovieClipInstance* parent() const { return parent_; }
    MovieClipInstance& rootInstance();

    const std::shared_ptr<avm1::Object>& scriptObject() const { return scriptObject_; }

    // Resolves an AS2 target path against this instance as the reference
    // point: leading "/" or a leading "_root" segment is absolute (resolved
    // from the tree root); anything else is relative to `this`. Both
    // slash-syntax ("/a/b") and dot-syntax ("_root.a.b", "a.b") are
    // accepted (dots are normalized to slashes); ".." steps to the parent.
    // Returns nullptr if any segment doesn't resolve to an existing child.
    MovieClipInstance* resolvePath(const std::string& path);

    // The depth this instance currently occupies in its PARENT's display
    // list (0 / meaningless for the root, which has no parent).
    int32_t depthInParent() const { return depthInParent_; }
    uint16_t characterId() const { return characterId_; }

    const std::map<int32_t, std::shared_ptr<MovieClipInstance>>& children() const {
        return children_;
    }

    // Placed Button2/DefineButton runtime instances at this clip's own
    // depth level (not recursive) — see buttonInstances_'s field comment.
    const std::map<int32_t, std::shared_ptr<ButtonInstance>>& buttonInstances() const {
        return buttonInstances_;
    }

    // --- script-mutable placement state --------------------------------
    const swf::Matrix& localMatrix() const { return matrix_; }
    const swf::ColorTransform& colorTransform() const { return colorTransform_; }
    bool visible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }

    double x() const { return matrix_.translateXTwips / 20.0; }
    void setX(double px);
    double y() const { return matrix_.translateYTwips / 20.0; }
    void setY(double px);
    double xScale() const;
    void setXScale(double percent);
    double yScale() const;
    void setYScale(double percent);
    double rotation() const;
    void setRotation(double degrees);
    double alpha() const { return colorTransform_.alphaMult * 100.0; }
    void setAlpha(double percent) { colorTransform_.alphaMult = percent / 100.0; }

    // Real `_width`/`_height`: the axis-aligned bounding box of this clip's
    // ENTIRE current display list (recursively, through nested MovieClip
    // children), transformed through this clip's own placement matrix
    // (matching real AS2 — rotating/scaling a clip changes its reported
    // _width/_height, since both are measured in the PARENT's coordinate
    // space), in pixels. Added during the interactivity-audit phase (see
    // docs/known-limitations.md) — previously both were hardcoded to 0.
    // See computeBoundsInOwnSpace()'s doc comment (MovieClipInstance.cpp)
    // for exactly what's included/excluded (notably: bitmap/DefineMorphShape
    // characters, still unimplemented per docs/compatibility-matrix.md,
    // contribute nothing, matching how they already render as nothing).
    double width() const;
    double height() const;

    // AS2 _xmouse/_ymouse: the current mouse/touch position, converted from
    // whatever raw pixel space the host's input backend reports in (see
    // InputState::setViewportSize()) into THIS MOVIE's own SWF stage-pixel
    // space — the same coordinate space _x/_y/_width/_height already use.
    // Added (interactivity phase, 2026-08-18) to fix a real bug: previously
    // _xmouse/_ymouse returned InputState's raw value directly, with NO
    // conversion, which was wrong whenever the input viewport's pixel size
    // differed from the movie's own stage size (e.g. hobo.swf's 600x450
    // stage vs. a 3DS reporting touch in 400x240 top-screen pixels). If no
    // viewport size was ever set (InputState's default), this is the
    // identity — see docs/input.md.
    double stageMouseX() const;
    double stageMouseY() const;

    // --- hit-testing (interactivity phase, 2026-08-19; design:
    // docs/hit-testing.md) --------------------------------------------------

    // This instance's own matrix_ composed with every ancestor's, exactly
    // matching how SceneRenderer computes a child's world transform while
    // rendering (concatMatrix(parentWorld, child.localMatrix()) at every
    // level, with the ROOT's OWN matrix_ as the base — see
    // SceneRenderer::render()'s `renderClip(root, root.localMatrix(), ...)`
    // entry call). Kept in lockstep with rendering deliberately, so
    // hit-testing always agrees with what's actually drawn (the same
    // guarantee `_width`/`_height`/`computeBoundsInOwnSpace()` already
    // provide one level up, via matrix_ alone).
    swf::Matrix worldMatrix() const;

    // The result of a successful hitTestPoint() query — deliberately
    // returns MORE than just "yes/no" (matching docs/hit-testing.md's
    // planned algorithm, which returns "(clip, characterId, depth)"): a
    // future button/mouse-event-dispatch phase needs to know not just
    // THAT something was hit but WHICH clip directly owns the hit content
    // (the clip whose onClipEvent/on() handlers should fire) and which
    // placement (characterId/depth) was actually hit within it.
    struct HitTestResult {
        MovieClipInstance* clip = nullptr;
        uint16_t characterId = 0;
        int32_t depth = 0;
        // Non-null iff the hit depth held a placed Button2/DefineButton —
        // i.e. `clip->buttonInstances_[depth].get()` (ButtonInstance
        // phase, 2026-08-19). nullptr for every other hit (a shape/text/
        // MovieClip). See docs/buttons.md.
        ButtonInstance* button = nullptr;
    };

    // Bounding-box hit test: given a point in STAGE-PIXEL space (the same
    // coordinate space _xmouse/_ymouse/_x/_y already use — see
    // stageMouseX()/stageMouseY()), walks this clip's own display-list
    // subtree in TOPMOST-FIRST (reverse depth) order and returns the
    // frontmost placed character whose (parent-space, matrix-transformed)
    // bounding box contains the point, or std::nullopt if nothing in this
    // subtree was hit. Invisible clips (visible() == false) never match,
    // and neither does anything nested inside them (matches real Flash:
    // invisible objects don't receive mouse input). A MovieClip child is
    // hit-tested via recursion into ITS OWN content — NOT via a shortcut
    // test against its own aggregate bounding box — so clicking a fully
    // transparent/empty area of a nested clip correctly finds whatever's
    // underneath (or nothing), matching real Flash's content-based (not
    // purely bbox-based) recursive hit-test behavior for MovieClips.
    //
    // Bounding-box only for this first pass (per the task charter that
    // commissioned docs/hit-testing.md's design — exact vector-shape hit
    // testing is a documented future upgrade, not implemented here; see
    // that doc's "Why bounding-box, not exact shape" section). Not yet
    // wired to any event dispatch — this is purely a query primitive.
    std::optional<HitTestResult> hitTestPoint(double stageXPixels, double stageYPixels) const;

    // AS2-visible `MovieClip.hitTest(x, y)` (2-argument form only — see
    // handleNativeGet()'s OOP-callable-method dispatch). Distinct from
    // hitTestPoint() above on purpose, matching real Flash Player
    // semantics exactly: this tests whether (x, y) [stage pixels] falls
    // within THIS clip's own full aggregate bounding box
    // (computeBoundsInOwnSpace(), transformed by worldMatrix()) —
    // regardless of visibility (real `MovieClip.hitTest()` tests geometry,
    // NOT rendered visibility — a genuinely surprising-but-real Flash
    // behavior, unlike hitTestPoint() above, which deliberately DOES
    // respect visible() since it models "what could receive a real click,"
    // a different question). Does not recurse looking for the topmost
    // nested object; a hit anywhere in the aggregate bounds counts.
    bool hitTestBounds(double stageXPixels, double stageYPixels) const;

    // --- AVM1 lifecycle actions (CloneSprite/RemoveSprite), applied
    // eagerly/synchronously — used by the internal HostBindings
    // implementation, exposed here so tests can drive them directly too.
    std::shared_ptr<MovieClipInstance> cloneSprite(const std::string& newName, int32_t depth);
    void removeFromParent();

    // Roadmap Phase 4 (2026-08-21, dynamic instantiation — see
    // docs/known-limitations.md L3): AS2 MovieClip.attachMovie()'s real
    // primitive. Instantiates `characterId` (which MUST resolve to a
    // SpriteDef — a MovieClip library symbol, exactly like cloneSprite()'s
    // own constraint, since attachMovie always returns a MovieClip) as a
    // new child of `this` (not of `this`'s parent — the key difference
    // from cloneSprite(), which clones INTO its own parent as a sibling of
    // itself) at `depth`, replacing any existing occupant there (matches
    // real Flash's own "attachMovie into an occupied depth replaces it"
    // behavior, same as cloneSprite's). Returns nullptr (logged) if
    // `characterId` doesn't resolve or isn't a sprite. `initObject`, if
    // non-null, has its own properties shallow-copied onto the new clip's
    // scriptObject BEFORE its first frame's scripts run (matches real
    // Flash's documented attachMovie(..., initObject) timing).
    std::shared_ptr<MovieClipInstance> attachCharacter(
        uint16_t characterId, const std::string& newName, int32_t depth,
        const std::shared_ptr<avm1::Object>& initObject);

    // AS2 MovieClip.createEmptyMovieClip()'s real primitive: a new child
    // of `this` with NO backing character (no shape/sprite content of its
    // own — see SceneRenderer::renderClip()'s own comment on why an empty
    // display list already renders as "nothing" with no special-casing
    // needed) at `depth`, ready to have further clips attachMovie()'d or
    // duplicateMovieClip()'d into it. Always succeeds (nothing to fail to
    // resolve, unlike attachCharacter()).
    std::shared_ptr<MovieClipInstance> createEmptyChild(const std::string& newName,
                                                           int32_t depth);

    // AS2 MovieClip.swapDepths(target)'s real primitive: swaps `this`'s
    // depth (within its parent's display list) with `otherDepth`'s current
    // occupant, if any (an empty target depth just moves `this` there,
    // matching real Flash — "if you specify a depth that is already
    // occupied, the current occupant is moved to this clip's old depth").
    // No-op (logged) if `this` is the root clip (nothing to swap within).
    void swapDepthsWith(int32_t otherDepth);

    // AS2 MovieClip.getNextHighestDepth()'s real primitive: 1 + the
    // highest depth currently occupied in `this` clip's OWN display list
    // (both statically-placed and dynamically-attached/created content —
    // both go through the same DisplayList, see attachCharacter()/
    // createEmptyChild()'s own applyPlaceObject() calls), or 0 if nothing
    // is placed yet. A caller (e.g. game code trying to attachMovie()
    // several clips without depth collisions) is expected to use this
    // rather than guessing at a free depth.
    int32_t nextHighestDepth() const;

    // AS2 MovieClip.loadMovie(url)'s real primitive. Real Flash's
    // loadMovie() has TWO forms — a full `_level`-indexed sibling-movie
    // load (_levelN.loadMovie(...)), and the far more common "load into an
    // existing target clip, replacing its own content while keeping its
    // depth/position/name" form (targetClip.loadMovie(...)) — this
    // implements ONLY the second, simpler form (see docs/known-limitations.md
    // L4's "loadMovie scoping decision" for why the `_level` form was
    // deemed too large a lift for this phase: it would need SceneRenderer
    // to walk multiple independent root movies, plus non-trivial
    // Movie/CharacterDictionary ownership-lifetime questions that this
    // form sidesteps by keeping everything inside the existing single-root
    // tree).
    //
    // On success: fetches `url` via ScriptEnvironment::fileLoader(), parses
    // it as a fresh SWF (SwfLoader::loadSwf), builds a fresh
    // CharacterDictionary for it, hands both to
    // ScriptEnvironment::ownLoadedMovie() (so they outlive this call — see
    // that method's own doc comment), tears down `this` clip's ENTIRE
    // existing content (every current child/button, each properly
    // unloaded via the same notifyRemoved()/notifyButtonRemoved() path
    // syncChildren()'s own prune loop uses — see the .cpp), rebinds
    // `this`'s own movie_/characters_ pointers to the freshly-loaded pair,
    // replaces `this`'s Timeline with one built from the new movie's
    // top-level tags, then runs the new content's frame-1 scripts (mirrors
    // createRoot()'s own "place children before running frame 1" order).
    // `this`'s OWN depth/position/name/parent are left untouched — matches
    // real Flash's documented "loaded content replaces the target clip's
    // own content, keeping its depth/position/name" behavior.
    //
    // Returns false (logged) without changing anything if: no IFileLoader
    // is wired up (NullFileLoader — see ScriptEnvironment::setFileLoader()),
    // the fetch fails, or the fetched bytes don't parse as a valid SWF.
    // `this`'s prior content is left completely untouched in every failure
    // case — a failed loadMovie() never leaves a clip half-torn-down.
    //
    // KNOWN LIMITATION (documented, not silently glossed over): the new
    // movie's DoInitAction bodies ARE scanned into ScriptEnvironment (so
    // sprites newly instantiated from the loaded movie's own character
    // dictionary get their init actions), but ScriptEnvironment's single
    // env-wide movie_/characters_ binding (used by Sound.attachSound(id)/
    // playSoundById() for numeric-ID sound resolution) is NOT rebound to
    // the loaded sub-movie — it stays pointed at whichever movie
    // originally called ScriptEnvironment::bindMovie()/bindCharacters()
    // (almost always the true top-level root). A loaded sub-movie's OWN
    // sound characters therefore won't resolve by numeric ID through that
    // path; this is a real, narrow gap, not an oversight — see
    // docs/known-limitations.md L4 for the full writeup and why widening
    // ScriptEnvironment's binding to be per-MovieClipInstance rather than
    // env-wide is out of scope for this phase.
    bool loadMovie(const std::string& url);

private:
    MovieClipInstance(const Movie& movie, const CharacterDictionary& characters,
                       ScriptEnvironment& env, MovieClipInstance* parent, std::string name,
                       int32_t depthInParent, std::unique_ptr<Timeline> timeline);

    void wireScriptObject();
    bool handleNativeGet(const std::string& name, avm1::Value& out) const;
    bool handleNativeSet(const std::string& name, const avm1::Value& value);

    void initializeNewlyCreated();
    void runCurrentFrameScripts();
    void runCurrentFrameSounds();
    void syncChildren();

    // Per-tick UP/OVER/DOWN state driver (ButtonInstance phase,
    // 2026-08-19) — mirrors ScriptEnvironment::updateDrag()'s own
    // "one global per-tick update, driven only from the ROOT's
    // advanceFrame()" precedent, but implemented as a tree-recursive
    // MovieClipInstance method rather than centralized in
    // ScriptEnvironment, since (unlike drag, which tracks at most one
    // target) buttons are per-clip/tree-distributed — every
    // MovieClipInstance in the tree may own its own buttonInstances_.
    // `hitButton` is the SINGLE topmost ButtonInstance the mouse/touch
    // point currently hits anywhere in the whole tree (computed ONCE by
    // the root, via hitTestPoint(), before this call — see
    // advanceFrame()) — every OTHER button in the tree gets isOver=false.
    void updateButtonStatesRecursive(ButtonInstance* hitButton, bool mouseDown);

    // CondKeyPress dispatch driver (event-dispatch phase, 2026-08-19) —
    // mirrors updateButtonStatesRecursive()'s own "root-only, once per
    // tick, recurse the WHOLE tree" shape exactly, but keyboard triggers
    // are not mouse-position-dependent (unlike hover/press/release, which
    // only ever apply to the SINGLE topmost hitTestPoint() result) — every
    // currently-placed button anywhere in the tree must be checked every
    // tick. Snapshots buttonInstances_/children_ into local shared_ptr
    // vectors before dispatching into any of them (see docs/events.md's
    // "Event target safety" section) — a dispatched keypress action can
    // mutate a display list, and iterating a live map while calling into
    // AVM1 is not safe, mirroring the exact same idiom advanceFrame()
    // already uses for children_ before its own recursive descent.
    void dispatchButtonKeyPressesRecursive();

    // The union of every currently-placed character/child's bounds, in
    // THIS instance's own local space (i.e. BEFORE this instance's own
    // matrix_ is applied) — see MovieClipInstance.cpp for the full doc
    // comment and exactly what's included. Backs width()/height() and is
    // intended to be reused as-is by a future hit-testing pass (see
    // docs/hit-testing.md) rather than duplicated.
    swf::Rect computeBoundsInOwnSpace() const;

    // Recursive worker behind hitTestPoint() (see its public doc comment
    // for the full contract): `localPoint` is already in THIS instance's
    // own local space (same "before matrix_ is applied" convention as
    // computeBoundsInOwnSpace()) — the public entry point is responsible
    // for converting a stage-pixel point into that space via
    // worldMatrix()'s inverse before the first call.
    std::optional<HitTestResult> hitTestPointInOwnSpace(const swf::Point& localPoint) const;

    // Runs every ClipActionRecord in clipActions_ whose eventFlags include
    // `flag` (Phase 6 — see the class header's onClipEvent limitations
    // note for which flags are actually fired anywhere in this file).
    void runClipEvent(swf::ClipEventFlag flag);

    const Movie* movie_;
    const CharacterDictionary* characters_;
    ScriptEnvironment* env_;
    MovieClipInstance* parent_ = nullptr;
    std::string name_;
    int32_t depthInParent_ = 0;
    uint16_t characterId_ = 0;

    std::unique_ptr<Timeline> timeline_;
    std::map<int32_t, std::shared_ptr<MovieClipInstance>> children_;
    // Placed Button2/DefineButton runtime instances — a SEPARATE map from
    // children_ (ButtonInstance phase, 2026-08-19), deliberately NOT a
    // MovieClipInstance subtype and NOT visible to SceneRenderer (which
    // keeps walking the raw DisplayList exactly as before — see
    // ButtonInstance.h's class header for why this guarantees pixel-
    // identical rendering by construction). Keyed by depth, exactly like
    // children_ and the underlying DisplayList itself.
    std::map<int32_t, std::shared_ptr<ButtonInstance>> buttonInstances_;
    std::unordered_map<std::string, int32_t> childNameToDepth_;
    std::vector<swf::ClipActionRecord> clipActions_;  // Phase 6, copied from the placing DisplayListEntry

    swf::Matrix matrix_ = swf::Matrix::identity();
    swf::ColorTransform colorTransform_ = swf::ColorTransform::identity();
    bool visible_ = true;

    std::shared_ptr<avm1::Object> scriptObject_;

    // Guards against a malformed/cyclic sprite reference recursing forever
    // (mirrors SceneRenderer's own kMaxRecursionDepth): each child records
    // one more than its parent's value at creation time; syncChildren()
    // refuses to create further children once this hits kMaxDepth.
    int childrenSyncDepth_ = 0;
    static constexpr int kMaxDepth = 64;
};

}  // namespace flash3ds::runtime
