#pragma once
// Minigame free play: each game's minigames, playable at any time with all
// their content, on progress kept apart from the story's saves. The game hub
// shows how many are complete and resets them all at once.
//
//  * Second adventure: the four minigames run on a progress block of their
//    own (the game's 0x320-byte save block, SaveProgress). It starts as the
//    game's new game (02083368) with everything the story hands out: the ten
//    camera parts (CameraPrt, +0x24c), the eight herbs (HerbteaELM, +0x280),
//    the hamster items (+0x284.., one count per kind: every reward the game
//    data gives - puzzles' completion values and events' 0x78 SetReward, the
//    pre-pass's rule of 0207cff0 - see rewards 40..59 of command 0x74) and the
//    nine photos taken (TakePhotoFlag, +0x2ed). Complete: the camera works
//    (CameraSolved); all nine photos solved (CompletePhotoFlag); the hamster
//    at level 6 (+0x283); all twelve teas brewed (HerbteaRCP).
//  * Third adventure: the parrot with all twelve stages open (complete when
//    every stage is delivered), the toy car with all ten courses open
//    (complete when all are cleared) and the picture book with every sticker
//    found (complete when the three books are solved).
//
//  * First adventure: the trunk's three pages, run on their own (02001b88,
//    0202467c, 020019bc without the story around them): the robot dog with
//    all 20 parts waiting (complete when all are in place), the hotel with
//    all 30 pieces of furniture (handed out alternately to the two rooms;
//    in the story the player chooses; complete when both rooms are as happy
//    as can be) and the torn painting with all 20 pieces in the box
//    (complete when it is whole). Completing one runs its event as in the
//    story (0x100..0x102), the dog's naming left out.
//
// Files: <library>/yltd-minigames.bin (the block) and c3jd-minigames.bin:
// one byte per parrot stage (12), per car course cleared (10), per book
// solved (3), then the gap each sticker was left on (older files hold only
// the parrot's 12 bytes; the rest starts empty); a5fp-minigames.bin: the
// first adventure's lists in their save order (Adventure1Minigames).
#include "system/resources.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace picarat::shell {

struct MinigameStatus {
    std::string name;
    bool available = false; // reconstructed
    bool complete = false;
    std::string progress; // "3 of 9 photos solved"
    float fraction = 0;   // 0..1
};

class Adventure2Minigames {
public:
    // Scenes the minigames run as: camera, photo album, hamster, herb tea.
    static constexpr std::uint8_t kScenes[4] = {14, 27, 16, 15};

    Adventure2Minigames(std::filesystem::path pack, std::filesystem::path file);

    // The progress block, made (all unlocked) when there is none yet.
    std::vector<std::uint8_t> progress();
    void store(const std::vector<std::uint8_t>& block);
    void reset();

    bool saved() const { return fileExists(file_); }

    std::vector<MinigameStatus> status();

private:
    std::filesystem::path pack_, file_;
};

class Adventure3Minigames {
public:
    explicit Adventure3Minigames(std::filesystem::path file);

    // The progress record (see above); at least kHeader bytes.
    static constexpr std::size_t kParrot = 0, kCar = 12, kBooks = 22, kStickers = 25;
    std::vector<std::uint8_t> progress() const;
    void store(const std::vector<std::uint8_t>& progress);
    void reset();

    bool saved() const { return fileExists(file_); }

    std::vector<MinigameStatus> status() const;

private:
    std::filesystem::path file_;
};

} // namespace picarat::shell

namespace picarat::adventure1 {
struct GameState;
}

namespace picarat::shell {

class Adventure1Minigames {
public:
    // The trunk pages the minigames are: robot dog, hotel, painting.
    static constexpr int kCount = 3;

    explicit Adventure1Minigames(std::filesystem::path file);

    // The record: gizmos (32), dog parts (4), the painting's pieces owned,
    // places and turns (20 each), the hotel's four lists (32 each), the
    // completion events 0x100..0x102 (one byte, bits 0..2) and the dog's
    // name (20). A new record (no file) holds everything handed out.
    static constexpr std::size_t kSize = 32 + 4 + 60 + 128 + 1 + 20;
    std::vector<std::uint8_t> progress() const;
    void store(const std::vector<std::uint8_t>& progress);
    void reset();

    bool saved() const { return fileExists(file_); }

    std::vector<MinigameStatus> status() const;

    // The record into a game state and back.
    static void apply(const std::vector<std::uint8_t>& progress, adventure1::GameState& state);
    static std::vector<std::uint8_t> capture(const adventure1::GameState& state);

private:
    std::filesystem::path file_;
};

} // namespace picarat::shell

namespace picarat::shell {

// Fourth adventure: the bag's three minigames (railway, puppet show, fish)
// on a record of the game data's flags and variables
// (adventure4::minigameRecord); a new record opens everything the story
// would (adventure4::openMinigames). Complete: all 10 courses, all 3 plays,
// all 10 tanks cleared. File: <library>/blfd-minigames.bin.
class Adventure4Minigames {
public:
    static constexpr int kCount = 3;

    explicit Adventure4Minigames(std::filesystem::path file);

    std::vector<std::uint8_t> progress() const;
    void store(const std::vector<std::uint8_t>& progress);
    void reset();

    bool saved() const { return fileExists(file_); }

    std::vector<MinigameStatus> status() const;

private:
    std::filesystem::path file_;
};

} // namespace picarat::shell
