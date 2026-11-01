# Embedded reconstruction. Reprise owns the application, platform and presentation.
find_package(Threads REQUIRED)
add_library(captain_toad_importer STATIC
    src/importer/Animation.cpp
    src/importer/Archive.cpp
    src/importer/Bch.cpp
    src/importer/Binary.cpp
    src/importer/ExportCommon.cpp
    src/importer/Importer.cpp
    src/importer/Inputs.cpp
    src/importer/Json.cpp
    src/importer/Layout.cpp
    src/importer/Message.cpp
    src/importer/RomImage.cpp
    src/importer/SceneExport.cpp
    src/importer/Shaders.cpp
    src/importer/SoundExport.cpp
    src/importer/StageExport.cpp
    src/importer/Stages.cpp
    src/importer/Texture.cpp
    src/importer/UiExport.cpp
    src/platform/Sha256.cpp)
add_library(captain_toad_device STATIC src/gpu/Device.cpp)
add_library(captain_toad_session STATIC
    src/session/CourseUi.cpp
    src/session/FrontEnd.cpp
    src/session/Presentation.cpp
    src/session/Session.cpp
    src/session/SessionCourse.cpp
    src/session/SessionRender.cpp
    src/session/SessionResearch.cpp
    src/session/SessionShell.cpp
    src/session/SessionSkin.cpp
    src/session/WalkPlanner.cpp
    src/session/book/BookActor.cpp
    src/session/book/BookBottom.cpp
    src/session/book/BookPages.cpp
    src/session/book/BookScene.cpp
    src/session/book/BookSound.cpp
    src/session/book/CourseBook.cpp
    src/render/UiLayout.cpp)
target_link_libraries(captain_toad_session PUBLIC captain_toad_importer captain_toad_device Threads::Threads)
foreach(target captain_toad_importer captain_toad_device captain_toad_session)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /fp:precise)
    else()
        target_compile_options(${target} PRIVATE -ffp-contract=off)
    endif()
endforeach()

add_library(captain_toad_import_adapter STATIC reprise/import.cpp)
target_link_libraries(captain_toad_import_adapter PUBLIC captain_toad_importer platform)
target_include_directories(captain_toad_import_adapter PRIVATE ${PROJECT_SOURCE_DIR})

# Runtime material shaders are generated from imported data. Compile to SPIR-V,
# then remap SDL's resource sets; SPIRV-Cross emits Metal on Apple hosts.
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies statically")
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
FetchContent_Declare(captain_glslang
    URL https://github.com/KhronosGroup/glslang/archive/refs/tags/16.6.0.tar.gz
    URL_HASH SHA256=9c09b901149c729df745057dafa815278aaa101b84d2b6e14f16a42de52f97f2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(captain_spirv_cross
    URL https://github.com/KhronosGroup/SPIRV-Cross/archive/refs/tags/vulkan-sdk-1.4.328.1.tar.gz
    URL_HASH SHA256=5b1149927e40a67396b440711543a3b1f9d004c844ca7293582a72c01cb69756
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(captain_glslang captain_spirv_cross)
add_library(captain_toad_sdl STATIC src/gpu/SdlDevice.cpp src/gpu/VulkanShaders.cpp)
target_include_directories(captain_toad_sdl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(captain_toad_sdl PUBLIC captain_toad_device SDL3::SDL3
    glslang::glslang glslang::glslang-default-resource-limits
    spirv-cross-msl spirv-cross-glsl spirv-cross-core)
target_compile_features(captain_toad_sdl PUBLIC cxx_std_20)

add_library(captain_toad_module STATIC reprise/module.cpp)
target_include_directories(captain_toad_module PRIVATE ${PROJECT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(captain_toad_module PUBLIC captain_toad_session captain_toad_sdl platform)
