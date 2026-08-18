// HostBindings.h
//
// The seam between the AVM1 interpreter (this directory) and whatever
// "current target" a script is running against — a MovieClip's Timeline
// and DisplayList, in the real runtime. Phase 4 deliberately keeps the
// interpreter working in isolation (see docs/avm1-support.md /
// docs/architecture.md's Phase 4 scope note): every method here is a no-op
// default. Phase 5 (MovieClip API) will provide a real implementation that
// an ExecutionContext can point at, wiring GotoFrame/Play/Stop/GetProperty/
// etc. into an actual Timeline.
//
// Deliberately virtual (not a template/std::function bag): the set of
// timeline/clip operations AVM1 bytecode can invoke is fixed and small, and
// a real implementation will need to hold onto renderer/runtime state
// (Timeline&, DisplayList&, ...) that's awkward to thread through
// individual std::function captures one at a time.

#pragma once

#include <cstdint>
#include <string>

#include "avm1/Value.h"

namespace flash3ds::avm1 {

class HostBindings {
public:
    virtual ~HostBindings() = default;

    // Timeline control (GotoFrame/GotoFrame2/GotoLabel/Play/Stop/NextFrame/
    // PreviousFrame actions).
    virtual void gotoFrame(uint32_t frameIndex) { (void)frameIndex; }
    virtual void gotoLabel(const std::string& label) { (void)label; }
    virtual void play() {}
    virtual void stop() {}
    virtual void nextFrame() {}
    virtual void previousFrame() {}

    // MovieClip property access (GetProperty/SetProperty actions — property
    // index per the SWF spec's fixed 0-21 property list: _x, _y, _xscale,
    // _yscale, _currentframe, _totalframes, _alpha, _visible, _width,
    // _height, _rotation, _target, _framesloaded, _name, _droptarget,
    // _url, _highquality, _focusrect, _soundbuftime, _quality, _xmouse,
    // _ymouse). `target` is the action's Target operand (an AS2 target
    // path, e.g. "/mc1/mc2" or ""); an empty string means "the current
    // target" (whatever SetTarget last pointed at, or the clip whose
    // script is running if SetTarget was never called).
    virtual Value getProperty(const std::string& target, int propertyIndex) {
        (void)target;
        (void)propertyIndex;
        return Value::undefined();
    }
    virtual void setProperty(const std::string& target, int propertyIndex, const Value& value) {
        (void)target;
        (void)propertyIndex;
        (void)value;
    }

    // Sprite/clip lifecycle (CloneSprite/RemoveSprite/StartDrag/EndDrag).
    virtual void cloneSprite(const std::string& target, const std::string& newName, int depth) {
        (void)target;
        (void)newName;
        (void)depth;
    }
    virtual void removeSprite(const std::string& target) { (void)target; }
    virtual void startDrag(const std::string& target) { (void)target; }
    virtual void endDrag() {}

    // Target path (SetTarget/SetTarget2 actions — changes which MovieClip
    // subsequent variable/property references resolve against).
    virtual void setTarget(const std::string& targetPath) { (void)targetPath; }
};

}  // namespace flash3ds::avm1
