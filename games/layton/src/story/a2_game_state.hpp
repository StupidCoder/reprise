#pragma once
#include "system/resources.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

// The game state: everything the story needs to know about the player's
// progress, and the data tables that drive the story.
//
// How the story moves forward
// ---------------------------
//  * Story step. One number (saved at progress +0x234) says how far the
//    story has come. storyflag2.dat lists, for each step, up to eight
//    conditions ("story flag n is set", "puzzle n is solved"). Whenever all
//    conditions of the current step hold, the step moves on to the next
//    row's step (advanceStory()).
//  * Places. The world consists of up to 128 places (rooms and map areas).
//    Each place has up to 15 versions (place/<place>_<version>.dat), and the
//    story step picks which one is shown: placeflag.dat gives every version a
//    range of story steps and an optional counter condition; the last match
//    wins (updatePlaceVersion()). A version decides the picture, the people
//    standing around, exits, hint coins and what can be examined.
//  * Events. Conversations and story scenes are numbered events, each with
//    a script (event/e<group>_<number>.gds). The game remembers which events
//    have been seen. Entering a place can trigger an "automatic" event:
//    autoevent2.dat lists, per place, up to eight events with a story-step
//    range; an unseen one in range starts (findAutoEvent()).
//  * Flags and counters. Scripts set story flags (128), byte counters (128),
//    seen-event bits (1024), items, mysteries, tea recipes, photos and more,
//    all inside the progress block that is saved.
//  * Scenes. The current and following scene numbers (namespace `scene`)
//    tell the router (a2_story_game.cpp) what to show next: a room, an event, a
//    puzzle, a movie, a minigame...
//
// The original keeps all of this in one global object (pointer at 020889f4,
// 0x82c4 bytes; code in overlay 2, loaded at 02068880). Only the fields the
// reconstructed code uses are modelled; comments give their original offsets.

namespace picarat {

// The progress block: the part of the state that is saved (0x320 bytes at
// state +0xc8, cleared for a new game by 02082564). It is kept as raw bytes so
// it can be compared with the original's memory and saved exactly. The flag
// sets carry the names the game itself uses for them (debug strings at
// 020885cc..02088700), e.g. "EventViewed" or "HerbteaRCP".
class SaveProgress {
public:
    static constexpr std::size_t size = 0x320;
    std::array<std::uint8_t, size> bytes{};

    // 020820ec / 02082110: bit test/set with an exclusive bound; out of range
    // reads as clear and writes are ignored. Bit i is byte i>>3, bit i&7.
    bool bit(std::size_t offset, unsigned count, std::uint32_t index) const;
    void setBit(std::size_t offset, unsigned count, std::uint32_t index, bool value);

    bool eventViewed(std::uint32_t i) const {
        return bit(0x004, 1024, i);
    } // "EventViewed" 02082578

    void setEventViewed(std::uint32_t i, bool v) { setBit(0x004, 1024, i, v); } // 02082598

    bool storyFlag(std::uint32_t i) const { return bit(0x084, 128, i); } // "StoryFlag" 020825c0

    void setStoryFlag(std::uint32_t i, bool v) { setBit(0x084, 128, i, v); } // 020825e0

    // Byte counters at +0x94 (128). 0x36 ORs a mask in (020826d0); place
    // versions compare them (02082608).
    std::uint8_t counter(std::uint32_t i) const {
        return i < 128 ? bytes[0x94 + i] : 0;
    } // 0208268c

    void setCounter(std::uint32_t i, std::uint8_t v) {
        if (i < 128) {
            bytes[0x94 + i] = v;
        }
    } // 020826a0

    void addCounter(std::uint32_t i, std::uint8_t v) {
        if (i < 128) {
            bytes[0x94 + i] = std::uint8_t(bytes[0x94 + i] + v);
        }
    } // 020826b0

    void orCounter(std::uint32_t i, std::uint8_t v) {
        if (i < 128) {
            bytes[0x94 + i] |= v;
        }
    } // 020826d0

    bool compareCounter(std::uint32_t index, std::uint32_t value,
                        std::uint32_t mode) const; // 02082608

    // Packed puzzle bytes at +0x114, indexed by puzzle number - 1 (216 slots,
    // 020826f0 / 02082710). Bit layout is PuzzleProgress (a2_puzzle_progress.hpp).
    std::uint8_t& puzzleByte(std::uint32_t index) { return bytes.at(0x114 + index); }

    std::uint8_t puzzleByte(std::uint32_t index) const {
        return index < 216 ? bytes[0x114 + index] : 0;
    }

    bool hintCoin(std::uint32_t place, std::uint32_t i) const {
        return bit(0x1ec, 512, place * 4 + i);
    } // "HintCoin" 020828ac

    void setHintCoin(std::uint32_t place, std::uint32_t i, bool v) {
        setBit(0x1ec, 512, place * 4 + i, v);
    } // 020828cc

    std::uint16_t hintCoins() const { return u16(0x22c); } // +0x22c, 02082844

    void setHintCoins(std::uint16_t v) { put16(0x22c, v); }

    std::uint16_t hintCoinsFound() const { return u16(0x22e); } // +0x22e, 02082850

    void setHintCoinsFound(std::uint16_t v) { put16(0x22e, v); }

    std::uint16_t picarats() const { return u16(0x230); } // +0x230, 020828f0

    void setPicarats(std::uint16_t v) { put16(0x230, v); }

    std::int32_t storyStep() const { return s32(0x234); } // +0x234, 020828fc

    void setStoryStep(std::int32_t v) { put32(0x234, v); }

    std::int32_t place() const { return s32(0x238); } // +0x238, 02082904

    void setPlace(std::int32_t v) { put32(0x238, v); }

    std::int32_t placeVersion() const { return s32(0x23c); } // +0x23c, 0208290c

    void setPlaceVersion(std::int32_t v) { put32(0x23c, v); }

    bool teaCorrect(std::uint32_t i) const {
        return bit(0x2f4, 24, i);
    } // "TeaCorrectFlag" 02082ec0

    void setTeaCorrect(std::uint32_t i, bool v) { setBit(0x2f4, 24, i, v); } // 02082ee0

    bool herb(std::uint32_t i) const { return bit(0x280, 8, i); } // "HerbteaELM" 02082a18

    void setHerb(std::uint32_t i, bool v) { setBit(0x280, 8, i, v); } // 02082a38

    bool anyHerb() const { return bytes[0x280] != 0; } // 02082a04: tea set in the bag

    bool tea(std::uint32_t i) const {
        return bit(0x281, 12, i);
    } // "HerbteaRCP" 02082a60: brewed recipe

    void setTea(std::uint32_t i, bool v) { setBit(0x281, 12, i, v); } // 02082a84

    bool menuNew(std::uint32_t i) const { return bit(0x2f2, 16, i); } // "MenuNewFlag" 02082e70

    bool photoTaken(std::uint32_t i) const { return bit(0x2ed, 16, i); } // "TakePhotoFlag" 02082d80

    void setPhotoTaken(std::uint32_t i, bool v) { setBit(0x2ed, 16, i, v); } // 02082da4

    bool photoChecked(std::uint32_t i) const {
        return bit(0x2e9, 32, i);
    } // "CheckPhotoFlag" 02082d30 (3 per photo)

    void setPhotoChecked(std::uint32_t i, bool v) { setBit(0x2e9, 32, i, v); } // 02082d54

    bool photoComplete(std::uint32_t i) const {
        return bit(0x2ef, 16, i);
    } // "CompletePhotoFlag" 02082dd0

    void setPhotoComplete(std::uint32_t i, bool v) { setBit(0x2ef, 16, i, v); } // 02082df4

    bool cameraSolved() const { return bit(0x24c, 16, 10); } // "CameraSolved" 0208292c

    void setMenuNew(std::uint32_t i, bool v) { setBit(0x2f2, 16, i, v); } // 02082e94

    // state+0x387: a bag-menu tea request (protagonist or assistant) is pending (ov17 020efda0,
    // ov14 020f16dc).
    bool teaRequestPending() const { return bytes[0x2bf] != 0; }

    void setTeaRequestPending(bool v) { bytes[0x2bf] = v ? 1 : 0; }

    bool storyItem(std::uint32_t i) const { return bit(0x2f1, 8, i); } // "StoryItemFlag" 02082e20

    void setStoryItem(std::uint32_t i, bool v) { setBit(0x2f1, 8, i, v); } // 02082e44

    bool tutorial(std::uint32_t i) const { return bit(0x30d, 16, i); } // "TutorialFlag" 02082f58

    void setTutorial(std::uint32_t i, bool v) { setBit(0x30d, 16, i, v); } // 02082f7c

    bool memo(std::uint32_t i) const { return bit(0x2c3, 128, i); } // "MemoFlag" 02082ba0

    void setMemo(std::uint32_t i, bool v) { setBit(0x2c3, 128, i, v); }

    bool memoNew(std::uint32_t i) const { return bit(0x2d3, 128, i); } // "MemoNewFlag" 02082bf0

    void setMemoNew(std::uint32_t i, bool v) { setBit(0x2d3, 128, i, v); }

    // state+0x3d8/+0x3da are inside this block: the pending chained event
    // read after an event ends (ov8 020f3db4) is saved with the progress.
    std::int16_t pendingEvent() const { return std::int16_t(u16(0x312)); } // state+0x3da

    void setPendingEvent(std::int16_t v) { put16(0x312, std::uint16_t(v)); }

    std::uint16_t goal() const { return u16(0x31c); } // state+0x3e4, goal_%i.txt

    void setGoal(std::uint16_t v) { put16(0x31c, v); }

    std::uint16_t u16(std::size_t o) const { return std::uint16_t(bytes[o] | bytes[o + 1] << 8); }

    std::int32_t s32(std::size_t o) const {
        return std::int32_t(std::uint32_t(bytes[o]) | std::uint32_t(bytes[o + 1]) << 8 |
                            std::uint32_t(bytes[o + 2]) << 16 | std::uint32_t(bytes[o + 3]) << 24);
    }

    void put16(std::size_t o, std::uint16_t v) {
        bytes[o] = std::uint8_t(v);
        bytes[o + 1] = std::uint8_t(v >> 8);
    }

    void put32(std::size_t o, std::int32_t v) {
        for (int i = 0; i < 4; ++i) {
            bytes[o + i] = std::uint8_t(std::uint32_t(v) >> (8 * i));
        }
    }
};

// A data table from rc/*.dlz: a header (row count, header size, row size)
// followed by fixed-size rows, usually sorted by their first field. The game
// refuses a table whose row size differs from what its reader expects
// (e.g. 020848a4).
class DlzTable {
public:
    DlzTable() = default;
    DlzTable(Bytes data, std::uint16_t rowSize);

    std::size_t count() const { return count_; }

    std::span<const std::uint8_t> row(std::size_t i) const {
        return {rows_.data() + i * rowSize_, rowSize_};
    }
    // Shared lookup used by every sorted rc table (e.g. 020848f0): an upper
    // check against the last row, then power-of-two steps that probe lo+step
    // and move lo on a positive signed difference. Equal rows do not stop the
    // loop, so with duplicate keys the last probed match wins.
    template <class Key>
    const std::uint8_t* find(std::uint32_t key, Key keyOf) const;

private:
    Bytes rows_;
    std::size_t count_ = 0;
    std::uint16_t rowSize_ = 0;
    unsigned step_ = 1;
};

struct EventInfo { // ev_inf2 row, copied to state+0x825c by 0208499c
    std::uint16_t event = 0, kind = 0, sound = 0, puzzle = 0, value = 0, storyFlag = 0;
};

struct PuzzleInfo { // nz_lst row loaded by 02083c38 into state+0x7e88..
    std::int16_t internal = -1, number = -1;
    std::string title;
    std::int16_t extra = -1;
};

struct GoalInfo {
    std::uint16_t key = 0, kind = 0, goal = 0;
}; // goal_inf row

// Scene numbers, as stored in the current / following scene fields (state
// +0x478 / +0x479, set by script commands 0x06 / 0x07 via 02077eb0 /
// 02078188) and dispatched by 020688a8.
namespace scene {
inline constexpr std::uint8_t NewGame = 0;      // back to the title with a fresh game
inline constexpr std::uint8_t Room = 1;         // exploring a place
inline constexpr std::uint8_t DramaEvent = 3;   // a story event (conversation, cutscene)
inline constexpr std::uint8_t Movie = 6;        // an animated film
inline constexpr std::uint8_t PuzzleResult = 8; // after a story puzzle
inline constexpr std::uint8_t Puzzle = 10;
inline constexpr std::uint8_t Title = 12;
inline constexpr std::uint8_t Narration = 13; // narrated story text
inline constexpr std::uint8_t SubCamera = 14; // camera repair minigame
inline constexpr std::uint8_t SubHerb = 15;   // herb tea minigame
inline constexpr std::uint8_t SubHam = 16;    // hamster minigame
inline constexpr std::uint8_t Menu = 17;      // the bag
inline constexpr std::uint8_t PuzzleIndex = 19;
inline constexpr std::uint8_t Staff = 21; // staff roll
inline constexpr std::uint8_t Notes = 23; // the protagonist's notes
inline constexpr std::uint8_t Challenge = 24;
inline constexpr std::uint8_t TeaRequest = 25; // herb tea for a character, then an event
inline constexpr std::uint8_t PhotoTaken = 26; // photo hunt, after taking a photo in a room
inline constexpr std::uint8_t PhotoAlbum = 27; // photo hunt, opened on the album
inline constexpr std::uint8_t Name = 37;       // naming the hamster
inline constexpr std::uint8_t Passcode = 40;
inline constexpr std::uint8_t Diary = 43;
inline constexpr std::uint8_t Nazoba = 44; // the shack with puzzles missed in the story
} // namespace scene

class GameState {
public:
    explicit GameState(const std::filesystem::path& root);
    SaveProgress progress;
    std::uint8_t scene = scene::Room, following = scene::Room; // +0x478 / +0x479
    std::uint16_t eventId = 0;                                 // +0x5e7c
    std::uint16_t roomSoundEvent = 0;                          // +0x81d2
    bool placeChanged = false;                                 // +0x8229
    std::int32_t previousPlace = 0;                            // +0x822c
    std::uint16_t savedEvent = 0;                              // +0x8230 (event 18490 return)
    bool retryVariant = false;                                 // +0x7ec1, set by 020f2e1c
    bool revisitVariant = false;                               // +0x8228
    bool resultAdjustA = false, resultAdjustB = false;         // +0x821f / +0x8220 (scene 8)
    // +0x8221: set while the bonus section runs (title Secret, ov9
    // 020f0658; cleared by the secret menu's back button): the event runner
    // (ov8 020f3dd0) then does not chain the pending event.
    bool chainBlocked = false;
    bool movieReplay = false; // +0x821c: movie page (ov23 020f3030): movies return to scene 36
    std::int32_t challengePage = 0; // +0x82bc: challenge category shown (ov24 +0x34)
    // State +0x3e8: the 144-byte block shared by all slots (save file 0xb60):
    // +4 the number of downloaded WiFi puzzles, +0xc four bytes each.
    std::array<std::uint8_t, 144> shared{};
    // The console's MAC address (0201fed0), which the bonus passcodes are
    // computed from; natively a per-installation value supplied by the host.
    std::array<std::uint8_t, 6> consoleMac{};
    bool puzzleJudgedCorrect = false; // +0x7ebe (last puzzle judgment, ov27)
    // +0x8238: bag-menu tea request set by ov17 020efda0 (0 none, 1 protagonist,
    // 2 the assistant); ov8 020f3fe0 turns event 18490 into the menu while it is set.
    std::int32_t teaTutorial = 0;
    // +0x8224: room tea-request roll (ov7 020f08e4): 0 not rolled, 1 a talk
    // event became a tea request, 2 rolled without one; ov14 clears it on serving.
    std::int32_t teaRoll = 0;
    std::uint8_t teaRoomRequest = 0; // +0x8232 (ov7 020f1d40)
    // +0x8218: mystery-menu request for ov20 (-1 from the bag, set by
    // 020835a0; script 0x71 stores n-1 = reveal, 0x7d n+9 = solved stamp,
    // both reset to -1 afterwards by ov8 020f1170 / 020f2d18).
    std::int32_t mysteryMenu = -1;
    // Puzzle index (ov19) selection: +0x8204 the "all" list's puzzle number
    // (restored from progress byte 0x319 = state +0x3e1), +0x8208 the
    // favourites' one, +0x820c the tab (0 all, 1 favourites), +0x8214
    // cleared when a puzzle starts; +0x81d0 the touched row's list position.
    std::int32_t indexSelected = 0, indexFavorite = 0, indexMode = 0, indexReplay = 0;
    std::uint8_t indexRow = 0;
    std::int32_t savedBgm = 0;    // +0x7e84 (bag: 0207fa88 before a subgame)
    std::uint8_t photo = 0;       // +0x81d1 current photo (room camera / album)
    std::string placeName;        // +0x81e8 place label (p_%u.txt), expanded natively
    bool cleared = false;         // +0x821e (completed game, from the save slot)
    std::int32_t saveSlot = 0;    // +0x8210 slot loaded or last chosen (02086cbc)
    std::uint8_t teaRequest[2]{}; // +0x8233 / +0x8234 (script 0x73)
    std::int32_t reward = -1;     // +0x8200 (puzzle data +0x3f on solve, ov27 020f25dc)
    EventInfo current;            // +0x825c
    PuzzleInfo puzzle;            // +0x7e88..

    void newGame();                                            // 02083368 (progress part)
    void advanceStory();                                       // 02083aa8
    void updatePlaceVersion();                                 // 020839fc
    void setPlace(std::int32_t place);                         // 020839b0
    bool findAutoEvent();                                      // 02083b70
    bool eventViewed(std::uint32_t event) const;               // 02084c10
    void setEventViewed(std::uint32_t event, bool value);      // 02084c38
    bool loadEventInfo(std::uint32_t event);                   // 0208499c
    const std::uint8_t* eventFix(std::uint32_t event) const;   // 02084b64
    std::optional<GoalInfo> goalInfo(std::uint32_t key) const; // 02084570
    int puzzleNumber(std::int32_t internal) const;             // 02083c00
    unsigned puzzleStatus(std::int32_t number) const;          // 02083d28
    void setPuzzleStatus(std::int32_t number, std::uint32_t value, bool force); // 02083d3c
    unsigned solvedCount() const;                                               // 02083e7c
    unsigned foundCount() const; // 02083e40: puzzles 1..138 seen or solved
    // 02084078 / 02084184: Continue resumes the chp_inf row of the story step
    // (event, or the alternative once its condition event was seen); none = room.
    std::optional<std::uint16_t> continueEvent() const;
    bool loadPuzzle(std::int32_t internal);                              // 02083c38
    std::optional<PuzzleInfo> puzzleByNumber(std::int32_t number) const; // 02084dec
    bool allPuzzlesSolved(int last) const; // 02085a1c (153) / 02085a5c (138)
    bool teaComplete() const;              // 02085970
    // Herb tea tables (ov2 02085128/02085334/02085758, ht_event 02085574).
    const std::uint8_t* teaHerbInfo(std::uint32_t herb) const;     // 02085250: ht_elm row (0x30)
    const std::uint8_t* teaRecipe(std::uint32_t index) const;      // 020854e4: ht_rcp row (0xe0)
    const std::uint8_t* teaRecipeByCode(std::uint32_t code) const; // 02085514
    const std::uint8_t* teaTalk(std::uint32_t tea) const;          // 02085880: ht_tlk row (0x12)
    const std::uint8_t* teaEvent(std::uint32_t event) const;       // 02085680: ht_event row (4)
    static std::uint32_t teaRecipeCode(int a, int b, int c);       // 0208545c
    // Photo hunt (rc/sb_pht.dlz, 12-byte rows: photo, place, background,
    // difference count, gated event, puzzle, icon x/y; ov2 02084ed0).
    const std::uint8_t* photoByIndex(std::uint32_t photo) const; // 02084fe4 (sorted on byte 0)
    const std::uint8_t* photoByPlace(std::uint32_t place) const; // 02085090 (linear on byte 1)

    std::span<const std::uint8_t> placeFlags() const { return placeFlags_; }

private:
    Bytes placeFlags_, storyFlags_, autoEvents_; // data.plz members (+0x47c, +0x3c7c, +0x5e7e)
    DlzTable eventInfo_, eventFix_, puzzles_, goals_, teaHerbs_, teaRecipes_, teaTalk_, teaEvents_,
        photos_, chapters_;
};
} // namespace picarat
