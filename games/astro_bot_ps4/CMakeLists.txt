# First Beacon handoff: portable core only; Reprise owns all host services.
add_library(beacon_data STATIC src/data/Animation.cpp
    src/data/Bytes.cpp
    src/data/CollisionMesh.cpp
    src/data/GameFiles.cpp
    src/data/Gnf.cpp
    src/data/Jxm.cpp
    src/data/Model.cpp
    src/data/ObjectDefinition.cpp
    src/data/PhysicsFile.cpp
    src/data/ResourceArchive.cpp
    src/data/Skeleton.cpp
    src/data/SoundBank.cpp
    src/data/Xml.cpp)
target_include_directories(beacon_data PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(beacon_data PUBLIC cxx_std_20)
add_library(beacon_anim STATIC src/anim/Pose.cpp)
target_include_directories(beacon_anim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(beacon_anim PUBLIC cxx_std_20)
add_library(beacon_physics STATIC src/physics/CollisionWorld.cpp src/physics/PhysicsWorld.cpp src/physics/rigid/RigidBody.cpp src/physics/rigid/ContactManifold.cpp src/physics/rigid/ContactSolver.cpp
    src/physics/rigid/ConvexSolver.cpp
    src/physics/rigid/ContactCache.cpp src/physics/rigid/MeshContacts.cpp src/physics/rigid/MeshBuilder.cpp src/physics/rigid/SweptContacts.cpp
    src/physics/rigid/ShapeCasts.cpp
    src/physics/rigid/RigidWorld.cpp src/physics/rigid/PrimitiveContacts.cpp)
target_include_directories(beacon_physics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(beacon_physics PUBLIC cxx_std_20)
add_library(beacon_game STATIC src/game/LevelDocument.cpp src/game/LevelSchema.cpp src/game/LevelCollision.cpp
    src/game/Hero.cpp src/game/HeroTuning.cpp src/game/DynamicGrass.cpp src/game/LostBot.cpp src/game/TreasureBox.cpp src/game/MiniBlock.cpp src/game/Respawn.cpp src/game/SceneLighting.cpp src/game/PadStick.cpp src/game/HeroClips.cpp src/game/LevelRuntime.cpp src/game/PlayerRig.cpp
    src/game/enemy/EnemyTuning.cpp src/game/enemy/BasicEnemy.cpp src/game/enemy/EnemySystem.cpp
    src/game/enemy/Spitter.cpp src/game/MoviePrompt.cpp src/game/PublicScreen.cpp src/game/GoalBalloon.cpp src/game/LevelMusic.cpp)
target_include_directories(beacon_game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(beacon_game PUBLIC cxx_std_20)
add_library(beacon_atrac9 STATIC 
    src/third_party/atrac9/band_extension.c
    src/third_party/atrac9/bit_allocation.c
    src/third_party/atrac9/bit_reader.c
    src/third_party/atrac9/decinit.c
    src/third_party/atrac9/decoder.c
    src/third_party/atrac9/huffCodes.c
    src/third_party/atrac9/imdct.c
    src/third_party/atrac9/libatrac9.c
    src/third_party/atrac9/quantization.c
    src/third_party/atrac9/scale_factors.c
    src/third_party/atrac9/tables.c
    src/third_party/atrac9/unpack.c
    src/third_party/atrac9/utility.c)
target_include_directories(beacon_atrac9 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_library(beacon_audio STATIC src/audio/WaveDecoder.cpp src/audio/MusicCatalog.cpp)
target_include_directories(beacon_audio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(beacon_audio PUBLIC cxx_std_20)
target_link_libraries(beacon_anim PUBLIC beacon_data)
target_link_libraries(beacon_physics PUBLIC beacon_data)
target_link_libraries(beacon_game PUBLIC beacon_data beacon_physics beacon_anim)
target_link_libraries(beacon_audio PUBLIC beacon_data beacon_atrac9)
# Preserve support-point tie rounding from the source physics build.
if(MSVC)
    target_compile_options(beacon_physics PRIVATE /fp:strict)
else()
    target_compile_options(beacon_physics PRIVATE -ffp-contract=off)
endif()

if(BUILD_TESTING AND NOT ANDROID)
    add_executable(beacon_anim_check EXCLUDE_FROM_ALL src/tools/AnimTool.cpp)
    target_include_directories(beacon_anim_check PRIVATE src)
    target_link_libraries(beacon_anim_check PRIVATE beacon_anim)
    add_executable(beacon_model_check EXCLUDE_FROM_ALL src/tools/ModelTool.cpp)
    target_include_directories(beacon_model_check PRIVATE src)
    target_link_libraries(beacon_model_check PRIVATE beacon_data)
    add_executable(beacon_sound_check EXCLUDE_FROM_ALL src/tools/SoundTool.cpp)
    target_include_directories(beacon_sound_check PRIVATE src)
    target_link_libraries(beacon_sound_check PRIVATE beacon_audio)
    add_executable(beacon_physics_check EXCLUDE_FROM_ALL src/tools/PhysicsTool.cpp)
    target_include_directories(beacon_physics_check PRIVATE src)
    target_link_libraries(beacon_physics_check PRIVATE beacon_physics)
endif()

if(BUILD_TESTING AND NOT ANDROID)
    add_executable(beacon_hero_probe EXCLUDE_FROM_ALL src/tools/HeroProbe.cpp)
    target_compile_definitions(beacon_hero_probe PRIVATE REPRISE_BEACON_CORE)
    target_include_directories(beacon_hero_probe PRIVATE src)
    target_link_libraries(beacon_hero_probe PRIVATE beacon_game)
    add_executable(beacon_core_check tests/core.cpp)
    target_include_directories(beacon_core_check PRIVATE src)
    target_link_libraries(beacon_core_check PRIVATE beacon_game beacon_audio)
    add_test(NAME beacon_core COMMAND beacon_core_check)
endif()

add_library(beacon_assets STATIC reprise/import.cpp reprise/viewer.cpp)
target_include_directories(beacon_assets PRIVATE src ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/shared)
target_link_libraries(beacon_assets PUBLIC beacon_anim beacon_audio platform)
if(BUILD_TESTING AND NOT ANDROID)
    add_executable(beacon_assets_check tests/assets.cpp ${PROJECT_SOURCE_DIR}/app/library.cpp)
    target_link_libraries(beacon_assets_check PRIVATE beacon_assets ui)
    add_test(NAME beacon_assets COMMAND beacon_assets_check)
endif()

if(NOT ANDROID)
    add_executable(beacon_asset_list reprise/asset_list.cpp)
    target_link_libraries(beacon_asset_list PRIVATE beacon_assets)
endif()
