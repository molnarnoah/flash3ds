// ButtonInstance.h
//
// Runtime placement of a DefineButton/DefineButton2 (swf::ButtonDef)
// character — the button-phase counterpart of MovieClipInstance for
// SpriteDef. Added 2026-08-19 (ButtonInstance phase) to close a real,
// audit-confirmed gap: before this phase, a placed Button2 had NO runtime
// representation at all. MovieClipInstance::syncChildren() only created a
// child instance when the placed character was a SpriteDef — every other
// character type (buttons included) was simply skipped, and
// SceneRenderer::renderCharacter() rendered a button directly from its
// DisplayListEntry, always drawing the Up-state records with no notion of
// mouse state. See docs/buttons.md for the full writeup.
//
// IMPORTANT INVARIANT (mirrors the existing SpriteDef/MovieClipInstance
// split exactly — see MovieClipInstance.h's own class header):
//   - swf::ButtonDef (swf/DefineButtonTag.h) is SHARED IMMUTABLE data —
//     the parsed SWF definition, identical for every placement of the same
//     character. NEVER store per-placement mutable state in it.
//   - ButtonInstance is per-PLACEMENT mutable state: transform, color
//     transform, visibility, and the UP/OVER/DOWN button state. Two
//     placements of the SAME ButtonDef (e.g. two copies of one button
//     symbol) get two INDEPENDENT ButtonInstance objects — changing one's
//     state must never affect the other's (see test_button_instance.cpp's
//     "independent instances" regression tests).
//
// SCOPE OF THIS PHASE (see docs/buttons.md's "Not implemented yet"
// section for the full list): this class establishes the runtime
// identity/transform/depth/visibility/hit-area/UP-OVER-DOWN state of a
// placed button, and nothing else. It deliberately does NOT dispatch any
// ActionScript event (onPress/onRelease/onRollOver/onRollOut/
// releaseOutside/Mouse.onMouseDown/onMouseUp/onClipEvent(mouseDown/
// mouseUp)) and does NOT run the ButtonDef's parsed condActionsV2/
// actionsV1 bytecode anywhere. That is explicitly the NEXT phase's job.
//
// DISPLAY-LIST / RENDERING INTEGRATION: ButtonInstance objects live in
// MovieClipInstance's NEW, separate `buttonInstances_` map — deliberately
// NOT inside `children_` (which only ever holds MovieClipInstance/Sprite
// children) and NOT visible to SceneRenderer at all. SceneRenderer keeps
// walking the raw DisplayList exactly as before, so existing SWFs render
// BYTE-IDENTICAL pixels — the only new behavior this phase adds is runtime
// identity/state/hit-testing, layered alongside rendering rather than
// replacing any part of it.
//
// HIT-TESTING: reuses the exact same primitives MovieClipInstance's own
// hit-testing already uses (characterOwnBoundsRect/invertMatrix/
// transformPoint/rectContainsPoint) — see hitTestLocal()'s doc comment.
// No second hit-testing implementation is introduced.
//
// AS2 OBJECT IDENTITY: scriptObject() is a bare avm1::Object with NO
// native get/set/enumerate hooks wired — it exists purely so
// `_root.someButtonName` resolves to a real, distinct AS2 object
// (established via MovieClipInstance::handleNativeGet()'s
// childNameToDepth_ fallback into buttonInstances_ — see
// MovieClipInstance.cpp). No button-specific properties (_x/_y/_visible/
// etc.) or methods are exposed on it yet; that bridging is deferred to the
// event-dispatch phase this file explicitly does not implement. See
// docs/buttons.md's "AS2 object identity" section for the exact missing
// bridge this leaves (e.g. `_root.someButtonName._x` currently resolves to
// undefined, since scriptObject_ has no nativeGet hook at all).

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "avm1/Value.h"
#include "runtime/CharacterDictionary.h"
#include "swf/DefineButtonTag.h"
#include "swf/SwfRecords.h"

namespace flash3ds::runtime {

class MovieClipInstance;

class ButtonInstance {
public:
    // UP/OVER/DOWN only — matches this phase's charter exactly (see the
    // class header's "SCOPE OF THIS PHASE" note and docs/buttons.md's
    // "Known simplification" section for why real Flash's richer 5-state
    // model, e.g. distinguishing OutDown from Up while a press is dragged
    // off the button, is not modeled here: dragging off simply reports
    // kUp, matching the VISUAL convention of showing Up-state artwork,
    // which is the only thing observable without event dispatch anyway).
    enum class State { kUp, kOver, kDown };

    // `def` must outlive this instance — it's a non-owning reference into
    // CharacterDictionary's storage, exactly like MovieClipInstance's own
    // `characters_`/`movie_` pointers (see that class's constructor
    // comment). `parent` is the owning MovieClipInstance (never null in
    // practice — buttons are always placed inside some clip's display
    // list, including the root's).
    ButtonInstance(const swf::ButtonDef& def, uint16_t characterId, MovieClipInstance* parent,
                   int32_t depthInParent, std::string name);

    const swf::ButtonDef& def() const { return *def_; }
    uint16_t characterId() const { return characterId_; }
    MovieClipInstance* parent() const { return parent_; }
    int32_t depthInParent() const { return depthInParent_; }
    const std::string& name() const { return name_; }

    // --- placement state (set once at creation from the placing
    // DisplayListEntry — see MovieClipInstance::syncChildren()) ----------
    const swf::Matrix& localMatrix() const { return matrix_; }
    void setLocalMatrix(const swf::Matrix& m) { matrix_ = m; }
    const swf::ColorTransform& colorTransform() const { return colorTransform_; }
    void setColorTransform(const swf::ColorTransform& c) { colorTransform_ = c; }
    bool visible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }

    // This instance's own matrix_ composed with every ancestor
    // MovieClipInstance's world transform — mirrors MovieClipInstance::
    // worldMatrix() exactly (same concatMatrix(parentWorld, ownMatrix)
    // composition), so a button nested inside any depth of MovieClip
    // nesting (root -> MovieClip -> Button) still gets a correct world
    // transform for free.
    swf::Matrix worldMatrix() const;

    // --- UP/OVER/DOWN state machine --------------------------------------

    State state() const { return state_; }
    State previousState() const { return previousState_; }

    // Recomputes state_ from the current per-tick hit-test result
    // (`isOver`: is THIS button the topmost thing the mouse/touch point is
    // currently over?) and the current mouse/touch button state
    // (`mouseDown`). Formula (see the class header's State doc comment for
    // the documented simplification vs. real Flash's 5-state model):
    //   !isOver                -> kUp
    //   isOver && !mouseDown   -> kOver
    //   isOver &&  mouseDown   -> kDown
    // Returns true iff state_ actually changed (previousState_ != new
    // state_) — callers can use this later to know when to fire a
    // transition event, though THIS PHASE never does (see class header).
    bool updateState(bool isOver, bool mouseDown);

    // Bounding-box hit test against this button's HIT-TEST-state geometry
    // (falling back to Up-state geometry if no explicit HitTest state
    // exists) — reuses characterOwnBoundsRect()/invertMatrix()/
    // transformPoint()/rectContainsPoint() exactly as MovieClipInstance's
    // own hitTestPointInOwnSpace() leaf-character branch does; this is a
    // thin wrapper, not a second implementation. `localPoint` must already
    // be in the OWNING MovieClipInstance's own local space (the same
    // space its DisplayList entries' matrices apply directly against —
    // see MovieClipInstance::hitTestPointInOwnSpace()'s doc comment) —
    // i.e. the SAME convention entry.matrix uses, which is exactly what
    // matrix_ was set to at creation time.
    bool hitTestLocal(const swf::Point& localPoint, const CharacterDictionary& characters) const;

    // Bare AS2 identity object — see the class header's "AS2 OBJECT
    // IDENTITY" section. Called once by MovieClipInstance::syncChildren()
    // right after construction, mirroring MovieClipInstance::
    // wireScriptObject()'s naming (but with no native hooks wired).
    void wireScriptObject();
    const std::shared_ptr<avm1::Object>& scriptObject() const { return scriptObject_; }

private:
    const swf::ButtonDef* def_;
    uint16_t characterId_ = 0;
    MovieClipInstance* parent_ = nullptr;
    int32_t depthInParent_ = 0;
    std::string name_;

    swf::Matrix matrix_ = swf::Matrix::identity();
    swf::ColorTransform colorTransform_ = swf::ColorTransform::identity();
    bool visible_ = true;

    State state_ = State::kUp;
    State previousState_ = State::kUp;

    std::shared_ptr<avm1::Object> scriptObject_;
};

}  // namespace flash3ds::runtime
