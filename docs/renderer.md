# Renderer

**Status: not started.** Planned for Phase 3 (basic shape/sprite renderer)
and Phase 10 (Nintendo 3DS backend, dual-screen).

Design target: an abstract `IRenderer` with `DesktopRenderer` (OpenGL, for
development/testing) and `Nintendo3DSRenderer` implementations, operating
on a retained display-list model built by Phase 2. See
`docs/architecture.md` sections 14–15 for the target shape/sprite/text
rendering surface and the `Screen::Top` / `Screen::Bottom` dual-screen
architecture.
