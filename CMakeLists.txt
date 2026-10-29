cmake_minimum_required(VERSION 3.24)
project(Reprise VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(ANDROID)
    # Every static engine library is linked into libmain.so on Android.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
find_package(SDL3 3.4 CONFIG REQUIRED)

if(MSVC)
    set(PROJECT_WARNINGS /W4)
else()
    set(PROJECT_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# Every library includes headers relative to games/, e.g. "c64/tape_image.hpp".
set(SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/games)

# --- platform: host services (window, input, audio, files) --------------------
add_library(platform
    shared/platform/host_sdl.cpp
    shared/platform/sha256.cpp
    shared/platform/resources.cpp
    shared/platform/type.cpp)
target_include_directories(platform PUBLIC ${SOURCE_ROOT})
target_link_libraries(platform PUBLIC SDL3::SDL3)
target_compile_options(platform PRIVATE ${PROJECT_WARNINGS})

# --- ui: the launcher's drawing kit and shared picture filters --------------------------------------------
add_library(ui shared/ui/draw.cpp shared/ui/vhs.cpp shared/ui/shelf.cpp shared/ui/model_view.cpp)
target_link_libraries(ui PUBLIC platform)
target_include_directories(ui PUBLIC ${SOURCE_ROOT})
target_compile_options(ui PRIVATE ${PROJECT_WARNINGS})

# --- c64: formats and helpers shared by the C64 games --------------------------
add_library(c64
    shared/c64/tape_image.cpp
    shared/c64/kernal_tape.cpp
    shared/c64/disk_image.cpp
    shared/c64/character_rom.cpp
    shared/c64/text_frame.cpp
    shared/c64/sid.cpp)
target_include_directories(c64 PUBLIC ${SOURCE_ROOT})
target_compile_options(c64 PRIVATE ${PROJECT_WARNINGS})

# --- amiga: formats and helpers shared by the Amiga games -----------------------
add_library(amiga
    shared/amiga/adf.cpp
    shared/amiga/hunk.cpp
    shared/amiga/byte_run1.cpp
    shared/amiga/iff.cpp
    shared/amiga/paula.cpp
    shared/amiga/paula_voices.cpp
    shared/amiga/paula_channels.cpp
    shared/amiga/topaz.cpp
    shared/amiga/intuition_imagery.cpp)
target_include_directories(amiga PUBLIC ${SOURCE_ROOT})
target_compile_options(amiga PRIVATE ${PROJECT_WARNINGS})

# --- fort_apocalypse_c64: Fort Apocalypse (C64) --------------------------------
add_library(fort_apocalypse_c64
    games/fort_apocalypse_c64/entry.cpp
    games/fort_apocalypse_c64/data/game_file.cpp
    games/fort_apocalypse_c64/data/assets.cpp
    games/fort_apocalypse_c64/data/preview.cpp
    games/fort_apocalypse_c64/data/tables.cpp
    games/fort_apocalypse_c64/game/flow.cpp
    games/fort_apocalypse_c64/game/view.cpp
    games/fort_apocalypse_c64/game/player.cpp
    games/fort_apocalypse_c64/game/enemy.cpp
    games/fort_apocalypse_c64/game/objects.cpp
    games/fort_apocalypse_c64/game/effects.cpp
    games/fort_apocalypse_c64/game/carnage.cpp
    games/fort_apocalypse_c64/game/module.cpp
    games/fort_apocalypse_c64/enhanced/canvas.cpp
    games/fort_apocalypse_c64/enhanced/enhanced.cpp
    games/fort_apocalypse_c64/enhanced/widescreen.cpp
    games/fort_apocalypse_c64/import/novaload.cpp
    games/fort_apocalypse_c64/import/tape_import.cpp)
target_include_directories(fort_apocalypse_c64 PUBLIC ${SOURCE_ROOT})
target_link_libraries(fort_apocalypse_c64 PUBLIC c64 platform ui)
target_compile_options(fort_apocalypse_c64 PRIVATE ${PROJECT_WARNINGS})

# --- bruce_lee_c64: Bruce Lee (C64) ---------------------------------------------
add_library(bruce_lee_c64
    games/bruce_lee_c64/entry.cpp
    games/bruce_lee_c64/data/assets.cpp
    games/bruce_lee_c64/data/preview.cpp
    games/bruce_lee_c64/game/screen.cpp
    games/bruce_lee_c64/game/terrain.cpp
    games/bruce_lee_c64/game/actor.cpp
    games/bruce_lee_c64/game/world.cpp
    games/bruce_lee_c64/game/carnage.cpp
    games/bruce_lee_c64/game/rooms.cpp
    games/bruce_lee_c64/game/front.cpp
    games/bruce_lee_c64/game/sound.cpp
    games/bruce_lee_c64/game/random_source.cpp
    games/bruce_lee_c64/enhanced/effects.cpp
    games/bruce_lee_c64/enhanced/wide_view.cpp
    games/bruce_lee_c64/game/game.cpp
    games/bruce_lee_c64/game/module.cpp
    games/bruce_lee_c64/import/disk_import.cpp)
target_include_directories(bruce_lee_c64 PUBLIC ${SOURCE_ROOT})
target_link_libraries(bruce_lee_c64 PUBLIC c64 platform ui)
target_compile_options(bruce_lee_c64 PRIVATE ${PROJECT_WARNINGS})

# --- wizball_c64: Wizball (C64) ------------------------------------------------
add_library(wizball_c64
    games/wizball_c64/entry.cpp
    games/wizball_c64/data/assets.cpp
    games/wizball_c64/data/preview.cpp
    games/wizball_c64/game/display.cpp
    games/wizball_c64/enhanced/effects.cpp
    games/wizball_c64/game/random_source.cpp
    games/wizball_c64/game/playfield.cpp
    games/wizball_c64/game/wiz.cpp
    games/wizball_c64/game/catellite.cpp
    games/wizball_c64/game/tunnel.cpp
    games/wizball_c64/game/clear.cpp
    games/wizball_c64/game/lab.cpp
    games/wizball_c64/game/flight.cpp
    games/wizball_c64/game/colour_runs.cpp
    games/wizball_c64/game/scores.cpp
    games/wizball_c64/game/front_end.cpp
    games/wizball_c64/game/front_picture.cpp
    games/wizball_c64/game/play.cpp
    games/wizball_c64/game/picture.cpp
    games/wizball_c64/game/sound.cpp
    games/wizball_c64/game/music.cpp
    games/wizball_c64/game/module.cpp
    games/wizball_c64/game/objects.cpp
    games/wizball_c64/game/combat.cpp
    games/wizball_c64/game/carnage.cpp
    games/wizball_c64/game/catellite_ai.cpp
    games/wizball_c64/import/disk_import.cpp)
target_include_directories(wizball_c64 PUBLIC ${SOURCE_ROOT})
target_link_libraries(wizball_c64 PUBLIC c64 platform ui)
target_compile_options(wizball_c64 PRIVATE ${PROJECT_WARNINGS})

# --- marble_madness_amiga: Marble Madness (Amiga) -------------------------------
add_library(marble_madness_amiga
    games/marble_madness_amiga/entry.cpp
    games/marble_madness_amiga/data/assets.cpp
    games/marble_madness_amiga/data/preview.cpp
    games/marble_madness_amiga/data/game_files.cpp
    games/marble_madness_amiga/game/mechanics_chapter.cpp
    games/marble_madness_amiga/game/terrain.cpp
    games/marble_madness_amiga/game/probe.cpp
    games/marble_madness_amiga/game/regions.cpp
    games/marble_madness_amiga/game/race.cpp
    games/marble_madness_amiga/game/marble_rules.cpp
    games/marble_madness_amiga/game/marble_covers.cpp
    games/marble_madness_amiga/game/display_list.cpp
    games/marble_madness_amiga/game/steelies.cpp
    games/marble_madness_amiga/game/slinkies.cpp
    games/marble_madness_amiga/game/acid.cpp
    games/marble_madness_amiga/game/pistons.cpp
    games/marble_madness_amiga/game/silly.cpp
    games/marble_madness_amiga/game/front_end.cpp
    games/marble_madness_amiga/game/crowd.cpp
    games/marble_madness_amiga/sound/effects.cpp
    games/marble_madness_amiga/sound/music.cpp
    games/marble_madness_amiga/sound/sound_board.cpp
    games/marble_madness_amiga/game/marble_shine.cpp
    games/marble_madness_amiga/game/view.cpp
    games/marble_madness_amiga/game/module.cpp
    games/marble_madness_amiga/import/loader_cipher.cpp
    games/marble_madness_amiga/import/disk_import.cpp)
target_include_directories(marble_madness_amiga PUBLIC ${SOURCE_ROOT})
target_link_libraries(marble_madness_amiga PUBLIC amiga platform ui)
target_compile_options(marble_madness_amiga PRIVATE ${PROJECT_WARNINGS})

# --- mindwalker_amiga: Mind Walker (Amiga) --------------------------------------
add_library(mindwalker_amiga
    games/mindwalker_amiga/entry.cpp
    games/mindwalker_amiga/data/assets.cpp
    games/mindwalker_amiga/data/game_files.cpp
    games/mindwalker_amiga/data/preview.cpp
    games/mindwalker_amiga/world/world.cpp
    games/mindwalker_amiga/screen/canvas.cpp
    games/mindwalker_amiga/screen/area_fill.cpp
    games/mindwalker_amiga/screen/gadget_bar.cpp
    games/mindwalker_amiga/title/title_stage.cpp
    games/mindwalker_amiga/game/player_guide.cpp
    games/mindwalker_amiga/game/module.cpp
    games/mindwalker_amiga/landscape/view_cells.cpp
    games/mindwalker_amiga/landscape/view_scene.cpp
    games/mindwalker_amiga/landscape/landscape_bar.cpp
    games/mindwalker_amiga/landscape/creatures.cpp
    games/mindwalker_amiga/landscape/interlude.cpp
    games/mindwalker_amiga/landscape/zap_bolt.cpp
    games/mindwalker_amiga/landscape/map_view.cpp
    games/mindwalker_amiga/landscape/sound.cpp
    games/mindwalker_amiga/tunnel/tunnel_stage.cpp
    games/mindwalker_amiga/arena/arena_map.cpp
    games/mindwalker_amiga/arena/arena_stage.cpp
    games/mindwalker_amiga/psyche/psyche_sound.cpp
    games/mindwalker_amiga/psyche/psyche_stage.cpp
    games/mindwalker_amiga/landscape/landscape_stage.cpp
    games/mindwalker_amiga/import/disk_import.cpp)
target_include_directories(mindwalker_amiga PUBLIC ${SOURCE_ROOT})
target_link_libraries(mindwalker_amiga PUBLIC amiga platform ui)
target_compile_options(mindwalker_amiga PRIVATE ${PROJECT_WARNINGS})

# --- wings_of_fury_amiga: Wings of Fury (Amiga) ---------------------------------
add_library(wings_of_fury_amiga
    games/wings_of_fury_amiga/entry.cpp
    games/wings_of_fury_amiga/data/assets.cpp
    games/wings_of_fury_amiga/data/packed.cpp
    games/wings_of_fury_amiga/data/preview.cpp
    games/wings_of_fury_amiga/data/shapes.cpp
    games/wings_of_fury_amiga/game/ffp.cpp
    games/wings_of_fury_amiga/game/dashboard.cpp
    games/wings_of_fury_amiga/game/effects.cpp
    games/wings_of_fury_amiga/game/flight.cpp
    games/wings_of_fury_amiga/game/islands.cpp
    games/wings_of_fury_amiga/game/enemies.cpp
    games/wings_of_fury_amiga/game/module.cpp
    games/wings_of_fury_amiga/game/player_view.cpp
    games/wings_of_fury_amiga/game/scenery.cpp
    games/wings_of_fury_amiga/game/ticker.cpp
    games/wings_of_fury_amiga/game/briefing.cpp
    games/wings_of_fury_amiga/game/frontend.cpp
    games/wings_of_fury_amiga/game/music.cpp
    games/wings_of_fury_amiga/game/ships.cpp
    games/wings_of_fury_amiga/game/saved_game.cpp
    games/wings_of_fury_amiga/game/smooth_motion.cpp
    games/wings_of_fury_amiga/import/disk_import.cpp)
target_include_directories(wings_of_fury_amiga PUBLIC ${SOURCE_ROOT})
target_link_libraries(wings_of_fury_amiga PUBLIC amiga platform ui)
target_compile_options(wings_of_fury_amiga PRIVATE ${PROJECT_WARNINGS})

# --- cannon_fodder_amiga: Cannon Fodder (Amiga) ---------------------------------
add_library(cannon_fodder_amiga
    games/cannon_fodder_amiga/entry.cpp
    games/cannon_fodder_amiga/data/assets.cpp
    games/cannon_fodder_amiga/data/preview.cpp
    games/cannon_fodder_amiga/game/camera.cpp
    games/cannon_fodder_amiga/game/map_view.cpp
    games/cannon_fodder_amiga/game/mission_preview.cpp
    games/cannon_fodder_amiga/game/object_view.cpp
    games/cannon_fodder_amiga/game/objects.cpp
    games/cannon_fodder_amiga/game/panel.cpp
    games/cannon_fodder_amiga/game/walking.cpp
    games/cannon_fodder_amiga/game/combat.cpp
    games/cannon_fodder_amiga/game/tables.cpp
    games/cannon_fodder_amiga/game/explosives.cpp
    games/cannon_fodder_amiga/game/mission.cpp
    games/cannon_fodder_amiga/game/scenery.cpp
    games/cannon_fodder_amiga/game/vehicles.cpp
    games/cannon_fodder_amiga/game/campaign.cpp
    games/cannon_fodder_amiga/game/front_end.cpp
    games/cannon_fodder_amiga/game/dispatch.cpp
    games/cannon_fodder_amiga/game/module.cpp
    games/cannon_fodder_amiga/sound/rjp1.cpp
    games/cannon_fodder_amiga/sound/board.cpp
    games/cannon_fodder_amiga/sound/music_director.cpp
    games/cannon_fodder_amiga/import/disk_import.cpp
    games/cannon_fodder_amiga/import/rnc.cpp
    games/cannon_fodder_amiga/import/sensi_disk.cpp)
target_include_directories(cannon_fodder_amiga PUBLIC ${SOURCE_ROOT})
target_link_libraries(cannon_fodder_amiga PUBLIC amiga platform ui)
target_compile_options(cannon_fodder_amiga PRIVATE ${PROJECT_WARNINGS})

# --- rock_n_roll_amiga: Rock 'n Roll (Amiga) ------------------------------------
add_library(rock_n_roll_amiga
    games/rock_n_roll_amiga/entry.cpp
    games/rock_n_roll_amiga/data/assets.cpp
    games/rock_n_roll_amiga/data/preview.cpp
    games/rock_n_roll_amiga/game/ball.cpp
    games/rock_n_roll_amiga/game/level_map.cpp
    games/rock_n_roll_amiga/game/module.cpp
    games/rock_n_roll_amiga/game/screen.cpp
    games/rock_n_roll_amiga/import/cruncher.cpp
    games/rock_n_roll_amiga/import/disk_import.cpp)
target_include_directories(rock_n_roll_amiga PUBLIC ${SOURCE_ROOT})
target_link_libraries(rock_n_roll_amiga PUBLIC amiga platform ui)
target_compile_options(rock_n_roll_amiga PRIVATE ${PROJECT_WARNINGS})

# --- tower_of_babel_amiga: Tower of Babel (Amiga) -----------------------------
add_library(tower_of_babel_amiga
    games/tower_of_babel_amiga/entry.cpp
    games/tower_of_babel_amiga/data/assets.cpp
    games/tower_of_babel_amiga/data/model.cpp
    games/tower_of_babel_amiga/data/preview.cpp
    games/tower_of_babel_amiga/data/shape.cpp
    games/tower_of_babel_amiga/front/front_end.cpp
    games/tower_of_babel_amiga/front/module.cpp
    games/tower_of_babel_amiga/front/play.cpp
    games/tower_of_babel_amiga/front/screen.cpp
    games/tower_of_babel_amiga/game/animation.cpp
    games/tower_of_babel_amiga/game/behaviour.cpp
    games/tower_of_babel_amiga/game/panel.cpp
    games/tower_of_babel_amiga/game/rules.cpp
    games/tower_of_babel_amiga/game/tower_map.cpp
    games/tower_of_babel_amiga/game/tower_state.cpp
    games/tower_of_babel_amiga/import/decruncher.cpp
    games/tower_of_babel_amiga/import/disk_import.cpp
    games/tower_of_babel_amiga/view/overlay.cpp
    games/tower_of_babel_amiga/view/polygon.cpp
    games/tower_of_babel_amiga/view/sky.cpp
    games/tower_of_babel_amiga/view/tower_view.cpp)
target_include_directories(tower_of_babel_amiga PUBLIC ${SOURCE_ROOT})
target_link_libraries(tower_of_babel_amiga PUBLIC amiga platform ui)
target_compile_options(tower_of_babel_amiga PRIVATE ${PROJECT_WARNINGS})

# --- fa_18_interceptor_amiga: F/A-18 Interceptor (Amiga) ----------------------
add_library(fa_18_interceptor_amiga
    games/fa_18_interceptor_amiga/entry.cpp
    games/fa_18_interceptor_amiga/data/assets.cpp
    games/fa_18_interceptor_amiga/data/preview.cpp
    games/fa_18_interceptor_amiga/data/shapes.cpp
    games/fa_18_interceptor_amiga/data/world.cpp
    games/fa_18_interceptor_amiga/game/aircraft.cpp
    games/fa_18_interceptor_amiga/game/attitude.cpp
    games/fa_18_interceptor_amiga/game/cockpit.cpp
    games/fa_18_interceptor_amiga/game/flight_tables.cpp
    games/fa_18_interceptor_amiga/game/messages.cpp
    games/fa_18_interceptor_amiga/game/module.cpp
    games/fa_18_interceptor_amiga/game/scene.cpp
    games/fa_18_interceptor_amiga/game/sound_driver.cpp
    games/fa_18_interceptor_amiga/game/sounds.cpp
    games/fa_18_interceptor_amiga/game/text_screen.cpp
    games/fa_18_interceptor_amiga/import/disk_import.cpp)
target_include_directories(fa_18_interceptor_amiga PUBLIC ${SOURCE_ROOT})
target_link_libraries(fa_18_interceptor_amiga PUBLIC amiga platform ui)
target_compile_options(fa_18_interceptor_amiga PRIVATE ${PROJECT_WARNINGS})

# --- spindizzy_worlds_amiga: Spindizzy Worlds (Amiga) -------------------------
add_library(spindizzy_worlds_amiga
    games/spindizzy_worlds_amiga/entry.cpp
    games/spindizzy_worlds_amiga/data/assets.cpp
    games/spindizzy_worlds_amiga/data/preview.cpp
    games/spindizzy_worlds_amiga/game/front_end.cpp
    games/spindizzy_worlds_amiga/game/module.cpp
    games/spindizzy_worlds_amiga/game/play_screen.cpp
    games/spindizzy_worlds_amiga/import/disk_import.cpp
    games/spindizzy_worlds_amiga/play/explorer.cpp
    games/spindizzy_worlds_amiga/play/level_play.cpp
    games/spindizzy_worlds_amiga/play/objects.cpp
    games/spindizzy_worlds_amiga/play/surfaces.cpp
    games/spindizzy_worlds_amiga/view/camera.cpp
    games/spindizzy_worlds_amiga/view/front.cpp
    games/spindizzy_worlds_amiga/view/objects.cpp
    games/spindizzy_worlds_amiga/view/panel.cpp
    games/spindizzy_worlds_amiga/view/playfield.cpp
    games/spindizzy_worlds_amiga/view/screen.cpp
    games/spindizzy_worlds_amiga/view/sprites.cpp
    games/spindizzy_worlds_amiga/view/starfield.cpp
    games/spindizzy_worlds_amiga/view/text.cpp
    games/spindizzy_worlds_amiga/view/tile_map.cpp
    games/spindizzy_worlds_amiga/world/level_builder.cpp)
target_include_directories(spindizzy_worlds_amiga PUBLIC ${SOURCE_ROOT})
target_link_libraries(spindizzy_worlds_amiga PUBLIC amiga platform ui)
target_compile_options(spindizzy_worlds_amiga PRIVATE ${PROJECT_WARNINGS})

# --- spy_mouse_ios: Spy Mouse (iOS) --------------------------------------------
# The engine keeps its own build (standalone app, replay and verification tools); Reprise uses
# its libraries through the embedding interface only (src/embed), so the engine's include
# paths (its own "platform/...") never meet Reprise's.
add_subdirectory(games/spy_mouse_ios EXCLUDE_FROM_ALL)
add_library(spy_mouse_ios games/spy_mouse_ios/reprise/entry.cpp)
target_include_directories(spy_mouse_ios PUBLIC ${SOURCE_ROOT})
target_link_libraries(spy_mouse_ios PUBLIC platform ui PRIVATE doc_embed)
target_compile_options(spy_mouse_ios PRIVATE ${PROJECT_WARNINGS})

# --- reprise: the launcher application -----------------------------------------
set(REPRISE_SOURCES
    app/main.cpp
    app/catalog.cpp
    app/library.cpp
    games/bruce_lee_c64/game/chapter.cpp
    games/bruce_lee_c64/game/room_chapter.cpp
    games/bruce_lee_c64/game/background_article.cpp
    app/launcher.cpp app/quick_save.cpp
    app/pause_menu.cpp
    app/saved_options.cpp
    app/event_script.cpp)
if(ANDROID)
    add_library(reprise SHARED ${REPRISE_SOURCES})
    set_target_properties(reprise PROPERTIES OUTPUT_NAME main)
    target_link_options(reprise PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_executable(reprise ${REPRISE_SOURCES})
endif()
target_link_libraries(reprise PRIVATE fort_apocalypse_c64 bruce_lee_c64 wizball_c64 marble_madness_amiga mindwalker_amiga wings_of_fury_amiga cannon_fodder_amiga rock_n_roll_amiga tower_of_babel_amiga fa_18_interceptor_amiga spindizzy_worlds_amiga spy_mouse_ios ui)
target_compile_options(reprise PRIVATE ${PROJECT_WARNINGS})

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

add_executable(rock_n_roll_assets tools/rock_n_roll_assets.cpp)
target_link_libraries(rock_n_roll_assets PRIVATE rock_n_roll_amiga)
target_compile_options(rock_n_roll_assets PRIVATE ${PROJECT_WARNINGS})
add_executable(rock_n_roll_replay tools/rock_n_roll_replay.cpp)
target_link_libraries(rock_n_roll_replay PRIVATE rock_n_roll_amiga)
target_compile_options(rock_n_roll_replay PRIVATE ${PROJECT_WARNINGS})
add_executable(tower_of_babel_assets tools/tower_of_babel_assets.cpp)
target_link_libraries(tower_of_babel_assets PRIVATE tower_of_babel_amiga)
target_compile_options(tower_of_babel_assets PRIVATE ${PROJECT_WARNINGS})
add_executable(fa_18_interceptor_assets tools/fa_18_interceptor_assets.cpp)
target_link_libraries(fa_18_interceptor_assets PRIVATE fa_18_interceptor_amiga)
target_compile_options(fa_18_interceptor_assets PRIVATE ${PROJECT_WARNINGS})
add_executable(spindizzy_worlds_assets tools/spindizzy_worlds_assets.cpp)
target_link_libraries(spindizzy_worlds_assets PRIVATE spindizzy_worlds_amiga)
target_compile_options(spindizzy_worlds_assets PRIVATE ${PROJECT_WARNINGS})

add_executable(wings_of_fury_assets tools/wings_of_fury_assets.cpp)
target_link_libraries(wings_of_fury_assets PRIVATE wings_of_fury_amiga)
target_compile_options(wings_of_fury_assets PRIVATE ${PROJECT_WARNINGS})

add_executable(wings_of_fury_replay tools/wings_of_fury_replay.cpp)
target_link_libraries(wings_of_fury_replay PRIVATE wings_of_fury_amiga)
target_compile_options(wings_of_fury_replay PRIVATE ${PROJECT_WARNINGS})

# The same resources are used by loose builds and packaged hosts. Android may
# place their contents in assets/; access goes through SDL IO.
add_custom_target(reprise_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources $<TARGET_FILE_DIR:reprise>/resources)
add_dependencies(reprise reprise_resources)

include(CTest)
if(BUILD_TESTING)
    add_executable(shell_checks tests/shell.cpp app/catalog.cpp app/library.cpp
        games/bruce_lee_c64/game/chapter.cpp
    games/bruce_lee_c64/game/room_chapter.cpp
    games/bruce_lee_c64/game/background_article.cpp
    app/launcher.cpp app/quick_save.cpp app/pause_menu.cpp)
    target_link_libraries(shell_checks PRIVATE fort_apocalypse_c64 bruce_lee_c64 wizball_c64 marble_madness_amiga mindwalker_amiga wings_of_fury_amiga cannon_fodder_amiga rock_n_roll_amiga tower_of_babel_amiga fa_18_interceptor_amiga spindizzy_worlds_amiga spy_mouse_ios ui)
    target_compile_options(shell_checks PRIVATE ${PROJECT_WARNINGS})
    add_dependencies(shell_checks reprise_resources)
    add_test(NAME shell_navigation COMMAND shell_checks)
    add_test(NAME shell_motion COMMAND shell_checks --motion)
endif()

# The screen filter stage on this machine's renderer: GPU shader against its CPU form
# (opens a window; run by hand, see tools/screen_effect_check.cpp).
add_executable(screen_effect_check tools/screen_effect_check.cpp)
target_link_libraries(screen_effect_check PRIVATE ui)
target_compile_options(screen_effect_check PRIVATE ${PROJECT_WARNINGS})

add_executable(marble_madness_replay tools/marble_madness_replay.cpp)
target_link_libraries(marble_madness_replay PRIVATE marble_madness_amiga)
target_compile_options(marble_madness_replay PRIVATE ${PROJECT_WARNINGS})

if(BUILD_TESTING)
    add_executable(persistence_checks tests/persistence.cpp app/catalog.cpp app/quick_save.cpp app/saved_options.cpp)
    target_link_libraries(persistence_checks PRIVATE fort_apocalypse_c64 bruce_lee_c64 wizball_c64 marble_madness_amiga mindwalker_amiga wings_of_fury_amiga cannon_fodder_amiga rock_n_roll_amiga tower_of_babel_amiga fa_18_interceptor_amiga spindizzy_worlds_amiga spy_mouse_ios ui)
    target_compile_options(persistence_checks PRIVATE ${PROJECT_WARNINGS})
    add_test(NAME persistent_saves COMMAND persistence_checks)
    # Spy Mouse through the module interface; skipped unless SPY_MOUSE_CONTENT and
    # SPY_MOUSE_SCRIPT name an imported copy and a level 1-1 touch script (tests/spy_mouse.cpp).
    add_executable(spy_mouse_checks tests/spy_mouse.cpp app/quick_save.cpp)
    target_link_libraries(spy_mouse_checks PRIVATE spy_mouse_ios ui)
    target_compile_options(spy_mouse_checks PRIVATE ${PROJECT_WARNINGS})
    add_test(NAME spy_mouse_module COMMAND spy_mouse_checks)
endif()
