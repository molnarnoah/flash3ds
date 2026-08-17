# ExternalInterface

**Status: not started.** Planned for Phase 7.

Design target (see project spec section 10 and
`docs/shift-dx-behavior.md`'s "ExternalInterface" section for the Shift-DX
behavioral cross-check): an abstract `HostInterface` boundary so
`ExternalInterface.call()` / `.addCallback()` / `.removeCallback()` never
couple directly to Nintendo 3DS code.

```cpp
class HostInterface {
public:
    virtual ~HostInterface() = default;
    virtual Value externalCall(const std::string& name,
                                const std::vector<Value>& args) = 0;
    virtual void registerCallback(const std::string& name,
                                   Callback callback) = 0;
};
```

To be filled in once Phase 4/5 (AVM1 VM + object model) land, since
ExternalInterface is implemented in terms of the AS object model.
