cmake_minimum_required(VERSION 3.16)
project(FuryOfTheSky C CXX)

# Platform-neutral simulation code: built into the game and into host tests.
set(SIM_SOURCES
    src/game/flight_model.cpp
    src/game/chase_camera.cpp
)

if(NOT PSP)
    # Host build (macOS/Linux): unit tests for the simulation only.
    #   cmake -B build/host && cmake --build build/host && ctest --test-dir build/host
    enable_testing()
    add_executable(flight_tests tests/flight_model_test.cpp ${SIM_SOURCES})
    target_include_directories(flight_tests PRIVATE src)
    target_compile_features(flight_tests PRIVATE cxx_std_17)
    target_compile_options(flight_tests PRIVATE -Wall -Wextra)
    add_test(NAME flight_model COMMAND flight_tests)
    return()
endif()

add_executable(fury
    src/main.cpp
    ${SIM_SOURCES}
    src/core/file.cpp
    src/platform/psp_input.cpp
    src/platform/psp_system.cpp
    src/render/clouds.cpp
    src/render/gu_context.cpp
    src/render/mesh.cpp
    src/render/ocean.cpp
    src/render/screenshot.cpp
    src/render/sky.cpp
    src/render/texture.cpp
    src/render/view.cpp
    src/scenes/assets.cpp
    src/scenes/flight_scene.cpp
    src/scenes/hangar_scene.cpp
)
target_include_directories(fury PRIVATE src)
target_compile_features(fury PRIVATE cxx_std_17)
target_compile_options(fury PRIVATE
    -O2 -G0 -Wall -Wextra
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics>
)
# NOTE: never add pspkernel here - it pulls kernel-mode import stubs into a
# user-mode module and the game dies with a black screen (docs/psp/gotchas.md #1).
target_link_libraries(fury PRIVATE
    pspgum pspgu pspge pspdisplay pspctrl psprtc psppower pspdebug m
)

# Memory-stick layout: copy the whole folder to ms0:/PSP/GAME/
set(DIST_DIR ${CMAKE_BINARY_DIR}/FuryOfTheSky)

create_pbp_file(
    TARGET fury
    TITLE "Fury of the Sky"
    VERSION 00.02
    OUTPUT_DIR ${DIST_DIR}
)

add_custom_command(TARGET fury POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${DIST_DIR}/assets
    COMMENT "Copying assets into ${DIST_DIR}"
)
