cmake_minimum_required(VERSION 3.20)
project(picarat LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Engine, game logic and ROM importer: standard C++20 and zlib only.
add_library(picarat_core
    # system
    src/system/a1_language.cpp
    src/system/a1_script.cpp
    src/system/a1_ui.cpp
    src/system/a2_character_names.cpp
    src/system/a2_checkpoint.cpp
    src/system/interaction.cpp
    src/system/resources.cpp
    src/system/a2_save_file.cpp
    src/system/console_id.cpp
    src/system/scene_kit.cpp
    src/system/script.cpp
    src/system/a4_gss.cpp
    src/system/a56_xq.cpp
    src/graphics/a56_picture.cpp
    src/graphics/a56_mesh.cpp
    src/graphics/a56_model.cpp
    src/graphics/a56_camera.cpp
    src/graphics/a56_assets.cpp
    src/graphics/ctr3d/raster.cpp
    src/graphics/a56_font.cpp
    src/graphics/a56_text.cpp
    src/graphics/a56_message.cpp
    src/system/a56_tween.cpp
    src/system/a56_movie.cpp
    src/system/a56_scene.cpp
    src/system/a5_game.cpp
    src/system/a5_services.cpp
    src/system/a6_services.cpp
    src/system/a5_progress.cpp
    src/system/a5_saves.cpp
    src/system/a5_story_services.cpp
    src/story/a5_story.cpp
    src/story/a5_ruins.cpp
    src/story/a5_horse.cpp
    src/story/a5_rabbit.cpp
    src/story/a5_rabbit_commu.cpp
    src/story/a5_rabbit_nazo.cpp
    src/story/a5_rabbit_show.cpp
    src/story/a5_robot.cpp
    src/story/a5_secrets.cpp
    src/story/a5_shop.cpp
    src/puzzles/a5_puzzle.cpp
    src/puzzles/a6_puzzle.cpp
    src/puzzles/a6_arrow_pad.cpp
    src/puzzles/a6_bridge_puzzle.cpp
    src/puzzles/a6_fit_puzzle.cpp
    src/puzzles/a6_guard_puzzle.cpp
    src/puzzles/a6_loop.cpp
    src/puzzles/a6_partition_puzzle.cpp
    src/puzzles/a6_route_puzzle.cpp
    src/puzzles/a6_beam_puzzle.cpp
    src/puzzles/a6_pad_hooks.cpp
    src/puzzles/a6_slide2_puzzle.cpp
    src/puzzles/a6_texture_puzzle.cpp
    src/puzzles/a6_transfer_puzzle.cpp
    src/puzzles/a6_rope_puzzle.cpp
    src/puzzles/a5_special.cpp
    src/puzzles/a5_line_puzzle.cpp
    src/puzzles/a5_grid_line_puzzle.cpp
    src/puzzles/a5_pair_puzzle.cpp
    src/puzzles/a5_block_puzzle.cpp
    src/puzzles/a5_glide_puzzle.cpp
    src/puzzles/a5_place_puzzle.cpp
    src/puzzles/a5_pile_puzzle.cpp
    src/puzzles/a5_float_puzzle.cpp
    src/puzzles/a5_vanish_puzzle.cpp
    src/puzzles/a5_corn_puzzle.cpp
    src/puzzles/a5_brick_puzzle.cpp
    src/puzzles/a5_slip_puzzle.cpp
    src/puzzles/a5_flip_puzzle.cpp
    src/puzzles/a5_stroke_puzzle.cpp
    src/puzzles/a5_balloon_puzzle.cpp
    src/puzzles/a56_handwriting.cpp
    src/graphics/a4_assets.cpp
    src/system/a4_tween.cpp
    src/system/a4_scene.cpp
    src/system/a4_services.cpp
    src/system/a4_loop.cpp
    src/system/a4_definitions.cpp
    src/system/a4_progress.cpp
    src/system/a4_saves.cpp
    src/story/a4_game.cpp
    src/puzzles/a4_puzzle.cpp
    src/puzzles/a4_slide.cpp
    src/puzzles/a4_jump.cpp
    src/puzzles/a4_cut.cpp
    src/puzzles/a4_lead.cpp
    src/puzzles/a4_line.cpp
    src/puzzles/a4_vanish.cpp
    src/puzzles/a4_slide2.cpp
    src/puzzles/a4_trampoline.cpp
    src/puzzles/a4_ltgram.cpp
    src/puzzles/a4_loop_kit.cpp
    src/system/a3_lscr.cpp
    src/system/a3_lscr_core.cpp
    src/system/a3_lscr_scene.cpp
    src/system/a3_lscr_scene_commands.cpp
    src/puzzles/a1_catalogue.cpp
    src/puzzles/a1_trace_button.cpp
    src/puzzles/a1_cut.cpp
    src/puzzles/a1_tile.cpp
    src/puzzles/a1_slide.cpp
    src/puzzles/a1_queen.cpp
    src/puzzles/a1_onoff.cpp
    src/puzzles/a1_trace.cpp
    src/puzzles/a1_cup.cpp
    src/puzzles/a1_match.cpp
    src/puzzles/a1_drag.cpp
    src/puzzles/a1_scale.cpp
    src/puzzles/a1_connect.cpp
    src/puzzles/a1_river.cpp
    src/puzzles/a1_place.cpp
    src/puzzles/a1_coin.cpp
    src/puzzles/a1_engines.cpp
    src/puzzles/a1_puzzle.cpp
    src/puzzles/a1_puzzle_flow.cpp
    src/puzzles/a3_engines.cpp
    src/puzzles/a3_puzzle_loop.cpp
    src/system/a2_timing.cpp
    # graphics
    src/graphics/a2_ds3d.cpp
    src/graphics/ds_pipeline.cpp
    src/graphics/graphics.cpp
    src/graphics/a3_assets.cpp
    src/graphics/a3_text.cpp
    src/graphics/a3_message.cpp
    src/graphics/text.cpp
    # graphics/nds3d
    src/graphics/nds3d/fx.cpp
    src/graphics/nds3d/g3d.cpp
    src/graphics/nds3d/gx.cpp
    src/graphics/nds3d/raster.cpp
    # video
    src/video/mobiclip.cpp
    src/video/mobiclip_core.cpp
    src/video/moflex.cpp
    src/video/movie.cpp
    # sound
    src/sound/ds_sound.cpp
    src/sound/sdat.cpp
    src/sound/sdat_sequencer.cpp
    src/sound/sdat_player.cpp
    src/sound/a1_sound.cpp
    src/sound/a1_stream.cpp
    src/sound/dse_driver.cpp
    src/sound/sad_audio.cpp
    src/sound/sad_stream.cpp
    src/sound/sound_engine.cpp
    src/sound/a3_sound.cpp
    src/sound/a4_sound.cpp
    src/sound/a56_stream.cpp
    src/sound/a56_sound.cpp
    src/sound/a56_stream_players.cpp
    src/sound/a56_sound_archive.cpp
    src/sound/a56_sequence.cpp
    src/sound/sound_object.cpp
    # story
    src/story/a2_game_state.cpp
    src/story/a2_save_slots.cpp
    src/story/a3_data.cpp
    src/menus/a1_hotel.cpp
    src/menus/a1_mysteries.cpp
    src/menus/a1_painting.cpp
    src/menus/a1_puzzle_index.cpp
    src/menus/a1_robot_dog.cpp
    src/menus/a1_save.cpp
    src/menus/a1_bonus.cpp
    src/menus/a1_challenge.cpp
    src/menus/a1_wifi.cpp
    src/menus/a1_title.cpp
    src/menus/a1_trunk.cpp
    src/menus/a1_name_entry.cpp
    src/story/a1_event.cpp
    src/story/a1_opening.cpp
    src/story/a1_room.cpp
    src/story/a1_state.cpp
    src/story/a1_staff_roll.cpp
    src/story/a1_story.cpp
    src/story/a1_story_commands.cpp
    src/story/a3_story.cpp
    src/story/a3_story_commands.cpp
    src/story/a3_loops.cpp
    src/story/a3_name_entry.cpp
    src/story/a3_saves.cpp
    src/minigames/a3_book.cpp
    src/minigames/a3_car.cpp
  src/minigames/a3_parrot.cpp
    src/story/a3_talk.cpp
    src/story/a2_narration.cpp
    src/story/a2_game.cpp
    src/story/a2_staff_roll.cpp
    src/story/a2_story_game.cpp
    # puzzles
    src/puzzles/a2_answer_puzzle.cpp
    src/puzzles/written_answer.cpp
    src/puzzles/a2_bridge_puzzle.cpp
    src/puzzles/a2_choice_puzzle.cpp
    src/puzzles/circle_ink.cpp
    src/puzzles/circle_input.cpp
    src/puzzles/circle_parameters.cpp
    src/puzzles/circle_stroke.cpp
    src/puzzles/a2_color_puzzle.cpp
    src/puzzles/a2_couple_puzzle.cpp
    src/puzzles/handwriting.cpp
    src/puzzles/judgment.cpp
    src/puzzles/a2_knight_puzzle.cpp
    src/puzzles/a2_lamp_puzzle.cpp
    src/puzzles/line_puzzle.cpp
    src/puzzles/a2_peg_puzzle.cpp
    src/puzzles/a2_puzzle_game.cpp
    src/puzzles/a2_puzzle_progress.cpp
    src/puzzles/a2_rose_puzzle.cpp
    src/puzzles/a2_selection_puzzle.cpp
    src/puzzles/a2_skate_puzzle.cpp
    src/puzzles/a2_slide_puzzle.cpp
    src/puzzles/slot_puzzle.cpp
    src/puzzles/a2_tile_puzzle.cpp
    src/puzzles/a2_toggle_puzzle.cpp
    src/puzzles/a2_tower_puzzle.cpp
    src/puzzles/trace_puzzle.cpp
    # minigames
    src/minigames/a2_camera_game.cpp
    src/minigames/a2_hamster_game.cpp
    src/minigames/a2_hamster_walk.cpp
    src/minigames/a2_photo_game.cpp
    src/minigames/a2_tea_game.cpp
    src/minigames/a4_fish.cpp
    src/minigames/a4_minigames.cpp
    src/minigames/a4_mouse.cpp
    src/minigames/a4_puppet.cpp
    src/minigames/a4_train.cpp
    src/minigames/a6_garden.cpp
    src/minigames/a6_minigame.cpp
    src/minigames/a6_nut_roller.cpp
    # menus
    src/menus/a2_bag_menu.cpp
    src/menus/a2_bonus_menus.cpp
    src/menus/a2_challenge_menu.cpp
    src/menus/a2_diary_menu.cpp
    src/menus/a2_file_dialog.cpp
    src/menus/a2_mystery_menu.cpp
    src/menus/a2_nazoba_menu.cpp
    src/menus/a2_notes_menu.cpp
    src/menus/a2_puzzle_index.cpp
    # import
    src/import/a1_formats.cpp
    src/import/a1_pack.cpp
    src/import/a3_formats.cpp
    src/import/a3_pack.cpp
    src/import/a4_formats.cpp
    src/import/a4_pack.cpp
    src/import/a56_formats.cpp
    src/import/a56_pack.cpp
    src/import/asset_codecs.cpp
    src/import/asset_pack.cpp
    src/import/rom_image.cpp
    src/import/ctr_image.cpp
    src/import/rom_import.cpp
    src/import/rom_source.cpp
    src/import/sha256.cpp
)
find_package(ZLIB REQUIRED)
target_link_libraries(picarat_core PUBLIC ZLIB::ZLIB)
target_include_directories(picarat_core PUBLIC src)
# Warnings are errors. The recovered game logic keeps the original
# floating-point operation order: no fused multiply-add contraction.
if(MSVC)
  set(PICARAT_WARNINGS /W3)
  set(PICARAT_EXACT_FLOAT /fp:precise)
else()
  set(PICARAT_WARNINGS -Wall -Wextra -Wpedantic -Werror)
  set(PICARAT_EXACT_FLOAT -ffp-contract=off)
endif()
target_compile_options(picarat_core PRIVATE ${PICARAT_WARNINGS} ${PICARAT_EXACT_FLOAT})

# Embedded in Reprise (its CMakeLists includes this folder with PICARAT_EMBEDDED): the engine and
# its embedding interface (src/embed) only. The game sessions run as in the app, but their sound
# is pulled by the host in step with the frames (src/platform/audio_pull.*, host_embed.cpp) and
# no window, device or shell of Picarat's own is built. The standalone app, its tools and tests
# stay with the standalone build of this folder.
if(PICARAT_EMBEDDED)
  add_library(picarat_embed STATIC
    src/embed/layton_embed.cpp
    src/shell/game_session.cpp
    src/shell/audio_output.cpp
    src/shell/library.cpp
    src/shell/minigames.cpp
    src/shell/utf8.cpp
    src/platform/audio_pull.cpp
    src/platform/host_embed.cpp)
  target_include_directories(picarat_embed PRIVATE src INTERFACE src/embed)
  target_link_libraries(picarat_embed PRIVATE picarat_core)
  target_compile_options(picarat_embed PRIVATE ${PICARAT_WARNINGS} ${PICARAT_EXACT_FLOAT})
  return()
endif()

# Command-line importer: picarat_import ROM DESTINATION, or --pack for a test pack.
if(NOT ANDROID)
  add_executable(picarat_import tools/import_rom.cpp)
  target_link_libraries(picarat_import PRIVATE picarat_core)
endif()

# The app: the shell (library, puzzle collection, game screen), drawn by
# Picarat itself, on SDL 3 for the window, input, sound and file picker.
# Without SDL 3 only the library, the importer and the tests are built.
find_package(SDL3 3.4 CONFIG)
if(SDL3_FOUND)
  add_library(picarat_platform STATIC src/platform/host_sdl.cpp src/platform/type.cpp src/platform/audio_pull.cpp)
  target_include_directories(picarat_platform PUBLIC src)
  target_link_libraries(picarat_platform PUBLIC SDL3::SDL3)
  target_compile_options(picarat_platform PRIVATE ${PICARAT_WARNINGS})

  set(PICARAT_SHELL
    src/shell/app.cpp
    src/shell/audio_output.cpp
    src/shell/game_hub.cpp
    src/shell/game_menu.cpp
    src/shell/game_screen.cpp
    src/shell/game_session.cpp
    src/shell/library.cpp
    src/shell/library_screen.cpp
    src/shell/main.cpp
    src/shell/minigame_screen.cpp
    src/shell/minigames.cpp
    src/shell/movie_screen.cpp
    src/shell/movies.cpp
    src/shell/music_player.cpp
    src/shell/music_screen.cpp
    src/shell/puzzle_mode.cpp
    src/shell/self_test.cpp
    src/shell/style.cpp
    src/shell/utf8.cpp
    src/shell/ui_font_data.cpp
    src/shell/widgets.cpp)
  set(PICARAT_ART
    art/canal.png
    art/clock.png
    art/logo.png
    art/mask.png
    art/sanctuary.png
    art/train.png
    art/village.png)
  set(PICARAT_FONTS resources/fonts/Lato-Regular.ttf resources/fonts/Lato-Bold.ttf resources/fonts/OFL.txt)
  if(APPLE)
    set_source_files_properties(${PICARAT_FONTS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/fonts)
    set_source_files_properties(resources/licenses/stb_truetype.txt PROPERTIES MACOSX_PACKAGE_LOCATION Resources/licenses)
  endif()
  if(ANDROID)
    # Android loads the app as libmain.so from its Java activity;
    # build-android.sh packs it, SDL and the artwork (as assets) into an APK.
    add_library(picarat SHARED ${PICARAT_SHELL})
    set_target_properties(picarat PROPERTIES OUTPUT_NAME main)
  elseif(APPLE)
    # A macOS app bundle; build.sh adds SDL to it for distribution.
    add_executable(picarat MACOSX_BUNDLE ${PICARAT_SHELL} ${PICARAT_ART} ${PICARAT_FONTS} resources/licenses/stb_truetype.txt)
    set_source_files_properties(${PICARAT_ART} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/art)
    set_target_properties(picarat PROPERTIES
      OUTPUT_NAME Picarat
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in)
  else()
    # The artwork goes next to the executable.
    add_executable(picarat ${PICARAT_SHELL})
    add_custom_command(TARGET picarat POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory resources/fonts $<TARGET_FILE_DIR:picarat>/fonts
      COMMAND ${CMAKE_COMMAND} -E copy_directory resources/licenses $<TARGET_FILE_DIR:picarat>/licenses
      COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:picarat>/art
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PICARAT_ART} $<TARGET_FILE_DIR:picarat>/art
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
  target_link_libraries(picarat PRIVATE picarat_core picarat_platform)
  target_compile_options(picarat PRIVATE ${PICARAT_WARNINGS} ${PICARAT_EXACT_FLOAT})
else()
  message(STATUS "SDL 3.4 not found: building without the app")
endif()

# The Android build is the app alone; tests and tools run on the desktop.
if(ANDROID)
  return()
endif()

# Unit tests. Tests that need game data read a pack in the build folder,
# created with: picarat_import --pack ROM <build>/native-assets
# Without it they report "skipped" (exit code 77).
enable_testing()
set(PICARAT_TEST_ASSETS ${CMAKE_CURRENT_BINARY_DIR}/native-assets)
function(picarat_test name)
  add_executable(${name} tests/${name}.cpp)
  target_link_libraries(${name} PRIVATE picarat_core)
  add_test(NAME ${name} COMMAND ${name} ${ARGN})
  set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
endfunction()
picarat_test(core_tests)
picarat_test(import_tests)
picarat_test(a2_puzzle_progress_tests)
picarat_test(script_tests)
picarat_test(a3_lscr_tests)
# Fourth adventure: data part runs when build-release/blfd-assets holds its pack.
picarat_test(a4_gss_tests ${CMAKE_CURRENT_BINARY_DIR}/blfd-assets)
# 3DS adventures: data part runs when build-release/akkd-assets holds the fifth's pack.
picarat_test(a56_xq_tests ${CMAKE_CURRENT_BINARY_DIR}/akkd-assets)
picarat_test(a4_saves_tests ${CMAKE_CURRENT_BINARY_DIR}/a4-saves-test)
picarat_test(circle_stroke_tests)
picarat_test(a2_checkpoint_tests ${CMAKE_CURRENT_BINARY_DIR}/checkpoint-test)
picarat_test(a2_save_file_tests ${CMAKE_CURRENT_BINARY_DIR}/save-file-test)
picarat_test(a2_story_state_tests ${PICARAT_TEST_ASSETS})
picarat_test(a2_rotating_puzzle_tests ${PICARAT_TEST_ASSETS} ${CMAKE_CURRENT_SOURCE_DIR}/tests/data)
picarat_test(a2_tower_puzzle_tests ${PICARAT_TEST_ASSETS})
picarat_test(mobiclip_tests ${PICARAT_TEST_ASSETS})
# 3DS movies: data part runs when build-release/akkd-assets (and al6d-assets) hold the packs.
picarat_test(moflex_tests ${CMAKE_CURRENT_BINARY_DIR}/akkd-assets ${CMAKE_CURRENT_BINARY_DIR}/al6d-assets)
# First adventure: data part runs when build-release/a5fp-assets holds its pack.
picarat_test(a1_script_tests ${CMAKE_CURRENT_BINARY_DIR}/a5fp-assets)
picarat_test(a1_sound_tests ${CMAKE_CURRENT_BINARY_DIR}/a5fp-assets)

# Local research tools, kept outside the repository.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.analysis/CMakeLists.txt)
  include(.analysis/CMakeLists.txt)
endif()

# Shell regressions run without importing a cartridge or changing user saves.
if(SDL3_FOUND AND NOT ANDROID)
  set(PICARAT_PREVIEW_SOURCES ${PICARAT_SHELL})
  list(REMOVE_ITEM PICARAT_PREVIEW_SOURCES src/shell/main.cpp)
  add_executable(shell_layout_tests tests/shell_layout_tests.cpp ${PICARAT_PREVIEW_SOURCES})
  target_link_libraries(shell_layout_tests PRIVATE picarat_core picarat_platform)
  target_compile_definitions(shell_layout_tests PRIVATE PICARAT_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  file(COPY resources/fonts DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  add_test(NAME shell_layout_tests COMMAND shell_layout_tests)
endif()
