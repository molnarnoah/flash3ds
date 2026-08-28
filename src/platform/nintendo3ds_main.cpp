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
//   - TOP screen: the loaded game's SWF, rendered exactly as before.
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
// Virtual Console resource layer (2026-08-19, ported forward into this tree
// 2026-08-24 — see CLAUDE.md's "Virtual Console layer" section for the full
// provenance note): this app no longer plays a single EMBEDDED demo movie.
// It loads "config.ini" and (whatever config.ini's [game] swf= names,
// "game.swf" by default) from this project's own embedded RomFS section —
// see platform/Nintendo3DSRomfs.h for how that's read WITHOUT libctru's own
// romfsInit() (excluded from this from-source toolchain build, same sys/
// iosupport.h reason archive_dev.c is — see docs/3ds-toolchain.md),
// vc/GamePackage.h for how the fetched bytes become a runnable Movie, and
// docs/virtual-console.md for the full design/RomFS layout/config.ini
// syntax. Swapping which SWF plays no longer needs a C++ change or
// recompile — see that doc's "Replacing game.swf" section — only
// repackaging the .3dsx's RomFS section (CMakeLists.txt's
// FLASH3DS_BUILD_3DS block does this automatically from the checked-in
// romfs/ directory on every build). EmbeddedDemoSwf.h is no longer used by
// this file but is left in the tree (harmless — see
// tools/gen_3ds_demo_swf.py's --swf-out mode, which now generates
// romfs/game.swf instead of a C++ header).
//
// This file is only compiled for the 3DS target (guarded by __3DS__).

#ifndef __3DS__
#error "nintendo3ds_main.cpp is only valid in a 3DS cross-compile (__3DS__ not defined)"
#endif

#include <3ds.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "audio/Nintendo3DSAudioBackend.h"
#include "platform/Log.h"
#include "platform/MemoryDiagnostics.h"
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

// Per-phase frame-timing diagnostic (2026-08-28, "resolve the 7-12 FPS
// pacing" task -- see docs/performance-pacing.md for the full writeup).
// Averaged over a window of real loop iterations, drawn as horizontal bars
// in the bottom screen's unused middle column (x=106..214 -- none of
// kButtonBoxes/the circle pad/the touch dot touch that region) rather than
// printed as numbers, same "no font rendering available in this project"
// constraint showFatalErrorScreen()'s counted-squares technique above
// already works around. The same numbers also go to LOG_INFO once per
// window for anyone who *can* see a debug log viewer.
//
// Bars are drawn as a PROPORTION of the real measured period (not an
// absolute ms scale) -- a first version used an absolute px/ms scale
// capped at 20ms, which on the very first real recording immediately
// saturated BOTH the period bar (expected -- 7-12fps is 83-142ms, way
// past any fixed low cap) AND the renderTop bar at the same time, so it
// was impossible to tell whether renderTop was 20ms or 120ms -- i.e.
// whether it explained ALL of the missing time or just some of it. A
// period-relative bar has no such cap: each phase bar's length is
// literally "what fraction of one real frame this phase consumed," so a
// bar reaching the same length as the (always-full-width) period
// reference means that phase alone accounts for essentially the entire
// frame.
struct PhaseTimingWindow {
    double inputMs = 0.0;
    double advanceMs = 0.0;
    double renderTopMs = 0.0;
    // Sub-split of renderTopMs (2026-08-28, see Nintendo3DSRenderer::
    // lastRasterMs()/lastBlitMs()'s doc comment for exactly what each
    // covers): renderTopRasterMs is SoftwareRenderer's CPU scanline-fill
    // work (every fillPolygon()/strokePolyline() call during the tree
    // walk), renderTopBlitMs is Nintendo3DSRenderer::endFrame()'s
    // per-pixel copy into the real LCD framebuffer. Whatever's left of
    // renderTopMs after subtracting both is the tree-walk/character-
    // resolution/tessellation-lookup cost itself.
    double renderTopRasterMs = 0.0;
    double renderTopBlitMs = 0.0;
    double renderBottomMs = 0.0;
    double presentMs = 0.0;
    double vblankWaitMs = 0.0;
    double periodMs = 0.0;  // real wall-clock time between successive loop tops
    int samples = 0;
    int advanceSamples = 0;  // advanceFrame() isn't called every iteration
};

constexpr RgbaColor kBarInput{120, 120, 255, 255};
constexpr RgbaColor kBarAdvance{255, 120, 120, 255};
constexpr RgbaColor kBarRenderTop{255, 200, 60, 255};       // tree walk / char resolution (post-raster/blit subtraction)
constexpr RgbaColor kBarRenderTopRaster{255, 140, 0, 255};  // SoftwareRenderer fill/stroke
constexpr RgbaColor kBarRenderTopBlit{200, 80, 255, 255};   // endFrame() pixel blit to LCD fb
constexpr RgbaColor kBarRenderBottom{255, 160, 200, 255};
constexpr RgbaColor kBarPresent{120, 255, 180, 255};
constexpr RgbaColor kBarWait{100, 100, 110, 255};
constexpr RgbaColor kBarPeriodRef{255, 255, 255, 255};

// Draws one averaged timing window as horizontal bars (in the order the
// phases run in the main loop below -- renderTop is now split into three:
// tree-walk, raster, blit, per the 2026-08-28 sub-phase instrumentation
// added after the tessellation-cache fix alone showed no on-device
// improvement, see docs/performance-pacing.md), each scaled as a FRACTION
// of the real measured period (kRefPx wide = 100% of one real frame) --
// plus a final row: the period reference itself, always drawn as a
// full-width white OUTLINE (not filled -- it's the ruler, not a
// measurement) so every other bar's length is directly comparable to "the
// whole frame" at a glance.
void drawPhaseTimingBars(IRenderer& renderer, const PhaseTimingWindow& w) {
    if (w.samples == 0 || w.periodMs <= 0.0) return;
    constexpr int kBarX = 106;
    constexpr int kBarH = 22;
    constexpr int kBarGap = 3;
    constexpr int kRefPx = 100;  // width representing 100% of one real frame

    const double periodMs = w.periodMs / w.samples;
    const double renderTopRaster = w.renderTopRasterMs / w.samples;
    const double renderTopBlit = w.renderTopBlitMs / w.samples;
    double renderTopOther = (w.renderTopMs / w.samples) - renderTopRaster - renderTopBlit;
    if (renderTopOther < 0.0) renderTopOther = 0.0;  // TickCounter overhead/rounding, not a real negative cost

    struct Row {
        double ms;
        RgbaColor color;
    };
    const Row rows[] = {
        {w.inputMs / w.samples, kBarInput},
        {w.advanceSamples > 0 ? w.advanceMs / w.advanceSamples : 0.0, kBarAdvance},
        {renderTopOther, kBarRenderTop},
        {renderTopRaster, kBarRenderTopRaster},
        {renderTopBlit, kBarRenderTopBlit},
        {w.renderBottomMs / w.samples, kBarRenderBottom},
        {w.presentMs / w.samples, kBarPresent},
        {w.vblankWaitMs / w.samples, kBarWait},
    };

    int y = 8;
    for (const Row& row : rows) {
        int px = static_cast<int>((row.ms / periodMs) * kRefPx);
        if (px > kRefPx) px = kRefPx;  // a phase can't exceed the period it's part of
        if (px < 1) px = 1;
        drawFilledRect(renderer, kBarX, y, px, kBarH, row.color);
        drawRectOutline(renderer, kBarX, y, kRefPx, kBarH, kOutline);
        y += kBarH + kBarGap;
    }
    // The reference row: always full-width, outline only, brighter white --
    // "this is what 100% of one real frame looks like."
    drawRectOutline(renderer, kBarX, y, kRefPx, kBarH, kBarPeriodRef);
    drawRectOutline(renderer, kBarX + 1, y + 1, kRefPx - 2, kBarH - 2, kBarPeriodRef);
}

// Draws the full button/circle-pad/touch test picture for this frame onto
// `renderer`. `held` is this frame's hidKeysHeld() snapshot. `timing` is
// the most recently completed averaging window (samples==0 until the
// first window closes, in which case nothing is drawn for it yet).
void drawButtonTestScreen(IRenderer& renderer, u32 held, const PhaseTimingWindow& timing) {
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

    drawPhaseTimingBars(renderer, timing);

    renderer.endFrame();
}

// Which of showFatalErrorScreen()'s callers is reporting -- see that
// function's own comment for why this needs to be visually countable
// rather than just logged. Numbered in the order main() checks them.
enum class FatalError {
    kRomfsOpenFailed = 1,
    kInvalidMovie = 2,
    kNoRootMovieClip = 3,
};

// Shows a solid-color error screen on both LCDs, with `errorCode` small
// white squares drawn in the top screen's top-left corner, and blocks
// until START+SELECT is held, instead of the app just vanishing.
//
// Added 2026-08-19 while diagnosing a "loads in Azahar, then freezes and
// quits" report: every early-failure path here used to just call
// gfxExit()/return after an invisible LOG_ERROR (see Log.cpp's
// svcOutputDebugString addition, same date, for the other half of this
// fix) -- so a config/RomFS/SWF failure and an actual crash were
// indistinguishable to whoever was watching the screen. A solid red
// screen that STAYS UP (rather than a black screen that vanishes) is a
// clear, low-effort signal that this is a handled error, not a hang --
// distinguishing "the app is telling you something's wrong" from "the app
// died" is the whole point. The specific reason always still goes to
// LOG_ERROR (visible via svcOutputDebugString in an emulator's own Log
// Viewer, e.g. Citra/Azahar's Debug menu) -- but that's not reachable from
// every platform testers might use (e.g. iOS emulators typically expose
// no such log view), so the square count is a second, universally-visible
// encoding of WHICH check failed, needing no font rendering (none is
// available in this project -- see docs/architecture.md) or log access at
// all: just count the squares and report the number back.
// `subCode` (0 = none) draws a SECOND row of squares on the BOTTOM screen
// -- used for a sub-reason within one top-level FatalError category (e.g.
// which of Nintendo3DSRomfs::OpenFailure's ~10 checks actually failed
// inside a kRomfsOpenFailed). Kept on a different screen than the
// top-level `error` count specifically so the two numbers can never be
// misread as one combined count.
void showFatalErrorScreen(IRenderer& top, IRenderer& bottom, FatalError error, int subCode = 0) {
    constexpr RgbaColor kErrorColor{200, 30, 30, 255};
    constexpr RgbaColor kMarkerColor{255, 255, 255, 255};
    const int markerCount = static_cast<int>(error);

    while (aptMainLoop()) {
        hidScanInput();
        const u32 kHeld = hidKeysHeld();
        if ((kHeld & KEY_START) && (kHeld & KEY_SELECT)) {
            break;
        }

        top.beginFrame(kErrorColor);
        for (int i = 0; i < markerCount; ++i) {
            drawFilledRect(top, 12 + i * 24, 12, 16, 16, kMarkerColor);
        }
        top.endFrame();
        bottom.beginFrame(kErrorColor);
        for (int i = 0; i < subCode; ++i) {
            drawFilledRect(bottom, 12 + i * 24, 12, 16, 16, kMarkerColor);
        }
        bottom.endFrame();
        Nintendo3DSRenderer::presentFrame();

        gspWaitForVBlank();
    }
}

// Routes every Log::log() call through svcOutputDebugString as well as
// whatever setSink() (stderr, by default) points at -- see Log.h's
// setDebugCallback() doc comment for why this is needed at all on this
// target specifically. svcOutputDebugString is an ordinary public libctru
// SVC call, needs no filesystem/console setup, and Citra/Azahar's own Log
// Viewer displays it; real hardware silently ignores it if nothing is
// attached to observe it, so registering this is harmless either way.
void logToDebugSvc(flash3ds::LogLevel level, const char* category, const char* message) {
    (void)level;
    char buf[560];
    std::snprintf(buf, sizeof(buf), "[%s] %s", category, message);
    svcOutputDebugString(buf, static_cast<s32>(std::strlen(buf)));
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;

    flash3ds::Log::setDebugCallback(logToDebugSvc);

    // NOTE: no consoleInit()/consoleDebugInit() call here -- libctru's
    // console.c is one of the files this from-source toolchain build
    // deliberately excludes (it transitively needs sys/iosupport.h's full
    // device-table framework, which isn't available with a stock, non-
    // devkitARM-patched newlib -- see docs/3ds-toolchain.md). This app has
    // no text-console output on real hardware; [3DS]-category LOG_* calls
    // still work (Log's default sink is stderr, which is harmless -- just
    // unobserved -- with no console wired up).
    gfxInitDefault();

    // M2 RAM-validation phase (2026-08-24): hold L at boot to enable
    // MemoryDiagnostics checkpoint logging for this run (see
    // MemoryDiagnostics.h -- disabled by default so a normal run pays zero
    // extra cost/log spam; no persistent config file exists yet -- that's a
    // later, separate roadmap phase -- so a boot-held-button toggle is the
    // smallest mechanism that actually works today). LOG_* output has no
    // on-screen console (see this function's own note above), so this is
    // only observable via whatever captures stderr/svcOutputDebugString
    // (e.g. an emulator's log console).
    hidScanInput();
    if (hidKeysHeld() & KEY_L) {
        flash3ds::platform::setEnabled(true);
        flash3ds::platform::resetPeak();
    }
    flash3ds::platform::checkpoint("startup (gfxInitDefault done)");

    // Top screen: 400x240 logical pixels (standard 3DS top-screen
    // resolution; the wide/800px mode is not used here). Bottom screen:
    // 320x240 logical pixels (standard 3DS bottom-screen resolution).
    constexpr int kTopWidth = 400;
    constexpr int kTopHeight = 240;
    constexpr int kBottomWidth = 320;
    constexpr int kBottomHeight = 240;

    // Constructed up front (before any RomFS/config/SWF work below) purely
    // so showFatalErrorScreen() has a screen to draw on for every failure
    // path, including the very first one -- these are cheap to build
    // (SoftwareRenderer allocation only, see Nintendo3DSRenderer.h) and
    // were already unconditionally constructed later in this same
    // function on the success path, just moved up.
    Nintendo3DSRenderer topRenderer(kTopWidth, kTopHeight, GFX_TOP);
    Nintendo3DSRenderer bottomRenderer(kBottomWidth, kBottomHeight, GFX_BOTTOM);

    // --- Virtual Console resource layer: RomFS -> GamePackage -----------
    // See Nintendo3DSRomfs.h for why this ISN'T libctru's own romfsInit(),
    // and docs/virtual-console.md for the full design. `romfs` must
    // outlive `movie` below (a real RomFS-loaded movie owns no bytes of
    // its own -- Movie::data is a plain std::vector filled from the fetch
    // callback's own copy -- but `romfs`'s FSFILE handle is only needed
    // during the fetch itself, so this ordering is a "keep it simple, one
    // clear owner per resource" choice, not a strict lifetime requirement).
    Nintendo3DSRomfs romfs;
    Nintendo3DSRomfs::OpenFailure romfsFailure = Nintendo3DSRomfs::OpenFailure::kNone;
    if (!romfs.open(argv[0], &romfsFailure)) {
        LOG_ERROR("3DS", "Failed to open this app's own embedded RomFS section (see the "
                          "Nintendo3DSRomfs::open log line above for the specific reason) -- was "
                          "this .3dsx built with --romfs=... ? (see CMakeLists.txt) -- "
                          "OpenFailure code=%d", static_cast<int>(romfsFailure));
        showFatalErrorScreen(topRenderer, bottomRenderer, FatalError::kRomfsOpenFailed,
                              static_cast<int>(romfsFailure));
        gfxExit();
        return 1;
    }
    flash3ds::platform::checkpoint("after RomFS open");

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
        showFatalErrorScreen(topRenderer, bottomRenderer, FatalError::kInvalidMovie);
        gfxExit();
        return 1;
    }
    flash3ds::platform::checkpoint("after SWF load (movie parsed + valid)");

    CharacterDictionary characters = CharacterDictionary::build(*movie);
    flash3ds::platform::checkpoint("after CharacterDictionary::build");
    ScriptEnvironment env;

    Nintendo3DSAudioBackend audioBackend;
    env.setAudioBackend(&audioBackend);
    flash3ds::platform::checkpoint("after audio backend initialization");

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
        showFatalErrorScreen(topRenderer, bottomRenderer, FatalError::kNoRootMovieClip);
        gfxExit();
        return 1;
    }
    flash3ds::platform::checkpoint("after MovieClipInstance::createRoot (root clip built)");

    SceneRenderer scene(*movie, characters);
    bool loggedFirstFrame = false;
    bool loggedFirstRender = false;

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
    LOG_INFO("PERF", "swfFrameRate=%.2f fps -> vblanksPerSwfFrame=%d (target movie-tick rate %.2f fps)",
             swfFrameRate, vblanksPerSwfFrame, kVBlankHz / vblanksPerSwfFrame);

    // Per-phase timing (see PhaseTimingWindow's own comment above) --
    // `loopTimer` measures the REAL wall-clock period between successive
    // loop tops (ground truth: how long one real frame actually took,
    // independent of what the individual phases below add up to); `phase`
    // is reused to time each phase in turn. Averaged over
    // kTimingWindowSize real iterations, then logged and handed to
    // drawButtonTestScreen() as bars, then reset.
    constexpr int kTimingWindowSize = 60;
    PhaseTimingWindow timingAccum;
    PhaseTimingWindow timingLatest;  // last CLOSED window -- what gets drawn
    TickCounter loopTimer;
    osTickCounterStart(&loopTimer);

    while (aptMainLoop()) {
        TickCounter phase;
        osTickCounterStart(&phase);
        hidScanInput();
        input.poll(env.inputState());
        osTickCounterUpdate(&phase);
        timingAccum.inputMs += osTickCounterRead(&phase);

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

        osTickCounterStart(&phase);
        if (++vblankCounter >= vblanksPerSwfFrame) {
            vblankCounter = 0;
            root->advanceFrame();
            osTickCounterUpdate(&phase);
            timingAccum.advanceMs += osTickCounterRead(&phase);
            timingAccum.advanceSamples++;
            if (!loggedFirstFrame) {
                loggedFirstFrame = true;
                flash3ds::platform::checkpoint("after first frame (advanceFrame)");
            }
        }

        osTickCounterStart(&phase);
        scene.render(*root, topRenderer, kTopWidth, kTopHeight);
        osTickCounterUpdate(&phase);
        timingAccum.renderTopMs += osTickCounterRead(&phase);
        // Sub-phase split (2026-08-28) -- read right after render() since
        // both reflect only the frame just rendered (see Nintendo3DSRenderer::
        // lastRasterMs()/lastBlitMs()'s doc comment).
        timingAccum.renderTopRasterMs += topRenderer.lastRasterMs();
        timingAccum.renderTopBlitMs += topRenderer.lastBlitMs();
        if (!loggedFirstRender) {
            loggedFirstRender = true;
            flash3ds::platform::checkpoint("after first render (SceneRenderer::render)");
        }

        osTickCounterStart(&phase);
        drawButtonTestScreen(bottomRenderer, kHeld, timingLatest);
        osTickCounterUpdate(&phase);
        timingAccum.renderBottomMs += osTickCounterRead(&phase);

        // Present BOTH screens together -- see Nintendo3DSRenderer::
        // presentFrame()'s comment for why this must be called exactly
        // once per real frame rather than once per renderer.
        osTickCounterStart(&phase);
        Nintendo3DSRenderer::presentFrame();
        osTickCounterUpdate(&phase);
        timingAccum.presentMs += osTickCounterRead(&phase);

        osTickCounterStart(&phase);
        gspWaitForVBlank();
        osTickCounterUpdate(&phase);
        timingAccum.vblankWaitMs += osTickCounterRead(&phase);

        osTickCounterUpdate(&loopTimer);
        timingAccum.periodMs += osTickCounterRead(&loopTimer);
        timingAccum.samples++;

        if (timingAccum.samples >= kTimingWindowSize) {
            const double n = static_cast<double>(timingAccum.samples);
            const double avgPeriod = timingAccum.periodMs / n;
            LOG_INFO("PERF",
                     "avg ms/frame (n=%d): input=%.2f advance=%.2f(n=%d) renderTop=%.2f "
                     "(raster=%.2f blit=%.2f other=%.2f) renderBottom=%.2f present=%.2f "
                     "vblankWait=%.2f | period=%.2f (%.1f fps)",
                     timingAccum.samples, timingAccum.inputMs / n,
                     timingAccum.advanceSamples > 0 ? timingAccum.advanceMs / timingAccum.advanceSamples : 0.0,
                     timingAccum.advanceSamples, timingAccum.renderTopMs / n,
                     timingAccum.renderTopRasterMs / n, timingAccum.renderTopBlitMs / n,
                     std::max(0.0, timingAccum.renderTopMs / n - timingAccum.renderTopRasterMs / n -
                                       timingAccum.renderTopBlitMs / n),
                     timingAccum.renderBottomMs / n, timingAccum.presentMs / n, timingAccum.vblankWaitMs / n,
                     avgPeriod, avgPeriod > 0.0 ? 1000.0 / avgPeriod : 0.0);
            timingLatest = timingAccum;
            timingAccum = PhaseTimingWindow{};
        }
    }

    flash3ds::platform::checkpoint("shutdown (peak reflects the whole session)");
    audioBackend.stopAllSounds();
    gfxExit();
    return 0;
}
