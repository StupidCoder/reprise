cmake_minimum_required(VERSION 3.24)
project(BlueStreak VERSION 0.17.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(SDL3 3.2 CONFIG REQUIRED)

# Two ways to build this folder:
# - On its own (the top-level project): the standalone BlueStreak app with its own shell and
#   host (src/app, src/platform) and its checks.
# - Inside Reprise (add_subdirectory from Reprise's CMakeLists, which sets
#   BLUESTREAK_HOST_SHARED to its shared/ folder): only the engine libraries and the embedding
#   interface (src/embed). The engines are then compiled against Reprise's shared
#   "platform/..." headers (image, SHA-256 and the host's constants, which match BlueStreak's
#   copies) and link Reprise's `platform` library; BlueStreak's own host, shell and the
#   duplicate `platform` target are not built, so Reprise links one host implementation only.
set(BLUESTREAK_STANDALONE ${PROJECT_IS_TOP_LEVEL})
if(NOT BLUESTREAK_STANDALONE AND NOT BLUESTREAK_HOST_SHARED)
    message(FATAL_ERROR "Set BLUESTREAK_HOST_SHARED to the embedding host's shared folder.")
endif()

if(MSVC)
    set(PROJECT_WARNINGS /W4)
else()
    set(PROJECT_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# Every library includes headers relative to src/, e.g. "gamegear/psg.hpp". Embedded, the
# host's shared folder comes first, so "platform/..." names the host's headers.
if(BLUESTREAK_STANDALONE)
    set(SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src)
else()
    set(SOURCE_ROOT ${BLUESTREAK_HOST_SHARED} ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

# --- platform: host services (window, input, audio, GPU presentation) ---------
# Standalone only; embedded, `platform` is the host's own library.
if(BLUESTREAK_STANDALONE)
    find_package(Threads REQUIRED)
    add_library(platform
        src/platform/host_sdl.cpp
        src/platform/sha256.cpp
        src/platform/type.cpp
        src/platform/resources.cpp)
    target_include_directories(platform PUBLIC ${SOURCE_ROOT})
    target_link_libraries(platform PUBLIC SDL3::SDL3 gamegear)
    target_compile_options(platform PRIVATE ${PROJECT_WARNINGS})
endif()

# --- gamegear: hardware helpers shared by all Game Gear games ------------------
add_library(gamegear
    src/gamegear/video.cpp
    src/gamegear/tile_screen.cpp
    src/gamegear/psg.cpp
    src/gamegear/music_player.cpp
    src/gamegear/lcd_filter.cpp
    src/gamegear/wide_view.cpp
    src/gamegear/force_field.cpp
    src/gamegear/water_splash.cpp
    src/gamegear/lens_rain.cpp)
target_include_directories(gamegear PUBLIC ${SOURCE_ROOT})
target_compile_options(gamegear PRIVATE ${PROJECT_WARNINGS})

# --- sonic1_gg: Sonic the Hedgehog (Game Gear) reconstruction ------------------
add_library(sonic1_gg
    src/sonic1_gg/module.cpp
    src/sonic1_gg/music_player.cpp
    src/sonic1_gg/data/level_data.cpp
    src/sonic1_gg/data/flow_assets.cpp
    src/sonic1_gg/data/asset_pack.cpp
    src/sonic1_gg/data/content_store.cpp
    src/sonic1_gg/game/game.cpp
    src/sonic1_gg/game/sonic.cpp
    src/sonic1_gg/game/terrain.cpp
    src/sonic1_gg/game/objects.cpp
    src/sonic1_gg/game/common_objects.cpp
    src/sonic1_gg/game/boss.cpp
    src/sonic1_gg/game/special_stage.cpp
    src/sonic1_gg/game/bridge.cpp
    src/sonic1_gg/game/jungle.cpp
    src/sonic1_gg/game/labyrinth.cpp
    src/sonic1_gg/game/scrap_brain_sky_base.cpp
    src/sonic1_gg/game/final_boss.cpp
    src/sonic1_gg/frontend/flow.cpp
    src/sonic1_gg/frontend/session.cpp
    src/sonic1_gg/frontend/results.cpp
    src/sonic1_gg/frontend/game_over.cpp
    src/sonic1_gg/frontend/ending.cpp
    src/sonic1_gg/audio/sound.cpp
    src/sonic1_gg/audio/voice.cpp
    src/sonic1_gg/render/scene.cpp
    src/sonic1_gg/render/game_view.cpp
    src/sonic1_gg/enhancements/widescreen.cpp
    src/sonic1_gg/enhancements/water_effects.cpp
    src/sonic1_gg/enhancements/basin_water.cpp
    src/sonic1_gg/enhancements/ring_effects.cpp
    src/sonic1_gg/save/snapshot.cpp)
target_include_directories(sonic1_gg PUBLIC ${SOURCE_ROOT})
target_link_libraries(sonic1_gg PUBLIC gamegear platform)
target_compile_options(sonic1_gg PRIVATE ${PROJECT_WARNINGS})

# --- gg_engine: the engine Sonic 2 and Triple Trouble (Game Gear) share ------
add_library(gg_engine
    src/gg_engine/data/cartridge.cpp
    src/gg_engine/data/assets.cpp
    src/gg_engine/data/content_store.cpp
    src/gg_engine/video/display.cpp
    src/gg_engine/audio/sound_driver.cpp
    src/gg_engine/audio/voice.cpp
    src/gg_engine/audio/music_player.cpp
    src/gg_engine/enhancements/widescreen.cpp)
target_include_directories(gg_engine PUBLIC ${SOURCE_ROOT})
target_link_libraries(gg_engine PUBLIC gamegear platform)
target_compile_options(gg_engine PRIVATE ${PROJECT_WARNINGS})

# --- sonic2_gg: Sonic the Hedgehog 2 (Game Gear) reconstruction ----------------
add_library(sonic2_gg
    src/sonic2_gg/module.cpp
    src/sonic2_gg/music_player.cpp
    src/sonic2_gg/data/rom.cpp
    src/sonic2_gg/data/assets.cpp
    src/sonic2_gg/audio/sound_driver.cpp
    src/sonic2_gg/audio/voice.cpp
    src/sonic2_gg/frontend/attract.cpp
    src/sonic2_gg/frontend/intro.cpp
    src/sonic2_gg/frontend/zone_select.cpp
    src/sonic2_gg/game/game.cpp
    src/sonic2_gg/game/scripts.cpp
    src/sonic2_gg/game/routine_table.cpp
    src/sonic2_gg/game/timing.cpp
    src/sonic2_gg/game/ending.cpp
    src/sonic2_gg/game/final_boss.cpp
    src/sonic2_gg/game/crystal_egg.cpp
    src/sonic2_gg/game/gimmick.cpp
    src/sonic2_gg/game/gimmick_boss.cpp
    src/sonic2_gg/game/pipes.cpp
    src/sonic2_gg/game/scrambled_egg_boss.cpp
    src/sonic2_gg/game/aqua_lake.cpp
    src/sonic2_gg/game/aqua_lake_boss.cpp
    src/sonic2_gg/game/rain.cpp
    src/sonic2_gg/game/platforms.cpp
    src/sonic2_gg/game/glider.cpp
    src/sonic2_gg/game/green_hills_boss.cpp
    src/sonic2_gg/game/sky_high.cpp
    src/sonic2_gg/game/sky_high_boss.cpp
    src/sonic2_gg/game/capsule.cpp
    src/sonic2_gg/game/act_end.cpp
    src/sonic2_gg/game/game_over.cpp
    src/sonic2_gg/game/interrupt.cpp
    src/sonic2_gg/game/video.cpp
    src/sonic2_gg/game/palettes.cpp
    src/sonic2_gg/game/sprites.cpp
    src/sonic2_gg/game/map.cpp
    src/sonic2_gg/game/level.cpp
    src/sonic2_gg/game/main_loop.cpp
    src/sonic2_gg/game/score.cpp
    src/sonic2_gg/game/objects.cpp
    src/sonic2_gg/game/object_api.cpp
    src/sonic2_gg/game/spawner.cpp
    src/sonic2_gg/game/dispatch.cpp
    src/sonic2_gg/game/player.cpp
    src/sonic2_gg/game/player_support.cpp
    src/sonic2_gg/game/terrain.cpp
    src/sonic2_gg/game/objects_common.cpp
    src/sonic2_gg/game/underground.cpp
    src/sonic2_gg/game/underground_boss.cpp
    src/sonic2_gg/enhancements/widescreen.cpp
    src/sonic2_gg/enhancements/water.cpp
    src/sonic2_gg/render/wide_view.cpp)
target_include_directories(sonic2_gg PUBLIC ${SOURCE_ROOT})
target_link_libraries(sonic2_gg PUBLIC gg_engine gamegear platform)
target_compile_options(sonic2_gg PRIVATE ${PROJECT_WARNINGS})

# --- triple_trouble_gg: Sonic Triple Trouble (Game Gear) reconstruction --------
add_library(triple_trouble_gg
    src/triple_trouble_gg/module.cpp
    src/triple_trouble_gg/music_player.cpp
    src/triple_trouble_gg/data/rom.cpp
    src/triple_trouble_gg/audio/sound_driver.cpp
    src/triple_trouble_gg/audio/voice.cpp
    src/triple_trouble_gg/frontend/program.cpp
    src/triple_trouble_gg/game/game.cpp
    src/triple_trouble_gg/enhancements/widescreen.cpp
    src/triple_trouble_gg/render/wide_view.cpp
    src/triple_trouble_gg/game/scripts.cpp
    src/triple_trouble_gg/game/routine_table.cpp
    src/triple_trouble_gg/game/dispatch.cpp
    src/triple_trouble_gg/game/interrupt.cpp
    src/triple_trouble_gg/game/video.cpp
    src/triple_trouble_gg/game/palettes.cpp
    src/triple_trouble_gg/game/sprites.cpp
    src/triple_trouble_gg/game/objects.cpp
    src/triple_trouble_gg/game/timing.cpp
    src/triple_trouble_gg/game/level.cpp
    src/triple_trouble_gg/game/camera.cpp
    src/triple_trouble_gg/game/terrain.cpp
    src/triple_trouble_gg/game/intro.cpp
    src/triple_trouble_gg/frontend/intro.cpp
    src/triple_trouble_gg/frontend/title.cpp
    src/triple_trouble_gg/frontend/select.cpp
    src/triple_trouble_gg/frontend/title_sound_test.cpp
    src/triple_trouble_gg/frontend/play.cpp
    src/triple_trouble_gg/frontend/tally.cpp
    src/triple_trouble_gg/frontend/ending.cpp
    src/triple_trouble_gg/frontend/flight.cpp
    src/triple_trouble_gg/game/title.cpp
    src/triple_trouble_gg/frontend/demo.cpp
    src/triple_trouble_gg/game/physics.cpp
    src/triple_trouble_gg/game/player.cpp
    src/triple_trouble_gg/game/boss_turquoise.cpp
    src/triple_trouble_gg/game/card.cpp
    src/triple_trouble_gg/game/crawler.cpp
    src/triple_trouble_gg/game/board.cpp
    src/triple_trouble_gg/game/train.cpp
    src/triple_trouble_gg/game/rival.cpp
    src/triple_trouble_gg/game/boss_winter.cpp
    src/triple_trouble_gg/game/jet.cpp
    src/triple_trouble_gg/game/boss_destroyer.cpp
    src/triple_trouble_gg/game/boss_final.cpp
    src/triple_trouble_gg/game/special_guardians.cpp
    src/triple_trouble_gg/game/credits.cpp
    src/triple_trouble_gg/game/flight_objects.cpp
    src/triple_trouble_gg/game/save.cpp
    src/triple_trouble_gg/game/destroyer_end.cpp
    src/triple_trouble_gg/game/boss_jungle.cpp
    src/triple_trouble_gg/game/boss_tidal.cpp
    src/triple_trouble_gg/game/special.cpp
    src/triple_trouble_gg/game/sparkle.cpp
    src/triple_trouble_gg/game/collision.cpp
    src/triple_trouble_gg/game/damage.cpp
    src/triple_trouble_gg/game/enemies.cpp
    src/triple_trouble_gg/game/explosion.cpp
    src/triple_trouble_gg/game/item_box.cpp
    src/triple_trouble_gg/game/platforms.cpp
    src/triple_trouble_gg/game/power_ups.cpp
    src/triple_trouble_gg/game/rings.cpp
    src/triple_trouble_gg/game/shells.cpp
    src/triple_trouble_gg/game/barrel.cpp
    src/triple_trouble_gg/game/signpost.cpp
    src/triple_trouble_gg/game/springs.cpp
    src/triple_trouble_gg/game/water_objects.cpp
    src/triple_trouble_gg/game/paths.cpp
    src/triple_trouble_gg/game/player_states.cpp)
target_include_directories(triple_trouble_gg PUBLIC ${SOURCE_ROOT})
target_link_libraries(triple_trouble_gg PUBLIC gg_engine gamegear platform)
target_compile_options(triple_trouble_gg PRIVATE ${PROJECT_WARNINGS})

# --- gba: hardware helpers shared by all Game Boy Advance games ----------------
add_library(gba
    src/gba/display.cpp
    src/gba/apu.cpp)
target_include_directories(gba PUBLIC ${SOURCE_ROOT})
target_compile_options(gba PRIVATE ${PROJECT_WARNINGS})

# --- sonic_advance_gba: Sonic Advance (Game Boy Advance) reconstruction ----------
add_library(sonic_advance_gba
    src/sonic_advance_gba/module.cpp
    src/sonic_advance_gba/data/rom.cpp
    src/sonic_advance_gba/data/assets.cpp
    src/sonic_advance_gba/data/content_store.cpp
    src/sonic_advance_gba/game/game.cpp
    src/sonic_advance_gba/game/scheduler.cpp
    src/sonic_advance_gba/game/video.cpp
    src/sonic_advance_gba/game/background.cpp
    src/sonic_advance_gba/game/sprite.cpp
    src/sonic_advance_gba/game/fade.cpp
    src/sonic_advance_gba/game/graphics.cpp
    src/sonic_advance_gba/game/window_shapes.cpp
    src/sonic_advance_gba/game/searchlight.cpp
    src/sonic_advance_gba/frontend/logos.cpp
    src/sonic_advance_gba/frontend/intro.cpp
    src/sonic_advance_gba/frontend/title.cpp
    src/sonic_advance_gba/frontend/character_select.cpp
    src/sonic_advance_gba/game/sound.cpp
    src/sonic_advance_gba/audio/sound_engine.cpp
    src/sonic_advance_gba/game/collision.cpp
    src/sonic_advance_gba/game/player.cpp
    src/sonic_advance_gba/game/player_effects.cpp
    src/sonic_advance_gba/game/camera.cpp
    src/sonic_advance_gba/game/stage.cpp
    src/sonic_advance_gba/game/title_card.cpp
    src/sonic_advance_gba/game/hud.cpp
    src/sonic_advance_gba/game/rings.cpp
    src/sonic_advance_gba/game/objects.cpp
    src/sonic_advance_gba/game/demo.cpp
    src/sonic_advance_gba/game/object_util.cpp
    src/sonic_advance_gba/game/boss.cpp
    src/sonic_advance_gba/game/game_over.cpp
    src/sonic_advance_gba/game/secret_base.cpp
    src/sonic_advance_gba/game/casino_paradise.cpp
    src/sonic_advance_gba/game/ice_paradise.cpp
    src/sonic_advance_gba/game/angel_island.cpp
    src/sonic_advance_gba/game/egg_rocket.cpp
    src/sonic_advance_gba/game/x_zone.cpp
    src/sonic_advance_gba/game/moon_zone.cpp
    src/sonic_advance_gba/game/ending.cpp
    src/sonic_advance_gba/game/special_stage.cpp
    src/sonic_advance_gba/game/water.cpp
    src/sonic_advance_gba/game/destruction.cpp
    src/sonic_advance_gba/game/results.cpp
    src/sonic_advance_gba/game/enemies.cpp
    src/sonic_advance_gba/game/solid.cpp
    src/sonic_advance_gba/game/interactive.cpp
    src/sonic_advance_gba/game/item_box.cpp)
target_include_directories(sonic_advance_gba PUBLIC ${SOURCE_ROOT})
target_link_libraries(sonic_advance_gba PUBLIC gba platform)
target_compile_options(sonic_advance_gba PRIVATE ${PROJECT_WARNINGS})

# --- embed: the games inside another application (Reprise); its header needs no engine paths
add_library(bluestreak_embed src/embed/bluestreak_embed.cpp)
target_include_directories(bluestreak_embed PRIVATE ${SOURCE_ROOT})
target_include_directories(bluestreak_embed INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/embed)
target_link_libraries(bluestreak_embed PRIVATE sonic1_gg sonic2_gg triple_trouble_gg sonic_advance_gba
    gamegear platform)
target_compile_options(bluestreak_embed PRIVATE ${PROJECT_WARNINGS})

if(NOT BLUESTREAK_STANDALONE)
    return()
endif()

# --- app: the BlueStreak shell -------------------------------------------------
add_executable(bluestreak
    src/app/main.cpp
    src/app/shell_frame.cpp
    src/app/library_screen.cpp
    src/app/level_select_screen.cpp
    src/app/music_screen.cpp
    src/app/pause_menu.cpp
    src/app/transitions.cpp
    src/app/shell_style.cpp
    src/app/level_select.cpp
    src/app/quick_save.cpp
    src/app/titles.cpp)
target_link_libraries(bluestreak PRIVATE sonic1_gg sonic2_gg triple_trouble_gg sonic_advance_gba platform Threads::Threads)
target_compile_options(bluestreak PRIVATE ${PROJECT_WARNINGS})

# Fonts travel with loose builds and can be packaged as SDL assets on other hosts.
add_custom_target(shell_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/resources ${CMAKE_CURRENT_BINARY_DIR}/resources)
add_dependencies(bluestreak shell_resources)

include(CTest)
if(BUILD_TESTING)
    add_executable(shell_checks tests/shell.cpp
        src/app/shell_frame.cpp src/app/library_screen.cpp src/app/level_select_screen.cpp
        src/app/music_screen.cpp src/app/pause_menu.cpp src/app/transitions.cpp
        src/app/shell_style.cpp src/app/level_select.cpp src/app/titles.cpp)
    target_link_libraries(shell_checks PRIVATE sonic1_gg sonic2_gg triple_trouble_gg sonic_advance_gba platform Threads::Threads)
    target_compile_options(shell_checks PRIVATE ${PROJECT_WARNINGS})
    add_dependencies(shell_checks shell_resources)
    add_test(NAME shell_checks COMMAND shell_checks)
endif()
