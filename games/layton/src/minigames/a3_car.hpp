#pragma once
// The third adventure's toy car: a bag minigame. Three loops, each the
// native part around a script program, sharing the course data
// rc/mode/lt3_mini_car (overlay 20, lt3CLoopMiniCarBase):
//
//  * MINI_CAR_SLCT (overlay 21, evt/prg/lt3_mini_car_slct): pick one of ten
//    courses. The script does the buttons; the loop shows the picked
//    course's flags on its map on the upper screen (1280 loads the course's
//    sprites).
//  * MINI_CAR_EDIT (overlay 22, evt/prg/lt3_mini_car_edit): the course seen
//    from above; the player drags the course's pieces (arrows and jumps)
//    onto free cells and turns the car. 1280 makes a point on each cell a
//    piece may go on and the pieces themselves (linked to where they were
//    left), 1281 tells where the car starts, 1282 turns it. The pieces'
//    cells and the car's direction are kept in the game variable
//    ＜車＞アイテム配置 (a cell + 1, 0 for none) when the loop is covered or
//    ends.
//  * MINI_CAR_GAME (overlay 23, evt/prg/lt3_mini_car_game): the run. The
//    lower screen shows the course in an oblique view that follows the car,
//    every cell a ground sprite, its piece and its object (a flag, a rock,
//    a bridge...) drawn back to front; the upper screen the course's map
//    with its flags and the car. The script moves the car cell by cell
//    (commands 1280..1293, conditions 2210..2213); the loop moves what it
//    is told to, and when the car comes down on a cell it starts that
//    cell's section (a flag taken, fuel, a switch, water...).
//
// Reconstructed from overlays 20..23 (see .analysis/adventure3/docs/minigames.md).
#include "story/a3_story.hpp"
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace picarat::adventure3 {

// rc/mode/lt3_mini_car (read by 0211e6b4 with the handlers 0211dae0..0211e3d4).
struct CarData {
    // 4310 {id, name, sequence, section}: a piece the player places.
    struct Piece {
        int id = 0;
        std::string name, sequence, section;
    };

    // 4311 {name, placeable, flat, mark, height, ground, object, enter, act}:
    // a kind of cell.
    struct Tile {
        std::string name;
        bool placeable = false;     // a piece may be put here
        std::string flat, mark;     // seen from above: the cell, its flag
        int height = 0;             // pixels the car and pieces stand on (-1 none)
        std::string ground, object; // the oblique view's ground and object
        std::string enter, act;     // sections: the car arrives / 1289
    };

    // 4320 {name, width, height, x, y, direction, fuel?, ?, place bank, run
    // bank}, 4322 {pieces, per kind...}, 4323 rows of tiles, 4321 end.
    struct Course {
        std::string name;
        int width = 0, height = 0, startX = 0, startY = 0, direction = 0;
        std::array<int, 2> extra{}; // +0x1c, +0x20 (not used natively)
        std::string placeBank, runBank;
        int firstSlot = 0; // the course's first ＜車＞アイテム配置 slot
        int pieceTotal = 0;
        std::vector<int> pieceCounts; // per piece, in the 4310 order
        std::vector<int> tiles;       // index into tiles, -1 unknown

        int slots() const { return pieceTotal + 1; } // the pieces and the car
    };

    std::vector<Piece> pieces;
    std::vector<Tile> tiles;
    std::vector<Course> courses;

    const Tile* tile(const std::string& name) const;
    int tileIndex(const std::string& name) const;
    const Piece* pieceById(int id) const;

    static const CarData& get(const Story& story);
};

class CarSelectLoop : public Loop {
public:
    CarSelectLoop(Story& story, LoopArgs args);
    ~CarSelectLoop() override;

    void activate() override;
    void deactivate() override;
    void frame(const lscr::SceneInput& input) override;
    bool busy() const override;
    lscr::Result command(int index, lscr::Call& call) override;

private:
    class Engine;
    enum class State { Idle, Loading, Running, Finished };
    State state_ = State::Idle;
    bool started_ = false;
    std::unique_ptr<Engine> engine_;
};

class CarEditLoop : public Loop {
public:
    CarEditLoop(Story& story, LoopArgs args);
    ~CarEditLoop() override;

    void activate() override;
    void deactivate() override;
    void end() override;
    void frame(const lscr::SceneInput& input) override;
    bool busy() const override;
    lscr::Result command(int index, lscr::Call& call) override;

private:
    class Engine;
    void keep(); // 0211f830: the pieces' cells into ＜車＞アイテム配置
    enum class State { Idle, Loading, Running, Finished };
    State state_ = State::Idle;
    int direction_ = 0; // +0x9b0
    std::unique_ptr<Engine> engine_;
};

class CarGameLoop : public Loop {
public:
    CarGameLoop(Story& story, LoopArgs args);
    ~CarGameLoop() override;

    void activate() override;
    void deactivate() override;
    void frame(const lscr::SceneInput& input) override;
    bool busy() const override;
    lscr::Result command(int index, lscr::Call& call) override;
    bool condition(int index, lscr::Call& call, std::span<lscr::Value> args) override;

private:
    class Engine;
    enum class State { Idle, Loading, Running, Finished };
    State state_ = State::Idle;
    std::unique_ptr<Engine> engine_;
};

// The app's minigame free play: every course open (the select's DBG_ADD
// sets ＜車＞コース [1..9]; course 0 is always open) and which courses are
// cleared (the run's SUCCESS sets ＜車クリアー＞ [course + 1]). The flags'
// names are read from those two sections of the scripts.
constexpr int kCarCourses = 10;
void openCarCourses(Story& story);
std::vector<std::uint8_t> carCleared(const Story& story); // courses 1..10
void setCarCleared(Story& story, const std::vector<std::uint8_t>& cleared);

} // namespace picarat::adventure3
