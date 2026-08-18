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
//     NOT dispatched: hit-testing itself (which now HAS bounds to work
//     with, above) still doesn't exist yet — see docs/hit-testing.md for
//     the planned design. Button `on()` handlers are parsed
//     (DefineButton/2, Phase 8) but not dispatched for the same reason.
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
#include <vector>

#include "audio/IAudioBackend.h"
#include "audio/NullAudioBackend.h"
#include "avm1/HostBindings.h"
#include "avm1/Value.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/DisplayList.h"
#include "runtime/InputState.h"
#include "runtime/Movie.h"
#include "runtime/Timeline.h"
#include "swf/SwfRecords.h"

namespace flash3ds::runtime {

class MovieClipInstance;

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

    // Lets AVM1's native Sound object resolve a numeric attachSound() ID
    // against real DefineSound character data (see Phase 6 limitations
    // note on MovieClipInstance's class header: attachSound(name:String) —
    // real AS2's actual linkage-name form — is NOT resolvable without
    // ExportAssets parsing, which doesn't exist yet). Called once by
    // MovieClipInstance::createRoot(); non-owning, same lifetime
    // assumption as everything else here.
    void bindCharacters(const CharacterDictionary& characters) { characters_ = &characters; }
    const CharacterDictionary* characters() const { return characters_; }

    // ActionStartDrag/ActionEndDrag's real (Phase 6) implementation: tracks
    // at most one dragged clip at a time (matches real Flash — starting a
    // new drag implicitly ends any previous one), and updateDrag() (called
    // once per full-tree tick, from the ROOT MovieClipInstance's
    // advanceFrame() only — see its implementation) repositions the
    // dragged clip from the current mouse position.
    void startDrag(MovieClipInstance* clip, const avm1::HostBindings::DragOptions& options);
    void endDrag();
    void updateDrag();

    // Called by MovieClipInstance::removeFromParent() so a removed clip
    // never lingers as a dangling drag target — a no-op unless `clip` is
    // the currently-dragged one.
    void notifyRemoved(MovieClipInstance* clip) {
        if (dragTarget_ == clip) dragTarget_ = nullptr;
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

private:
    void registerCallback(const std::string& name, avm1::Value thisArg,
                           std::shared_ptr<avm1::Object> func) {
        callbacks_[name] = Callback{std::move(thisArg), std::move(func)};
    }

    struct Callback {
        avm1::Value thisArg;
        std::shared_ptr<avm1::Object> func;
    };

    std::shared_ptr<avm1::Object> global_;
    std::unordered_map<uint16_t, std::vector<uint8_t>> initActionsByCharacter_;
    std::unordered_set<uint16_t> initializedCharacters_;

    InputState inputState_;
    audio::NullAudioBackend nullAudioBackend_;
    audio::IAudioBackend* audioBackend_ = &nullAudioBackend_;
    const CharacterDictionary* characters_ = nullptr;

    MovieClipInstance* dragTarget_ = nullptr;
    avm1::HostBindings::DragOptions dragOptions_;
    double dragGrabOffsetX_ = 0.0;  // clip._x minus mouseX at drag start (non-lockCenter mode)
    double dragGrabOffsetY_ = 0.0;

    std::unordered_map<std::string, HostFunction> hostFunctions_;
    std::unordered_map<std::string, Callback> callbacks_;
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
