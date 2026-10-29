#pragma once
// The first adventure's game state: everything the story remembers between
// scenes (ARM9: the state block at *(020dec0c)), and its save slots.
//
// The story is a chain of scenes. The scene number says what runs next
// (the router, 02000d28): a puzzle, a room, an event (a script of the
// story), a movie, the trunk's pages, the staff roll. Scripts move the story
// on by setting the next scene and its argument (event number, room number,
// puzzle number) and by setting story flags, which later scripts test.
//
// What a save slot keeps is listed by its writer (02019c9c) and reader
// (0201a1dc); the fields below are those, plus the ones that only live while
// the game runs. Fields whose meaning is not established are kept under
// plain names with their save offset so they survive a save and load.
#include "puzzles/a1_puzzle.hpp"
#include <array>
#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

namespace picarat::adventure1 {

// Scene numbers of the router (02000d28). The scripts' "go to" command
// (0x51, 02012de0) names nine of them: Question 1, Room 3, Event 5, Movie 7,
// "Event Movie" 0x22, Reset 0, Taiken 0xb, "Challenge Mode" 0x1c and
// "Staff Roll" 0x26. The others are reached from native code; they are named
// after what the router runs for them.
namespace scene {
constexpr std::uint8_t Reset = 0, Question = 1, TrunkMenu = 2, Room = 3, IndexPuzzle = 4, Event = 5,
                       Movie = 7, Painting = 8, HotelRoom = 9, RobotDog = 10, Taiken = 0xb,
                       Mysteries = 0xc, TitleScreen = 0x15, IntroTalk = 0x16,
                       IntroTalkAfterPuzzle = 0x17, Logos = 0x18, TitleLogo = 0x19,
                       PuzzleIndex = 0x1a, DualMovie = 0x1b, Challenge = 0x1c, EventMovie = 0x22,
                       Continue = 0x24, StaffRoll = 0x26, EventThenQuestion = 0x27, Opening = 0x29;
}

struct GameState {
    // --- Kept in a save slot (save offsets in brackets).
    std::uint32_t event = 0; // +0 [0x110]: the event a scene "Event" runs (e<N>.gds), 0..479
    // +4..+0x534, +0x640 ...: the puzzle part (progress [0xc], hint coins
    // [0x376], picarats [0x42d], tutorials seen [0x2f6] ...).
    PuzzleState puzzle;
    std::int16_t puzzleNumber = 0; // +4 [0, low byte only]: the current puzzle
    std::uint8_t byte9 = 0;        // +9 [5]
    bool reward = false;           // +0xa [6]
    std::uint8_t room = 0;         // +0xe [1]: the room a scene "Room" enters
    std::uint8_t scene = 0;        // +0xf [9]: what the router runs next
    std::uint8_t returnScene = 0;  // +0x11 [0xa]: where a puzzle or menu page returns to
    std::uint8_t byte12 = 0;       // +0x12 [0xb]
    std::uint16_t value1e = 0;     // +0x1e [0x2f4]: a number scripts set (0x62) and test (0x63)
    // +0x128 [0x10c]: 1 while an event runs on after its puzzle (0x4a / 0x4b).
    std::int32_t afterPuzzle = 0;
    // +0x12c + N [0x114]: per event: 1 run, 2 marked by its script (0x55).
    std::array<std::uint8_t, 480> events{};
    std::uint64_t playSeconds = 0;       // +0x544 [0x378]: play time before this session
    std::array<std::uint8_t, 8> items{}; // +0x54c [0x380]: the list of 0x85 / 0x86 / 0xce / 0x56
    std::bitset<1024> flags;             // +0x554 [0x388]: story flags (0x8d tests, 0x8e sets)
    std::bitset<512> coins;              // +0x4f4 [0x336]: hint coins taken (0201ae1c)
    std::uint8_t chapter = 0;            // +0x5f4 [0x408]: story chapter (0x9e only raises it)
    std::array<std::uint8_t, 32>
        gizmos{}; // +0x602 [0x409]: robot dog parts waiting to be placed (0201b09c)
    std::array<std::uint8_t, 4> dogParts{}; // +0x622 [0x429]: robot dog parts in place (bits)
    // The torn painting's pieces (0xc5, 02029a4c; 0201b2b4): found, their
    // place in the painting, a third list.
    std::array<std::uint8_t, 20> pieceOwned{}, piecePlace{}, piece670{};
    // The hotel's furniture (0xcc, 02029d2c): in the first room, in the
    // second room (20 each), and two more lists.
    std::array<std::uint8_t, 32> furnitureA{}, furnitureB{}, furniture6c4{}, furniture6e4{};
    // "New" marks shown in the trunk (+0x704..+0x708 [0x4ed]): story memo,
    // robot dog, hotel room (cleared by 0202467c), painting (0204b7a8),
    // mysteries.
    bool newMemo = false, newGizmo = false, newFurniture = false, newPainting = false,
         newMystery = false;
    std::array<std::uint8_t, 32>
        puzzleMarks{};           // +0x5d4 [0x4f2]: puzzles marked in the index (bits)
    std::bitset<64> memoUnread;  // +0x5f8 [0x512]: story memo entries not read yet
    std::uint16_t mysteries = 0; // +0x600 [0x51a]: mysteries not looked at yet (0201ba34)
    std::int32_t nextEvent = -1; // +0x74c [0x51c]: event after a scene (0xea), -1 room
    std::array<std::uint8_t, 20> dogName{}; // +0x626 [0x520]: the robot dog's name (0201b71c)
    std::int32_t dogFinds = 0;              // +0x63c [0x534]: what the robot dog sniffed out
    std::int32_t shackCursor = 0;           // +0x124: the shack's last puzzle shown (not saved)
    // The puzzle index's place while the game runs (0220e900: the list
    // shown, its first rows; 020d50b0: the puzzle shown, -1 none). Opening
    // the trunk resets it (02031ec8).
    int indexList = 0, indexFirst[2] = {0, 0}, indexShown = -1;

    // --- Only while the game runs.
    std::int8_t slot = 0;            // the slot in use (save header +0x14)
    std::string place;               // +0x4a8: the place name shown (map%i.txt)
    std::string playerName;          // +0x738: the name given at the start (20 bytes)
    std::int32_t movie = 0;          // +0x70c: movie number (m%i.mods, emt/emb%i.mods)
    std::int32_t challengePage = -1; // +0x72c: the challenge shown (scene 0x1c), -1 its list

    // +0x724: the puzzles received over WiFi (backup 0x1b58, shared by the
    // slots; kept through a new game): the puzzle and the day it came
    // (year % 100, month, day).
    struct WifiPuzzle {
        std::uint8_t puzzle = 0, year = 0, month = 0, day = 0;
    };

    std::vector<WifiPuzzle> wifiPuzzles;
    // The WiFi list's puzzle shown and first row (020dec1c's pair, 020368d4
    // clears them when the secret menu opens).
    int wifiShown = 0, wifiFirst = 0;
    bool isWifiPuzzle(int number) const;
    std::int32_t choice = -1;        // +0x714: the last choice (1..3), -1 none
    std::int32_t introPart = 0;      // +0x30c: which half of the intro talk (0202c8ac)
    bool firstTouchHint = true;      // +0x710: the first room says "touch the screen"
    int enterSound = -1;             // +0x4ec: the effect a room plays as it appears
    std::uint64_t sessionFrames = 0; // frames shown since the game was started or loaded

    // Puzzle progress (byte +0x24 + N): N in 1..254 is stored at N; other
    // numbers at 199 - N (the original's mapping for special puzzles).
    std::uint8_t& progress(int number);

    bool solved(int number) { return progress(number) & 2; }

    bool solvedAt(int number) const { // the same, for a const state
        return const_cast<GameState*>(this)->progress(number) & 2;
    }

    // 0x48 (02012ac0): the current puzzle; a new number clears "solved".
    void setPuzzle(int number);

    // 0201aea4 / 0201aee8: story flag n (0..1023); flags 300..319 add a
    // mystery (marked new), 320..339 mark one solved (0201ba78).
    bool flag(int n) const { return n >= 0 && n < 1024 && flags[std::size_t(n)]; }

    void setFlag(int n, bool on);
    // 0201ad4c: puzzles solved so far (1..199); 0201ac40: puzzles seen.
    int solvedCount() const;
    int seenCount() const;

    bool coinTaken(int n) const { return n >= 0 && n < 512 && coins[std::size_t(n)]; }

    void takeCoin(int n) {
        if (n >= 0 && n < 512) {
            coins[std::size_t(n)] = true;
        }
    }

    // 0201b1e0: the robot dog has all 20 parts (bits of +0x622).
    bool dogComplete() const {
        for (int i = 0; i < 0x14; ++i) {
            if (!(dogParts[std::size_t(i >> 3)] >> (i & 7) & 1)) {
                return false;
            }
        }
        return true;
    }

    // 0201b8d8: puzzles waiting in the shack (flag 800 + n, not solved).
    int shackWaiting() const;
    // 0201b74c: the next of them after the last one shown (+0x124), in turn;
    // -1 when none waits.
    int nextShackPuzzle();

    // 02064ecc (room 0) / 02065404 (room 1): the hotel rooms' conditions
    // (which furniture a room holds); the hint script and the happiness
    // use them by number.
    bool hotelCondition(int room, int n) const;
    // 0206593c / 020659ec: how happy a room's guest is (0..5).
    int hotelHappiness(int room) const;

    // 0201b664: both rooms as happy as can be, or event 257 has run.
    bool hotelComplete() const {
        return (hotelHappiness(0) == 5 && hotelHappiness(1) == 5) || (events[257] & 1);
    }

    // 0201b578: the painting is whole (event 258 has run, or all 20 pieces
    // lie in their places the right way up).
    bool paintingComplete() const {
        if (events[258] & 1) {
            return true;
        }
        for (std::size_t i = 0; i < 20; ++i) {
            if (!pieceOwned[i] || piecePlace[i] != i || piece670[i]) {
                return false;
            }
        }
        return true;
    }

    // 0201b9c0: story memo entry n becomes unread (new).
    void addMemo(int n);
    // 02019be0: play time in seconds (at most 99:59:59).
    std::uint64_t playTime() const;
    // 0201974c: a new game.
    void reset();
};

// The cartridge's backup (8 KB, ARM9 02024364 .. 0201a8e8): a header of 0x150
// bytes at 0 ("ATAMFIREBELLNY", a checksum, what each of the three slots
// shows), the slots at 1000 + 2000 * n (0x53c bytes each, with a checksum).
class SaveImage {
public:
    static constexpr std::size_t kSize = 0x2000;

    struct SlotInfo {
        bool used = false, cleared = false;
        std::string place, name;
        std::uint32_t hours = 0, minutes = 0, seen = 0, solved = 0;
    };

    // An empty backup; `defaultName` is what an empty slot's name holds (the
    // program's string at 020d3808, given by the caller).
    explicit SaveImage(std::string defaultName);
    // Reads an image; a damaged header becomes an empty one (02024538).
    SaveImage(std::string defaultName, std::vector<std::uint8_t> bytes);

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

    SlotInfo slot(int n) const;
    bool anyUsed() const;
    // 0201a8e8: the game into slot n (header and slot).
    void store(int n, const GameState& state);
    // 0201aadc / 0201a1dc: slot n into `state` (false when its checksum fails).
    bool load(int n, GameState& state) const;
    // 0201a7fc: the WiFi puzzles' block (0x78 bytes at 0x1b58: a checksum
    // over the rest, the count, a word, four bytes a puzzle); a damaged
    // block is an empty list (0201a88c).
    std::vector<GameState::WifiPuzzle> wifiPuzzles() const;

private:
    std::vector<std::uint8_t> bytes_;
    std::string defaultName_;
    void writeHeaderChecksum();
    void clearSlotInfo(int n);
};

// 0200b7d0: the save checksum (two 16-bit running sums over halfwords).
std::uint32_t saveChecksum(const std::uint8_t* data, std::size_t size);

} // namespace picarat::adventure1
