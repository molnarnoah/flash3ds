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
    src/swf/SwfReader.cpp
    src/swf/SwfRecords.cpp
    src/swf/TagCode.cpp
    src/swf/TagDispatcher.cpp
    src/swf/PlaceObjectTag.cpp
    src/swf/ShapeRecords.cpp
    src/swf/DefineShapeTag.cpp
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
    src/runtime/CharacterDictionary.cpp
    src/runtime/CharacterBounds.cpp
    src/runtime/InputState.cpp
    src/runtime/ButtonInstance.cpp
    src/runtime/MovieClipInstance.cpp
    src/renderer/ShapeTessellator.cpp
    src/renderer/SoftwareRenderer.cpp
    src/renderer/SceneRenderer.cpp
    src/audio/NullAudioBackend.cpp
    src/avm1/Value.cpp
    src/avm1/ActionCode.cpp
    src/avm1/Scope.cpp
    src/avm1/GlobalObject.cpp
    src/avm1/ExecutionContext.cpp
    src/avm1/Interpreter.cpp
)

target_include_directories(flash3ds_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(flash3ds_core PUBLIC ZLIB::ZLIB)

target_compile_options(flash3ds_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
)

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
if(FLASH3DS_BUILD_TOOLS)
    add_executable(flash_runtime tools/flash_runtime/main.cpp)
    target_link_libraries(flash_runtime PRIVATE flash3ds_core)
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

    if(FLASH3DS_3DSXTOOL)
        add_custom_command(
            TARGET flash3ds_3ds POST_BUILD
            COMMAND "${FLASH3DS_3DSXTOOL}" "$<TARGET_FILE:flash3ds_3ds>"
                    "$<TARGET_FILE_DIR:flash3ds_3ds>/flash3ds_3ds.3dsx"
            COMMENT "Packaging flash3ds_3ds.3dsx"
            VERBATIM
        )
    else()
        message(WARNING
            "FLASH3DS_3DSXTOOL not found -- flash3ds_3ds will link to an "
            ".elf but won't be packaged into a runnable .3dsx. Set "
            "-DFLASH3DS_3DSXTOOL=/path/to/3dsxtool (see docs/3ds-toolchain.md).")
    endif()
endif()
