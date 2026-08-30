cmake_minimum_required(VERSION 3.16)
project(flash3ds_runtime CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# FLASH3DS_TARGET_3DS is set (FORCE) by cmake/Toolchain-3DS.cmake when
# cross-compiling for the 3DS -- see that file. It is never set for a
# normal (desktop) configure.
option(FLASH3DS_BUILD_TESTS "Build the flash3ds test suite" ON)
option(FLASH3DS_BUILD_TOOLS "Build command-line tools (flash_runtime inspector)" ON)
option(FLASH3DS_BUILD_3DS "Build the Nintendo 3DS executable (requires -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-3DS.cmake)" ${FLASH3DS_TARGET_3DS})

if(FLASH3DS_TARGET_3DS)
    # Cross-compiling: tests/tools default OFF (a test binary/CLI tool
    # built for ARM can't run on this host, and the CLI inspector's
    # file-argument-driven usage doesn't fit an SD-card-less 3DS app
    # anyway -- see src/platform/nintendo3ds_main.cpp's scope note).
    # Still overridable (-DFLASH3DS_BUILD_TESTS=ON) if a future phase adds
    # a way to actually run them (e.g. under an emulator).
    set(FLASH3DS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FLASH3DS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

    # No ARM build of zlib exists anywhere prebuilt -- cmake/Toolchain-3DS.cmake
    # sets FLASH3DS_ZLIB_LIB/FLASH3DS_ZLIB_INCLUDE to the toolchain
    # bootstrap's own from-source ARM zlib build (see docs/3ds-toolchain.md).
    # Declared as an IMPORTED target under the SAME ZLIB::ZLIB name
    # find_package(ZLIB) would normally produce, so flash3ds_core's
    # `target_link_libraries(... ZLIB::ZLIB)` below needs no change at all
    # between desktop and 3DS configures.
    if(NOT EXISTS "${FLASH3DS_ZLIB_LIB}")
        message(FATAL_ERROR
            "FLASH3DS_ZLIB_LIB ('${FLASH3DS_ZLIB_LIB}') not found. Build the "
            "ARM zlib per docs/3ds-toolchain.md, or pass -DFLASH3DS_ZLIB_LIB=... "
            "-DFLASH3DS_ZLIB_INCLUDE=... explicitly.")
    endif()
    add_library(ZLIB::ZLIB STATIC IMPORTED)
    set_target_properties(ZLIB::ZLIB PROPERTIES
        IMPORTED_LOCATION "${FLASH3DS_ZLIB_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${FLASH3DS_ZLIB_INCLUDE}")
else()
    find_package(ZLIB REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Core runtime library (platform-independent).
# ---------------------------------------------------------------------------
add_library(flash3ds_core
    src/platform/Log.cpp
    src/platform/MemoryDiagnostics.cpp
    src/swf/SwfReader.cpp
    src/swf/SwfRecords.cpp
    src/swf/TagCode.cpp
    src/swf/TagDispatcher.cpp
    src/swf/PlaceObjectTag.cpp
    src/swf/ShapeRecords.cpp
    src/swf/DefineShapeTag.cpp
    src/swf/DefineMorphShapeTag.cpp
    src/swf/DefineSoundTag.cpp
    src/swf/StartSoundTag.cpp
    src/swf/DefineFontTag.cpp
    src/swf/DefineTextTag.cpp
    src/swf/DefineButtonTag.cpp
    src/swf/DefineEditTextTag.cpp
    src/swf/SwfLoader.cpp
    src/runtime/Movie.cpp
    src/runtime/DisplayList.cpp
    src/runtime/Timeline.cpp
    src/runtime/FramePacer.cpp
    src/runtime/CharacterDictionary.cpp
    src/runtime/CharacterBounds.cpp
    src/runtime/InputState.cpp
    src/runtime/ButtonInstance.cpp
    src/runtime/MovieClipInstance.cpp
    src/runtime/IFileLoader.cpp
    src/runtime/LocalFileLoader.cpp
    src/renderer/ShapeTessellator.cpp
    src/renderer/SoftwareRenderer.cpp
    src/renderer/SceneRenderer.cpp
    src/audio/NullAudioBackend.cpp
    src/audio/Mp3Decoder.cpp
    src/audio/PcmSoundDecoder.cpp
    src/avm1/Value.cpp
    src/avm1/ActionCode.cpp
    src/avm1/Scope.cpp
    src/avm1/GlobalObject.cpp
    src/avm1/ExecutionContext.cpp
    src/avm1/Interpreter.cpp
    src/vc/IniDocument.cpp
    src/vc/GameConfig.cpp
    src/vc/GamePackage.cpp
)

target_include_directories(flash3ds_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# minimp3.h (third_party/minimp3, vendored -- see its own README.md) is
# only ever #include'd from src/audio/Mp3Decoder.cpp, so this is PRIVATE:
# no other translation unit needs (or should reach for) it directly.
target_include_directories(flash3ds_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minimp3
)

target_link_libraries(flash3ds_core PUBLIC ZLIB::ZLIB)

# src/platform/MemoryDiagnostics.cpp is the one flash3ds_core translation
# unit that needs a real libctru header (<3ds.h>, for
# osGetMemRegionUsed()/osGetMemRegionFree()) when built for the 3DS target
# (see that file for why -- it's __3DS__-gated at the #include level,
# mirroring Mp3Decoder.cpp's existing #ifdef __3DS__ pattern, but the
# *include path* itself still has to be visible to this target, which
# nothing in flash3ds_core needed before this file). PRIVATE: no other
# flash3ds_core translation unit includes <3ds.h> directly.
if(FLASH3DS_TARGET_3DS)
    target_include_directories(flash3ds_core PRIVATE
        "${FLASH3DS_LIBCTRU_INCLUDE}"
    )
endif()

target_compile_options(flash3ds_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
)

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
if(FLASH3DS_BUILD_TOOLS)
    add_executable(flash_runtime tools/flash_runtime/main.cpp)
    target_link_libraries(flash_runtime PRIVATE flash3ds_core)

    # Real-game compatibility corpus phase (2026-08-18): a read-only,
    # analysis-only SWF diagnostic tool (tag histogram, AVM1 opcode
    # profile, AS2 API scan, button/sound/rendering-feature profile). See
    # tools/swf_diagnostic/main.cpp and docs/real-game-compatibility.md.
    add_executable(swf_diagnostic tools/swf_diagnostic/main.cpp)
    target_link_libraries(swf_diagnostic PRIVATE flash3ds_core)

    # Implementation-roadmap Phase 1 (2026-08-21): read-only verification
    # that simulates a real hover/press/release mouse sequence against a
    # loaded movie and reports whether the (now-committed) event-dispatch
    # mechanism actually produced an observable effect. See
    # tools/real_game_harness/click_probe.cpp and
    # docs/real-game-readiness.md.
    add_executable(click_probe tools/real_game_harness/click_probe.cpp)
    target_link_libraries(click_probe PRIVATE flash3ds_core)

    # Phase 1 support diagnostics: explain WHY click_probe found no effect
    # for hobo.swf's 3 frame-1 buttons (condActionsV2 introspection), and
    # whether that's a corpus-wide pattern or specific to those 3.
    add_executable(button_debug tools/real_game_harness/button_debug.cpp)
    target_link_libraries(button_debug PRIVATE flash3ds_core)
    add_executable(button_scan tools/real_game_harness/button_scan.cpp)
    target_link_libraries(button_scan PRIVATE flash3ds_core)

    # Phase 2 (2026-08-21): byte-level breakdown of CharacterDictionary's
    # memory cost by CharacterDef variant arm. See
    # tools/real_game_harness/memory_breakdown.cpp and
    # docs/memory-audit.md's "Byte-level breakdown" section.
    add_executable(memory_breakdown tools/real_game_harness/memory_breakdown.cpp)
    target_link_libraries(memory_breakdown PRIVATE flash3ds_core)

    # Roadmap Phase 7 (2026-08-25): holds InputState::kEnd down across many
    # ticks against a real corpus movie and reports root timeline/display-
    # list state plus every real AVM1 call executed, vs. an inert-key
    # control run -- see tools/real_game_harness/hobo_end_key_probe.cpp and
    # docs/hobo-title-progression.md.
    add_executable(hobo_end_key_probe tools/real_game_harness/hobo_end_key_probe.cpp)
    target_link_libraries(hobo_end_key_probe PRIVATE flash3ds_core)

    # Roadmap Phase 9 (2026-08-25): scans every DefineMorphShape character
    # and every PlaceObject2 record targeting one, across a movie (including
    # nested DefineSprite tag streams), reporting the distinct Ratio values
    # actually used -- evidence for whether the "start-shape-only" morph
    # rendering simplification is corpus-safe. See
    # tools/real_game_harness/morph_ratio_scan.cpp and
    # docs/implementation-roadmap-2026-08-21-part2.md's Phase 9 section.
    add_executable(morph_ratio_scan tools/real_game_harness/morph_ratio_scan.cpp)
    target_link_libraries(morph_ratio_scan PRIVATE flash3ds_core)

    # Task prompt Track A, A1 (2026-08-27): renders real pixel frames (via
    # SceneRenderer/SoftwareRenderer) for a movie across many ticks while
    # holding the exact keys hobo.swf polls every onEnterFrame tick
    # (Left/Up/Right/Down + 'A'/'S'), plus a no-input control run, so the
    # two PPM sequences can be diffed to determine whether the game is
    # already playable at the engine level. See
    # tools/real_game_harness/hobo_playability_probe.cpp and
    # docs/hobo-title-progression.md.
    add_executable(hobo_playability_probe tools/real_game_harness/hobo_playability_probe.cpp)
    target_link_libraries(hobo_playability_probe PRIVATE flash3ds_core)

    # Track A, A1 follow-up (2026-08-27): captures the actual AVM1 call
    # trace for a held-movement-keys run vs an inert-key control, to
    # characterize WHY hobo_playability_probe found zero pixel effect from
    # holding those keys. See
    # tools/real_game_harness/hobo_movement_key_trace.cpp.
    add_executable(hobo_movement_key_trace tools/real_game_harness/hobo_movement_key_trace.cpp)
    target_link_libraries(hobo_movement_key_trace PRIVATE flash3ds_core)

    # Track A, A1 follow-up (2026-08-27, Hobo1 Track A Step 0 gap): taps
    # InputState::kEnd every other tick (guaranteeing many real press-edges
    # spread across a long run, rather than one continuous hold's single
    # tick-0 edge) while holding the movement keys throughout, reporting
    # every root-timeline frame change and gotoAndStop/gotoAndPlay/GetURL
    # call as it happens -- to find out whether ordinary repeated player
    # input can drive root all the way from frame 1 to frame 10 (where the
    # real "hobo" player-character clip lives), not just the single
    # timed-by-hand press docs/hobo-playability-verification.md's task #68
    # addendum confirmed. See
    # tools/real_game_harness/hobo_frame_progression_probe.cpp.
    add_executable(hobo_frame_progression_probe tools/real_game_harness/hobo_frame_progression_probe.cpp)
    target_link_libraries(hobo_frame_progression_probe PRIVATE flash3ds_core)

    # Phase 3 investigation (2026-08-30, post-SELECT-fix azahar_test_v21.mkv
    # report): drives the same confirmed input pattern
    # hobo_frame_progression_probe.cpp used to reach the "CHOOSE DIFFICULTY"
    # screen, then dumps the full recursive display list (character kind,
    # visibility, placement) and every audio-backend/Sound-related call --
    # aimed at Task #55 (missing EASY/NORMAL/HARD text/buttons) and Task #57
    # (no sound from the main menu). See
    # tools/real_game_harness/hobo_menu_content_probe.cpp.
    add_executable(hobo_menu_content_probe tools/real_game_harness/hobo_menu_content_probe.cpp)
    target_link_libraries(hobo_menu_content_probe PRIVATE flash3ds_core)

    # Track B, B1 (2026-08-27): re-verifies Extreme Pamplona's L6 "proven
    # negative" with a REAL simulated mouse click (idle-only traces cannot
    # reach a code path gated behind a Play-button click). See
    # tools/real_game_harness/pamplona_click_trace.cpp and
    # docs/known-limitations.md L6.
    add_executable(pamplona_click_trace tools/real_game_harness/pamplona_click_trace.cpp)
    target_link_libraries(pamplona_click_trace PRIVATE flash3ds_core)

    # Track A follow-up (2026-08-27, task #66): a static AVM1 disassembler
    # that walks EVERY embedded-bytecode container this project's SWF
    # parsing knows about -- DoAction/DoInitAction tag bodies, PlaceObject2's
    # ClipActionRecord (onClipEvent) bytecode, and DefineButton/
    # DefineButton2's own ButtonCondAction bytecode -- and prints a best-
    # effort symbolic disassembly (see the file's own header comment for the
    # linear-pass/no-branch-following caveats). Pre-existing tool, only now
    # CMake-registered: previously built/run standalone via `g++` per-
    # investigation (see docs/hobo-playability-verification.md's 2026-08-27
    # addendum for what it found). Registering it here so it stays buildable
    # without hand-reconstructing the right g++ invocation each time a future
    # session needs it again.
    add_executable(avm1_loader_disasm tools/real_game_harness/avm1_loader_disasm.cpp)
    target_link_libraries(avm1_loader_disasm PRIVATE flash3ds_core)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(FLASH3DS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Nintendo 3DS executable (Phase 10). Links flash3ds_core UNCHANGED --
# nothing above this point is 3DS-specific -- plus the 3DS-only backend
# implementations (renderer/input/audio) and entry point. See
# docs/3ds-toolchain.md for how FLASH3DS_3DS_TOOLCHAIN_ROOT's libctru/
# citro3d/zlib are produced, and cmake/Toolchain-3DS.cmake for the compiler/
# link flags this target relies on (-specs=3dsx.specs etc.).
# ---------------------------------------------------------------------------
if(FLASH3DS_BUILD_3DS)
    if(NOT FLASH3DS_TARGET_3DS)
        message(FATAL_ERROR
            "FLASH3DS_BUILD_3DS=ON requires configuring with "
            "-DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-3DS.cmake (a normal "
            "desktop compiler cannot produce a 3DS binary).")
    endif()

    # `-specs=3dsx.specs`'s *startfile: line looks up "3dsx_crt0.o" by bare
    # filename via gcc's -B/-L spec search path -- it does NOT compile
    # 3dsx_crt0.s from source itself. Assemble it once here (into the build
    # tree, not committed as a binary blob) with the same ARCH flags the
    # rest of this target uses, matching the working recipe verified in
    # docs/3ds-toolchain.md's smoke test.
    set(FLASH3DS_3DS_CRT0_DIR "${CMAKE_CURRENT_BINARY_DIR}/3ds-support")
    set(FLASH3DS_3DS_CRT0_OBJ "${FLASH3DS_3DS_CRT0_DIR}/3dsx_crt0.o")
    # FLASH3DS_3DS_ARCH_FLAGS is a single space-separated string (set by
    # cmake/Toolchain-3DS.cmake); add_custom_command's COMMAND needs it as
    # a proper CMake list to expand into separate argv entries.
    separate_arguments(FLASH3DS_3DS_ARCH_FLAGS_LIST UNIX_COMMAND "${FLASH3DS_3DS_ARCH_FLAGS}")
    add_custom_command(
        OUTPUT "${FLASH3DS_3DS_CRT0_OBJ}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FLASH3DS_3DS_CRT0_DIR}"
        COMMAND "${CMAKE_C_COMPILER}" ${FLASH3DS_3DS_ARCH_FLAGS_LIST} -x assembler-with-cpp
                -c "${FLASH3DS_3DS_SUPPORT_DIR}/3dsx_crt0.s" -o "${FLASH3DS_3DS_CRT0_OBJ}"
        DEPENDS "${FLASH3DS_3DS_SUPPORT_DIR}/3dsx_crt0.s"
        COMMENT "Assembling 3dsx_crt0.o"
        VERBATIM
    )
    add_custom_target(flash3ds_3ds_crt0 DEPENDS "${FLASH3DS_3DS_CRT0_OBJ}")

    add_executable(flash3ds_3ds
        src/platform/nintendo3ds_main.cpp
        src/renderer/Nintendo3DSRenderer.cpp
        src/platform/Nintendo3DSInput.cpp
        src/platform/Nintendo3DSRomfs.cpp
        src/audio/Nintendo3DSAudioBackend.cpp
    )
    add_dependencies(flash3ds_3ds flash3ds_3ds_crt0)
    target_link_options(flash3ds_3ds PRIVATE "-B${FLASH3DS_3DS_CRT0_DIR}" "-L${FLASH3DS_3DS_CRT0_DIR}")
    target_link_libraries(flash3ds_3ds PRIVATE flash3ds_core)
    target_include_directories(flash3ds_3ds PRIVATE
        "${FLASH3DS_LIBCTRU_INCLUDE}"
        "${FLASH3DS_LIBCITRO3D_INCLUDE}"
    )
    # Link order matters: citro3d references libctru symbols (GX/gpu
    # command-queue functions), so -lcitro3d must come before -lctru --
    # verified empirically in docs/3ds-toolchain.md's smoke test (the
    # reverse order fails with undefined references).
    # newlib's generic reentrant syscall wrappers (_read_r/_write_r/...,
    # pulled in transitively by flash3ds_core's use of stdio via
    # platform/Log.cpp) need _read/_write/... themselves defined; supplied
    # by -specs=nosys.specs (see cmake/Toolchain-3DS.cmake) rather than a
    # plain -lnosys here, since a plain library reference in the wrong
    # link-line position does not resolve the libc/libnosys/libgcc
    # circular dependency (confirmed empirically -- see that file's
    # comment).
    target_link_libraries(flash3ds_3ds PRIVATE
        "${FLASH3DS_LIBCITRO3D_LIB}"
        "${FLASH3DS_LIBCTRU_LIB}"
        m
    )
    target_compile_options(flash3ds_3ds PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
    )

    # --- Virtual Console resource layer: RomFS packaging ------------------
    #
    # The checked-in romfs/ directory (romfs/config.ini + romfs/game.swf --
    # see docs/virtual-console.md) is embedded into the .3dsx via
    # 3dsxtool's own --romfs= flag, read back at runtime by
    # Nintendo3DSRomfs (see that class's header for why that's a bespoke
    # reader rather than libctru's own romfsInit()). Swapping romfs/game.swf
    # for a different (compatible) SWF and rebuilding repackages the RomFS
    # section from the new file's bytes WITHOUT touching or recompiling any
    # C++ -- see docs/virtual-console.md's "Replacing game.swf" section.
    #
    # 3dsxtool's own --romfs= implementation (see 3dstools/src/3dsxtool.cpp)
    # unconditionally requires an --smdh= file too whenever EITHER flag is
    # used (it does not validate the SMDH's actual CONTENTS at all -- it's
    # embedded as an opaque blob, sized/positioned only), so a minimal
    # placeholder SMDH (title/publisher text + a solid-color icon -- NOT a
    # real banner/icon asset, and unrelated to CIAToolsR's own separate
    # icon/banner requirements for CIA packaging, see docs/virtual-console.md's
    # CIA packaging boundary section) is generated at build time.
    set(FLASH3DS_ROMFS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/romfs")
    find_program(FLASH3DS_PYTHON3 python3)

    set(FLASH3DS_3DSX_OUT "${CMAKE_CURRENT_BINARY_DIR}/flash3ds_3ds.3dsx")

    if(FLASH3DS_3DSXTOOL AND FLASH3DS_PYTHON3 AND EXISTS "${FLASH3DS_ROMFS_DIR}")
        set(FLASH3DS_DEV_SMDH "${CMAKE_CURRENT_BINARY_DIR}/3ds-support/dev.smdh")
        add_custom_command(
            OUTPUT "${FLASH3DS_DEV_SMDH}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/3ds-support"
            COMMAND "${FLASH3DS_PYTHON3}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_placeholder_smdh.py"
                    "${FLASH3DS_DEV_SMDH}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_placeholder_smdh.py"
            COMMENT "Generating placeholder dev.smdh (required by 3dsxtool --romfs=)"
            VERBATIM
        )

        # Deliberately a proper add_custom_command(OUTPUT ...) + an ALL
        # add_custom_target DEPENDS-ing on it -- NOT a `TARGET ... POST_BUILD`
        # hook (an earlier version of this used POST_BUILD, and it silently
        # DIDN'T repackage the .3dsx when only romfs/ contents changed, since
        # POST_BUILD only fires when the ELF itself relinks -- caught by this
        # project's own "swap game.swf, rebuild, confirm the new content is
        # actually in the .3dsx" verification pass, see
        # docs/virtual-console.md's SWF-replacement test). Listing every file
        # under romfs/ as an explicit DEPENDS means plain `cmake --build`
        # alone correctly repackages the .3dsx whenever an EXISTING romfs/
        # file's contents change (e.g. swapping romfs/game.swf for a
        # different SWF) -- no C++ recompile, no manual extra step. Adding a
        # brand-new file under romfs/ (rather than editing an existing one)
        # does need a re-configure to be picked up by this GLOB, same as any
        # other CMake file(GLOB ...) source list -- documented in
        # docs/virtual-console.md, not something this build silently gets
        # wrong.
        file(GLOB_RECURSE FLASH3DS_ROMFS_FILES "${FLASH3DS_ROMFS_DIR}/*")

        add_custom_command(
            OUTPUT "${FLASH3DS_3DSX_OUT}"
            COMMAND "${FLASH3DS_3DSXTOOL}" "$<TARGET_FILE:flash3ds_3ds>" "${FLASH3DS_3DSX_OUT}"
                    "--smdh=${FLASH3DS_DEV_SMDH}"
                    "--romfs=${FLASH3DS_ROMFS_DIR}"
            DEPENDS flash3ds_3ds "${FLASH3DS_DEV_SMDH}" ${FLASH3DS_ROMFS_FILES}
            COMMENT "Packaging flash3ds_3ds.3dsx (with RomFS: ${FLASH3DS_ROMFS_DIR})"
            VERBATIM
        )
        add_custom_target(flash3ds_3ds_3dsx ALL DEPENDS "${FLASH3DS_3DSX_OUT}")
    elseif(FLASH3DS_3DSXTOOL)
        message(WARNING
            "romfs/ directory or python3 not found -- flash3ds_3ds.3dsx will be "
            "packaged WITHOUT an embedded RomFS section, and will fail to boot "
            "(Nintendo3DSRomfs::open() will report no RomFS section present). "
            "See docs/virtual-console.md.")
        add_custom_command(
            OUTPUT "${FLASH3DS_3DSX_OUT}"
            COMMAND "${FLASH3DS_3DSXTOOL}" "$<TARGET_FILE:flash3ds_3ds>" "${FLASH3DS_3DSX_OUT}"
            DEPENDS flash3ds_3ds
            COMMENT "Packaging flash3ds_3ds.3dsx (NO RomFS)"
            VERBATIM
        )
        add_custom_target(flash3ds_3ds_3dsx ALL DEPENDS "${FLASH3DS_3DSX_OUT}")
    else()
        message(WARNING
            "FLASH3DS_3DSXTOOL not found -- flash3ds_3ds will link to an "
            ".elf but won't be packaged into a runnable .3dsx. Set "
            "-DFLASH3DS_3DSXTOOL=/path/to/3dsxtool (see docs/3ds-toolchain.md).")
    endif()
endif()
