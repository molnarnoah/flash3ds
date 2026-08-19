// nintendo3ds_main.cpp
//
// Phase 10 — Nintendo 3DS entry point / dual-screen test app. Ties together
// SwfLoader, CharacterDictionary, MovieClipInstance/ScriptEnvironment,
// SceneRenderer, Nintendo3DSRenderer, Nintendo3DSInput, and
// Nintendo3DSAudioBackend into a runnable app loop on real 3DS hardware (or
// a compatible emulator) — confirmed booting and running in Azahar (a
// Citra-based 3DS emulator) by the user in this session.
//
// This app now drives BOTH screens every frame and doubles as a hardware
// input/audio smoke test:
//   - TOP screen: the embedded demo SWF, rendered exactly as before.
//   - BOTTOM screen: a live button/circle-pad/touch visualization (see
//     drawButtonTestScreen() below) — every mapped input lights up on
//     screen the instant it's pressed/moved/touched, independent of
//     whatever the AS2 side of InputState is doing with the same raw hid
//     state.
//   - A/B/X/Y each trigger a short, distinctly-pitched synthesized tone via
//     Nintendo3DSAudioBackend::playTestTone() (see that class — diagnostic
//     only, unrelated to SWF sound playback) — a real, audible test of the
//     ndsp audio pipeline, independent of the still-missing SWF codec
//     decode step.
//   - Quit is START+SELECT held together (not START alone, so START's own
//     indicator on the bottom screen is actually visible/testable).
//
// Virtual Console resource layer (2026-08-19): this app no longer plays a
// single EMBEDDED demo movie. It loads "config.ini" and (whatever
// config.ini's [game] swf= names, "game.swf" by default) from this
// project's own embedded RomFS section — see platform/Nintendo3DSRomfs.h
// for how that's read WITHOUT libctru's own romfsInit() (excluded from
// this from-source toolchain build, same sys/iosupport.h reason
// archive_dev.c is — see docs/3ds-toolchain.md), vc/GamePackage.h for how
// the fetched bytes become a runnable Movie, and docs/virtual-console.md
// for the full design/RomFS layout/config.ini syntax. Swapping which SWF
// plays no longer needs a C++ change or recompile — see that doc's
// "Replacing game.swf" section — only repackaging the .3dsx's RomFS
// section (CMakeLists.txt's FLASH3DS_BUILD_3DS block does this
// automatically from the checked-in romfs/ directory on every build).
//
// This file is only compiled for the 3DS target (guarded by __3DS__).

#ifndef __3DS__
#error "nintendo3ds_main.cpp is only valid in a 3DS cross-compile (__3DS__ not defined)"
#endif

#include <3ds.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "audio/Nintendo3DSAudioBackend.h"
#include "platform/Log.h"
#include "platform/Nintendo3DSInput.h"
#include "platform/Nintendo3DSRomfs.h"
#include "renderer/IRenderer.h"
#include "renderer/Nintendo3DSRenderer.h"
#include "renderer/SceneRenderer.h"
#include "renderer/ShapeTessellator.h"
#include "runtime/CharacterDictionary.h"
#include "runtime/MovieClipInstance.h"
#include "swf/SwfRecords.h"
#include "vc/GamePackage.h"

using flash3ds::Log;
using flash3ds::LogLevel;
using flash3ds::audio::Nintendo3DSAudioBackend;
using flash3ds::platform::Nintendo3DSInput;
using flash3ds::platform::Nintendo3DSRomfs;
using flash3ds::renderer::IRenderer;
using flash3ds::renderer::Nintendo3DSRenderer;
using flash3ds::renderer::PointTwips;
using flash3ds::renderer::SceneRenderer;
using flash3ds::runtime::CharacterDictionary;
using flash3ds::runtime::MovieClipInstance;
using flash3ds::runtime::ScriptEnvironment;
using flash3ds::swf::RgbaColor;
using flash3ds::vc::GamePackage;

namespace {

// --- small drawing helpers for the button-test screen -----------------
// (Not part of any reusable renderer API — this is test-app-only
// scaffolding, deliberately kept local to this file. Both draw directly in
// device pixel space, matching IRenderer::fillPolygon/strokePolyline's own
// documented contract.)

void drawFilledRect(IRenderer& r, int x, int y, int w, int h, RgbaColor color) {
    std::vector<PointTwips> pts = {
        {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h},
    };
    r.fillPolygon(pts, color);
}

void drawRectOutline(IRenderer& r, int x, int y, int w, int h, RgbaColor color) {
    std::vector<PointTwips> pts = {
        {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, {x, y},
    };
    r.strokePolyline(pts, color, 1);
}

// One entry in the button layout table below.
struct ButtonBox {
    u32 keyMask;
    int x, y, w, h;
};

// Bottom-screen (320x240 logical) button layout. Positions approximate the
// real 3DS's physical button layout (D-Pad left, Circle Pad below it,
// Y/X/A/B diamond right, L/R along the top edge, Start/Select bottom
// center) so the on-screen test picture reads naturally next to the
// physical console -- not derived from any spec, just a reasonable-effort
// diagnostic layout.
const ButtonBox kButtonBoxes[] = {
    // D-Pad (raw D-Pad bits only, NOT the merged KEY_LEFT/etc aliases --
    // see drawButtonTestScreen()'s comment for why that distinction
    // matters here specifically).
    {KEY_DUP, 46, 40, 28, 28},
    {KEY_DDOWN, 46, 96, 28, 28},
    {KEY_DLEFT, 18, 68, 28, 28},
    {KEY_DRIGHT, 74, 68, 28, 28},
    // Y / X / A / B diamond (real physical layout: Y top, X left, A right,
    // B bottom).
    {KEY_Y, 246, 40, 28, 28},
    {KEY_X, 218, 68, 28, 28},
    {KEY_A, 274, 68, 28, 28},
    {KEY_B, 246, 96, 28, 28},
    // Shoulder buttons.
    {KEY_L, 8, 8, 50, 18},
    {KEY_R, 262, 8, 50, 18},
    // Start / Select.
    {KEY_SELECT, 120, 210, 34, 18},
    {KEY_START, 166, 210, 34, 18},
};

constexpr RgbaColor kBgColor{20, 20, 30, 255};
constexpr RgbaColor kBoxUnpressed{60, 60, 70, 255};
constexpr RgbaColor kBoxPressed{80, 230, 120, 255};
constexpr RgbaColor kOutline{140, 140, 160, 255};
constexpr RgbaColor kCirclePadDot{80, 200, 255, 255};
constexpr RgbaColor kTouchDot{255, 200, 60, 255};

// Draws the full button/circle-pad/touch test picture for this frame onto
// `renderer`. `held` is this frame's hidKeysHeld() snapshot.
void drawButtonTestScreen(IRenderer& renderer, u32 held) {
    renderer.beginFrame(kBgColor);

    for (const ButtonBox& box : kButtonBoxes) {
        const bool pressed = (held & box.keyMask) != 0;
        drawFilledRect(renderer, box.x, box.y, box.w, box.h,
                        pressed ? kBoxPressed : kBoxUnpressed);
        drawRectOutline(renderer, box.x, box.y, box.w, box.h, kOutline);
    }

    // Circle Pad: bounding box + a dot offset from center by the raw
    // analog reading (hidCircleRead's dx/dy range is documented as roughly
    // -156..+156 at full deflection; scaled down to fit the box radius).
    constexpr int kPadCenterX = 60;
    constexpr int kPadCenterY = 170;
    constexpr int kPadRadius = 30;
    drawRectOutline(renderer, kPadCenterX - kPadRadius, kPadCenterY - kPadRadius,
                     kPadRadius * 2, kPadRadius * 2, kOutline);
    circlePosition pad;
    hidCircleRead(&pad);
    const int dotX = kPadCenterX + (static_cast<int>(pad.dx) * (kPadRadius - 6)) / 156;
    const int dotY = kPadCenterY - (static_cast<int>(pad.dy) * (kPadRadius - 6)) / 156;
    drawFilledRect(renderer, dotX - 4, dotY - 4, 8, 8, kCirclePadDot);

    // Touch: libctru's hidTouchRead() reads (0,0) by convention when the
    // panel isn't being touched (see docs/input.md's Nintendo3DSInput
    // section for the same open question about KEY_TOUCH's reliability) --
    // used here purely as a "is it being touched right now" heuristic for
    // this diagnostic screen, independent of Nintendo3DSInput's own
    // KEY_TOUCH-gated logic.
    touchPosition touch;
    hidTouchRead(&touch);
    if (touch.px != 0 || touch.py != 0) {
        drawFilledRect(renderer, static_cast<int>(touch.px) - 5, static_cast<int>(touch.py) - 5,
                        10, 10, kTouchDot);
    }

    renderer.endFrame();
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;

    // NOTE: no consoleInit()/consoleDebugInit() call here -- libctru's
    // console.c is one of the files this from-source toolchain build
    // deliberately excludes (it transitively needs sys/iosupport.h's full
    // device-table framework, which isn't available with a stock, non-
    // devkitARM-patched newlib -- see docs/3ds-toolchain.md). This app has
    // no text-console output on real hardware; [3DS]-category LOG_* calls
    // still work (Log's default sink is stderr, which is harmless -- just
    // unobserved -- with no console wired up).
    gfxInitDefault();

    // Top screen: 400x240 logical pixels (standard 3DS top-screen
    // resolution; the wide/800px mode is not used here). Bottom screen:
    // 320x240 logical pixels (standard 3DS bottom-screen resolution).
    constexpr int kTopWidth = 400;
    constexpr int kTopHeight = 240;
    constexpr int kBottomWidth = 320;
    constexpr int kBottomHeight = 240;

    // --- Virtual Console resource layer: RomFS -> GamePackage -----------
    // See Nintendo3DSRomfs.h for why this ISN'T libctru's own romfsInit(),
    // and docs/virtual-console.md for the full design. `romfs` must
    // outlive `movie` below (a real RomFS-loaded movie owns no bytes of
    // its own -- Movie::data is a plain std::vector filled from the fetch
    // callback's own copy -- but `romfs`'s FSFILE handle is only needed
    // during the fetch itself, so this ordering is a "keep it simple, one
    // clear owner per resource" choice, not a strict lifetime requirement).
    Nintendo3DSRomfs romfs;
    if (!romfs.open(argv[0])) {
        LOG_ERROR("3DS", "Failed to open this app's own embedded RomFS section (see the "
                          "Nintendo3DSRomfs::open log line above for the specific reason) -- was "
                          "this .3dsx built with --romfs=... ? (see CMakeLists.txt)");
        gfxExit();
        return 1;
    }

    GamePackage package = flash3ds::vc::buildGamePackage(
        [&romfs](const std::string& name, std::vector<uint8_t>& outBytes) {
            return romfs.readFile(name, outBytes);
        });

    // "Invalid SWF: produce a clear runtime error" / "Missing game.swf:
    // produce a clear runtime error" (docs/virtual-console.md) -- both
    // cases are already surfaced as a specific, human-readable
    // movie->errorMessage by buildGamePackage()/SwfLoader (see
    // vc/GamePackage.cpp), so this check and its log line stay generic on
    // purpose; the SPECIFIC reason is always in errorMessage, not
    // hardcoded here.
    auto& movie = package.movie;
    if (!movie || !movie->valid) {
        LOG_ERROR("3DS", "Could not load '%s': %s", package.config.swfFilename.c_str(),
                   movie ? movie->errorMessage.c_str() : "(no Movie produced)");
        gfxExit();
        return 1;
    }

    CharacterDictionary characters = CharacterDictionary::build(*movie);
    ScriptEnvironment env;

    Nintendo3DSAudioBackend audioBackend;
    env.setAudioBackend(&audioBackend);

    // The touch digitizer is only a physical thing on the BOTTOM screen on
    // real 3DS hardware -- config.ini's [touch] screen= selects which
    // LOGICAL screen dimensions Nintendo3DSInput rescales raw touch
    // coordinates into (see vc::InputMapping, GameConfig.h), defaulting to
    // "bottom" (320x240), the hardware-accurate choice. This also fixes a
    // pre-existing Phase 10 quirk documented in Nintendo3DSInput.h's own
    // header comment: the embedded-demo app used to always pass the TOP
    // screen's dimensions here regardless of which screen was actually
    // being touched -- now genuinely configurable instead of hardcoded.
    const int touchScreenWidth = package.config.input.touchUsesBottomScreen ? kBottomWidth : kTopWidth;
    const int touchScreenHeight =
        package.config.input.touchUsesBottomScreen ? kBottomHeight : kTopHeight;
    Nintendo3DSInput input(touchScreenWidth, touchScreenHeight, package.config.input);

    auto root = MovieClipInstance::createRoot(*movie, characters, env);
    if (!root) {
        LOG_ERROR("3DS", "'%s' parsed but produced no root MovieClip (no frames?)",
                   package.config.swfFilename.c_str());
        gfxExit();
        return 1;
    }

    Nintendo3DSRenderer topRenderer(kTopWidth, kTopHeight, GFX_TOP);
    Nintendo3DSRenderer bottomRenderer(kBottomWidth, kBottomHeight, GFX_BOTTOM);
    SceneRenderer scene(*movie, characters);

    // Frame-rate pacing: advance the loaded movie's own timeline at
    // whatever frame rate ITS OWN header declares (falling back to 12fps
    // only if that field is somehow zero), regardless of the 3DS's ~60Hz
    // vblank, so playback speed matches what the SWF actually specifies
    // rather than running 5x too fast. gspWaitForVBlank() is still called
    // every iteration (throttles the render/input loop to vsync, avoiding
    // a busy-spin), it just doesn't always advance the movie frame. The
    // bottom-screen button test picture is redrawn every real frame
    // regardless (input should feel immediate, unlike movie playback).
    const double swfFrameRate = movie->frameRateFps() > 0.0 ? movie->frameRateFps() : 12.0;
    constexpr double kVBlankHz = 60.0;
    int vblanksPerSwfFrame = std::max(1, static_cast<int>(std::lround(kVBlankHz / swfFrameRate)));
    int vblankCounter = 0;

    while (aptMainLoop()) {
        hidScanInput();
        input.poll(env.inputState());

        const u32 kHeld = hidKeysHeld();
        const u32 kDown = hidKeysDown();

        // START alone no longer quits -- it needs to be visible/testable
        // on the bottom-screen button picture. START+SELECT held together
        // is the standard homebrew "quit" convention instead.
        if ((kHeld & KEY_START) && (kHeld & KEY_SELECT)) {
            break;
        }

        // Sound test: A/B/X/Y each trigger a short, distinctly-pitched
        // synthesized tone via the diagnostic playTestTone() path (see
        // Nintendo3DSAudioBackend's header) -- a real, audible test of the
        // ndsp pipeline, independent of the still-missing SWF codec
        // decode. Triggered on the DOWN edge (hidKeysDown()), not held, so
        // holding a button doesn't retrigger every frame.
        if (kDown & KEY_A) audioBackend.playTestTone(440.0, 0.15);   // A4
        if (kDown & KEY_B) audioBackend.playTestTone(330.0, 0.15);   // E4
        if (kDown & KEY_X) audioBackend.playTestTone(554.0, 0.15);   // C#5
        if (kDown & KEY_Y) audioBackend.playTestTone(659.0, 0.15);   // E5

        if (++vblankCounter >= vblanksPerSwfFrame) {
            vblankCounter = 0;
            root->advanceFrame();
        }

        scene.render(*root, topRenderer, kTopWidth, kTopHeight);
        drawButtonTestScreen(bottomRenderer, kHeld);
        // Present BOTH screens together -- see Nintendo3DSRenderer::
        // presentFrame()'s comment for why this must be called exactly
        // once per real frame rather than once per renderer.
        Nintendo3DSRenderer::presentFrame();

        gspWaitForVBlank();
    }

    audioBackend.stopAllSounds();
    gfxExit();
    return 0;
}
