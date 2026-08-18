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
//   - _width/_height are not computed (would require full recursive
//     subtree bounding-box computation); they always return 0.
//   - onClipEvent/button `on()` handlers are not implemented — most useful
//     triggers (mouseDown, press, ...) need an input model that doesn't
//     exist yet (Phase 6), and `onClipEvent` itself requires parsing
//     PlaceObject2's optional ClipActionRecord section, which isn't parsed
//     yet either. DoAction/DoInitAction frame scripts ARE fully wired.
//   - StartDrag/EndDrag are recognized and forwarded but are no-ops (no
//     input/pointer model yet — Phase 6).
//   - _xscale/_yscale/_rotation use a standard decomposition of the
//     placement MATRIX that assumes no independent (non-rotational) skew —
//     exactly correct for any transform actually produced by _xscale/
//     _yscale/_rotation themselves, an approximation for a hand-authored
//     skewed matrix (rare in practice).

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "avm1/Value.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/DisplayList.h"
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

private:
    std::shared_ptr<avm1::Object> global_;
    std::unordered_map<uint16_t, std::vector<uint8_t>> initActionsByCharacter_;
    std::unordered_set<uint16_t> initializedCharacters_;
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
    void syncChildren();

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
