# Host-native (Linux) test harness. Builds the plugin's portable logic
# against tests/shim/{windows,xinput}.h and an in-memory Logger, then runs
# real-time scenarios against it. See tests/README.md.

find_package(Threads REQUIRED)

set(SRC ${CMAKE_SOURCE_DIR}/src)

add_library(wfsm_core STATIC
    ${SRC}/controller/VirtualController.cpp
    ${SRC}/config/Config.cpp
    ${SRC}/config/KeyCodes.cpp
    ${SRC}/modes/ModeManager.cpp
    ${SRC}/timing/ClutchController.cpp
    ${SRC}/timing/ShiftObserver.cpp
    support/TestLogger.cpp
)
target_include_directories(wfsm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shim ${SRC}
                                            ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_include_directories(wfsm_core SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(wfsm_core PUBLIC
    WRECKFEST_DEFAULT_CONFIG="${CMAKE_SOURCE_DIR}/config/default.toml")
target_link_libraries(wfsm_core PUBLIC Threads::Threads)
target_compile_options(wfsm_core PUBLIC -Wall -Wextra)

if(WRECKFEST_TSAN)
    target_compile_options(wfsm_core PUBLIC -fsanitize=thread -g -O1)
    target_link_options(wfsm_core PUBLIC -fsanitize=thread)
endif()

foreach(name test_clutch_sequences test_modes_and_analog test_fuzz_invariants test_game_polling
             test_frame_timing)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE wfsm_core)
    add_test(NAME ${name} COMMAND ${name})
endforeach()
