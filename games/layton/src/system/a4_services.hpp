#pragma once
// The fourth adventure's script services: the commands 1000 and up that the
// game's scripts call (registered by overlay 12; table at 021039a0). They
// act on the scene (objects, resources, messages, screens), the game data
// (flags, variables, the current puzzle) and the running loop.
//
// See .analysis/adventure4/docs/script-machine.md (services) and
// research/decomp/puzzle-services.txt for the handlers' code.
#include "system/a4_scene.hpp"
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace picarat {
class Adventure4Sound;
}

namespace picarat::adventure4 {

// Where the game keeps its flags and variables (res/@/lt4_def.cb): a flag
// is a bit numbered by 2001 {number, name} - a name with an index is the
// bit at the name's number plus the index (ov7 020b9708 / 020b9758), so
// "＜演技＞" 1 and "＜演技＞洗った" are the same bit; a variable is a field
// of a byte block at 2011 {offset, size, name}'s offset plus the index
// times its size (1, 2 or 4 bytes; negative: signed), and names may share
// bytes.
struct DataLayout {
    std::map<std::string, int> flags;

    struct Field {
        int offset = 0, kind = 0;
    };

    std::map<std::string, Field> variables;
    std::map<int, Field> variablesAt; // by offset (the first name there)
};

// The game data the services read and write (game +0x18c): flags and
// variables, and the puzzle being played.
struct GameData {
    // Flags and variables. With a layout, the table's flags are kept as
    // "#F<bit>" and its variables byte by byte as "#V<offset>"; names the
    // tables don't know (events keep their "done" flag and their counter
    // under their own name, ov7 020c38cc / 020c38e8) as "name#index".
    std::map<std::string, int> flags;
    std::map<std::string, int> variables;
    const DataLayout* layout = nullptr;

    std::string flagKey(const std::string& name, int index) const {
        if (layout) {
            if (const auto it = layout->flags.find(name); it != layout->flags.end()) {
                return "#F" + std::to_string(it->second + index);
            }
        }
        return name + "#" + std::to_string(index);
    }

    bool flag(const std::string& name, int index = 0) const {
        const auto it = flags.find(flagKey(name, index));
        return it != flags.end() && it->second != 0;
    }

    void setFlag(const std::string& name, bool on, int index = 0) {
        flags[flagKey(name, index)] = on ? 1 : 0;
    }

    // A flag by its number (2001 field 0).
    bool flagAt(int bit) const {
        const auto it = flags.find("#F" + std::to_string(bit));
        return it != flags.end() && it->second != 0;
    }

    void setFlagAt(int bit, bool on) { flags["#F" + std::to_string(bit)] = on ? 1 : 0; }

    int variable(const std::string& name, int index = 0) const;
    void setVariable(const std::string& name, int value, int index = 0);
    // A variable by its offset (2011 field 0).
    int variableAt(int offset, int index = 0) const;
    void setVariableAt(int offset, int value, int index = 0);

    // Keys of an older save ("name#index") moved to the layout's.
    void normalize();

    // Every puzzle's progress by number (the puzzle records' save bits:
    // 0 new, 1 seen, 2 solved; wrong answers; hints opened).
    struct Progress {
        int state = 0, failures = 0, hints = 0;
        // The save record's bit 9: a favourite of the puzzle index (3431);
        // bit 8: passed by - the story moved past it unsolved (020bc0b8),
        // and it waits in the lost-puzzle menu (lt4_menu_nazoba, list 3442).
        bool favourite = false, flag100 = false;
    };

    std::map<int, Progress> puzzles;
    // The puzzles passed by since the last notice (game data +0x58ee, rows
    // of the index order, at most 170; 3461 reads, 3460 clears them).
    std::vector<int> passedBy;
    // The game's collections (lt4_def 2021 {id, name, ...}: investigation
    // notes, episodes, the puzzle collection's marks...) by name and index
    // (ov7 020bb24c / 020bb204).
    std::map<std::string, int> marks;

    bool mark(const std::string& name, int index = 0) const {
        const auto it = marks.find(name + "#" + std::to_string(index));
        return it != marks.end() && it->second != 0;
    }

    void setMark(const std::string& name, int value = 1, int index = 0) {
        marks[name + "#" + std::to_string(index)] = value;
    }

    // Notices of items received, for the item messages (game data
    // +0x540c, at most 16: item, index, mask, kind; 3490..3493).
    struct Notice {
        int item = -1, index = 0, mask = 0, kind = 0;
    };

    std::deque<Notice> notices;
    // The player's names (3010/3011; save +0x194).
    std::map<int, std::string> names;
    // The event being played (game data +0x5a04: its record).
    int event = -1;
    // Frames since the game began (the system's frame counter, 02085d48)
    // and 2900's mark (game +0xa79c).
    std::uint32_t frames = 0, timerStart = 0;
    // 4121's setting, read back by 4120 (game +0xfc +0x584; *role not
    // identified*).
    int setting4121 = 0;
    // Play time in frames (3042/3043: game data +0x5498..+0x54a4 as system
    // ticks; counted here per frame at the DS's 59.8261 Hz, *hypothesis*
    // for the tick rate's effect on rounding).
    std::uint64_t playFrames = 0;
    // The memo's drawing (7101/7102: game data +0x194, kept with the
    // game) and a second one outside the game data (7103/7104: game +0x148,
    // *which memo uses it not identified*), RGB555 of the paint's area.
    std::vector<std::uint16_t> memo, memoScratch;
    // Game +0xaaf8, set around films (2922; *role not identified*).
    bool filmFlag = false;

    struct Puzzle {
        int number = 0;                // record +4 (3400): the puzzle's id
        int index = 0;                 // record +6 (3401): shown as index + 1
        int type = 0;                  // record +8 (3404)
        std::array<int, 3> picarats{}; // record +9.. (3403)
        std::string title;             // record +0xc (3402)
        // Record +0x14/+0x18/+0x1c: the lower picture, its solved variant and
        // the thumbnail are language-specific (lt4_def 2301 operands 8..10).
        std::array<bool, 3> localised{};
    } puzzle; // its progress is in `puzzles` (3920, 3922, 3926)

    // 3940: the puzzle's twelve parameters (game data +0x59a8), loaded from
    // res/naz/scr/@/nNNN.cb (loadPuzzleParameters).
    std::array<gss::Value, 12> parameters;

    // The save slots as the title and the save screens read them (game
    // +0x5214: three of 0x80 bytes from +0x14; 3051): in use (+0 bit 0),
    // two texts (+0xc, +0x38) and five numbers (+4..+8).
    struct Slot {
        bool used = false;
        std::string first, second;
        std::array<int, 5> numbers{};
    };

    std::array<Slot, 3> slots;

    // The place the story is at (3910 sets it by name or number; 020c0860).
    int place = -1;
    // The slot last saved or loaded, -1 after a failure (game data +0x5308;
    // 3052).
    int currentSlot = -1;

    // 3000 (020b854c): a new game's data.
    void resetStory() {
        flags.clear();
        variables.clear();
        puzzles.clear();
        marks.clear();
        notices.clear();
        names.clear();
        place = -1;
        event = -1;
        playFrames = 0;
        memo.clear();
        memoScratch.clear();
    }

    // Game +0x18: the language index (Assets::languageIndex; 5 on the
    // German cartridge, checked in RAM).
    int language = 0;
};

// What the running loop answers (lt4CLoop and its puzzle loops).
struct LoopHost {
    virtual ~LoopHost() = default;
    // 2400: the loop's own commands (vtable +0x40).
    virtual bool command(gss::Call& call, int id) = 0;
    // 2200: the loop's input switch (+0x48 bit 0).
    virtual void setBusy(bool busy) = 0;
    virtual bool inputOff() const = 0;
    // 2300 (loop +0x100 != -1): the pen holds an object.
    virtual bool holding() const = 0;
    // 2101: a callback on loop thread 0; 2110: start thread 1.
    virtual bool callback(int thread, const std::string& block,
                          std::span<const gss::Value> args) = 0;
    virtual bool startThread(int thread, const std::string& block) = 0;
    // 2102 (020e1e6c): one of the loop's values.
    virtual gss::Value loopValue(int index) const = 0;

    // 2411 (020e1f20): the loop's background job starts (the card / Wi-Fi
    // job queue at game +0x8c00); 2410 (020e1f08): it still runs.
    virtual bool startJob() { return false; }

    virtual bool jobRunning() const { return false; }
};

// The loop manager as the scripts see it (game +0x8b90; ov7 020c971c asks
// for a loop by number with up to six numbers).
struct LoopRequests {
    virtual ~LoopRequests() = default;
    // 2000 (020c96d4): requests wait to be carried out.
    virtual bool pending() const = 0;
    // 020c971c: asks for loop `id`; false when refused (unknown id, or the
    // top loop holds requests).
    virtual bool request(int id, std::array<int, 6> values) = 0;
    // 2002 (020cc2e0): the top loop is to end.
    virtual bool pop() = 0;
    // 2003 (020cc2a0): the top loop is suspended and resumed (it begins
    // again).
    virtual bool restart() = 0;
    // 2001 (020c9708): loops on the stack.
    virtual int depth() const = 0;
};

class SaveSlots;

struct Services {
    Services(Scene& s, GameData& d) : scene(s), data(d) {}

    Scene& scene;
    GameData& data;
    SaveSlots* saves = nullptr; // the save slots (null: saving fails)
    LoopHost* loop = nullptr;
    LoopRequests* loops = nullptr;

    // Touch input of the frame (game +0x96c4).
    struct Touch {
        int x = 0, y = 0;           // +0x96e8 (1211/1212)
        int startX = 0, startY = 0; // +0x96e4 (1214/1215)
        // +0x96d8: -1 waiting for the pen to lift (a new loop), 0 up,
        // 1 just pressed, 2 held, 3 just lifted.
        int state = -1;
        // ov6 020a3ae8, once a frame. The game eases the held position
        // toward the pen by min(16 d^2, 1) (d in pixels), which only holds
        // back moves under a quarter pixel; whole-pixel input passes as is.
        void update(bool touching, int penX, int penY);

        bool down() const { return state == 1 || state == 2; }
    } touch;

    // The buttons (game +0x96c4: +2 held, +4 held the frame before; bits
    // as the hardware's KEYINPUT: 0 A, 1 B, 2 Select, 3 Start, 4 right,
    // 5 left, 6 up, 7 down, 8 R, 9 L, 10 X, 11 Y). ov6 020a3814 also lets
    // down cancel up and left cancel right.
    struct Keys {
        std::uint16_t held = 0, before = 0;
        void update(std::uint16_t now);

        bool pressed(int k) const {
            return k >= 0 && k < 12 && (held >> k & 1) && !(before >> k & 1);
        }

        bool released(int k) const {
            return k >= 0 && k < 12 && !(held >> k & 1) && (before >> k & 1);
        }
    } keys;

    // The game's sound manager (game +0x9174), null without sound.
    Adventure4Sound* audio = nullptr;
    // Every sound request is also reported here (research probes).
    std::function<void(int opcode, const gss::Call&)> sound;

    // Effects by number as the loops use them: play (020da688), stop
    // (020da6ac), playing (020da628).
    void effect(int id) const;
    void stopEffect(int id) const;
    bool effectPlaying(int id) const;
};

// The puzzle catalogue (res/@/lt4_def.cb opcode 2301: number, Japanese
// name, title, type, type name, picarats for 0/1/2 wrong answers, the
// pictures' language flags); a puzzle number of 0 when there is none.
GameData::Puzzle findPuzzle(Assets& assets, int number);

// Every puzzle of the catalogue, with its type's name (operand 4).
struct CatalogueEntry {
    GameData::Puzzle puzzle;
    std::string typeName;
};

// An event of the game's table (res/@/lt4_def.cb opcode 2321: group,
// number, name, kind name, ...; in RAM 16 bytes: the name's CRC-32, group
// << 24 | number, the kind, ...). Its script is evt/GG/GG_NNNNNN.
struct EventRecord {
    int group = 0, number = -1; // number < 0: none
    std::string name, kind;
};

// 020c2640: an event by number (020c2398, the low 24 bits) or, for a text,
// by name (020c254c).
EventRecord findEvent(Assets& assets, const gss::Value& key);

// 020c156c: a puzzle's number from its number (020c12c8) or, for a text,
// its Japanese name (020c1478); 0 when there is none.
int puzzleNumber(Assets& assets, const gss::Value& key);

// 020c092c: the story moves to a place (lt4_def 2151, by number or name).
bool setPlace(Assets& assets, GameData& data, const gss::Value& key);

// The sound sets: lt4_def 2111 {set -> music, GE bank, SI bank}.
std::map<int, std::array<int, 3>> soundSets(Assets& assets);

std::vector<CatalogueEntry> puzzleCatalogue(Assets& assets);

// The name of flag `id` in the game's flag table (res/@/lt4_def.cb opcode
// 2001 {id, name}); scripts name flags, the game data keeps them by id.
std::string flagName(Assets& assets, int id);

// ov7 020c3508 / 020b8160: reads the puzzle's parameter file
// (res/naz/scr/@/nNNN.cb): 3000 {a, b, c, d, e} fills slots 0, 1, 3, 2, 4
// (in that order; slot 1 the help picture, 4 the upper background), 3001
// the question (slot 5), 3002/3003 the answers' texts (6, 7), 3004..3007 the
// hints (8..11).
void loadPuzzleParameters(Assets& assets, GameData& data);

// The layout of the asset folder's tables (cached).
const DataLayout& dataLayout(Assets& assets);

// Registers every reconstructed service on the machine.
void installServices(gss::Machine& machine, Services& services);

} // namespace picarat::adventure4
