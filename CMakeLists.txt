cmake_minimum_required(VERSION 3.16)

project(wreckfest-shift-macro
    VERSION 0.2.0
    DESCRIPTION "Ultimate ASI Loader plugin: keyboard-to-virtual-Xbox-controller with analog clutch emulation for Wreckfest"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR
        "This is a Windows DLL (.asi plugin). Cross-compile with MinGW-w64: "
        "cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake ..., "
        "or use the Docker build (see README).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_library(wreckfest-shift-macro SHARED
    src/dllmain.cpp
    src/hooks/ImportHook.cpp
    src/hooks/KeyboardHook.cpp
    src/hooks/XInputHook.cpp
    src/controller/VirtualController.cpp
    src/config/Config.cpp
    src/config/KeyCodes.cpp
    src/modes/ModeManager.cpp
    src/timing/ClutchController.cpp
    src/logging/Logger.cpp
)

# Ultimate ASI Loader picks up any DLL named *.asi in the game's scripts/
# folder.
set_target_properties(wreckfest-shift-macro PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
)

target_include_directories(wreckfest-shift-macro PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# toml++ is vendored as a single amalgamated header (MIT licensed;
# see third_party/README.md) so the build has no network/package-manager
# dependency for TOML parsing. Marked SYSTEM to suppress warnings from
# code we don't own.
target_include_directories(wreckfest-shift-macro SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party
)

target_compile_definitions(wreckfest-shift-macro PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

# winmm: timeBeginPeriod. XInput itself is deliberately *not* linked: the
# plugin only ever calls the game's own already-loaded XInput through the
# pointers it replaces.
target_link_libraries(wreckfest-shift-macro PRIVATE winmm)

if(MINGW)
    target_compile_options(wreckfest-shift-macro PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<CONFIG:Release>:-O2>
    )
    # Fully static runtime so the .asi is a single self-contained file:
    # the game folder has no libstdc++/libgcc/winpthread DLLs to lean on.
    target_link_options(wreckfest-shift-macro PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
        -s
    )
endif()
