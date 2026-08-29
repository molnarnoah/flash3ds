#include "runtime/MovieClipInstance.h"

#include <algorithm>
#include <cmath>
#include <variant>

#include "audio/PcmSoundDecoder.h"
#include "avm1/GlobalObject.h"
#include "avm1/HostBindings.h"
#include "avm1/Interpreter.h"
#include "avm1/Scope.h"
#include "platform/Log.h"
#include "runtime/ButtonInstance.h"
#include "runtime/CharacterBounds.h"
#include "swf/SwfLoader.h"
#include "swf/TagCode.h"

namespace flash3ds::runtime {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Registers one Key.* named constant. A small helper rather than 17
// near-identical setOwnProperty calls.
void setKeyConstant(avm1::Object& keyObj, const char* name, int code) {
    keyObj.setOwnProperty(name, avm1::Value::number(code));
}

// MovieClipHostBindings — this translation unit only (AVM1 code only ever
// sees it through the abstract HostBindings seam). Originally a class local
// to ScriptEnvironment::run() (Phase 5); promoted to file scope (event-
// dispatch phase, 2026-08-19) so ScriptEnvironment::callHandler() — the new
// entry point for invoking an already-resolved AS2 Function value (onPress/
// onRelease/onRollOver/onRollOut property handlers, condActionsV2's
// eventual `this`-vs-HostBindings-target split — see docs/events.md's
// "Execution target" section) — can build one too, without duplicating this
// whole class. Behavior is 100% unchanged from the original local version;
// only its scope moved.
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
        //
        // 2026-08-27 (task #68 -- real bug found via a real corpus file,
        // not a synthetic case): this MUST be the neutral Timeline::
        // gotoFrame(), not gotoAndStop(). The bare AVM1 ActionGotoFrame
        // this method backs is the interpreter's handler for BOTH
        // gotoAndPlay(n) (compiles to ActionGotoFrame(n) alone) and
        // gotoAndStop(n) (compiles to ActionGotoFrame(n) + a SEPARATE
        // ActionStop right after it -- see Interpreter.cpp's own Stop
        // case, which already calls host->stop() for that second action).
        // Calling gotoAndStop() here unconditionally force-stopped the
        // target timeline on EVERY bare GotoFrame, silently turning every
        // gotoAndPlay(literalFrame) in the corpus into a gotoAndStop --
        // confirmed via hobo.swf's real "preloader" sprite, whose own
        // frame-1 script is exactly `gotoAndPlay(3)` and got stuck
        // forever at frame 1 as a result (see docs/known-limitations.md).
        // ActionGotoFrame2 (the flag-based action) is unaffected either
        // way -- it always calls play()/stop() explicitly itself right
        // after repositioning (see its own Interpreter.cpp case).
        if (current_) current_->timeline().gotoFrame(frameIndex + 1);
    }
    void gotoLabel(const std::string& label) override {
        // Same reasoning as gotoFrame() just above -- ActionGotoLabel is
        // the label-based sibling of bare ActionGotoFrame, backing both
        // gotoAndPlay("label") and gotoAndStop("label") (the latter via a
        // separate trailing ActionStop), so it must be equally neutral.
        if (current_) current_->timeline().gotoFrame(label);
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
                         playSoundById(static_cast<uint16_t>(idVal.toNumber()), loopCount);
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
            "setVolume", [this](avm1::ExecutionContext&, const avm1::Value& thisVal,
                                  const std::vector<avm1::Value>& args) {
                if (thisVal.isObject() && thisVal.asObject()) {
                    double vol = args.empty() ? 100.0 : args[0].toNumber();
                    thisVal.asObject()->setOwnProperty("_volume", avm1::Value::number(vol));
                    // 2026-08-29 (docs/flash-fidelity-audit.md TASK 1,
                    // divergence #1): previously this ONLY stored the AS2-
                    // side _volume property (so getVolume() round-tripped
                    // it) and never reached the actual audio backend at
                    // all -- setVolume() had zero audible effect. Now also
                    // forwards to IAudioBackend::setVolume() when a sound
                    // is resolvably attached (_soundId set, via
                    // attachSound() or numeric-id construction), converting
                    // AS2's 0-100 percentage scale to the backend's
                    // normalized [0,1] scale.
                    avm1::Value idVal = thisVal.asObject()->getOwnProperty("_soundId");
                    if (idVal.isNumber() && audioBackend_) {
                        float normalized = static_cast<float>(
                            std::clamp(vol, 0.0, 100.0) / 100.0);
                        audioBackend_->setVolume(static_cast<uint16_t>(idVal.toNumber()),
                                                  normalized);
                    }
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

// ===========================================================================
// Event-dispatch phase (2026-08-19) — pointer-event dispatcher.
// Design: docs/events.md. See MovieClipInstance.h's doc comments on
// dispatchPointerEvents()/fireButtonKeyPressIfMatched()/notifyRemoved()/
// notifyButtonRemoved() for the full per-method contract; this block is the
// implementation.
// ===========================================================================

namespace {

// SWF spec's BUTTONCONDACTION "CondKeyPress" field uses ITS OWN small
// key-code table (1=Left, 2=Right, 3=Home, 4=End, 5=Insert, 6=Delete,
// 8=Backspace, 13=Enter, 14=Up, 15=Down, 16=PageUp, 17=PageDown, 18=Tab,
// 19=Escape, 32-126=ASCII) — a DIFFERENT, SWF-spec-legacy table from AS2's
// Key.* class constants (Key.END=35, Key.LEFT=37, ...) that InputState
// models (see InputState.h's own header comment: "Key codes follow AS2's
// Key class conventions"). Confirmed evidence-based, not assumed: a raw-
// byte probe against the real corpus (before writing this code) found
// EVERY Hobo game defines several DefineButton2 condActionsV2 records
// whose ONLY condition is a bare CondKeyPress=4 with zero mouse condition
// bits — under the SWF table that's "End"; treating 4 as a raw Key.*
// code (which would be totally different — no assigned Key.* constant at
// all) would silently make those never-fire. This helper is the one place
// that conversion happens, so InputState/Key.* stays untouched (no
// duplicate/incompatible key-code system introduced elsewhere).
int condKeyPressToInputKeyCode(uint8_t condKeyPress) {
    switch (condKeyPress) {
        case 1: return InputState::kLeft;
        case 2: return InputState::kRight;
        case 3: return InputState::kHome;
        case 4: return InputState::kEnd;
        case 5: return InputState::kInsert;
        case 6: return InputState::kDelete;
        case 8: return InputState::kBackspace;
        case 13: return InputState::kEnter;
        case 14: return InputState::kUp;
        case 15: return InputState::kDown;
        case 16: return InputState::kPageUp;
        case 17: return InputState::kPageDown;
        case 18: return InputState::kTab;
        case 19: return InputState::kEscape;
        default:
            // 32-126: ASCII — identical value in both tables, no
            // conversion needed. 0, 7, 9-12, 20-31, 127+: reserved/unused
            // in the SWF spec's table — never matches any real key.
            return (condKeyPress >= 32 && condKeyPress <= 126) ? condKeyPress : 0;
    }
}

}  // namespace

void ScriptEnvironment::notifyRemoved(MovieClipInstance* clip) {
    if (dragTarget_ == clip) dragTarget_ = nullptr;
    if (hoverClip_ == clip) hoverClip_ = nullptr;
    if (pressedClip_ == clip) pressedClip_ = nullptr;
    // See the header's doc comment: called BEFORE `clip` (and everything it
    // owns, including its buttonInstances_) is actually erased/destroyed,
    // so reading button->parent() here is still safe — this is what makes
    // clearing hoverButton_/pressedButton_ HERE (rather than trying to
    // detect the dangling pointer after the fact) correct.
    if (hoverButton_ && hoverButton_->parent() == clip) hoverButton_ = nullptr;
    if (pressedButton_ && pressedButton_->parent() == clip) pressedButton_ = nullptr;
}

void ScriptEnvironment::notifyRemovedRecursive(MovieClipInstance* clip) {
    // See this method's header doc comment (2026-08-27 crash fix) for why
    // this recursion exists. Depth-first, children/buttons before `clip`
    // itself — order doesn't matter for correctness (these are independent
    // pointer-equality checks, not destructive operations), kept this way
    // simply to clear from the leaves up.
    if (!clip) return;
    for (const auto& [depth, button] : clip->buttonInstances()) {
        (void)depth;
        if (button) notifyButtonRemoved(button.get());
    }
    for (const auto& [depth, child] : clip->children()) {
        (void)depth;
        if (child) notifyRemovedRecursive(child.get());
    }
    notifyRemoved(clip);
}

void ScriptEnvironment::fireButtonCondition(ButtonInstance& button, swf::ButtonCondition cond) {
    MovieClipInstance* parent = button.parent();
    if (!parent) return;
    auto condBit = static_cast<uint16_t>(cond);
    // Snapshot the condActionsV2 vector reference is safe to iterate
    // directly (ButtonDef is SHARED IMMUTABLE data owned by
    // CharacterDictionary — see ButtonInstance.h's class header — it is
    // never mutated or destroyed for the lifetime of the loaded movie,
    // unlike the per-placement runtime objects this dispatcher tracks).
    for (const auto& ca : button.def().condActionsV2) {
        if ((ca.conditions & condBit) == 0) continue;
        LOG_DEBUG("BUTTON2", "charId=%u condition=0x%03x actionBytes=%zu — dispatching",
                  button.characterId(), condBit, ca.actionBytes.size());
        run(*parent, ca.actionBytes.data(), ca.actionBytes.size());
    }
}

void ScriptEnvironment::fireButtonKeyPressIfMatched(ButtonInstance& button) {
    MovieClipInstance* parent = button.parent();
    if (!parent) return;
    for (const auto& ca : button.def().condActionsV2) {
        if (!ca.keyCode) continue;
        int mapped = condKeyPressToInputKeyCode(*ca.keyCode);
        if (mapped == 0 || !inputState_.isKeyPressed(mapped)) continue;
        LOG_DEBUG("BUTTON2", "charId=%u CondKeyPress=%u (mapped key %d) actionBytes=%zu — dispatching",
                  button.characterId(), *ca.keyCode, mapped, ca.actionBytes.size());
        run(*parent, ca.actionBytes.data(), ca.actionBytes.size());
    }
}

void ScriptEnvironment::firePropertyHandler(MovieClipInstance& owner, const char* name) {
    avm1::Value fn = owner.scriptObject()->getMember(name);
    if (!fn.isObject() || !fn.asObject() || !fn.asObject()->isFunction()) return;
    LOG_DEBUG("EVENT", "target=%s type=%s — dispatching AS2 property handler",
              owner.targetPath().c_str(), name);
    callHandler(owner, fn.asObject(), avm1::Value::object(owner.scriptObject()));
}

void ScriptEnvironment::firePropertyHandler(ButtonInstance& owner, const char* name) {
    MovieClipInstance* parent = owner.parent();
    if (!parent) return;
    avm1::Value fn = owner.scriptObject()->getMember(name);
    if (!fn.isObject() || !fn.asObject() || !fn.asObject()->isFunction()) return;
    LOG_DEBUG("EVENT", "target=button(charId=%u) type=%s — dispatching AS2 property handler",
              owner.characterId(), name);
    // hostBindingsTarget = the PARENT (bare gotoAndPlay()/stop()/... inside
    // the handler act on the timeline containing the button); thisVal =
    // the BUTTON's own scriptObject (real AS2 `this`-follows-the-property-
    // owner rule) — see docs/events.md's "Execution target" section.
    callHandler(*parent, fn.asObject(), avm1::Value::object(owner.scriptObject()));
}

void ScriptEnvironment::dispatchPointerEvents(MovieClipInstance& root, ButtonInstance* hitButton,
                                                MovieClipInstance* hitClip) {
    (void)root;
    const bool mouseDown = inputState_.isMouseDown();
    const bool mousePressed = inputState_.isMousePressed();
    const bool mouseReleased = inputState_.isMouseReleased();

    // --- 1) roll-over / roll-out (buttons) ---------------------------------
    if (hitButton != hoverButton_) {
        if (!pressedButton_) {
            // Plain hover transition — nothing currently pressed.
            if (hoverButton_) {
                fireButtonCondition(*hoverButton_, swf::ButtonCondition::kOverUpToIdle);
                // Re-read the member before the second dispatch call on the
                // SAME object: the condActionsV2 action just run above could
                // have removed hoverButton_'s parent clip synchronously
                // (notifyRemoved() would have nulled the member already —
                // see docs/events.md's "Event target safety" section).
                if (hoverButton_) firePropertyHandler(*hoverButton_, "onRollOut");
            }
            if (hitButton) {
                fireButtonCondition(*hitButton, swf::ButtonCondition::kIdleToOverUp);
                if (hoverButton_ != hitButton || hoverButton_ == hitButton) {
                    // hitButton is a local snapshot, not a member we clear
                    // on removal — guard via pressedButton_/hoverButton_'s
                    // own bookkeeping is not applicable here (hitButton
                    // isn't tracked yet), so this specific dispatch pair
                    // relies on fireButtonCondition/firePropertyHandler
                    // each independently re-validating `button.parent()`
                    // before touching it (both do — see their own bodies).
                }
                firePropertyHandler(*hitButton, "onRollOver");
            }
        } else {
            // A press is in progress — drag-based Over/Out transitions
            // apply ONLY to the CAPTURED button (real Flash mouse-capture
            // semantics; trackAsMenu's "pick up whichever button is under
            // the pointer" override is not implemented — see
            // MovieClipInstance.h's pressedButton_ doc comment).
            if (hitButton == pressedButton_) {
                fireButtonCondition(*pressedButton_, swf::ButtonCondition::kOutDownToOverDown);
            } else if (hoverButton_ == pressedButton_) {
                fireButtonCondition(*pressedButton_, swf::ButtonCondition::kOverDownToOutDown);
            }
        }
        hoverButton_ = hitButton;
    }

    // --- 2) press ------------------------------------------------------------
    if (mousePressed) {
        if (hitButton) {
            pressedButton_ = hitButton;
            fireButtonCondition(*hitButton, swf::ButtonCondition::kOverUpToOverDown);
            if (pressedButton_ == hitButton) firePropertyHandler(*hitButton, "onPress");
        } else {
            pressedButton_ = nullptr;
        }
    }

    // --- 3) release ------------------------------------------------------------
    if (mouseReleased) {
        if (pressedButton_) {
            ButtonInstance* captured = pressedButton_;
            if (hitButton == captured) {
                fireButtonCondition(*captured, swf::ButtonCondition::kOverDownToOverUp);
                if (pressedButton_ == captured) firePropertyHandler(*captured, "onRelease");
            } else {
                // Released while not over the button that captured the
                // press ("release outside"/cancel). Corpus evidence: zero
                // buttons in the real-game corpus define BOTH
                // OverDownToIdle and OutDownToIdle on the same button (see
                // docs/events.md), so firing only OverDownToIdle here
                // (rather than also trying to distinguish "already dragged
                // fully outside" via OutDownToIdle, which ButtonInstance's
                // 3-state UP/OVER/DOWN model — deliberately not extended,
                // see ButtonInstance.h — cannot cleanly distinguish) is a
                // documented, corpus-justified simplification, not a gap
                // discovered to matter yet.
                fireButtonCondition(*captured, swf::ButtonCondition::kOverDownToIdle);
                // No onReleaseOutside dispatch — not confirmed used by any
                // corpus content's AS2 API scan (docs/real-game-compatibility.md).
            }
        }
        pressedButton_ = nullptr;
    }

    // --- 4) plain-MovieClip AS2 property-handler dispatch (Extreme
    // Pamplona: onPress/onRelease/onRollOver/onRollOut assigned directly to
    // a MovieClip — not necessarily a Button2 character — a real, common
    // AS2 "button-mode clip" pattern). Mirrors steps 1-3 above exactly,
    // minus any condActionsV2 concept (plain clips have none). A button
    // hit always takes priority in hitTestPoint() itself, so hitClip is
    // only ever non-null when hitButton is null — these two blocks never
    // fire for the SAME tick's SAME hit. ---------------------------------
    if (hitClip != hoverClip_) {
        if (hoverClip_) firePropertyHandler(*hoverClip_, "onRollOut");
        if (hitClip) firePropertyHandler(*hitClip, "onRollOver");
        hoverClip_ = hitClip;
    }
    if (mousePressed) {
        if (hitClip) {
            pressedClip_ = hitClip;
            firePropertyHandler(*hitClip, "onPress");
        } else {
            pressedClip_ = nullptr;
        }
    }
    if (mouseReleased && pressedClip_) {
        MovieClipInstance* captured = pressedClip_;
        if (hitClip == captured) firePropertyHandler(*captured, "onRelease");
        pressedClip_ = nullptr;
    }

    (void)mouseDown;  // read via inputState_ directly by updateButtonStatesRecursive(); kept
                       // here only for the doc-comment-visible local-variable trio above.
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

const std::optional<audio::DecodedAudio>& ScriptEnvironment::ensureSoundDecoded(
    uint16_t soundId) {
    auto cached = decodedSoundCache_.find(soundId);
    if (cached != decodedSoundCache_.end()) {
        return cached->second;
    }

    // Insert-then-fill (rather than filling a local and inserting once at
    // the end) so every early-return path below shares one
    // "cache the negative result" line instead of repeating it.
    auto [it, inserted] = decodedSoundCache_.emplace(soundId, std::nullopt);
    (void)inserted;  // always true here (cached lookup above already missed)

    const CharacterDef* def = characters_ ? characters_->find(soundId) : nullptr;
    const swf::SoundDef* soundDef = def ? std::get_if<swf::SoundDef>(def) : nullptr;
    if (!soundDef) {
        return it->second;  // not a DefineSound character at all
    }
    // 2026-08-29 (docs/flash-fidelity-audit.md TASK 1, divergence #6):
    // uncompressed (formats 0/3) and ADPCM (format 1) decode added
    // alongside the existing MP3 path — see PcmSoundDecoder.h for the
    // full design/scope notes, including why Nellymoser (4/5/6) and Speex
    // (11) are explicitly still NOT handled here (an explicit user scope
    // decision, not an oversight).
    const bool isUncompressed = soundDef->format == swf::SoundFormat::kUncompressedNative ||
                                 soundDef->format == swf::SoundFormat::kUncompressedLittleEndian;
    const bool isAdpcm = soundDef->format == swf::SoundFormat::kAdpcm;
    if (soundDef->format != swf::SoundFormat::kMp3 && !isUncompressed && !isAdpcm) {
        // Nellymoser/Speex decode isn't implemented — see
        // docs/known-limitations.md L1 and PcmSoundDecoder.h's own scope
        // note for why. Logged once (here, at first reference), not once
        // per playSoundById() call.
        LOG_DEBUG("AUDIO",
                  "ScriptEnvironment: soundId=%u uses an unsupported codec (format=%d) -- no "
                  "decode, playSound() will still fire",
                  soundId, static_cast<int>(soundDef->format));
        return it->second;
    }
    if (!movie_ || soundDef->dataOffset + soundDef->dataLength > movie_->data.size()) {
        LOG_WARN("AUDIO", "ScriptEnvironment: soundId=%u has out-of-range sample data, skipping decode",
                  soundId);
        return it->second;
    }

    const uint8_t* raw = movie_->data.data() + soundDef->dataOffset;
    const double rateHz = swf::soundRateHz(soundDef->rate);
    if (soundDef->format == swf::SoundFormat::kMp3) {
        it->second = audio::decodeSwfMp3Sound(raw, soundDef->dataLength);
    } else if (isUncompressed) {
        it->second = audio::decodeSwfUncompressedSound(raw, soundDef->dataLength, rateHz,
                                                          soundDef->is16Bit, soundDef->stereo,
                                                          soundDef->sampleCount);
    } else {  // isAdpcm
        it->second = audio::decodeSwfAdpcmSound(raw, soundDef->dataLength, rateHz,
                                                  soundDef->stereo, soundDef->sampleCount);
    }
    if (!it->second) {
        LOG_WARN("AUDIO", "ScriptEnvironment: soundId=%u decode failed (format=%d)", soundId,
                  static_cast<int>(soundDef->format));
    }
    return it->second;
}

void ScriptEnvironment::playSoundById(uint16_t soundId, int loopCount, uint32_t startFrame,
                                        uint32_t endFrame) {
    // Checked BEFORE ensureSoundDecoded() (which populates the cache as a
    // side effect) so this reliably distinguishes "this is the first-ever
    // reference to soundId" from "already decoded (or already known
    // undecodable) on a previous call" — loadSound() should only ever
    // reach the backend once per distinct soundId, per playSoundById()'s
    // own doc comment; a backend like Nintendo3DSAudioBackend keeps its
    // own copy of the PCM once loaded, so re-sending it on every replay
    // would be pure waste (and, worse, would keep re-copying into 3DS
    // linear-heap memory on every trigger of a frequently-replayed sound).
    bool alreadyKnown = decodedSoundCache_.count(soundId) != 0;
    const std::optional<audio::DecodedAudio>& decoded = ensureSoundDecoded(soundId);
    if (decoded && !alreadyKnown) {
        audioBackend_->loadSound(soundId, decoded->samples.data(), decoded->samples.size(),
                                  decoded->sampleRate, decoded->channels);
    }
    audioBackend_->playSound(soundId, loopCount, startFrame, endFrame);
}

std::pair<const Movie*, const CharacterDictionary*> ScriptEnvironment::ownLoadedMovie(
    std::unique_ptr<Movie> movie, std::unique_ptr<CharacterDictionary> characters) {
    loadedMovies_.push_back(std::move(movie));
    loadedCharacterDicts_.push_back(std::move(characters));
    return {loadedMovies_.back().get(), loadedCharacterDicts_.back().get()};
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
    MovieClipHostBindings host(target, *this);

    avm1::Scope scope = avm1::Scope::topLevel(global_).pushed(target.scriptObject());
    avm1::ExecutionContext ctx(scope, global_);
    ctx.thisValue = avm1::Value::object(target.scriptObject());
    ctx.host = &host;
    ctx.callTraceSink = callTraceSink;

    return avm1::Interpreter::execute(ctx, code, length);
}

avm1::Value ScriptEnvironment::callHandler(MovieClipInstance& hostBindingsTarget,
                                              const std::shared_ptr<avm1::Object>& func,
                                              avm1::Value thisVal) {
    if (!func || !func->isFunction()) return avm1::Value::undefined();
    MovieClipHostBindings host(hostBindingsTarget, *this);
    avm1::Scope scope = avm1::Scope::topLevel(global_).pushed(hostBindingsTarget.scriptObject());
    avm1::ExecutionContext ctx(scope, global_);
    ctx.thisValue = thisVal;
    ctx.host = &host;
    ctx.callTraceSink = callTraceSink;
    // No arguments — every handler this phase dispatches (onPress/
    // onRelease/onRollOver/onRollOut) is real AS2's zero-argument form.
    return avm1::Interpreter::callFunction(ctx, func, thisVal, {});
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
    env.bindMovie(movie);

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

// --- hit-testing (interactivity phase, 2026-08-19; design: docs/hit-
// testing.md) ---------------------------------------------------------------

swf::Matrix MovieClipInstance::worldMatrix() const {
    return parent_ ? swf::concatMatrix(parent_->worldMatrix(), matrix_) : matrix_;
}

std::optional<MovieClipInstance::HitTestResult> MovieClipInstance::hitTestPoint(
    double stageXPixels, double stageYPixels) const {
    swf::Matrix inverseWorld;
    if (!swf::invertMatrix(worldMatrix(), &inverseWorld)) {
        // Degenerate world transform (e.g. this clip or an ancestor has
        // _xscale == 0) -- correctly un-hit-testable, matching real Flash
        // (see swf::invertMatrix()'s own doc comment).
        return std::nullopt;
    }
    swf::Point stagePoint{stageXPixels * 20.0, stageYPixels * 20.0};
    swf::Point localPoint = swf::transformPoint(inverseWorld, stagePoint);
    return hitTestPointInOwnSpace(localPoint);
}

std::optional<MovieClipInstance::HitTestResult> MovieClipInstance::hitTestPointInOwnSpace(
    const swf::Point& localPoint) const {
    if (!visible_) return std::nullopt;  // invisible objects never receive input

    const auto& entries = timeline_->displayList().entries();
    // Walk in REVERSE (descending) depth order -- topmost/frontmost first,
    // the opposite of SceneRenderer's ascending back-to-front PAINT order
    // -- so an overlapping higher-depth object wins the hit test, matching
    // real Flash's front-to-back hit-test order (see docs/hit-testing.md).
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        int32_t depthValue = it->first;
        const DisplayListEntry& entry = it->second;

        auto childIt = children_.find(depthValue);
        if (childIt != children_.end() && childIt->second) {
            // A MovieClip child -- recurse into ITS OWN content (not a
            // shortcut test against its own aggregate bounding box), using
            // ITS OWN (possibly script-mutated) matrix_, exactly mirroring
            // how SceneRenderer composes world transforms for rendering.
            const auto& child = childIt->second;
            swf::Matrix inverseChild;
            if (!swf::invertMatrix(child->matrix_, &inverseChild)) continue;
            swf::Point childLocal = swf::transformPoint(inverseChild, localPoint);
            if (auto hit = child->hitTestPointInOwnSpace(childLocal)) {
                return hit;
            }
            continue;
        }

        // A placed Button2/DefineButton (ButtonInstance phase, 2026-08-19)
        // -- reuse the EXACT SAME primitives the generic leaf-character
        // branch below uses (characterOwnBoundsRect/invertMatrix/
        // transformPoint/rectContainsPoint), just via ButtonInstance::
        // hitTestLocal() so the result can carry a ButtonInstance* back to
        // the caller (see docs/buttons.md). Not a second hit-testing
        // implementation -- hitTestLocal() is a thin wrapper.
        auto buttonIt = buttonInstances_.find(depthValue);
        if (buttonIt != buttonInstances_.end() && buttonIt->second) {
            if (!characters_) continue;
            if (buttonIt->second->hitTestLocal(localPoint, *characters_)) {
                return HitTestResult{const_cast<MovieClipInstance*>(this), entry.characterId,
                                      depthValue, buttonIt->second.get()};
            }
            continue;
        }

        // Not a MovieClipInstance or ButtonInstance -- a leaf character (or an unresolved/
        // still-unsupported one, e.g. bitmap/DefineMorphShape --
        // characters_->find() returns nullptr for those, contributing
        // nothing, matching how they already render as nothing and
        // contribute nothing to computeBoundsInOwnSpace()).
        if (!characters_) continue;
        const CharacterDef* def = characters_->find(entry.characterId);
        if (!def) continue;
        swf::Rect leafBounds = characterOwnBoundsRect(*def, *characters_);
        if (isEmptyBoundsRect(leafBounds)) continue;
        swf::Matrix inverseEntry;
        if (!swf::invertMatrix(entry.matrix, &inverseEntry)) continue;
        swf::Point leafLocal = swf::transformPoint(inverseEntry, localPoint);
        if (swf::rectContainsPoint(leafBounds, leafLocal)) {
            return HitTestResult{const_cast<MovieClipInstance*>(this), entry.characterId,
                                  depthValue};
        }
    }
    return std::nullopt;
}

bool MovieClipInstance::hitTestBounds(double stageXPixels, double stageYPixels) const {
    // Real MovieClip.hitTest(x, y) semantics: tests this clip's own full
    // aggregate bounding box, regardless of visible() -- deliberately NOT
    // the same visibility rule hitTestPoint() uses (see this method's own
    // header doc comment for why the two intentionally differ).
    swf::Rect ownBounds = computeBoundsInOwnSpace();
    if (isEmptyBoundsRect(ownBounds)) return false;
    swf::Rect worldBounds = swf::transformRect(worldMatrix(), ownBounds);
    swf::Point stagePoint{stageXPixels * 20.0, stageYPixels * 20.0};
    return swf::rectContainsPoint(worldBounds, stagePoint);
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

    // AS2-visible `MovieClip.hitTest(x, y)` (interactivity phase,
    // 2026-08-19 — see hitTestBounds()'s own doc comment in
    // MovieClipInstance.h for the exact semantics: aggregate-bounding-box
    // test, ignores visible()). Only the 2-argument (x, y) form is
    // implemented — real Flash's other two overloads are explicitly NOT
    // supported yet and are flagged rather than silently misbehaving:
    //   - `hitTest(x, y, shapeFlag)` with shapeFlag == true (exact vector-
    //     shape test): falls back to the same bounding-box test as the
    //     2-arg form (matches docs/hit-testing.md's own charter — "okay to
    //     use bounding-box hit testing if exact shape hit testing doesn't
    //     exist yet... architecture must allow it later," which
    //     hitTestBounds()'s doc comment confirms this does).
    //   - `hitTest(target)` (1-argument, bounding-box-vs-another-
    //     DisplayObject form): NOT implemented at all — returns false and
    //     logs a warning rather than guessing; a genuinely separate,
    //     unscoped feature (see docs/hit-testing.md's "explicitly deferred
    //     design questions").
    if (name == "hitTest") {
        auto* self = const_cast<MovieClipInstance*>(this);
        std::weak_ptr<MovieClipInstance> weakSelf = self->shared_from_this();
        out = avm1::Value::object(avm1::makeNativeFunction(
            "hitTest",
            [weakSelf](avm1::ExecutionContext&, const avm1::Value&,
                       const std::vector<avm1::Value>& args) -> avm1::Value {
                auto mc = weakSelf.lock();
                if (!mc) return avm1::Value::boolean(false);
                if (args.size() < 2) {
                    LOG_WARN("MOVIECLIP",
                              "hitTest(target) (1-argument form) is not implemented — only "
                              "hitTest(x, y[, shapeFlag]) is supported; returning false");
                    return avm1::Value::boolean(false);
                }
                double x = args[0].toNumber();
                double y = args[1].toNumber();
                return avm1::Value::boolean(mc->hitTestBounds(x, y));
            }));
        return true;
    }

    // AS2-visible dynamic-instantiation / depth-management methods (Roadmap
    // Phase 4, 2026-08-21 — see docs/known-limitations.md L4). All five
    // are thin wrappers around the primitives added alongside
    // CharacterDictionary::findByLinkageName()/attachCharacter()/
    // createEmptyChild()/swapDepthsWith()/nextHighestDepth() — see those
    // methods' own doc comments in MovieClipInstance.h for exact semantics.
    // `this` is already the clip the method was called on (CallMethod
    // dispatch resolves `someClip.foo(...)` to `someClip`'s own
    // scriptObject_ before invoking handleNativeGet on it), so — unlike
    // the bare action-code CloneSprite/RemoveSprite forms above, which
    // take an explicit target-path string and must resolve() it — none of
    // these need path resolution.
    if (name == "attachMovie" || name == "createEmptyMovieClip" ||
        name == "duplicateMovieClip" || name == "removeMovieClip" ||
        name == "swapDepths" || name == "getNextHighestDepth" || name == "loadMovie") {
        auto* self = const_cast<MovieClipInstance*>(this);
        std::weak_ptr<MovieClipInstance> weakSelf = self->shared_from_this();
        std::string methodName = name;
        out = avm1::Value::object(avm1::makeNativeFunction(
            methodName,
            [weakSelf, methodName](avm1::ExecutionContext&, const avm1::Value&,
                                    const std::vector<avm1::Value>& args) -> avm1::Value {
                auto mc = weakSelf.lock();
                if (!mc) return avm1::Value::undefined();
                if (methodName == "attachMovie") {
                    // attachMovie(idOrLinkageName: String, newName: String,
                    // depth: Number, [initObject: Object]) — real AS2's id
                    // argument is always the library symbol's *linkage*
                    // name (set in the Flash IDE's "Linkage" properties),
                    // never a raw numeric character ID, so only the
                    // String form is resolved here.
                    if (args.size() < 3 || !args[0].isString()) {
                        LOG_WARN("MOVIECLIP",
                                  "attachMovie: expected (linkageName: String, newName: "
                                  "String, depth: Number[, initObject])");
                        return avm1::Value::undefined();
                    }
                    const uint16_t* characterId =
                        mc->characters_->findByLinkageName(args[0].toString());
                    if (!characterId) {
                        LOG_WARN("MOVIECLIP",
                                  "attachMovie: no ExportAssets linkage entry for '%s'",
                                  args[0].toString().c_str());
                        return avm1::Value::undefined();
                    }
                    std::string newName = args[1].toString();
                    int32_t depth = static_cast<int32_t>(args[2].toNumber());
                    std::shared_ptr<avm1::Object> initObject =
                        (args.size() > 3 && args[3].isObject()) ? args[3].asObject() : nullptr;
                    auto child = mc->attachCharacter(*characterId, newName, depth, initObject);
                    return child ? avm1::Value::object(child->scriptObject_)
                                  : avm1::Value::undefined();
                } else if (methodName == "createEmptyMovieClip") {
                    // createEmptyMovieClip(newName: String, depth: Number)
                    if (args.size() < 2) {
                        LOG_WARN("MOVIECLIP",
                                  "createEmptyMovieClip: expected (newName: String, depth: "
                                  "Number)");
                        return avm1::Value::undefined();
                    }
                    auto child = mc->createEmptyChild(args[0].toString(),
                                                        static_cast<int32_t>(args[1].toNumber()));
                    return child ? avm1::Value::object(child->scriptObject_)
                                  : avm1::Value::undefined();
                } else if (methodName == "duplicateMovieClip") {
                    // duplicateMovieClip(newName: String, depth: Number) —
                    // real AS2's third (initObject) argument is not
                    // supported by cloneSprite() (which just clones `this`
                    // at a new depth, matching CloneSprite's own action-
                    // code semantics) — same limitation either call form
                    // has.
                    if (args.size() < 2) {
                        LOG_WARN("MOVIECLIP",
                                  "duplicateMovieClip: expected (newName: String, depth: "
                                  "Number)");
                        return avm1::Value::undefined();
                    }
                    auto child = mc->cloneSprite(args[0].toString(),
                                                   static_cast<int32_t>(args[1].toNumber()));
                    return child ? avm1::Value::object(child->scriptObject_)
                                  : avm1::Value::undefined();
                } else if (methodName == "removeMovieClip") {
                    mc->removeFromParent();
                } else if (methodName == "swapDepths") {
                    // swapDepths(target: Number | MovieClip). Real AS2 also
                    // accepts another MovieClip instance (swaps with ITS
                    // current depth) — supported here by reading the
                    // target object's own _x-style native "depth" via its
                    // depthInParent_ is not reachable through avm1::Object,
                    // so only the Number-depth form is implemented; passing
                    // an object logs and no-ops rather than guessing.
                    if (args.empty() || !args[0].isNumber()) {
                        LOG_WARN("MOVIECLIP",
                                  "swapDepths(MovieClip) (object-target form) is not "
                                  "implemented — only swapDepths(depth: Number) is "
                                  "supported");
                        return avm1::Value::undefined();
                    }
                    mc->swapDepthsWith(static_cast<int32_t>(args[0].toNumber()));
                } else if (methodName == "getNextHighestDepth") {
                    return avm1::Value::number(mc->nextHighestDepth());
                } else if (methodName == "loadMovie") {
                    // loadMovie(url: String) — see loadMovie()'s own doc
                    // comment in MovieClipInstance.h for exactly what this
                    // does and doesn't cover (the target-clip-replacement
                    // form only, no `_level` sibling-movie loading).
                    if (args.empty() || !args[0].isString()) {
                        LOG_WARN("MOVIECLIP", "loadMovie: expected (url: String)");
                        return avm1::Value::boolean(false);
                    }
                    return avm1::Value::boolean(mc->loadMovie(args[0].toString()));
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
        // A named placed Button2/DefineButton (ButtonInstance phase,
        // 2026-08-19) -- establishes AS2 object identity only (a bare
        // object with no properties/methods wired — see ButtonInstance.h's
        // "AS2 OBJECT IDENTITY" section for exactly what this does and
        // does NOT provide yet). `_root.someButtonName` now resolves to a
        // real, distinct object; `_root.someButtonName._x` does not (the
        // object has no nativeGet hook), and resolvePath()/target-path
        // traversal (SetTarget, tellTarget, CallMethod on a path) still
        // can't reach a button at all, since resolvePath() only walks
        // children_ (MovieClipInstance objects) — documented as the exact
        // missing bridge in docs/buttons.md rather than worked around here.
        auto buttonIt = buttonInstances_.find(it->second);
        if (buttonIt != buttonInstances_.end() && buttonIt->second) {
            out = avm1::Value::object(buttonIt->second->scriptObject());
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
        // 2026-08-29 (docs/flash-fidelity-audit.md TASK 1, divergence #4):
        // SyncNoMultiple ("don't restart this sound if it's already
        // playing") was parsed into SoundInfo but never consulted — a
        // StartSound flagged this way would still re-trigger every time
        // its frame was re-entered (e.g. a looping timeline re-visiting a
        // frame with a background-loop StartSound on it), causing
        // overlapping/duplicate playback real Flash Player would have
        // suppressed. IAudioBackend::isPlaying() (default false, so a
        // backend that never tracks playback state — NullAudioBackend,
        // any test double that doesn't override it — just always lets the
        // event through unchanged, same as before this fix) is the query
        // that makes this checkable at all.
        if (event.info.syncNoMultiple && env_->audioBackend().isPlaying(event.soundId)) {
            continue;
        }
        int loopCount = event.info.hasLoops && event.info.loopCount
                             ? std::max<int>(1, *event.info.loopCount)
                             : 1;
        // 2026-08-29 (docs/flash-fidelity-audit.md TASK 1, divergence #7):
        // forward SOUNDINFO InPoint/OutPoint through — real corpus
        // evidence found InPoint set on ~35% of all StartSound triggers
        // (see docs/audio.md), so this is NOT a rare edge case.
        uint32_t startFrame = event.info.hasInPoint && event.info.inPointSamples
                                    ? *event.info.inPointSamples
                                    : 0;
        uint32_t endFrame = event.info.hasOutPoint && event.info.outPointSamples
                                  ? *event.info.outPointSamples
                                  : audio::IAudioBackend::kPlayToEnd;
        env_->playSoundById(event.soundId, loopCount, startFrame, endFrame);
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
            env_->notifyRemovedRecursive(it->second.get());
            for (auto nameIt = childNameToDepth_.begin(); nameIt != childNameToDepth_.end();) {
                if (nameIt->second == it->first) nameIt = childNameToDepth_.erase(nameIt);
                else ++nameIt;
            }
            it = children_.erase(it);
        } else {
            ++it;
        }
    }

    // 1b) Remove button instances whose depth no longer has a valid entry,
    // or whose entry's characterId changed (ButtonInstance phase,
    // 2026-08-19) — mirrors the children_ removal loop above exactly
    // (same display-list-driven lifetime mechanism, reused rather than
    // duplicated — see docs/buttons.md's "Instance lifetime" section).
    // Buttons have no onClipEvent/drag-target concept, so this is simpler
    // than the children_ loop: no runClipEvent(kUnload) / notifyRemoved()
    // call is needed.
    for (auto it = buttonInstances_.begin(); it != buttonInstances_.end();) {
        const DisplayListEntry* entry = dl.find(it->first);
        bool stillValid = entry && entry->characterId == it->second->characterId();
        if (!stillValid) {
            // Event-dispatch phase (2026-08-19): this display-list-driven
            // removal path (distinct from removeFromParent()'s explicit
            // RemoveSprite path, which goes through notifyRemoved() for the
            // OWNING clip) can also drop a button that's currently tracked
            // as hoverButton_/pressedButton_ — null those out BEFORE erase,
            // same "notify before destroy" pattern notifyRemoved() uses.
            env_->notifyButtonRemoved(it->second.get());
            for (auto nameIt = childNameToDepth_.begin(); nameIt != childNameToDepth_.end();) {
                if (nameIt->second == it->first) nameIt = childNameToDepth_.erase(nameIt);
                else ++nameIt;
            }
            it = buttonInstances_.erase(it);
        } else {
            ++it;
        }
    }

    // 2) For every depth in the current display list that resolves to a
    // Sprite character and doesn't already have a live child, create one
    // and run its initial (frame 1 / DoInitAction) scripts. See the class
    // header for why a SURVIVING child's transform is deliberately NOT
    // re-copied from the display-list entry here. As of the ButtonInstance
    // phase (2026-08-19), a depth resolving to a ButtonDef instead gets a
    // new ButtonInstance the same way (see the second branch below) — the
    // exact same "don't already have one -> create it" loop, just
    // branching by which CharacterDef alternative is at this depth.
    if (childrenSyncDepth_ >= kMaxDepth) {
        LOG_WARN("MOVIECLIP",
                  "recursion depth limit (%d) exceeded while syncing children — possible cyclic "
                  "sprite reference; not creating new children this pass",
                  kMaxDepth);
        return;
    }

    for (const auto& [depthValue, entry] : dl.entries()) {
        if (children_.count(depthValue) || buttonInstances_.count(depthValue)) continue;

        const CharacterDef* def = characters_->find(entry.characterId);
        if (!def) continue;

        if (std::holds_alternative<SpriteDef>(*def)) {
            const SpriteDef& spriteDef = std::get<SpriteDef>(*def);
            auto childTimeline = Timeline::build(*movie_, spriteDef.tags);
            if (!childTimeline) continue;

            std::string childName = entry.name.value_or("");
            std::shared_ptr<MovieClipInstance> child(
                new MovieClipInstance(*movie_, *characters_, *env_, this, childName, depthValue,
                                       std::move(childTimeline)));
            child->characterId_ = entry.characterId;
            child->matrix_ = entry.matrix;
            child->colorTransform_ = entry.colorTransform;
            child->clipActions_ = entry.clipActions;
            child->childrenSyncDepth_ = childrenSyncDepth_ + 1;
            child->wireScriptObject();

            children_[depthValue] = child;
            if (!childName.empty()) childNameToDepth_[childName] = depthValue;

            child->initializeNewlyCreated();
            continue;
        }

        if (const auto* buttonDef = std::get_if<swf::ButtonDef>(def)) {
            // ButtonInstance phase (2026-08-19): give this placement a
            // real runtime instance instead of leaving it as a bare
            // DisplayListEntry SceneRenderer draws directly (see
            // ButtonInstance.h's class header). No onClipEvent(load)/
            // DoInitAction equivalent runs for buttons — real Flash
            // buttons don't have their own timeline/frame scripts, and
            // this phase doesn't dispatch condActionsV1/condActionsV2
            // (see ButtonInstance.h's "SCOPE OF THIS PHASE").
            std::string buttonName = entry.name.value_or("");
            auto button = std::make_shared<ButtonInstance>(*buttonDef, entry.characterId, this,
                                                              depthValue, buttonName);
            button->setLocalMatrix(entry.matrix);
            button->setColorTransform(entry.colorTransform);
            button->wireScriptObject();

            buttonInstances_[depthValue] = button;
            if (!buttonName.empty()) childNameToDepth_[buttonName] = depthValue;
            continue;
        }
    }
}

void MovieClipInstance::updateButtonStatesRecursive(ButtonInstance* hitButton, bool mouseDown) {
    for (auto& [depthValue, button] : buttonInstances_) {
        (void)depthValue;
        if (button) button->updateState(button.get() == hitButton, mouseDown);
    }
    for (auto& [depthValue, child] : children_) {
        (void)depthValue;
        if (child) child->updateButtonStatesRecursive(hitButton, mouseDown);
    }
}

void MovieClipInstance::dispatchButtonKeyPressesRecursive() {
    // Snapshot before dispatching — mirrors the EXISTING children_
    // snapshot idiom advanceFrame() already uses just below this method
    // (see its own comment: a fired condActionsV2 action block can run
    // arbitrary AVM1, including RemoveSprite/CloneSprite/removeMovieClip,
    // which can mutate buttonInstances_/children_ out from under a live
    // iteration — walking a snapshot instead makes that safe). Snapshotting
    // shared_ptrs (not raw pointers) also keeps each snapshotted object
    // alive for the duration of this call even if its owning map entry is
    // erased mid-dispatch (see docs/events.md's "Event target safety").
    std::vector<std::shared_ptr<ButtonInstance>> buttonSnapshot;
    buttonSnapshot.reserve(buttonInstances_.size());
    for (auto& [depthValue, button] : buttonInstances_) {
        (void)depthValue;
        if (button) buttonSnapshot.push_back(button);
    }
    for (auto& button : buttonSnapshot) {
        env_->fireButtonKeyPressIfMatched(*button);
    }

    std::vector<std::shared_ptr<MovieClipInstance>> childSnapshot;
    childSnapshot.reserve(children_.size());
    for (auto& [depthValue, child] : children_) {
        (void)depthValue;
        if (child) childSnapshot.push_back(child);
    }
    for (auto& child : childSnapshot) {
        child->dispatchButtonKeyPressesRecursive();
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
        // Button UP/OVER/DOWN state driver (ButtonInstance phase,
        // 2026-08-19) -- same "root-only, once per full-tree tick"
        // precedent as updateDrag() just above. ONE hitTestPoint() call
        // from the root already recurses through the ENTIRE tree
        // (children_ + buttonInstances_ at every level -- see
        // hitTestPointInOwnSpace()), so this finds the single topmost
        // button anywhere in the tree the mouse/touch point currently
        // hits (or none) in one pass, then propagates isOver/mouseDown to
        // every ButtonInstance in the tree. Does NOT dispatch any
        // ActionScript event -- see ButtonInstance.h's "SCOPE OF THIS
        // PHASE".
        auto hit = hitTestPoint(stageMouseX(), stageMouseY());
        ButtonInstance* hitButton = (hit && hit->button) ? hit->button : nullptr;
        // A button hit always takes priority (see hitTestPoint()'s own
        // contract, restated in HitTestResult's doc comment) — hitClip
        // (for plain-MovieClip onPress/onRelease/onRollOver/onRollOut
        // dispatch) is only set when nothing hit a button.
        MovieClipInstance* hitClip = (!hitButton && hit) ? hit->clip : nullptr;
        updateButtonStatesRecursive(hitButton, env_->inputState().isMouseDown());
        // Event-dispatch phase (2026-08-19, docs/events.md): drive the
        // actual AS2/condActionsV2 dispatch from the same single root-only
        // hit-test result updateButtonStatesRecursive() already used above
        // — no second hit-test implementation. Root-only + once-per-tick
        // for the same reason updateDrag()/updateButtonStatesRecursive()
        // are: exactly one deterministic evaluation of the whole tree.
        env_->dispatchPointerEvents(*this, hitButton, hitClip);
        dispatchButtonKeyPressesRecursive();
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
    env_->notifyRemovedRecursive(this);
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

std::shared_ptr<MovieClipInstance> MovieClipInstance::attachCharacter(
    uint16_t characterId, const std::string& newName, int32_t depth,
    const std::shared_ptr<avm1::Object>& initObject) {
    const CharacterDef* def = characters_->find(characterId);
    if (!def || !std::holds_alternative<SpriteDef>(*def)) {
        LOG_WARN("MOVIECLIP", "attachMovie: character %u is not a sprite/MovieClip symbol",
                  characterId);
        return nullptr;
    }

    swf::PlaceObjectRecord record;
    record.version = 2;
    record.depth = depth;
    record.move = false;
    record.characterId = characterId;
    // A freshly-attached clip starts at the identity transform (real
    // Flash's attachMovie() places the new clip at (0,0), full scale, no
    // rotation — the caller repositions it afterward via _x/_y/etc, same
    // as this project's own tests would expect) — deliberately NOT
    // inheriting `this`'s own matrix/colorTransform, unlike cloneSprite()
    // (which clones a SIBLING of itself and so keeps its transform).
    timeline_->mutableDisplayListForScripting().applyPlaceObject(record);

    auto existing = children_.find(depth);
    if (existing != children_.end()) {
        for (auto nameIt = childNameToDepth_.begin(); nameIt != childNameToDepth_.end();) {
            if (nameIt->second == depth) nameIt = childNameToDepth_.erase(nameIt);
            else ++nameIt;
        }
        children_.erase(existing);
    }

    const SpriteDef& spriteDef = std::get<SpriteDef>(*def);
    auto childTimeline = Timeline::build(*movie_, spriteDef.tags);
    if (!childTimeline) return nullptr;

    std::shared_ptr<MovieClipInstance> child(new MovieClipInstance(
        *movie_, *characters_, *env_, this, newName, depth, std::move(childTimeline)));
    child->characterId_ = characterId;
    child->wireScriptObject();

    children_[depth] = child;
    if (!newName.empty()) childNameToDepth_[newName] = depth;

    // initObject's OWN (non-inherited) properties are copied onto the new
    // clip BEFORE initializeNewlyCreated() runs its first frame — matches
    // real Flash's documented attachMovie(..., initObject) timing (the
    // init values are visible to the clip's own onClipEvent(load)/frame-1
    // scripts, not applied after the fact).
    if (initObject) {
        for (const auto& [propName, propValue] : initObject->ownProperties()) {
            child->scriptObject_->setMember(propName, propValue);
        }
    }

    child->initializeNewlyCreated();
    return child;
}

std::shared_ptr<MovieClipInstance> MovieClipInstance::createEmptyChild(const std::string& newName,
                                                                          int32_t depth) {
    swf::PlaceObjectRecord record;
    record.version = 2;
    record.depth = depth;
    record.move = false;
    record.characterId = 0;  // no backing character — see this method's own doc comment
    timeline_->mutableDisplayListForScripting().applyPlaceObject(record);

    auto existing = children_.find(depth);
    if (existing != children_.end()) {
        for (auto nameIt = childNameToDepth_.begin(); nameIt != childNameToDepth_.end();) {
            if (nameIt->second == depth) nameIt = childNameToDepth_.erase(nameIt);
            else ++nameIt;
        }
        children_.erase(existing);
    }

    auto emptyTimeline = Timeline::build(*movie_, {});
    if (!emptyTimeline) return nullptr;

    std::shared_ptr<MovieClipInstance> child(new MovieClipInstance(
        *movie_, *characters_, *env_, this, newName, depth, std::move(emptyTimeline)));
    // characterId_ stays 0 (default) — deliberately: there is no character
    // to resolve for an empty clip. See createEmptyChild()'s own header
    // comment for why this is safe (SceneRenderer never looks a
    // MovieClipInstance's own characterId up; only its display list).
    child->wireScriptObject();

    children_[depth] = child;
    if (!newName.empty()) childNameToDepth_[newName] = depth;

    child->initializeNewlyCreated();
    return child;
}

void MovieClipInstance::swapDepthsWith(int32_t otherDepth) {
    if (!parent_) {
        LOG_WARN("MOVIECLIP", "swapDepths: the root clip has no parent display list to swap within");
        return;
    }
    if (otherDepth == depthInParent_) return;  // no-op, matches real Flash

    // depthInParent_ gets overwritten below once `this` is moved to
    // otherDepth, so the old depth (where any displaced occupant must be
    // re-placed) has to be captured now.
    const int32_t oldDepth = depthInParent_;

    DisplayList& dl = parent_->timeline_->mutableDisplayListForScripting();
    const DisplayListEntry* myEntry = dl.find(depthInParent_);
    if (!myEntry) {
        LOG_WARN("MOVIECLIP", "swapDepths: this clip has no display-list entry at depth %d",
                  depthInParent_);
        return;
    }
    swf::PlaceObjectRecord myRecord;
    myRecord.version = 2;
    myRecord.move = false;
    myRecord.characterId = myEntry->characterId;
    myRecord.matrix = myEntry->matrix;
    myRecord.colorTransform = myEntry->colorTransform;
    myRecord.name = name_;

    // Capture the target depth's occupant (if any) BEFORE either
    // PlaceObject call below mutates the display list out from under it.
    const DisplayListEntry* otherEntryPtr = dl.find(otherDepth);
    std::optional<swf::PlaceObjectRecord> otherRecord;
    auto otherChildIt = parent_->children_.find(otherDepth);
    std::shared_ptr<MovieClipInstance> otherChild =
        otherChildIt != parent_->children_.end() ? otherChildIt->second : nullptr;
    std::string otherName = otherChild ? otherChild->name_ : std::string();
    if (otherEntryPtr) {
        swf::PlaceObjectRecord rec;
        rec.version = 2;
        rec.move = false;
        rec.characterId = otherEntryPtr->characterId;
        rec.matrix = otherEntryPtr->matrix;
        rec.colorTransform = otherEntryPtr->colorTransform;
        rec.name = otherName;
        otherRecord = rec;
    }

    // Move `this` to otherDepth, and (if occupied) move the previous
    // occupant to `this`'s old depth — real Flash's documented swap
    // semantics ("the current occupant is moved to this clip's old
    // depth"), not a simple erase.
    dl.remove(depthInParent_);
    if (otherChild) parent_->children_.erase(otherDepth);
    for (auto nameIt = parent_->childNameToDepth_.begin();
         nameIt != parent_->childNameToDepth_.end();) {
        if (nameIt->second == depthInParent_ || nameIt->second == otherDepth) {
            nameIt = parent_->childNameToDepth_.erase(nameIt);
        } else {
            ++nameIt;
        }
    }

    myRecord.depth = otherDepth;
    dl.applyPlaceObject(myRecord);
    parent_->children_[otherDepth] = shared_from_this();
    if (!name_.empty()) parent_->childNameToDepth_[name_] = otherDepth;
    depthInParent_ = otherDepth;

    // Re-place the displaced occupant (if any) at `this`'s vacated OLD
    // depth — the symmetric half of the swap that placing `this` at
    // otherDepth (just above) already performed for `this` itself.
    if (otherRecord) {
        otherRecord->depth = oldDepth;
        dl.applyPlaceObject(*otherRecord);
    }
    if (otherChild) {
        parent_->children_[oldDepth] = otherChild;
        otherChild->depthInParent_ = oldDepth;
        if (!otherName.empty()) parent_->childNameToDepth_[otherName] = oldDepth;
    }
}

int32_t MovieClipInstance::nextHighestDepth() const {
    const auto& entries = timeline_->displayList().entries();
    if (entries.empty()) return 0;
    int32_t maxDepth = entries.begin()->first;
    for (const auto& [depthValue, entry] : entries) {
        (void)entry;
        maxDepth = std::max(maxDepth, depthValue);
    }
    return maxDepth + 1;
}

bool MovieClipInstance::loadMovie(const std::string& url) {
    std::optional<std::vector<uint8_t>> bytes = env_->fileLoader().loadFile(url);
    if (!bytes) {
        LOG_WARN("MOVIECLIP", "loadMovie('%s'): fetch failed — target clip left unchanged",
                  url.c_str());
        return false;
    }

    std::unique_ptr<Movie> newMovie = swf::SwfLoader::loadSwf(bytes->data(), bytes->size());
    if (!newMovie || !newMovie->valid) {
        LOG_WARN("MOVIECLIP",
                  "loadMovie('%s'): fetched bytes did not parse as a valid SWF — target clip "
                  "left unchanged",
                  url.c_str());
        return false;
    }

    auto newCharacters = std::make_unique<CharacterDictionary>(CharacterDictionary::build(*newMovie));
    auto newTimeline = Timeline::build(*newMovie);
    if (!newTimeline || newTimeline->frameCount() == 0) {
        LOG_WARN("MOVIECLIP",
                  "loadMovie('%s'): parsed SWF has no usable timeline — target clip left "
                  "unchanged",
                  url.c_str());
        return false;
    }

    // Everything above can still fail cleanly with `this` untouched. From
    // here on, the load has succeeded and the target clip's existing
    // content is torn down for real — mirrors syncChildren()'s own
    // removal-loop teardown (onClipEvent(unload) + notifyRemoved() per
    // child, notifyButtonRemoved() per button) rather than just discarding
    // the maps, so nothing lingers as a dangling drag/hover/press target.
    for (auto& [depthValue, child] : children_) {
        (void)depthValue;
        if (!child) continue;
        child->runClipEvent(swf::ClipEventFlag::kUnload);
        env_->notifyRemovedRecursive(child.get());
    }
    for (auto& [depthValue, button] : buttonInstances_) {
        (void)depthValue;
        if (button) env_->notifyButtonRemoved(button.get());
    }
    children_.clear();
    buttonInstances_.clear();
    childNameToDepth_.clear();

    // Own the freshly-parsed Movie/CharacterDictionary for the rest of
    // this ScriptEnvironment's lifetime (see ownLoadedMovie()'s own doc
    // comment for why), then rebind `this` clip's non-owning movie_/
    // characters_ pointers to them and swap in the new Timeline. `this`'s
    // own depthInParent_/name_/parent_/matrix_/colorTransform_ are
    // deliberately left untouched — matches real Flash's documented
    // "loaded content replaces the target clip's own content, keeping its
    // depth/position/name" behavior.
    auto [ownedMovie, ownedCharacters] =
        env_->ownLoadedMovie(std::move(newMovie), std::move(newCharacters));
    movie_ = ownedMovie;
    characters_ = ownedCharacters;
    timeline_ = std::move(newTimeline);

    // See this method's own header comment for why DoInitAction bodies ARE
    // (re)scanned for the new movie's characters, but the env-wide Sound
    // movie_/characters_ binding is deliberately NOT rebound here.
    env_->scanInitActions(*movie_);

    // Place-before-script ordering, same as createRoot()/advanceFrame().
    syncChildren();
    runCurrentFrameScripts();
    runCurrentFrameSounds();
    return true;
}

}  // namespace flash3ds::runtime
