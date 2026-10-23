cmake_minimum_required(VERSION 3.24)
project(Joyport VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(SDL3 3.2 CONFIG REQUIRED)

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
    src/platform/sha256.cpp)
target_include_directories(platform PUBLIC ${SOURCE_ROOT})
target_link_libraries(platform PUBLIC SDL3::SDL3)
target_compile_options(platform PRIVATE ${PROJECT_WARNINGS})

# --- ui: the launcher's drawing kit --------------------------------------------
add_library(ui src/ui/draw.cpp)
target_include_directories(ui PUBLIC ${SOURCE_ROOT})
target_compile_options(ui PRIVATE ${PROJECT_WARNINGS})

# --- c64: formats and helpers shared by the C64 games --------------------------
add_library(c64
    src/c64/tape_image.cpp
    src/c64/kernal_tape.cpp
    src/c64/text_frame.cpp
    src/c64/sid.cpp)
target_include_directories(c64 PUBLIC ${SOURCE_ROOT})
target_compile_options(c64 PRIVATE ${PROJECT_WARNINGS})

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
    src/fort_apocalypse_c64/game/module.cpp
    src/fort_apocalypse_c64/enhanced/canvas.cpp
    src/fort_apocalypse_c64/enhanced/enhanced.cpp
    src/fort_apocalypse_c64/import/novaload.cpp
    src/fort_apocalypse_c64/import/tape_import.cpp)
target_include_directories(fort_apocalypse_c64 PUBLIC ${SOURCE_ROOT})
target_link_libraries(fort_apocalypse_c64 PUBLIC c64 platform)
target_compile_options(fort_apocalypse_c64 PRIVATE ${PROJECT_WARNINGS})

# --- joyport: the launcher application -----------------------------------------
add_executable(joyport
    src/app/main.cpp
    src/app/catalog.cpp
    src/app/library.cpp
    src/app/launcher.cpp
    src/app/pause_menu.cpp)
target_link_libraries(joyport PRIVATE ui fort_apocalypse_c64)
target_compile_options(joyport PRIVATE ${PROJECT_WARNINGS})

# --- tools ---------------------------------------------------------------------
add_executable(fort_apocalypse_assets tools/fort_apocalypse_assets.cpp)
target_link_libraries(fort_apocalypse_assets PRIVATE fort_apocalypse_c64)
target_compile_options(fort_apocalypse_assets PRIVATE ${PROJECT_WARNINGS})

add_executable(fort_apocalypse_replay tools/fort_apocalypse_replay.cpp)
target_link_libraries(fort_apocalypse_replay PRIVATE fort_apocalypse_c64)
target_compile_options(fort_apocalypse_replay PRIVATE ${PROJECT_WARNINGS})
