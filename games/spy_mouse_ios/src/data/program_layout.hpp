// Where the supported build keeps the data the port reads from its program image: Spy Mouse
// 1.1.1 (iOS 3.1, armv7 slice, LC_UUID 0B8B1588-9119-36F9-9EA6-EEA7B51316BF). Addresses are
// armv7 virtual addresses; names are the executable's own symbols. The Valentine's Day build
// 1.0.5 kept the same tables at other addresses, with fewer entries; it is no longer read.
#pragma once

#include <cstddef>
#include <cstdint>

namespace doc::data::layout {

// File keys.
constexpr std::uint32_t kContainerKey = 0x4BC16F;  // NUL-terminated (Map.cpp's PFile key)
constexpr std::uint32_t kChunkTreeKey = 0x526950;  // g_XorKey (BrdReader::loadFile)
constexpr std::size_t kChunkTreeKeyLength = 29;
constexpr std::uint32_t kGuiKey = 0x4C20B8;        // NUL-terminated (GuiItem::loadLayout)

// getStrID(char const*): the compiled name -> string id decision tree.
constexpr std::uint32_t kStrIdStart = 0x1FF4C0;
constexpr std::uint32_t kStrIdEnd = 0x1FF4C0 + 45500;

// Tables.
constexpr std::uint32_t kParticleTable = 0x5F0980;  // g_pParticleSprites, 20-byte entries
constexpr int kParticleKinds = 50;
constexpr std::uint32_t kFluffTable = 0x5F0D80;     // g_pFluffData, 24-byte entries
constexpr int kFluffKinds = 76;
constexpr std::uint32_t kEyeMapping = 0x526DD0;     // g_vEyeMapping, 5 bytes per pose
constexpr std::uint32_t kMouseEyeOffsets = 0x5F1D30;  // g_vMouseEyeOffset, {x, y} per pose
constexpr int kMousePoses = 8;
constexpr std::uint32_t kLevelGoalNames = 0x5F5C20;  // g_LevelGoalNames
constexpr std::uint32_t kGoalStarImages = 0x5F1C30;  // g_sStarImgs, {normal, gold} per kind
constexpr int kGoalKinds = 30;
constexpr std::uint32_t kSoundFiles = 0x5F1620;      // SoundListFileArray
constexpr int kSounds = 186;
constexpr std::uint32_t kScoreNotes = 0x525210;      // NEW_SND_MAPPING (SceneGame::playScoreSound)

}  // namespace doc::data::layout
