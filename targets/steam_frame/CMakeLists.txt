# Can also be configured directly for a hardware bring-up build.
cmake_minimum_required(VERSION 3.18)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(RepriseSteamFrameProbe LANGUAGES CXX)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    message(FATAL_ERROR "Build the Steam Frame probe for Linux ARM64")
endif()

find_package(Vulkan REQUIRED)
find_package(OpenXR CONFIG QUIET)
if(NOT TARGET OpenXR::openxr_loader)
    # SteamOS ships its loader with SteamVR, without a development package.
    find_path(OPENXR_INCLUDE_DIR openxr/openxr.h REQUIRED)
    find_library(OPENXR_LOADER openxr_loader REQUIRED)
    add_library(OpenXR::openxr_loader SHARED IMPORTED)
    set_target_properties(OpenXR::openxr_loader PROPERTIES
        IMPORTED_LOCATION "${OPENXR_LOADER}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENXR_INCLUDE_DIR}")
endif()

add_executable(reprise-frame-probe probe.cpp floor.cpp xr_texture.cpp)
target_include_directories(reprise-frame-probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../shared")
find_program(REPRISE_GLSLC glslc REQUIRED)
foreach(name floor scene)
foreach(stage vert frag)
    set(shader "${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.${stage}.spv")
    add_custom_command(OUTPUT "${shader}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND "${REPRISE_GLSLC}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.${stage}" -o "${shader}"
        DEPENDS "shaders/${name}.${stage}" VERBATIM)
    target_sources(reprise-frame-probe PRIVATE "${shader}")
    install(FILES "${shader}" DESTINATION shaders COMPONENT SteamFrame)
endforeach()
endforeach()
target_compile_features(reprise-frame-probe PRIVATE cxx_std_20)
target_compile_options(reprise-frame-probe PRIVATE
    -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
target_link_libraries(reprise-frame-probe PRIVATE Vulkan::Vulkan OpenXR::openxr_loader)
option(REPRISE_FRAME_GAME_BRIDGE "Test the existing Spy Mouse module through SDL GPU readback" OFF)
if(REPRISE_FRAME_GAME_BRIDGE)
    find_package(SDL3 3.4 CONFIG REQUIRED)
    get_filename_component(REPRISE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    target_sources(reprise-frame-probe PRIVATE game_bridge.cpp ring.cpp stereo_study.cpp)
    if(TARGET spy_mouse_ios)
        target_link_libraries(reprise-frame-probe PRIVATE spy_mouse_ios)
    else()
        add_subdirectory("${REPRISE_ROOT}/games/spy_mouse_ios" spy-mouse EXCLUDE_FROM_ALL)
        target_sources(reprise-frame-probe PRIVATE "${REPRISE_ROOT}/games/spy_mouse_ios/reprise/entry.cpp")
        target_link_libraries(reprise-frame-probe PRIVATE doc_embed)
    endif()
    target_include_directories(reprise-frame-probe PRIVATE "${REPRISE_ROOT}"
        "${REPRISE_ROOT}/shared" "${REPRISE_ROOT}/games")
    target_link_libraries(reprise-frame-probe PRIVATE SDL3::SDL3)
    target_compile_definitions(reprise-frame-probe PRIVATE REPRISE_FRAME_GAME_BRIDGE=1)
endif()
if(REPRISE_STEAM_FRAME_LAUNCHER)
    # GCC 15 diagnoses the existing handwriting permutation indexing at -O3.
    # Keep the diagnostic visible; do not alter recovered game rules merely to
    # bring up this presentation target. This game path needs a separate audit.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_property(SOURCE "${REPRISE_ROOT}/games/layton/src/puzzles/handwriting.cpp"
            TARGET_DIRECTORY picarat_core APPEND PROPERTY COMPILE_OPTIONS -Wno-error=array-bounds)
    endif()
    set(FRAME_APP_SOURCES ${REPRISE_SOURCES})
    list(REMOVE_ITEM FRAME_APP_SOURCES app/main.cpp)
    list(TRANSFORM FRAME_APP_SOURCES PREPEND "${REPRISE_ROOT}/")
    target_sources(reprise-frame-probe PRIVATE ${FRAME_APP_SOURCES} launcher_panel.cpp gameplay.cpp scene_gpu.cpp scene_textures.cpp stereo_scene.cpp)
    target_link_libraries(reprise-frame-probe PRIVATE ${REPRISE_CATALOG_LIBRARIES})
    target_compile_definitions(reprise-frame-probe PRIVATE REPRISE_FRAME_LAUNCHER=1)
    install(DIRECTORY "${REPRISE_ROOT}/resources/" DESTINATION resources COMPONENT SteamFrame)
endif()
set_target_properties(reprise-frame-probe PROPERTIES INSTALL_RPATH "$ORIGIN/lib")

install(TARGETS reprise-frame-probe RUNTIME DESTINATION . COMPONENT SteamFrame)
option(REPRISE_BUNDLE_OPENXR_LOADER "Bundle a redistributable OpenXR loader" OFF)
if(REPRISE_BUNDLE_OPENXR_LOADER)
    install(FILES "$<TARGET_FILE:OpenXR::openxr_loader>"
        DESTINATION lib RENAME libopenxr_loader.so.1 COMPONENT SteamFrame)
endif()
install(PROGRAMS launch-probe.sh DESTINATION . COMPONENT SteamFrame)

include(CTest)
if(BUILD_TESTING)
    add_executable(frame_room_math_checks room_math_checks.cpp)
    target_compile_features(frame_room_math_checks PRIVATE cxx_std_20)
    target_link_libraries(frame_room_math_checks PRIVATE OpenXR::openxr_loader)
    add_test(NAME frame_room_coordinates COMMAND frame_room_math_checks)
endif()
