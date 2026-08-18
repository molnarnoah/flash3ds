# cmake/Toolchain-3DS.cmake
#
# Phase 10 — cross-compilation toolchain file for a Nintendo 3DS build of
# flash3ds-runtime, using a from-source-bootstrapped equivalent of
# devkitARM (see docs/3ds-toolchain.md for how that toolchain was built —
# this file only *consumes* it, it does not build it).
#
# Usage:
#   cmake -B build-3ds -S . \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-3DS.cmake \
#       -DFLASH3DS_3DS_TOOLCHAIN_ROOT=/path/to/3ds-toolchain
#   cmake --build build-3ds
#
# FLASH3DS_3DS_TOOLCHAIN_ROOT must point at a directory laid out the way
# docs/3ds-toolchain.md's bootstrap script produces it:
#   <root>/libctru/libctru/{include,lib/libctru.a}
#   <root>/citro3d/{include,lib/libcitro3d.a}
#   <root>/zlib/{*.h, build_arm/libz.a}
# It defaults to the FLASH3DS_3DS_TOOLCHAIN_ROOT environment variable, then
# to /opt/3ds-toolchain, since there's no universal standard install path
# for a from-source bootstrap (unlike devkitPro's own $DEVKITPRO/$DEVKITARM
# convention, which this toolchain deliberately does NOT assume — it was
# built without devkitPro's own installer, see docs/3ds-toolchain.md).

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT FLASH3DS_3DS_TOOLCHAIN_ROOT)
    if(DEFINED ENV{FLASH3DS_3DS_TOOLCHAIN_ROOT})
        set(FLASH3DS_3DS_TOOLCHAIN_ROOT "$ENV{FLASH3DS_3DS_TOOLCHAIN_ROOT}")
    else()
        set(FLASH3DS_3DS_TOOLCHAIN_ROOT "/opt/3ds-toolchain")
    endif()
endif()
set(FLASH3DS_3DS_TOOLCHAIN_ROOT "${FLASH3DS_3DS_TOOLCHAIN_ROOT}" CACHE PATH
    "Root of the from-source 3DS toolchain bootstrap (libctru/citro3d/zlib) -- see docs/3ds-toolchain.md")

# --- compilers --------------------------------------------------------------
find_program(FLASH3DS_ARM_GCC arm-none-eabi-gcc)
find_program(FLASH3DS_ARM_GXX arm-none-eabi-g++)
find_program(FLASH3DS_ARM_AR arm-none-eabi-ar)
if(NOT FLASH3DS_ARM_GCC OR NOT FLASH3DS_ARM_GXX)
    message(FATAL_ERROR
        "arm-none-eabi-gcc/g++ not found. Install a generic ARM EABI "
        "cross-compiler (e.g. Ubuntu's gcc-arm-none-eabi/g++-arm-none-eabi "
        "packages) -- this toolchain file does NOT require devkitARM's own "
        "compiler, only its libraries (see docs/3ds-toolchain.md).")
endif()

set(CMAKE_C_COMPILER "${FLASH3DS_ARM_GCC}")
set(CMAKE_CXX_COMPILER "${FLASH3DS_ARM_GXX}")
if(FLASH3DS_ARM_AR)
    set(CMAKE_AR "${FLASH3DS_ARM_AR}")
endif()

# CMake can't run a 3DS binary to test the compiler; this is the standard
# workaround for bare-metal/cross toolchain files.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# --- ARMv6K/MPCore ABI flags -------------------------------------------------
# See docs/3ds-toolchain.md for why these specific flags (in particular why
# -mfpu=vfp is required in addition to -march/-mtune with a stock, non-
# devkitARM-patched GCC, and why linking against Ubuntu's armv5te-multilib
# runtime objects is correct despite the -march=armv6k mismatch -- ARMv6K is
# a strict superset of ARMv5TE with an identical EABI hard-float calling
# convention).
set(FLASH3DS_3DS_ARCH_FLAGS "-march=armv6k -mtune=mpcore -mfpu=vfp -mfloat-abi=hard -mtp=soft")

set(FLASH3DS_3DS_SUPPORT_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/3ds-support")

set(CMAKE_C_FLAGS_INIT
    "${FLASH3DS_3DS_ARCH_FLAGS} -D__3DS__ -DFLASH3DS_TARGET_3DS -mword-relocations -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT
    "${CMAKE_C_FLAGS_INIT} -fno-rtti")
# (Exceptions are kept ON, unlike libctru's own build -- flash3ds_core's
# platform-independent code already uses C++ exceptions for parse-error
# handling; disabling them would require a parallel error-handling scheme
# for the 3DS target only, which is out of scope for Phase 10.)

set(CMAKE_ASM_FLAGS_INIT "${FLASH3DS_3DS_ARCH_FLAGS} -x assembler-with-cpp")

# --- link flags: -specs=3dsx.specs wires in the crt0/linker-script combo
# from third_party/3ds-support (see that directory's README.md). -B adds it
# to gcc's spec/startfile search path so the bare filenames in 3dsx.specs
# resolve. -specs=nosys.specs (shipped by Ubuntu's gcc-arm-none-eabi
# package, NOT vendored by this project) supplies newlib's "always fail
# with ENOSYS" default _read/_write/_close/_lseek/_fstat/_isatty/
# _gettimeofday/_kill/_getpid/_getentropy -- needed because
# flash3ds_syscalls.c (see docs/3ds-toolchain.md) deliberately does NOT
# redefine those (this project's own stdio usage, e.g. platform/Log.cpp's
# fprintf, pulls in newlib's reentrant wrappers that call through to them).
# Discovered empirically: an unqualified `-lnosys` in the wrong link-line
# position does NOT work (single-pass linking means order matters);
# nosys.specs' own `*link_gcc_c_sequence` override wraps libc/libnosys/
# libgcc in a --start-group/--end-group, which is the actually-correct fix.
# ------------------------------------------------------------------
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${FLASH3DS_3DS_ARCH_FLAGS} -specs=3dsx.specs -specs=nosys.specs -B${FLASH3DS_3DS_SUPPORT_DIR} -L${FLASH3DS_3DS_SUPPORT_DIR}")

# --- where the bootstrapped libctru/citro3d/zlib live -----------------------
set(CMAKE_FIND_ROOT_PATH "${FLASH3DS_3DS_TOOLCHAIN_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(FLASH3DS_LIBCTRU_INCLUDE "${FLASH3DS_3DS_TOOLCHAIN_ROOT}/libctru/libctru/include" CACHE PATH "")
set(FLASH3DS_LIBCTRU_LIB "${FLASH3DS_3DS_TOOLCHAIN_ROOT}/libctru/libctru/lib/libctru.a" CACHE FILEPATH "")
set(FLASH3DS_LIBCITRO3D_INCLUDE "${FLASH3DS_3DS_TOOLCHAIN_ROOT}/citro3d/include" CACHE PATH "")
set(FLASH3DS_LIBCITRO3D_LIB "${FLASH3DS_3DS_TOOLCHAIN_ROOT}/citro3d/lib/libcitro3d.a" CACHE FILEPATH "")
set(FLASH3DS_ZLIB_INCLUDE "${FLASH3DS_3DS_TOOLCHAIN_ROOT}/zlib" CACHE PATH "")
set(FLASH3DS_ZLIB_LIB "${FLASH3DS_3DS_TOOLCHAIN_ROOT}/zlib/build_arm/libz.a" CACHE FILEPATH "")

# Host-side (native, NOT cross-compiled) tool that packages the linked ELF
# into a runnable .3dsx. Defaults to a sibling of the toolchain root, since
# that's where docs/3ds-toolchain.md's bootstrap script puts it; override
# with -DFLASH3DS_3DSXTOOL=/path/to/3dsxtool if it lives elsewhere.
find_program(FLASH3DS_3DSXTOOL_DEFAULT 3dsxtool
    PATHS "${FLASH3DS_3DS_TOOLCHAIN_ROOT}/.." "${FLASH3DS_3DS_TOOLCHAIN_ROOT}"
    NO_DEFAULT_PATH)
set(FLASH3DS_3DSXTOOL "${FLASH3DS_3DSXTOOL_DEFAULT}" CACHE FILEPATH
    "Path to the host-native 3dsxtool binary (packages an ELF into a .3dsx) -- see docs/3ds-toolchain.md")

set(FLASH3DS_TARGET_3DS TRUE CACHE BOOL "" FORCE)
