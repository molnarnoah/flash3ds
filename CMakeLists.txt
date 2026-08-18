cmake_minimum_required(VERSION 3.16)
project(flash3ds_runtime CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(FLASH3DS_BUILD_TESTS "Build the flash3ds test suite" ON)
option(FLASH3DS_BUILD_TOOLS "Build command-line tools (flash_runtime inspector)" ON)

find_package(ZLIB REQUIRED)

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
    src/swf/SwfLoader.cpp
    src/runtime/Movie.cpp
    src/runtime/DisplayList.cpp
    src/runtime/Timeline.cpp
    src/runtime/CharacterDictionary.cpp
    src/runtime/InputState.cpp
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
