# Audio

**Status: not started.** Planned for Phase 6 (desktop backend) / Phase 10
(Nintendo 3DS backend).

Design target: `AudioManager` + `Sound`, decoupled from SWF parsing (tag
parsing must not depend on audio being initialized). Must support
`StartSound`, `stopSounds()`/`stopAllSounds()`, and `new Sound()` AS2
objects. See `docs/shift-dx-behavior.md`'s "Open items" section for what
Shift-DX RE could and couldn't confirm about its own sound-tag handling.
