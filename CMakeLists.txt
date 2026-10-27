cmake_minimum_required(VERSION 3.24)
project(Joyport VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(SDL3 3.4 CONFIG REQUIRED)

if(MSVC)
    set(PROJECT_WARNINGS /W4)
else()
    set(PROJECT_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# Every library includes headers relative to src/, e.g. "c64/tape_image.hpp".
set(SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- platform: host services (window, input, audio, files) --------------------
add_library(platform
    src/platform/host_sdl.cpp
    src/platform/sha256.cpp
    src/platform/resources.cpp
    src/platform/type.cpp)
target_include_directories(platform PUBLIC ${SOURCE_ROOT})
target_link_libraries(platform PUBLIC SDL3::SDL3)
target_compile_options(platform PRIVATE ${PROJECT_WARNINGS})

# --- ui: the launcher's drawing kit and shared picture filters --------------------------------------------
add_library(ui src/ui/draw.cpp src/ui/vhs.cpp src/ui/shelf.cpp)
target_link_libraries(ui PUBLIC platform)
target_include_directories(ui PUBLIC ${SOURCE_ROOT})
target_compile_options(ui PRIVATE ${PROJECT_WARNINGS})

# --- c64: formats and helpers shared by the C64 games --------------------------
add_library(c64
    src/c64/tape_image.cpp
    src/c64/kernal_tape.cpp
    src/c64/disk_image.cpp
    src/c64/character_rom.cpp
    src/c64/text_frame.cpp
    src/c64/sid.cpp)
target_include_directories(c64 PUBLIC ${SOURCE_ROOT})
target_compile_options(c64 PRIVATE ${PROJECT_WARNINGS})

# --- amiga: formats and helpers shared by the Amiga games -----------------------
add_library(amiga
    src/amiga/adf.cpp
    src/amiga/hunk.cpp
    src/amiga/byte_run1.cpp
    src/amiga/iff.cpp
    src/amiga/paula.cpp
    src/amiga/topaz.cpp
    src/amiga/intuition_imagery.cpp)
target_include_directories(amiga PUBLIC ${SOURCE_ROOT})
target_compile_options(amiga PRIVATE ${PROJECT_WARNINGS})

# --- fort_apocalypse_c64: Fort Apocalypse (C64) --------------------------------
add_library(fort_apocalypse_c64
    src/fort_apocalypse_c64/entry.cpp
    src/fort_apocalypse_c64/data/game_file.cpp
    src/fort_apocalypse_c64/data/assets.cpp
    src/fort_apocalypse_c64/data/preview.cpp
    src/fort_apocalypse_c64/data/tables.cpp
    src/fort_apocalypse_c64/game/flow.cpp
    src/fort_apocalypse_c64/game/view.cpp
    src/fort_apocalypse_c64/game/player.cpp
    src/fort_apocalypse_c64/game/enemy.cpp
    src/fort_apocalypse_c64/game/objects.cpp
    src/fort_apocalypse_c64/game/effects.cpp
    src/fort_apocalypse_c64/game/carnage.cpp
    src/fort_apocalypse_c64/game/module.cpp
    src/fort_apocalypse_c64/enhanced/canvas.cpp
    src/fort_apocalypse_c64/enhanced/enhanced.cpp
    src/fort_apocalypse_c64/enhanced/widescreen.cpp
    src/fort_apocalypse_c64/import/novaload.cpp
    src/fort_apocalypse_c64/import/tape_import.cpp)
target_include_directories(fort_apocalypse_c64 PUBLIC ${SOURCE_ROOT})
target_link_libraries(fort_apocalypse_c64 PUBLIC c64 platform ui)
target_compile_options(fort_apocalypse_c64 PRIVATE ${PROJECT_WARNINGS})

# --- bruce_lee_c64: Bruce Lee (C64) ---------------------------------------------
add_library(bruce_lee_c64
    src/bruce_lee_c64/entry.cpp
    src/bruce_lee_c64/data/assets.cpp
    src/bruce_lee_c64/data/preview.cpp
    src/bruce_lee_c64/game/screen.cpp
    src/bruce_lee_c64/game/terrain.cpp
    src/bruce_lee_c64/game/actor.cpp
    src/bruce_lee_c64/game/world.cpp
    src/bruce_lee_c64/game/carnage.cpp
    src/bruce_lee_c64/game/rooms.cpp
    src/bruce_lee_c64/game/front.cpp
    src/bruce_lee_c64/game/sound.cpp
    src/bruce_lee_c64/game/random_source.cpp
    src/bruce_lee_c64/enhanced/effects.cpp
    src/bruce_lee_c64/enhanced/wide_view.cpp
    src/bruce_lee_c64/game/game.cpp
    src/bruce_lee_c64/game/module.cpp
    src/bruce_lee_c64/import/disk_import.cpp)
target_include_directories(bruce_lee_c64 PUBLIC ${SOURCE_ROOT})
target_link_libraries(bruce_lee_c64 PUBLIC c64 platform ui)
target_compile_options(bruce_lee_c64 PRIVATE ${PROJECT_WARNINGS})

# --- wizball_c64: Wizball (C64) ------------------------------------------------
add_library(wizball_c64
    src/wizball_c64/entry.cpp
    src/wizball_c64/data/assets.cpp
    src/wizball_c64/data/preview.cpp
    src/wizball_c64/game/display.cpp
    src/wizball_c64/enhanced/effects.cpp
    src/wizball_c64/game/random_source.cpp
    src/wizball_c64/game/playfield.cpp
    src/wizball_c64/game/wiz.cpp
    src/wizball_c64/game/catellite.cpp
    src/wizball_c64/game/tunnel.cpp
    src/wizball_c64/game/clear.cpp
    src/wizball_c64/game/lab.cpp
    src/wizball_c64/game/flight.cpp
    src/wizball_c64/game/colour_runs.cpp
    src/wizball_c64/game/scores.cpp
    src/wizball_c64/game/front_end.cpp
    src/wizball_c64/game/front_picture.cpp
    src/wizball_c64/game/play.cpp
    src/wizball_c64/game/picture.cpp
    src/wizball_c64/game/sound.cpp
    src/wizball_c64/game/music.cpp
    src/wizball_c64/game/module.cpp
    src/wizball_c64/game/objects.cpp
    src/wizball_c64/game/combat.cpp
    src/wizball_c64/game/carnage.cpp
    src/wizball_c64/game/catellite_ai.cpp
    src/wizball_c64/import/disk_import.cpp)
target_include_directories(wizball_c64 PUBLIC ${SOURCE_ROOT})
target_link_libraries(wizball_c64 PUBLIC c64 platform ui)
target_compile_options(wizball_c64 PRIVATE ${PROJECT_WARNINGS})

# --- marble_madness_amiga: Marble Madness (Amiga) -------------------------------
add_library(marble_madness_amiga
    src/marble_madness_amiga/entry.cpp
    src/marble_madness_amiga/data/assets.cpp
    src/marble_madness_amiga/data/preview.cpp
    src/marble_madness_amiga/data/game_files.cpp
    src/marble_madness_amiga/game/mechanics_chapter.cpp
    src/marble_madness_amiga/game/terrain.cpp
    src/marble_madness_amiga/game/probe.cpp
    src/marble_madness_amiga/game/regions.cpp
    src/marble_madness_amiga/game/race.cpp
    src/marble_madness_amiga/game/marble_rules.cpp
    src/marble_madness_amiga/game/marble_covers.cpp
    src/marble_madness_amiga/game/display_list.cpp
    src/marble_madness_amiga/game/steelies.cpp
    src/marble_madness_amiga/game/slinkies.cpp
    src/marble_madness_amiga/game/acid.cpp
    src/marble_madness_amiga/game/pistons.cpp
    src/marble_madness_amiga/game/silly.cpp
    src/marble_madness_amiga/game/front_end.cpp
    src/marble_madness_amiga/game/crowd.cpp
    src/marble_madness_amiga/sound/effects.cpp
    src/marble_madness_amiga/sound/music.cpp
    src/marble_madness_amiga/sound/sound_board.cpp
    src/marble_madness_amiga/game/marble_shine.cpp
    src/marble_madness_amiga/game/view.cpp
    src/marble_madness_amiga/game/module.cpp
    src/marble_madness_amiga/import/loader_cipher.cpp
    src/marble_madness_amiga/import/disk_import.cpp)
target_include_directories(marble_madness_amiga PUBLIC ${SOURCE_ROOT})
target_link_libraries(marble_madness_amiga PUBLIC amiga platform ui)
target_compile_options(marble_madness_amiga PRIVATE ${PROJECT_WARNINGS})

# --- mindwalker_amiga: Mind Walker (Amiga) --------------------------------------
add_library(mindwalker_amiga
    src/mindwalker_amiga/entry.cpp
    src/mindwalker_amiga/data/assets.cpp
    src/mindwalker_amiga/data/game_files.cpp
    src/mindwalker_amiga/data/preview.cpp
    src/mindwalker_amiga/world/world.cpp
    src/mindwalker_amiga/screen/canvas.cpp
    src/mindwalker_amiga/screen/gadget_bar.cpp
    src/mindwalker_amiga/title/title_stage.cpp
    src/mindwalker_amiga/game/module.cpp
    src/mindwalker_amiga/landscape/view_cells.cpp
    src/mindwalker_amiga/landscape/view_scene.cpp
    src/mindwalker_amiga/landscape/landscape_bar.cpp
    src/mindwalker_amiga/landscape/creatures.cpp
    src/mindwalker_amiga/landscape/interlude.cpp
    src/mindwalker_amiga/landscape/zap_bolt.cpp
    src/mindwalker_amiga/landscape/map_view.cpp
    src/mindwalker_amiga/landscape/landscape_stage.cpp
    src/mindwalker_amiga/import/disk_import.cpp)
target_include_directories(mindwalker_amiga PUBLIC ${SOURCE_ROOT})
target_link_libraries(mindwalker_amiga PUBLIC amiga platform ui)
target_compile_options(mindwalker_amiga PRIVATE ${PROJECT_WARNINGS})

# --- wings_of_fury_amiga: Wings of Fury (Amiga) ---------------------------------
add_library(wings_of_fury_amiga
    src/wings_of_fury_amiga/entry.cpp
    src/wings_of_fury_amiga/data/assets.cpp
    src/wings_of_fury_amiga/data/packed.cpp
    src/wings_of_fury_amiga/data/preview.cpp
    src/wings_of_fury_amiga/data/shapes.cpp
    src/wings_of_fury_amiga/game/ffp.cpp
    src/wings_of_fury_amiga/game/dashboard.cpp
    src/wings_of_fury_amiga/game/effects.cpp
    src/wings_of_fury_amiga/game/flight.cpp
    src/wings_of_fury_amiga/game/module.cpp
    src/wings_of_fury_amiga/game/player_view.cpp
    src/wings_of_fury_amiga/game/scenery.cpp
    src/wings_of_fury_amiga/import/disk_import.cpp)
target_include_directories(wings_of_fury_amiga PUBLIC ${SOURCE_ROOT})
target_link_libraries(wings_of_fury_amiga PUBLIC amiga platform ui)
target_compile_options(wings_of_fury_amiga PRIVATE ${PROJECT_WARNINGS})

# --- cannon_fodder_amiga: Cannon Fodder (Amiga) ---------------------------------
add_library(cannon_fodder_amiga
    src/cannon_fodder_amiga/entry.cpp
    src/cannon_fodder_amiga/data/assets.cpp
    src/cannon_fodder_amiga/data/preview.cpp
    src/cannon_fodder_amiga/game/camera.cpp
    src/cannon_fodder_amiga/game/map_view.cpp
    src/cannon_fodder_amiga/import/disk_import.cpp
    src/cannon_fodder_amiga/import/rnc.cpp
    src/cannon_fodder_amiga/import/sensi_disk.cpp)
target_include_directories(cannon_fodder_amiga PUBLIC ${SOURCE_ROOT})
target_link_libraries(cannon_fodder_amiga PUBLIC amiga platform ui)
target_compile_options(cannon_fodder_amiga PRIVATE ${PROJECT_WARNINGS})

# --- joyport: the launcher application -----------------------------------------
add_executable(joyport
    src/app/main.cpp
    src/app/catalog.cpp
    src/app/library.cpp
    src/bruce_lee_c64/game/chapter.cpp
    src/bruce_lee_c64/game/room_chapter.cpp
    src/bruce_lee_c64/game/background_article.cpp
    src/app/launcher.cpp
    src/app/pause_menu.cpp
    src/app/saved_options.cpp)
target_link_libraries(joyport PRIVATE fort_apocalypse_c64 bruce_lee_c64 wizball_c64 marble_madness_amiga mindwalker_amiga wings_of_fury_amiga cannon_fodder_amiga ui)
target_compile_options(joyport PRIVATE ${PROJECT_WARNINGS})

# --- tools ---------------------------------------------------------------------
add_executable(fort_apocalypse_assets tools/fort_apocalypse_assets.cpp)
target_link_libraries(fort_apocalypse_assets PRIVATE fort_apocalypse_c64)
target_compile_options(fort_apocalypse_assets PRIVATE ${PROJECT_WARNINGS})

add_executable(bruce_lee_assets tools/bruce_lee_assets.cpp)
target_link_libraries(bruce_lee_assets PRIVATE bruce_lee_c64)
target_compile_options(bruce_lee_assets PRIVATE ${PROJECT_WARNINGS})

add_executable(wizball_replay tools/wizball_replay.cpp)
target_link_libraries(wizball_replay PRIVATE wizball_c64)
target_compile_options(wizball_replay PRIVATE ${PROJECT_WARNINGS})

add_executable(wizball_assets tools/wizball_assets.cpp)
target_link_libraries(wizball_assets PRIVATE wizball_c64)
target_compile_options(wizball_assets PRIVATE ${PROJECT_WARNINGS})

add_executable(bruce_lee_replay tools/bruce_lee_replay.cpp)
target_link_libraries(bruce_lee_replay PRIVATE bruce_lee_c64)
target_compile_options(bruce_lee_replay PRIVATE ${PROJECT_WARNINGS})

add_executable(fort_apocalypse_replay tools/fort_apocalypse_replay.cpp)
target_link_libraries(fort_apocalypse_replay PRIVATE fort_apocalypse_c64)
target_compile_options(fort_apocalypse_replay PRIVATE ${PROJECT_WARNINGS})

add_executable(amiga_disk tools/amiga_disk.cpp)
target_link_libraries(amiga_disk PRIVATE amiga)
target_compile_options(amiga_disk PRIVATE ${PROJECT_WARNINGS})

add_executable(cannon_fodder_assets tools/cannon_fodder_assets.cpp)
target_link_libraries(cannon_fodder_assets PRIVATE cannon_fodder_amiga)
target_compile_options(cannon_fodder_assets PRIVATE ${PROJECT_WARNINGS})

add_executable(marble_madness_assets tools/marble_madness_assets.cpp)
target_link_libraries(marble_madness_assets PRIVATE marble_madness_amiga)
target_compile_options(marble_madness_assets PRIVATE ${PROJECT_WARNINGS})

add_executable(mindwalker_assets tools/mindwalker_assets.cpp)
target_link_libraries(mindwalker_assets PRIVATE mindwalker_amiga)
target_compile_options(mindwalker_assets PRIVATE ${PROJECT_WARNINGS})

add_executable(mindwalker_world tools/mindwalker_world.cpp)
target_link_libraries(mindwalker_world PRIVATE mindwalker_amiga)
target_compile_options(mindwalker_world PRIVATE ${PROJECT_WARNINGS})

add_executable(mindwalker_replay tools/mindwalker_replay.cpp)
target_link_libraries(mindwalker_replay PRIVATE mindwalker_amiga)
target_compile_options(mindwalker_replay PRIVATE ${PROJECT_WARNINGS})

add_executable(wings_of_fury_assets tools/wings_of_fury_assets.cpp)
target_link_libraries(wings_of_fury_assets PRIVATE wings_of_fury_amiga)
target_compile_options(wings_of_fury_assets PRIVATE ${PROJECT_WARNINGS})

add_executable(wings_of_fury_replay tools/wings_of_fury_replay.cpp)
target_link_libraries(wings_of_fury_replay PRIVATE wings_of_fury_amiga)
target_compile_options(wings_of_fury_replay PRIVATE ${PROJECT_WARNINGS})

# The same resources are used by loose builds and packaged hosts. Android may
# place their contents in assets/; access goes through SDL IO.
add_custom_target(joyport_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources $<TARGET_FILE_DIR:joyport>/resources)
add_dependencies(joyport joyport_resources)

include(CTest)
if(BUILD_TESTING)
    add_executable(shell_checks tests/shell.cpp src/app/catalog.cpp src/app/library.cpp
        src/bruce_lee_c64/game/chapter.cpp
    src/bruce_lee_c64/game/room_chapter.cpp
    src/bruce_lee_c64/game/background_article.cpp
    src/app/launcher.cpp src/app/pause_menu.cpp)
    target_link_libraries(shell_checks PRIVATE fort_apocalypse_c64 bruce_lee_c64 wizball_c64 marble_madness_amiga mindwalker_amiga wings_of_fury_amiga cannon_fodder_amiga ui)
    target_compile_options(shell_checks PRIVATE ${PROJECT_WARNINGS})
    add_dependencies(shell_checks joyport_resources)
    add_test(NAME shell_navigation COMMAND shell_checks)
endif()

add_executable(marble_madness_replay tools/marble_madness_replay.cpp)
target_link_libraries(marble_madness_replay PRIVATE marble_madness_amiga)
target_compile_options(marble_madness_replay PRIVATE ${PROJECT_WARNINGS})
