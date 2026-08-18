#include "runtime/MovieClipInstance.h"

#include <algorithm>
#include <cmath>
#include <variant>

#include "avm1/GlobalObject.h"
#include "avm1/HostBindings.h"
#include "avm1/Interpreter.h"
#include "avm1/Scope.h"
#include "platform/Log.h"
#include "swf/TagCode.h"

namespace flash3ds::runtime {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Registers one Key.* named constant. A small helper rather than 17
// near-identical setOwnProperty calls.
void setKeyConstant(avm1::Object& keyObj, const char* name, int code) {
    keyObj.setOwnProperty(name, avm1::Value::number(code));
}

// --- _width/_height bounds computation (interactivity-audit phase) --------
//
// A small "empty rect" sentinel/union algebra, analogous to swf::
// ColorTransform::identity() being the neutral element for
// concatColorTransform: emptyBoundsRect() is the neutral element for
// unionBoundsRect() (unioning with it returns the other operand unchanged),
// detected via the classic "inverted" empty-rect convention (xMin > xMax)
// rather than a separate bool flag.
swf::Rect emptyBoundsRect() {
    return swf::Rect{INT32_MAX, INT32_MIN, INT32_MAX, INT32_MIN};
}

bool isEmptyBoundsRect(const swf::Rect& r) { return r.xMin > r.xMax || r.yMin > r.yMax; }

swf::Rect unionBoundsRect(const swf::Rect& a, const swf::Rect& b) {
    if (isEmptyBoundsRect(a)) return b;
    if (isEmptyBoundsRect(b)) return a;
    return swf::Rect{std::min(a.xMin, b.xMin), std::max(a.xMax, b.xMax), std::min(a.yMin, b.yMin),
                      std::max(a.yMax, b.yMax)};
}

// Resolves a LEAF character's own (untransformed) bounds — the same
// "which CharacterDef alternative is this" dispatch renderer::SceneRenderer
// uses, but returning a bounding Rect instead of drawing. Deliberately
// scoped (matches this codebase's "trace real gaps, don't guess" habit):
//   - ShapeDef/TextDef/EditTextDef: use the tag's own already-parsed
//     ShapeBounds/TextBounds/bounds RECT directly — no computation needed.
//   - ButtonDef: unions the HitTest-state records' underlying character
//     bounds (falling back to Up-state records if the button defines no
//     explicit HitTest state at all — matches real Flash, which requires
//     an author-supplied hit area but tolerates its absence by using
//     Up-state geometry). Only resolves ONE level deep (a button record
//     referencing a Shape/Text/EditText) — a button record referencing a
//     nested Sprite for its hit area is rare/unusual authoring and is
//     skipped rather than recursed into, to keep this a bounded, simple
//     first pass (see docs/hit-testing.md).
//   - SpriteDef/SoundDef/FontDef: empty — sprites are never leaf display-
//     list entries at runtime (they resolve to a MovieClipInstance child
//     instead, handled separately by computeBoundsInOwnSpace() below), and
//     Sound/Font are never placeable characters at all.
//   - Bitmap characters (DefineBits*) and DefineMorphShape/2 don't resolve
//     into CharacterDictionary at all yet (see docs/compatibility-matrix.md)
//     so `def` itself would be null for those — never reaches this function.
swf::Rect characterOwnBoundsRect(const CharacterDef& def, const CharacterDictionary& characters) {
    if (const auto* shape = std::get_if<swf::ShapeDef>(&def)) return shape->bounds;
    if (const auto* text = std::get_if<swf::TextDef>(&def)) return text->bounds;
    if (const auto* editText = std::get_if<swf::EditTextDef>(&def)) return editText->bounds;
    if (const auto* button = std::get_if<swf::ButtonDef>(&def)) {
        bool anyHitTest = false;
        for (const auto& rec : button->records) {
            if (rec.stateHitTest) {
                anyHitTest = true;
                break;
            }
        }
        swf::Rect result = emptyBoundsRect();
        for (const auto& rec : button->records) {
            bool use = anyHitTest ? rec.stateHitTest : rec.stateUp;
            if (!use) continue;
            const CharacterDef* nested = characters.find(rec.characterId);
            if (!nested) continue;
            swf::Rect nestedBounds;
            if (const auto* nShape = std::get_if<swf::ShapeDef>(nested)) {
                nestedBounds = nShape->bounds;
            } else if (const auto* nText = std::get_if<swf::TextDef>(nested)) {
                nestedBounds = nText->bounds;
            } else if (const auto* nEdit = std::get_if<swf::EditTextDef>(nested)) {
                nestedBounds = nEdit->bounds;
            } else {
                continue;
            }
            result = unionBoundsRect(result, swf::transformRect(rec.matrix, nestedBounds));
        }
        return result;
    }
    return emptyBoundsRect();
}

}  // namespace

// ===========================================================================
// ScriptEnvironment
// ===========================================================================

ScriptEnvironment::ScriptEnvironment() : global_(avm1::GlobalObject::create()) {
    // --- Key (Phase 6) ------------------------------------------------------
    // A plain object used AS2-statically (Key.isDown(...), never `new
    // Key()`) — isDown()/getCode() close over `this` (ScriptEnvironment) by
    // raw pointer, matching the "ScriptEnvironment outlives everything built
    // from it" convention already used by MovieClipInstance's own movie_/
    // characters_/env_ members.
    auto keyObj = std::make_shared<avm1::Object>();
    keyObj->setOwnProperty(
        "isDown", avm1::Value::object(avm1::makeNativeFunction(
                      "isDown", [this](avm1::ExecutionContext&, const avm1::Value&,
                                        const std::vector<avm1::Value>& args) {
                          int code = args.empty() ? 0 : static_cast<int>(args[0].toNumber());
                          return avm1::Value::boolean(inputState_.isKeyDown(code));
                      })));
    keyObj->setOwnProperty(
        "getCode", avm1::Value::object(avm1::makeNativeFunction(
                       "getCode", [this](avm1::ExecutionContext&, const avm1::Value&,
                                          const std::vector<avm1::Value>&) {
                           return avm1::Value::number(inputState_.lastKeyCode());
                       })));
    setKeyConstant(*keyObj, "BACKSPACE", InputState::kBackspace);
    setKeyConstant(*keyObj, "CAPSLOCK", InputState::kCapsLock);
    setKeyConstant(*keyObj, "CONTROL", InputState::kControl);
    setKeyConstant(*keyObj, "DELETEKEY", InputState::kDelete);
    setKeyConstant(*keyObj, "DOWN", InputState::kDown);
    setKeyConstant(*keyObj, "END", InputState::kEnd);
    setKeyConstant(*keyObj, "ENTER", InputState::kEnter);
    setKeyConstant(*keyObj, "ESCAPE", InputState::kEscape);
    setKeyConstant(*keyObj, "HOME", InputState::kHome);
    setKeyConstant(*keyObj, "INSERT", InputState::kInsert);
    setKeyConstant(*keyObj, "LEFT", InputState::kLeft);
    setKeyConstant(*keyObj, "PGDN", InputState::kPageDown);
    setKeyConstant(*keyObj, "PGUP", InputState::kPageUp);
    setKeyConstant(*keyObj, "RIGHT", InputState::kRight);
    setKeyConstant(*keyObj, "SHIFT", InputState::kShift);
    setKeyConstant(*keyObj, "SPACE", InputState::kSpace);
    setKeyConstant(*keyObj, "TAB", InputState::kTab);
    setKeyConstant(*keyObj, "UP", InputState::kUp);
    global_->setOwnProperty("Key", avm1::Value::object(keyObj));

    // --- Mouse (Phase 6) -----------------------------------------------------
    // show()/hide() are recognized no-ops: this runtime has no cursor
    // rendering model (headless-first, 3DS-bound — see docs/renderer.md).
    auto mouseObj = std::make_shared<avm1::Object>();
    mouseObj->setOwnProperty(
        "show", avm1::Value::object(avm1::makeNativeFunction(
                    "show", [](avm1::ExecutionContext&, const avm1::Value&,
                               const std::vector<avm1::Value>&) {
                        LOG_DEBUG("MOVIECLIP", "Mouse.show() — no cursor rendering model");
                        return avm1::Value::undefined();
                    })));
    mouseObj->setOwnProperty(
        "hide", avm1::Value::object(avm1::makeNativeFunction(
                    "hide", [](avm1::ExecutionContext&, const avm1::Value&,
                               const std::vector<avm1::Value>&) {
                        LOG_DEBUG("MOVIECLIP", "Mouse.hide() — no cursor rendering model");
                        return avm1::Value::undefined();
                    })));
    global_->setOwnProperty("Mouse", avm1::Value::object(mouseObj));

    // --- Sound (Phase 6) ------------------------------------------------------
    // See the "Sound" bullet in MovieClipInstance.h's DOCUMENTED
    // LIMITATIONS for exactly what's and isn't resolvable here.
    auto soundProto = std::make_shared<avm1::Object>();
    soundProto->setOwnProperty(
        "attachSound",
        avm1::Value::object(avm1::makeNativeFunction(
            "attachSound", [this](avm1::ExecutionContext&, const avm1::Value& thisVal,
                                   const std::vector<avm1::Value>& args) {
                if (!thisVal.isObject() || !thisVal.asObject() || args.empty()) {
                    return avm1::Value::undefined();
                }
                if (!args[0].isNumber()) {
                    LOG_WARN("MOVIECLIP",
                              "Sound.attachSound('%s'): linkage-name resolution not "
                              "implemented (needs ExportAssets — later phase)",
                              args[0].toString().c_str());
                    return avm1::Value::undefined();
                }
                uint16_t id = static_cast<uint16_t>(args[0].toNumber());
                const CharacterDef* def = characters_ ? characters_->find(id) : nullptr;
                if (!def || !std::holds_alternative<swf::SoundDef>(*def)) {
                    LOG_WARN("MOVIECLIP",
                              "Sound.attachSound(%u): no DefineSound character with that id",
                              id);
                }
                thisVal.asObject()->setOwnProperty("_soundId", avm1::Value::number(id));
                return avm1::Value::undefined();
            })));
    soundProto->setOwnProperty(
        "start", avm1::Value::object(avm1::makeNativeFunction(
                     "start", [this](avm1::ExecutionContext&, const avm1::Value& thisVal,
                                      const std::vector<avm1::Value>& args) {
                         if (!thisVal.isObject() || !thisVal.asObject()) return avm1::Value::undefined();
                         avm1::Value idVal = thisVal.asObject()->getOwnProperty("_soundId");
                         if (!idVal.isNumber()) {
                             LOG_WARN("MOVIECLIP", "Sound.start(): no resolvable sound attached");
                             return avm1::Value::undefined();
                         }
                         int loopCount =
                             args.size() > 1 ? std::max(1, static_cast<int>(args[1].toNumber())) : 1;
                         audioBackend_->playSound(static_cast<uint16_t>(idVal.toNumber()), loopCount);
                         return avm1::Value::undefined();
                     })));
    soundProto->setOwnProperty(
        "stop", avm1::Value::object(avm1::makeNativeFunction(
                    "stop", [this](avm1::ExecutionContext&, const avm1::Value& thisVal,
                                    const std::vector<avm1::Value>&) {
                        avm1::Value idVal = (thisVal.isObject() && thisVal.asObject())
                                                 ? thisVal.asObject()->getOwnProperty("_soundId")
                                                 : avm1::Value::undefined();
                        if (idVal.isNumber()) {
                            audioBackend_->stopSound(static_cast<uint16_t>(idVal.toNumber()));
                        } else {
                            audioBackend_->stopAllSounds();
                        }
                        return avm1::Value::undefined();
                    })));
    soundProto->setOwnProperty(
        "setVolume",
        avm1::Value::object(avm1::makeNativeFunction(
            "setVolume", [](avm1::ExecutionContext&, const avm1::Value& thisVal,
                             const std::vector<avm1::Value>& args) {
                if (thisVal.isObject() && thisVal.asObject()) {
                    double vol = args.empty() ? 100.0 : args[0].toNumber();
                    thisVal.asObject()->setOwnProperty("_volume", avm1::Value::number(vol));
                }
                return avm1::Value::undefined();
            })));
    soundProto->setOwnProperty(
        "getVolume", avm1::Value::object(avm1::makeNativeFunction(
                         "getVolume", [](avm1::ExecutionContext&, const avm1::Value& thisVal,
                                          const std::vector<avm1::Value>&) {
                             if (thisVal.isObject() && thisVal.asObject()) {
                                 avm1::Value v = thisVal.asObject()->getOwnProperty("_volume");
                                 if (v.isNumber()) return v;
                             }
                             return avm1::Value::number(100.0);
                         })));

    auto soundCtor = avm1::makeNativeFunction(
        "Sound", [](avm1::ExecutionContext&, const avm1::Value& thisVal,
                     const std::vector<avm1::Value>& args) {
            if (thisVal.isObject() && thisVal.asObject() && !args.empty() && args[0].isObject()) {
                thisVal.asObject()->setOwnProperty("_target", args[0]);
            }
            return avm1::Value::undefined();
        });
    soundCtor->setOwnProperty("prototype", avm1::Value::object(soundProto));
    global_->setOwnProperty("Sound", avm1::Value::object(soundCtor));

    // --- ExternalInterface (Phase 7) -----------------------------------------
    // See the class header's "Phase 7: ExternalInterface" doc comment for
    // the full AS2<->native design (in particular: Value crosses directly,
    // no JS/XML-style marshalling — this is a same-process C++ embedding,
    // not a browser+JS bridge).
    auto extInterfaceObj = std::make_shared<avm1::Object>();
    extInterfaceObj->setOwnProperty("available", avm1::Value::boolean(true));
    extInterfaceObj->setOwnProperty(
        "call", avm1::Value::object(avm1::makeNativeFunction(
                    "call", [this](avm1::ExecutionContext&, const avm1::Value&,
                                    const std::vector<avm1::Value>& args) {
                        if (args.empty()) return avm1::Value::undefined();
                        std::string name = args[0].toString();
                        std::vector<avm1::Value> callArgs(args.begin() + 1, args.end());
                        return callHostFunction(name, callArgs);
                    })));
    extInterfaceObj->setOwnProperty(
        "addCallback",
        avm1::Value::object(avm1::makeNativeFunction(
            "addCallback", [this](avm1::ExecutionContext&, const avm1::Value&,
                                    const std::vector<avm1::Value>& args) {
                // AS2 signature: addCallback(methodName:String, instance:Object,
                // function:Function):Boolean.
                if (args.size() < 3 || !args[2].isObject() || !args[2].asObject() ||
                    !args[2].asObject()->isFunction()) {
                    return avm1::Value::boolean(false);
                }
                registerCallback(args[0].toString(), args[1], args[2].asObject());
                return avm1::Value::boolean(true);
            })));
    global_->setOwnProperty("ExternalInterface", avm1::Value::object(extInterfaceObj));
}

avm1::Value ScriptEnvironment::callHostFunction(const std::string& name,
                                                  const std::vector<avm1::Value>& args) {
    auto it = hostFunctions_.find(name);
    if (it == hostFunctions_.end()) {
        LOG_WARN("MOVIECLIP",
                  "ExternalInterface.call('%s'): no host function registered — undefined",
                  name.c_str());
        return avm1::Value::undefined();
    }
    return it->second(args);
}

avm1::Value ScriptEnvironment::invokeCallback(const std::string& name,
                                                const std::vector<avm1::Value>& args) {
    auto it = callbacks_.find(name);
    if (it == callbacks_.end()) {
        LOG_WARN("MOVIECLIP", "invokeCallback('%s'): no AS2 callback registered — undefined",
                  name.c_str());
        return avm1::Value::undefined();
    }
    const Callback& cb = it->second;
    // Phase 7 known limitation (see class header doc comment): this
    // top-level scope/context has NO HostBindings attached, matching Phase
    // 4's "host-less no-op" precedent — MovieClip-affecting actions
    // (GotoFrame/Play/GetProperty/...) called directly inside the callback
    // body are recognized-but-no-op, not crashes. Ordinary computation and
    // calling other AS2 functions/objects work normally.
    avm1::Scope scope = avm1::Scope::topLevel(global_);
    avm1::ExecutionContext ctx(scope, global_);
    return avm1::Interpreter::callFunction(ctx, cb.func, cb.thisArg, args);
}

void ScriptEnvironment::scanInitActions(const Movie& movie) {
    for (const auto& tag : movie.tags) {
        if (static_cast<swf::TagCode>(tag.code) != swf::TagCode::DoInitAction) continue;
        swf::SwfReader reader = movie.tagBodyReader(tag);
        uint16_t spriteId = reader.readU16();
        if (reader.failed()) continue;
        initActionsByCharacter_[spriteId] = reader.readBytes(reader.bytesRemaining());
    }
}

const std::vector<uint8_t>* ScriptEnvironment::takeInitActionsOnce(uint16_t characterId) {
    if (initializedCharacters_.count(characterId)) return nullptr;
    auto it = initActionsByCharacter_.find(characterId);
    if (it == initActionsByCharacter_.end()) return nullptr;
    initializedCharacters_.insert(characterId);
    return &it->second;
}

void ScriptEnvironment::startDrag(MovieClipInstance* clip,
                                    const avm1::HostBindings::DragOptions& options) {
    if (!clip) return;
    dragTarget_ = clip;
    dragOptions_ = options;
    // Non-lockCenter drag preserves the offset between the clip's origin
    // and the mouse at the moment the drag started (matches real Flash:
    // the clip doesn't jump to snap its origin under the cursor unless
    // LockCenter was requested).
    dragGrabOffsetX_ = clip->x() - inputState_.mouseX();
    dragGrabOffsetY_ = clip->y() - inputState_.mouseY();
}

void ScriptEnvironment::endDrag() { dragTarget_ = nullptr; }

void ScriptEnvironment::updateDrag() {
    if (!dragTarget_) return;
    double x = inputState_.mouseX();
    double y = inputState_.mouseY();
    if (!dragOptions_.lockCenter) {
        x += dragGrabOffsetX_;
        y += dragGrabOffsetY_;
    }
    if (dragOptions_.hasConstraint) {
        double lo = std::min(dragOptions_.left, dragOptions_.right);
        double hi = std::max(dragOptions_.left, dragOptions_.right);
        x = std::clamp(x, lo, hi);
        lo = std::min(dragOptions_.top, dragOptions_.bottom);
        hi = std::max(dragOptions_.top, dragOptions_.bottom);
        y = std::clamp(y, lo, hi);
    }
    dragTarget_->setX(x);
    dragTarget_->setY(y);
}

avm1::Value ScriptEnvironment::run(MovieClipInstance& target, const uint8_t* code, size_t length) {
    // MovieClipHostBindings is defined below (this translation unit only —
    // AVM1 code only ever sees it through the abstract HostBindings seam).
    class MovieClipHostBindings : public avm1::HostBindings {
    public:
        MovieClipHostBindings(MovieClipInstance& natural, ScriptEnvironment& env)
            : natural_(&natural), current_(&natural), env_(&env) {}

        MovieClipInstance* resolve(const std::string& target) {
            if (target.empty()) return current_;
            MovieClipInstance* r = natural_->resolvePath(target);
            return r ? r : nullptr;
        }

        void gotoFrame(uint32_t frameIndex) override {
            // AVM1's GotoFrame/GotoFrame2 frame numbers are 0-based;
            // Timeline's are 1-based (see Timeline.h).
            if (current_) current_->timeline().gotoAndStop(frameIndex + 1);
        }
        void gotoLabel(const std::string& label) override {
            if (current_) current_->timeline().gotoAndStop(label);
        }
        void play() override {
            if (current_) current_->timeline().play();
        }
        void stop() override {
            if (current_) current_->timeline().stop();
        }
        void nextFrame() override {
            if (current_) current_->timeline().nextFrame();
        }
        void previousFrame() override {
            if (current_) current_->timeline().prevFrame();
        }

        avm1::Value getProperty(const std::string& targetPath, int propertyIndex) override {
            MovieClipInstance* mc = resolve(targetPath);
            if (!mc) return avm1::Value::undefined();
            switch (propertyIndex) {
                case 0: return avm1::Value::number(mc->x());
                case 1: return avm1::Value::number(mc->y());
                case 2: return avm1::Value::number(mc->xScale());
                case 3: return avm1::Value::number(mc->yScale());
                case 4: return avm1::Value::number(mc->timeline().currentFrame());
                case 5: return avm1::Value::number(mc->timeline().frameCount());
                case 6: return avm1::Value::number(mc->alpha());
                case 7: return avm1::Value::boolean(mc->visible());
                case 8: return avm1::Value::number(mc->width());   // _width
                case 9: return avm1::Value::number(mc->height());  // _height
                case 10: return avm1::Value::number(mc->rotation());
                case 11: return avm1::Value::string(mc->targetPath());
                case 12: return avm1::Value::number(mc->timeline().frameCount());  // _framesloaded
                case 13: return avm1::Value::string(mc->name());
                case 14: return avm1::Value::string("");  // _droptarget — no drag model yet
                case 15: return avm1::Value::string("");  // _url
                case 20: return avm1::Value::number(mc->stageMouseX());  // _xmouse
                case 21: return avm1::Value::number(mc->stageMouseY());  // _ymouse
                default:
                    LOG_DEBUG("MOVIECLIP", "GetProperty: index %d not modeled — undefined",
                              propertyIndex);
                    return avm1::Value::undefined();
            }
        }
        void setProperty(const std::string& targetPath, int propertyIndex,
                          const avm1::Value& value) override {
            MovieClipInstance* mc = resolve(targetPath);
            if (!mc) return;
            switch (propertyIndex) {
                case 0: mc->setX(value.toNumber()); break;
                case 1: mc->setY(value.toNumber()); break;
                case 2: mc->setXScale(value.toNumber()); break;
                case 3: mc->setYScale(value.toNumber()); break;
                case 6: mc->setAlpha(value.toNumber()); break;
                case 7: mc->setVisible(value.toBoolean()); break;
                case 10: mc->setRotation(value.toNumber()); break;
                default:
                    LOG_DEBUG("MOVIECLIP", "SetProperty: index %d not modeled/read-only — ignored",
                              propertyIndex);
                    break;
            }
        }

        void cloneSprite(const std::string& targetPath, const std::string& newName,
                          int depth) override {
            MovieClipInstance* mc = resolve(targetPath);
            if (!mc) {
                LOG_WARN("MOVIECLIP", "CloneSprite: target '%s' not found", targetPath.c_str());
                return;
            }
            mc->cloneSprite(newName, depth);
        }
        void removeSprite(const std::string& targetPath) override {
            MovieClipInstance* mc = resolve(targetPath);
            if (!mc) return;
            mc->removeFromParent();
        }
        void startDrag(const std::string& targetPath, const DragOptions& options) override {
            MovieClipInstance* mc = resolve(targetPath);
            if (!mc) return;
            env_->startDrag(mc, options);
        }
        void endDrag() override { env_->endDrag(); }

        void setTarget(const std::string& targetPath) override {
            if (targetPath.empty()) {
                current_ = natural_;
                return;
            }
            MovieClipInstance* resolved = natural_->resolvePath(targetPath);
            if (resolved) {
                current_ = resolved;
            } else {
                LOG_WARN("MOVIECLIP", "SetTarget: '%s' did not resolve — target unchanged",
                          targetPath.c_str());
            }
        }

    private:
        MovieClipInstance* natural_;
        MovieClipInstance* current_;
        ScriptEnvironment* env_;
    };

    MovieClipHostBindings host(target, *this);

    avm1::Scope scope = avm1::Scope::topLevel(global_).pushed(target.scriptObject());
    avm1::ExecutionContext ctx(scope, global_);
    ctx.thisValue = avm1::Value::object(target.scriptObject());
    ctx.host = &host;

    return avm1::Interpreter::execute(ctx, code, length);
}

// ===========================================================================
// MovieClipInstance
// ===========================================================================

MovieClipInstance::MovieClipInstance(const Movie& movie, const CharacterDictionary& characters,
                                      ScriptEnvironment& env, MovieClipInstance* parent,
                                      std::string name, int32_t depthInParent,
                                      std::unique_ptr<Timeline> timeline)
    : movie_(&movie),
      characters_(&characters),
      env_(&env),
      parent_(parent),
      name_(std::move(name)),
      depthInParent_(depthInParent),
      timeline_(std::move(timeline)) {}

std::shared_ptr<MovieClipInstance> MovieClipInstance::createRoot(
    const Movie& movie, const CharacterDictionary& characters, ScriptEnvironment& env) {
    if (!movie.valid) return nullptr;
    auto timeline = Timeline::build(movie);
    if (!timeline || timeline->frameCount() == 0) return nullptr;

    env.scanInitActions(movie);
    env.bindCharacters(characters);

    std::shared_ptr<MovieClipInstance> root(new MovieClipInstance(
        movie, characters, env, nullptr, "", 0, std::move(timeline)));
    root->wireScriptObject();
    // Sync (place) children BEFORE running frame 1's script — matches a
    // real player, which builds the frame's display list from its
    // PlaceObject tags before running that frame's DoAction, so a script
    // referencing a clip placed on the SAME frame (`mc._x = 10;`) sees it.
    root->syncChildren();
    root->runCurrentFrameScripts();
    root->runCurrentFrameSounds();
    return root;
}

// Recursion safety: this walks EXACTLY the same children_/displayList()
// structure SceneRenderer::renderClip() walks, and that structure is
// guaranteed acyclic by construction — every MovieClipInstance in the tree
// is a genuinely distinct object created once by syncChildren() (itself
// depth-guarded at creation time via childrenSyncDepth_/kMaxDepth, see the
// class header), never a shared/aliased pointer back to an ancestor. So,
// like SceneRenderer's own tree walk, this recursion is bounded by the
// tree's already-enforced maximum depth (64) and needs no separate guard
// here.
swf::Rect MovieClipInstance::computeBoundsInOwnSpace() const {
    swf::Rect result = emptyBoundsRect();
    for (const auto& [depthValue, entry] : timeline_->displayList().entries()) {
        auto childIt = children_.find(depthValue);
        if (childIt != children_.end() && childIt->second) {
            // A MovieClip child — recurse for ITS own bounds (in its own
            // local space), then transform by ITS OWN (possibly script-
            // mutated) matrix, exactly mirroring how SceneRenderer composes
            // world transforms for rendering (concatMatrix using the
            // child's localMatrix(), not the placement entry's matrix).
            swf::Rect childOwn = childIt->second->computeBoundsInOwnSpace();
            if (!isEmptyBoundsRect(childOwn)) {
                result = unionBoundsRect(
                    result, swf::transformRect(childIt->second->localMatrix(), childOwn));
            }
            continue;
        }
        // A leaf character (or an unresolved/still-unsupported one, e.g.
        // bitmap/DefineMorphShape — characters_->find() returns nullptr for
        // those, contributing nothing, matching how they already render as
        // nothing — see docs/compatibility-matrix.md).
        if (!characters_) continue;
        const CharacterDef* def = characters_->find(entry.characterId);
        if (!def) continue;
        swf::Rect leafOwn = characterOwnBoundsRect(*def, *characters_);
        if (!isEmptyBoundsRect(leafOwn)) {
            result = unionBoundsRect(result, swf::transformRect(entry.matrix, leafOwn));
        }
    }
    return result;
}

double MovieClipInstance::width() const {
    swf::Rect own = computeBoundsInOwnSpace();
    if (isEmptyBoundsRect(own)) return 0.0;
    // Real AS2 _width/_height are measured in the PARENT's coordinate
    // space — i.e. AFTER this clip's own matrix_ (including any script-set
    // _xscale/_yscale/_rotation) is applied, so rotating/scaling a clip
    // changes its reported _width/_height, matching real Flash Player
    // behavior (not independently verified against a real Flash-authored
    // file this phase — see docs/known-limitations.md's confidence note).
    return swf::transformRect(matrix_, own).widthPixels();
}

double MovieClipInstance::height() const {
    swf::Rect own = computeBoundsInOwnSpace();
    if (isEmptyBoundsRect(own)) return 0.0;
    return swf::transformRect(matrix_, own).heightPixels();
}

// --- _xmouse/_ymouse coordinate-space conversion (interactivity phase,
// 2026-08-18) ---------------------------------------------------------------
//
// Mirrors SceneRenderer::render()'s own stage-twips <-> output-viewport-
// pixels ratio (see SceneRenderer.cpp's pixelsPerTwipX/pixelsPerTwipY) —
// this IS that same non-uniform (independent X/Y), no-offset stretch-to-fill
// ratio, just inverted (input-viewport pixels -> stage pixels instead of
// stage twips -> output pixels) and expressed in pixels throughout
// (matching _xmouse/_ymouse's existing pixel-valued contract, same as _x/
// _y, not twips). Deliberately NOT a second, incompatible coordinate
// system: every movie shares this same non-flipped, non-letterboxed
// mapping, because that's what the renderer itself actually does (confirmed
// — no offset/letterboxing/pillarboxing logic exists anywhere in
// SceneRenderer.cpp), and no Y-axis flip is needed because both SWF stage Y
// and the 3DS's raw touch/device pixel Y already increase downward (same
// convention SceneRenderer's own twipsToDevice() relies on).
//
// `movie_` is the SAME top-level Movie pointer for the root AND every
// descendant MovieClipInstance (see createRoot()/syncChildren()/
// cloneSprite(), which all thread `*movie_` straight through rather than
// substituting a sprite's own definition) — so movie_->frameSize is always
// the one true stage size, no matter which instance in the tree this is
// called on.
double MovieClipInstance::stageMouseX() const {
    const InputState& input = env_->inputState();
    const double viewportWidth = input.viewportWidth();
    const double stageWidth = movie_->frameSize.widthPixels();
    if (viewportWidth <= 0.0 || stageWidth <= 0.0) {
        // No known input viewport (InputState::setViewportSize() was never
        // called — the default for every test predating this fix and for
        // the desktop CLI) -- treat the raw value as already being in
        // stage-pixel space. This is what keeps every existing caller's
        // behavior byte-for-byte unchanged (stage size == input viewport is
        // the implicit assumption when no viewport is known).
        return input.mouseX();
    }
    return input.mouseX() * (stageWidth / viewportWidth);
}

double MovieClipInstance::stageMouseY() const {
    const InputState& input = env_->inputState();
    const double viewportHeight = input.viewportHeight();
    const double stageHeight = movie_->frameSize.heightPixels();
    if (viewportHeight <= 0.0 || stageHeight <= 0.0) {
        return input.mouseY();
    }
    return input.mouseY() * (stageHeight / viewportHeight);
}

void MovieClipInstance::wireScriptObject() {
    scriptObject_ = std::make_shared<avm1::Object>();
    std::weak_ptr<MovieClipInstance> weak = shared_from_this();
    scriptObject_->nativeGet = [weak](const std::string& name, avm1::Value& out) -> bool {
        auto self = weak.lock();
        return self && self->handleNativeGet(name, out);
    };
    scriptObject_->nativeSet = [weak](const std::string& name, const avm1::Value& value) -> bool {
        auto self = weak.lock();
        return self && self->handleNativeSet(name, value);
    };
    scriptObject_->nativeEnumerate = [weak]() -> std::vector<std::string> {
        std::vector<std::string> names;
        if (auto self = weak.lock()) {
            names.reserve(self->childNameToDepth_.size());
            for (const auto& [n, depth] : self->childNameToDepth_) names.push_back(n);
        }
        return names;
    };
}

bool MovieClipInstance::handleNativeGet(const std::string& name, avm1::Value& out) const {
    if (name == "_x") { out = avm1::Value::number(x()); return true; }
    if (name == "_y") { out = avm1::Value::number(y()); return true; }
    if (name == "_xscale") { out = avm1::Value::number(xScale()); return true; }
    if (name == "_yscale") { out = avm1::Value::number(yScale()); return true; }
    if (name == "_rotation") { out = avm1::Value::number(rotation()); return true; }
    if (name == "_alpha") { out = avm1::Value::number(alpha()); return true; }
    if (name == "_visible") { out = avm1::Value::boolean(visible_); return true; }
    if (name == "_currentframe") { out = avm1::Value::number(timeline_->currentFrame()); return true; }
    if (name == "_totalframes" || name == "_framesloaded") {
        out = avm1::Value::number(timeline_->frameCount());
        return true;
    }
    if (name == "_name") { out = avm1::Value::string(name_); return true; }
    if (name == "_target") { out = avm1::Value::string(targetPath()); return true; }
    if (name == "_droptarget" || name == "_url") { out = avm1::Value::string(""); return true; }
    if (name == "_width") { out = avm1::Value::number(width()); return true; }
    if (name == "_height") { out = avm1::Value::number(height()); return true; }
    if (name == "_xmouse") { out = avm1::Value::number(stageMouseX()); return true; }
    if (name == "_ymouse") { out = avm1::Value::number(stageMouseY()); return true; }
    if (name == "_parent") {
        out = parent_ ? avm1::Value::object(parent_->scriptObject_) : avm1::Value::undefined();
        return true;
    }
    if (name == "_root") {
        auto* self = const_cast<MovieClipInstance*>(this);
        out = avm1::Value::object(self->rootInstance().scriptObject_);
        return true;
    }
    if (name == "_global") { out = avm1::Value::object(env_->globalObject()); return true; }

    // OOP-callable MovieClip methods (Phase 9, added against real hobo.swf
    // content: a preloader-gate script called `_root.getBytesLoaded()`/
    // `getBytesTotal()`/`.stop()` via CallMethod bytecode, which only the
    // bare unqualified action-code forms — stop()/play()/gotoAndStop() as
    // *statements*, dispatched through HostBindings — previously handled.
    // These reuse the exact same Timeline primitives HostBindings uses, so
    // behavior is identical whether a script says `stop();` (current
    // target, via HostBindings/SetTarget) or `someClip.stop()` (this exact
    // clip, via CallMethod). getBytesLoaded()/getBytesTotal() always
    // report the whole file as loaded — this runtime loads a movie
    // synchronously and never streams, so "loaded" is trivially "total"
    // from the very first frame.
    if (name == "stop" || name == "play" || name == "nextFrame" || name == "prevFrame" ||
        name == "gotoAndStop" || name == "gotoAndPlay" || name == "getBytesLoaded" ||
        name == "getBytesTotal") {
        auto* self = const_cast<MovieClipInstance*>(this);
        std::weak_ptr<MovieClipInstance> weakSelf = self->shared_from_this();
        std::string methodName = name;
        out = avm1::Value::object(avm1::makeNativeFunction(
            methodName,
            [weakSelf, methodName](avm1::ExecutionContext&, const avm1::Value&,
                                    const std::vector<avm1::Value>& args) -> avm1::Value {
                auto mc = weakSelf.lock();
                if (!mc) return avm1::Value::undefined();
                if (methodName == "stop") {
                    mc->timeline().stop();
                } else if (methodName == "play") {
                    mc->timeline().play();
                } else if (methodName == "nextFrame") {
                    mc->timeline().nextFrame();
                } else if (methodName == "prevFrame") {
                    mc->timeline().prevFrame();
                } else if (methodName == "gotoAndStop" || methodName == "gotoAndPlay") {
                    if (args.empty()) return avm1::Value::undefined();
                    bool isPlay = (methodName == "gotoAndPlay");
                    if (args[0].isString()) {
                        bool found = isPlay ? mc->timeline().gotoAndPlay(args[0].toString())
                                              : mc->timeline().gotoAndStop(args[0].toString());
                        if (!found) {
                            LOG_WARN("MOVIECLIP", "%s('%s'): label not found",
                                      methodName.c_str(), args[0].toString().c_str());
                        }
                    } else {
                        // AS2's gotoAndStop/gotoAndPlay take 1-based frame
                        // numbers directly — same convention as Timeline's
                        // own gotoAndStop/gotoAndPlay(uint32_t), unlike the
                        // 0-based GotoFrame *action code* (see
                        // MovieClipHostBindings::gotoFrame's +1 above).
                        uint32_t frame = static_cast<uint32_t>(std::max(0.0, args[0].toNumber()));
                        if (isPlay) {
                            mc->timeline().gotoAndPlay(frame);
                        } else {
                            mc->timeline().gotoAndStop(frame);
                        }
                    }
                } else if (methodName == "getBytesLoaded" || methodName == "getBytesTotal") {
                    // Always fully loaded — see comment above.
                    return avm1::Value::number(mc->movie_->declaredFileLength);
                }
                return avm1::Value::undefined();
            }));
        return true;
    }

    auto it = childNameToDepth_.find(name);
    if (it != childNameToDepth_.end()) {
        auto childIt = children_.find(it->second);
        if (childIt != children_.end() && childIt->second) {
            out = avm1::Value::object(childIt->second->scriptObject_);
            return true;
        }
    }
    return false;
}

bool MovieClipInstance::handleNativeSet(const std::string& name, const avm1::Value& value) {
    if (name == "_x") { setX(value.toNumber()); return true; }
    if (name == "_y") { setY(value.toNumber()); return true; }
    if (name == "_xscale") { setXScale(value.toNumber()); return true; }
    if (name == "_yscale") { setYScale(value.toNumber()); return true; }
    if (name == "_rotation") { setRotation(value.toNumber()); return true; }
    if (name == "_alpha") { setAlpha(value.toNumber()); return true; }
    if (name == "_visible") { visible_ = value.toBoolean(); return true; }
    static const char* kReadOnly[] = {
        "_currentframe", "_totalframes", "_framesloaded", "_name",  "_target",
        "_droptarget",   "_url",         "_width",        "_height", "_parent",
        "_root",         "_global",      "_xmouse",       "_ymouse",
    };
    for (const char* ro : kReadOnly) {
        if (name == ro) {
            LOG_DEBUG("MOVIECLIP", "ignoring write to read-only property '%s'", name.c_str());
            return true;
        }
    }
    return false;
}

void MovieClipInstance::initializeNewlyCreated() {
    // onClipEvent(load) fires when the instance is placed, before its own
    // DoInitAction/frame-1 scripts run (matches real Flash's documented
    // clip-event ordering: load, then the character's own init/frame
    // actions).
    runClipEvent(swf::ClipEventFlag::kLoad);
    if (const std::vector<uint8_t>* init = env_->takeInitActionsOnce(characterId_)) {
        env_->run(*this, init->data(), init->size());
    }
    // Same place-before-script ordering as createRoot() — see its comment.
    syncChildren();
    runCurrentFrameScripts();
    runCurrentFrameSounds();
}

void MovieClipInstance::runCurrentFrameScripts() {
    for (const auto& bytes : timeline_->currentFrameDoActionBodies()) {
        env_->run(*this, bytes.data(), bytes.size());
    }
}

void MovieClipInstance::runCurrentFrameSounds() {
    for (const auto& event : timeline_->currentFrameStartSoundEvents()) {
        if (event.info.syncStop) {
            env_->audioBackend().stopSound(event.soundId);
            continue;
        }
        int loopCount = event.info.hasLoops && event.info.loopCount
                             ? std::max<int>(1, *event.info.loopCount)
                             : 1;
        env_->audioBackend().playSound(event.soundId, loopCount);
    }
}

void MovieClipInstance::runClipEvent(swf::ClipEventFlag flag) {
    for (const auto& rec : clipActions_) {
        if (rec.eventFlags & static_cast<uint32_t>(flag)) {
            env_->run(*this, rec.actionBytes.data(), rec.actionBytes.size());
        }
    }
}

void MovieClipInstance::syncChildren() {
    const DisplayList& dl = timeline_->displayList();

    // 1) Remove children whose depth no longer has an entry, or whose
    // entry's characterId changed (a genuine replacement, not the same
    // instance continuing).
    for (auto it = children_.begin(); it != children_.end();) {
        const DisplayListEntry* entry = dl.find(it->first);
        bool stillValid = entry && entry->characterId == it->second->characterId_;
        if (!stillValid) {
            // A display-list-driven removal/replacement (not an explicit
            // AVM1 RemoveSprite — that path is MovieClipInstance::
            // removeFromParent(), which already does this) — still fires
            // onClipEvent(unload) and clears a stale drag target for the
            // same reason removeFromParent() does.
            it->second->runClipEvent(swf::ClipEventFlag::kUnload);
            env_->notifyRemoved(it->second.get());
            for (auto nameIt = childNameToDepth_.begin(); nameIt != childNameToDepth_.end();) {
                if (nameIt->second == it->first) nameIt = childNameToDepth_.erase(nameIt);
                else ++nameIt;
            }
            it = children_.erase(it);
        } else {
            ++it;
        }
    }

    // 2) For every depth in the current display list that resolves to a
    // Sprite character and doesn't already have a live child, create one
    // and run its initial (frame 1 / DoInitAction) scripts. See the class
    // header for why a SURVIVING child's transform is deliberately NOT
    // re-copied from the display-list entry here.
    if (childrenSyncDepth_ >= kMaxDepth) {
        LOG_WARN("MOVIECLIP",
                  "recursion depth limit (%d) exceeded while syncing children — possible cyclic "
                  "sprite reference; not creating new children this pass",
                  kMaxDepth);
        return;
    }

    for (const auto& [depthValue, entry] : dl.entries()) {
        if (children_.count(depthValue)) continue;

        const CharacterDef* def = characters_->find(entry.characterId);
        if (!def || !std::holds_alternative<SpriteDef>(*def)) continue;

        const SpriteDef& spriteDef = std::get<SpriteDef>(*def);
        auto childTimeline = Timeline::build(*movie_, spriteDef.tags);
        if (!childTimeline) continue;

        std::string childName = entry.name.value_or("");
        std::shared_ptr<MovieClipInstance> child(new MovieClipInstance(
            *movie_, *characters_, *env_, this, childName, depthValue, std::move(childTimeline)));
        child->characterId_ = entry.characterId;
        child->matrix_ = entry.matrix;
        child->colorTransform_ = entry.colorTransform;
        child->clipActions_ = entry.clipActions;
        child->childrenSyncDepth_ = childrenSyncDepth_ + 1;
        child->wireScriptObject();

        children_[depthValue] = child;
        if (!childName.empty()) childNameToDepth_[childName] = depthValue;

        child->initializeNewlyCreated();
    }
}

void MovieClipInstance::advanceFrame() {
    if (timeline_->isPlaying()) {
        timeline_->advanceOneFrame();
    }
    // Place-before-script ordering — see createRoot()'s comment.
    syncChildren();
    runClipEvent(swf::ClipEventFlag::kEnterFrame);
    runCurrentFrameScripts();
    runCurrentFrameSounds();
    // StartDrag/EndDrag reposition their target once per FULL-TREE tick —
    // only the root's own advanceFrame() call actually drives it (every
    // child's advanceFrame() below is part of the same tick, so calling
    // updateDrag() there too would just reapply the same mouse position
    // redundantly, harmlessly but wastefully).
    if (!parent_) {
        env_->updateDrag();
    }
    // Copy the child list before recursing: a script that ran just above
    // (via runCurrentFrameScripts/syncChildren) could have removed a child
    // via RemoveSprite, and a child's own advanceFrame() could in turn
    // trigger further CloneSprite/RemoveSprite calls on ITS children —
    // iterating a live map while it mutates is undefined behavior, so walk
    // a snapshot instead.
    std::vector<std::shared_ptr<MovieClipInstance>> snapshot;
    snapshot.reserve(children_.size());
    for (auto& [depthValue, child] : children_) {
        (void)depthValue;
        snapshot.push_back(child);
    }
    for (auto& child : snapshot) {
        child->advanceFrame();
    }
}

std::string MovieClipInstance::targetPath() const {
    if (!parent_) return "/";
    std::string path;
    const MovieClipInstance* cursor = this;
    std::vector<std::string> segments;
    while (cursor && cursor->parent_) {
        segments.push_back(cursor->name_);
        cursor = cursor->parent_;
    }
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
        path += "/" + *it;
    }
    return path.empty() ? "/" : path;
}

MovieClipInstance& MovieClipInstance::rootInstance() {
    MovieClipInstance* cursor = this;
    while (cursor->parent_) cursor = cursor->parent_;
    return *cursor;
}

MovieClipInstance* MovieClipInstance::resolvePath(const std::string& pathIn) {
    if (pathIn.empty()) return this;

    std::string path = pathIn;
    bool absolute = false;
    if (path[0] == '/') {
        absolute = true;
        path = path.substr(1);
    } else if (path.rfind("_root", 0) == 0 &&
               (path.size() == 5 || path[5] == '.' || path[5] == '/')) {
        absolute = true;
        path = path.size() == 5 ? "" : path.substr(6);
    }
    // Dot-syntax ("a.b.c") and slash-syntax ("a/b/c", "../c", "..") are
    // mutually exclusive in real AS2 content — legitimate dot-syntax member
    // chains never contain two consecutive dots, so treat any "/" OR ".."
    // as a sign this is already slash-syntax and leave it alone (blindly
    // replacing every '.' would mangle ".." into "//").
    if (path.find('/') == std::string::npos && path.find("..") == std::string::npos) {
        std::replace(path.begin(), path.end(), '.', '/');
    }

    MovieClipInstance* cursor = absolute ? &rootInstance() : this;
    if (path.empty()) return cursor;

    size_t start = 0;
    while (cursor && start <= path.size()) {
        size_t slash = path.find('/', start);
        std::string segment =
            path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (segment == "..") {
            cursor = cursor->parent_ ? cursor->parent_ : cursor;
        } else if (!segment.empty()) {
            auto it = cursor->childNameToDepth_.find(segment);
            if (it == cursor->childNameToDepth_.end()) return nullptr;
            auto childIt = cursor->children_.find(it->second);
            if (childIt == cursor->children_.end() || !childIt->second) return nullptr;
            cursor = childIt->second.get();
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return cursor;
}

void MovieClipInstance::setX(double px) {
    matrix_.translateXTwips = static_cast<int32_t>(std::lround(px * 20.0));
}
void MovieClipInstance::setY(double px) {
    matrix_.translateYTwips = static_cast<int32_t>(std::lround(px * 20.0));
}

double MovieClipInstance::xScale() const {
    return std::sqrt(matrix_.scaleX * matrix_.scaleX + matrix_.rotateSkew0 * matrix_.rotateSkew0) *
           100.0;
}
double MovieClipInstance::yScale() const {
    return std::sqrt(matrix_.scaleY * matrix_.scaleY + matrix_.rotateSkew1 * matrix_.rotateSkew1) *
           100.0;
}
double MovieClipInstance::rotation() const {
    return std::atan2(matrix_.rotateSkew0, matrix_.scaleX) * 180.0 / kPi;
}

void MovieClipInstance::setXScale(double percent) {
    double theta = std::atan2(matrix_.rotateSkew0, matrix_.scaleX);
    double s = percent / 100.0;
    matrix_.scaleX = s * std::cos(theta);
    matrix_.rotateSkew0 = s * std::sin(theta);
}
void MovieClipInstance::setYScale(double percent) {
    double theta = std::atan2(-matrix_.rotateSkew1, matrix_.scaleY);
    double s = percent / 100.0;
    matrix_.scaleY = s * std::cos(theta);
    matrix_.rotateSkew1 = -s * std::sin(theta);
}
void MovieClipInstance::setRotation(double degrees) {
    double theta = degrees * kPi / 180.0;
    double sx = xScale() / 100.0;
    double sy = yScale() / 100.0;
    matrix_.scaleX = sx * std::cos(theta);
    matrix_.rotateSkew0 = sx * std::sin(theta);
    matrix_.rotateSkew1 = -sy * std::sin(theta);
    matrix_.scaleY = sy * std::cos(theta);
}

std::shared_ptr<MovieClipInstance> MovieClipInstance::cloneSprite(const std::string& newName,
                                                                    int32_t depth) {
    if (!parent_) {
        LOG_WARN("MOVIECLIP", "CloneSprite: the root clip cannot be cloned");
        return nullptr;
    }
    const CharacterDef* def = characters_->find(characterId_);
    if (!def || !std::holds_alternative<SpriteDef>(*def)) {
        LOG_WARN("MOVIECLIP", "CloneSprite: source character %u is not a sprite", characterId_);
        return nullptr;
    }

    swf::PlaceObjectRecord record;
    record.version = 2;
    record.depth = depth;
    record.move = false;
    record.characterId = characterId_;
    record.matrix = matrix_;
    record.colorTransform = colorTransform_;
    record.name = newName;
    parent_->timeline_->mutableDisplayListForScripting().applyPlaceObject(record);

    // Remove any existing occupant at this depth first (a clone can
    // legitimately overwrite one, per spec).
    auto existing = parent_->children_.find(depth);
    if (existing != parent_->children_.end()) {
        for (auto nameIt = parent_->childNameToDepth_.begin();
             nameIt != parent_->childNameToDepth_.end();) {
            if (nameIt->second == depth) nameIt = parent_->childNameToDepth_.erase(nameIt);
            else ++nameIt;
        }
        parent_->children_.erase(existing);
    }

    const SpriteDef& spriteDef = std::get<SpriteDef>(*def);
    auto childTimeline = Timeline::build(*movie_, spriteDef.tags);
    if (!childTimeline) return nullptr;

    std::shared_ptr<MovieClipInstance> clone(new MovieClipInstance(
        *movie_, *characters_, *env_, parent_, newName, depth, std::move(childTimeline)));
    clone->characterId_ = characterId_;
    clone->matrix_ = matrix_;
    clone->colorTransform_ = colorTransform_;
    clone->childrenSyncDepth_ = childrenSyncDepth_;
    clone->wireScriptObject();

    parent_->children_[depth] = clone;
    if (!newName.empty()) parent_->childNameToDepth_[newName] = depth;

    clone->initializeNewlyCreated();
    return clone;
}

void MovieClipInstance::removeFromParent() {
    if (!parent_) {
        LOG_WARN("MOVIECLIP", "RemoveSprite: the root clip cannot be removed");
        return;
    }
    runClipEvent(swf::ClipEventFlag::kUnload);
    env_->notifyRemoved(this);
    parent_->timeline_->mutableDisplayListForScripting().remove(depthInParent_);
    for (auto nameIt = parent_->childNameToDepth_.begin();
         nameIt != parent_->childNameToDepth_.end();) {
        if (nameIt->second == depthInParent_) nameIt = parent_->childNameToDepth_.erase(nameIt);
        else ++nameIt;
    }
    // Erasing from children_ may destroy `this` (it's the last shared_ptr
    // reference in the common case) — do it last, and don't touch any
    // member after this line.
    parent_->children_.erase(depthInParent_);
}

}  // namespace flash3ds::runtime
