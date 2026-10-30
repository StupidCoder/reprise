cmake_minimum_required(VERSION 3.24)
project(DoubleOCheese VERSION 0.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(SDL3 3.2 CONFIG REQUIRED)

# The recovered game rules keep the original's single-precision operation order: no fused
# multiply-add contraction and no fast-math reassociation, on every compiler.
if(MSVC)
    set(PROJECT_WARNINGS /W3)
    set(EXACT_FLOAT /fp:precise)
else()
    set(PROJECT_WARNINGS -Wall -Wextra -Wno-unused-parameter)
    # The original calls sinf and cosf separately; a fused __sincosf can differ in the last bit.
    set(EXACT_FLOAT -ffp-contract=off -fno-fast-math -fno-builtin-sin -fno-builtin-cos -fno-builtin-sinf -fno-builtin-cosf)
endif()

# Every library includes headers relative to src/, e.g. "game/mouse.hpp".
set(SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- data: readers for the game's own files (keys come from the user's executable) -----
add_library(doc_data
    src/data/game_files.cpp
    src/data/mach_o.cpp
    src/data/obfuscated_file.cpp
    src/data/brd.cpp
    src/data/map_file.cpp
    src/data/level_table.cpp
    src/data/program_tables.cpp
    src/data/anim_sequences.cpp
    src/data/strings.cpp
    src/data/gui_layout.cpp
    src/data/obj_mesh.cpp
    src/data/cutscenes.cpp)
target_include_directories(doc_data PUBLIC ${SOURCE_ROOT})
target_compile_options(doc_data PRIVATE ${PROJECT_WARNINGS})

# --- game: the recovered rules (no platform dependencies) ------------------------------
add_library(doc_game
    src/game/geometry.cpp
    src/game/level_geometry.cpp
    src/game/drawn_path.cpp
    src/game/mouse.cpp
    src/game/objects.cpp
    src/game/random.cpp
    src/game/sprite_anim.cpp
    src/game/particles.cpp
    src/game/ambience.cpp
    src/game/hands.cpp
    src/game/hand_tutorial.cpp
    src/game/goal_popup.cpp
    src/game/pause.cpp
    src/game/cat.cpp
    src/game/droid.cpp
    src/game/session_boss.cpp
    src/game/session_spitter.cpp
    src/game/session_vacuum.cpp
    src/game/session_turret.cpp
    src/game/turret.cpp
    src/game/session_switches.cpp
    src/game/spitter.cpp
    src/game/vacuum.cpp
    src/game/projectiles.cpp
    src/game/object_motion.cpp
    src/game/kiska_popup.cpp
    src/game/dossier.cpp
    src/game/notice.cpp
    src/game/hud.cpp
    src/game/results.cpp
    src/game/profile.cpp
    src/game/level.cpp
    src/game/session.cpp
    src/game/roamer.cpp
    src/game/map_paths.cpp
    src/game/world_map.cpp)
target_include_directories(doc_game PUBLIC ${SOURCE_ROOT})
target_link_libraries(doc_game PUBLIC doc_data)
target_compile_options(doc_game PRIVATE ${PROJECT_WARNINGS} ${EXACT_FLOAT})

# --- media: the game's music (HE-AAC in MP4), decoded portably ----------------------------
add_library(doc_media
    src/media/mp4_audio.cpp
    src/media/aac_tables.cpp
    src/media/transforms.cpp
    src/media/aac_decoder.cpp
    src/media/sbr_decoder.cpp
    src/media/aac_stream.cpp)
target_include_directories(doc_media PUBLIC ${SOURCE_ROOT})
target_compile_options(doc_media PRIVATE ${PROJECT_WARNINGS})

add_executable(media_checks tools/media_checks.cpp)
target_link_libraries(media_checks PRIVATE doc_media)
target_compile_options(media_checks PRIVATE ${PROJECT_WARNINGS})

# --- importer: copies the game's files from the user's IPA (zlib for the ZIP archive) -----
find_package(ZLIB REQUIRED)
add_library(doc_importer src/importer/ipa_import.cpp)
target_include_directories(doc_importer PUBLIC ${SOURCE_ROOT})
target_link_libraries(doc_importer PUBLIC doc_data ZLIB::ZLIB)
target_compile_options(doc_importer PRIVATE ${PROJECT_WARNINGS})

# --- shell: SDL 3 platform layer, drawing, sound and the game's flow (map, levels, saves) ----
# Shared by the standalone app and the Reprise adapter (src/embed).
add_library(doc_shell
    src/app/game_flow.cpp
    src/app/sound_output.cpp
    src/platform/host_sdl.cpp
    src/platform/audio.cpp
    src/platform/image.cpp
    src/render/canvas.cpp
    src/render/level_view.cpp
    src/render/map_view.cpp
    src/render/text.cpp
    src/render/hud_view.cpp
    src/render/popup_view.cpp
    src/render/results_view.cpp
    src/render/kiska_view.cpp
    src/render/dossier_view.cpp
    src/render/pause_view.cpp)
target_include_directories(doc_shell PUBLIC ${SOURCE_ROOT})
# The game's music is HE-AAC, decoded by doc_media on every platform.
target_link_libraries(doc_shell PUBLIC doc_game doc_media SDL3::SDL3)
target_compile_options(doc_shell PRIVATE ${PROJECT_WARNINGS} ${EXACT_FLOAT})

# FreeType renders the game's own TrueType fonts (the original uses FreeType too). Linked
# statically so the app bundle carries no extra library.
find_path(FREETYPE_INCLUDE ft2build.h PATH_SUFFIXES freetype2 REQUIRED)
find_library(FREETYPE_STATIC NAMES libfreetype.a freetype REQUIRED)
find_library(PNG_STATIC NAMES libpng16.a png16 png)
find_library(BZ2_LIB NAMES bz2)
target_include_directories(doc_shell PRIVATE ${FREETYPE_INCLUDE})
target_link_libraries(doc_shell PUBLIC ${FREETYPE_STATIC} ZLIB::ZLIB)
if(PNG_STATIC)
    target_link_libraries(doc_shell PUBLIC ${PNG_STATIC})
endif()
if(BZ2_LIB)
    target_link_libraries(doc_shell PUBLIC ${BZ2_LIB})
endif()

# --- embed: the game inside another application (Reprise); its header needs no engine paths
add_library(doc_embed src/embed/spy_mouse_embed.cpp)
target_include_directories(doc_embed PRIVATE ${SOURCE_ROOT})
target_include_directories(doc_embed INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/embed)
target_link_libraries(doc_embed PRIVATE doc_shell doc_importer)
target_compile_options(doc_embed PRIVATE ${PROJECT_WARNINGS} ${EXACT_FLOAT})

# --- the standalone app (launcher and game) -----------------------------------------------
add_executable(doubleocheese
    src/app/main.cpp
    src/app/launcher.cpp
    src/app/shell_style.cpp)
target_include_directories(doubleocheese PRIVATE ${SOURCE_ROOT})
target_link_libraries(doubleocheese PRIVATE doc_shell doc_importer)
target_compile_options(doubleocheese PRIVATE ${PROJECT_WARNINGS})

# --- verification against reference traces from the original (research tool) ----------
add_executable(doc-verify tools/verify_traces.cpp)
target_include_directories(doc-verify PRIVATE ${SOURCE_ROOT})
target_link_libraries(doc-verify PRIVATE doc_game ZLIB::ZLIB)
target_compile_options(doc-verify PRIVATE ${PROJECT_WARNINGS} ${EXACT_FLOAT})
