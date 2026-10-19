cmake_minimum_required(VERSION 3.16)
project(FuryOfTheSky C CXX)

# Platform-neutral simulation code: built into the game and into host tests.
set(SIM_SOURCES
    src/game/flight_model.cpp
    src/game/chase_camera.cpp
    src/game/control_surfaces.cpp
    src/game/countermeasures.cpp
    src/game/autopilot.cpp
    src/game/combat.cpp
    src/game/radar.cpp
    src/game/missile.cpp
    src/game/gun.cpp
    src/game/armament.cpp
    src/game/bot.cpp
    src/game/rwr.cpp
    src/game/boundary.cpp
    src/game/ship.cpp
    src/fx/particles.cpp
    src/fx/aircraft_fx.cpp
    src/fx/weapon_fx.cpp
    src/fx/precipitation.cpp
    src/fx/water_marks.cpp
    src/fx/ship_fx.cpp
    src/audio/mixer.cpp
    src/audio/wav.cpp
    src/audio/flight_audio.cpp
    src/audio/voice_informer.cpp
    src/meta/catalog.cpp
    src/meta/profile.cpp
    src/meta/sortie.cpp
    src/world/environment.cpp
    src/world/sea.cpp
    src/world/terrain.cpp
    src/world/rock_shapes.cpp
    src/world/missions.cpp
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
    add_executable(fx_tests tests/fx_test.cpp ${SIM_SOURCES})
    target_include_directories(fx_tests PRIVATE src)
    target_compile_features(fx_tests PRIVATE cxx_std_17)
    target_compile_options(fx_tests PRIVATE -Wall -Wextra)
    add_test(NAME effects COMMAND fx_tests)
    add_executable(audio_tests tests/audio_test.cpp ${SIM_SOURCES})
    target_include_directories(audio_tests PRIVATE src)
    target_compile_features(audio_tests PRIVATE cxx_std_17)
    target_compile_options(audio_tests PRIVATE -Wall -Wextra)
    add_test(NAME audio COMMAND audio_tests)
    add_executable(combat_tests tests/combat_test.cpp ${SIM_SOURCES})
    target_include_directories(combat_tests PRIVATE src)
    target_compile_features(combat_tests PRIVATE cxx_std_17)
    target_compile_options(combat_tests PRIVATE -Wall -Wextra)
    add_test(NAME combat COMMAND combat_tests)
    add_executable(meta_tests tests/meta_test.cpp ${SIM_SOURCES})
    target_include_directories(meta_tests PRIVATE src)
    target_compile_features(meta_tests PRIVATE cxx_std_17)
    target_compile_options(meta_tests PRIVATE -Wall -Wextra)
    add_test(NAME meta COMMAND meta_tests)
    # Offline listening: the sortie's soundtrack through the real mixer.
    add_executable(audio_preview tools/audio/preview.cpp ${SIM_SOURCES})
    target_include_directories(audio_preview PRIVATE src)
    target_compile_features(audio_preview PRIVATE cxx_std_17)
    target_compile_options(audio_preview PRIVATE -Wall -Wextra)
    add_executable(world_tests tests/world_test.cpp ${SIM_SOURCES})
    target_include_directories(world_tests PRIVATE src)
    target_compile_features(world_tests PRIVATE cxx_std_17)
    target_compile_options(world_tests PRIVATE -Wall -Wextra)
    target_compile_definitions(world_tests PRIVATE FURY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME world COMMAND world_tests)
    # Simulation-step benchmark (docs/decisions/0049): shares of the step.
    add_executable(sim_bench tools/perf/sim_bench.cpp ${SIM_SOURCES})
    target_include_directories(sim_bench PRIVATE src)
    target_compile_features(sim_bench PRIVATE cxx_std_17)
    add_executable(ge_precision_tests tests/ge_precision_test.cpp)
    target_include_directories(ge_precision_tests PRIVATE src)
    target_compile_features(ge_precision_tests PRIVATE cxx_std_17)
    add_test(NAME ge_precision COMMAND ge_precision_tests)
    return()
endif()

add_executable(fury
    src/main.cpp
    ${SIM_SOURCES}
    src/core/file.cpp
    src/platform/psp_audio.cpp
    src/platform/psp_input.cpp
    src/platform/psp_save.cpp
    src/platform/psp_system.cpp
    src/render/cloud_deck.cpp
    src/render/clouds.cpp
    src/render/draw2d.cpp
    src/render/font.cpp
    src/render/fx_draw.cpp
    src/render/grid_mesh.cpp
    src/render/gu_context.cpp
    src/render/hud_draw.cpp
    src/render/mesh.cpp
    src/render/ocean.cpp
    src/render/screenshot.cpp
    src/render/sky.cpp
    src/render/terrain_mesh.cpp
    src/render/texture.cpp
    src/render/view.cpp
    src/scenes/assets.cpp
    src/scenes/flight_hud.cpp
    src/scenes/flight_scene.cpp
    src/scenes/menu/briefing_screen.cpp
    src/scenes/menu/campaign_screen.cpp
    src/scenes/menu/debrief_screen.cpp
    src/scenes/menu/emblem_screen.cpp
    src/scenes/menu/hangar_screen.cpp
    src/scenes/menu/location_screen.cpp
    src/scenes/menu/main_screen.cpp
    src/scenes/menu/menu_scene.cpp
    src/scenes/menu/multiplayer_screen.cpp
    src/scenes/menu/profile_screen.cpp
    src/scenes/menu/settings_screen.cpp
    src/scenes/menu/training_screen.cpp
    src/ui/ui.cpp
)
# Build id shown in the HUD (regenerated on every build).
set(BUILD_INFO_CPP ${CMAKE_BINARY_DIR}/generated/build_info.cpp)
add_custom_target(build_info
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${BUILD_INFO_CPP} -P ${CMAKE_SOURCE_DIR}/cmake/build_info.cmake
    BYPRODUCTS ${BUILD_INFO_CPP}
    COMMENT "Stamping build id"
)
target_sources(fury PRIVATE ${BUILD_INFO_CPP})
add_dependencies(fury build_info)

target_include_directories(fury PRIVATE src)
target_compile_features(fury PRIVATE cxx_std_17)
target_compile_options(fury PRIVATE
    -O2 -G0 -Wall -Wextra
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics>
)
# NOTE: never add pspkernel here - it pulls kernel-mode import stubs into a
# user-mode module and the game dies with a black screen (docs/psp/gotchas.md #1).
target_link_libraries(fury PRIVATE
    pspgum pspgu pspge pspdisplay pspctrl pspaudio psprtc psppower psputility pspdebug m
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
