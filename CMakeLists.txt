cmake_minimum_required(VERSION 3.16)

project(virtual-wheel
    VERSION 0.1.0
    DESCRIPTION "Keyboard-to-virtual-Xbox-controller daemon with analog clutch emulation"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)

find_package(Threads REQUIRED)

add_executable(virtual-wheel
    src/main.cpp
    src/input/KeyboardReader.cpp
    src/controller/VirtualController.cpp
    src/config/Config.cpp
    src/config/KeyCodes.cpp
    src/modes/ModeManager.cpp
    src/timing/ClutchController.cpp
    src/logging/Logger.cpp
)

target_include_directories(virtual-wheel PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# toml++ is vendored as a single amalgamated header (MIT licensed;
# see third_party/README.md) so the build has no network/package-manager
# dependency for TOML parsing. Marked SYSTEM to suppress warnings from
# code we don't own.
target_include_directories(virtual-wheel SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(virtual-wheel PRIVATE
    PkgConfig::LIBEVDEV
    Threads::Threads
)

target_compile_options(virtual-wheel PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Release>:-O2>
)

install(TARGETS virtual-wheel RUNTIME DESTINATION bin)
install(FILES config/default.toml DESTINATION share/wreckfest-virtual-wheel)
