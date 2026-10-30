// Data tables the original keeps in its program image rather than in files, read from the
// user's executable (supported build only; addresses are armv7 virtual addresses).
#pragma once

#include "data/game_files.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace doc::data {

// Particle types 0..0x34 (table at 0x33f9c0, 20 bytes each).
struct ParticleKindData {
    int priority = 0;       // pool-full replacement priority (0: never replaces)
    std::string sheet;      // sprite sheet under res/
    float frameWidth = 0;
    int layer = 0;          // third word of the entry (1..3; meaning not recovered)
};
std::vector<ParticleKindData> readParticleKinds(const GameFiles& files);

// Map decoration ("fluff") types 500..573 (g_pFluffData at 0x33fe00, 24 bytes each).
struct FluffKindData {
    std::string sheet;
    float frameWidth = 0;
    int imageFlags = 0;     // third argument of Sprite::setImage
    int frameMs = 0;
    int mode = 0;           // animation mode when played
    bool autoplay = false;  // plays from the start, else only when triggered
};
std::vector<FluffKindData> readFluffKinds(const GameFiles& files);

// (1.1.1) The dossiers (data/program_layout.hpp): the picture name ("ui/dossiers/
// portrait_<name>.png", "screenshot_<name>.png"), the title's string id, five lines of
// "known facts"/"extra intel" (-1: none) and where the subject is first seen (a level uid; -1
// and -2 are places outside the levels).
struct DossierData {
    std::string name;
    int title = -1;
    std::array<int, 5> tips{};
    int lastSeen = -1;
};
std::vector<DossierData> readDossiers(const GameFiles& files);

// Eye pictures each popup mouse pose can show (g_vEyeMapping: 8 poses x 5 bytes).
std::vector<std::uint8_t> readEyeMapping(const GameFiles& files);
// Where a posed mouse's eyes sit, as fractions of its pose picture (g_vMouseEyeOffset
// 8 poses x {x, y}; GuiPosedMouse::render).
std::vector<std::pair<float, float>> readMouseEyeOffsets(const GameFiles& files);
// Level goal kinds by number (g_LevelGoalNames: "score", the seven combo kinds,
// "time", "paths", ...), and the medal picture of each kind, not won and won
// (LevelGoal::getStarImage, g_sStarImgs). Addresses: data/program_layout.hpp.
std::vector<std::string> readLevelGoalNames(const GameFiles& files);
// The game's effect table (SoundList: file per number, 186 entries, LoadSoundGroup).
std::vector<std::string> readSoundTable(const GameFiles& files);
// The six score notes (SceneGame::playScoreSound, NEW_SND_MAPPING).
std::vector<int> readScoreNotes(const GameFiles& files);
std::vector<std::pair<std::string, std::string>> readGoalStarImages(const GameFiles& files);

}  // namespace doc::data
