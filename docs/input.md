# Input

**Status: not started.** Planned for Phase 6 (desktop backend) / Phase 10
(Nintendo 3DS backend).

Design target: `InputManager` with abstract keyboard/mouse/button/touch
events, exposing `Key.isDown()` to AVM1 without the VM knowing about
Nintendo 3DS hardware. `docs/shift-dx-behavior.md`'s "Input mapping"
section confirms Shift-DX routed 3DS touch through the same code path as
Flash "mouse" events — the same design this project targets.
