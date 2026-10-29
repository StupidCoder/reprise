#pragma once
// The sixth adventure's Flower Garden ("Blumengarten", loop 6200,
// lt6CLoopMiniGarden: create 0x106000, vtable 0x3c8cec, command method
// 0x2fc65c; its board routines at 0x21a8b0..0x21cb60). The player puts
// flowers from a pallet onto the soil of a dry garden; "Los!" lets the sun
// bloom the start cell and a chain reaction runs: a flower that is reached
// blooms and sends waves out, each step one cell further, in its pattern -
// red the eight cells around it and from there across its 3x3 patch, yellow
// a cross two cells long, blue across four cells, purple up and down four
// cells. Every cell a wave crosses blooms. A wave stops at a wall, a hole,
// or a cell that was already in bloom when the step began (that cell
// blinks). A blooming poison mushroom withers its 3x3 surroundings. The
// garden is done when every withered tree blooms, perfect when the whole
// soil does.
//
// The board (res/sub/garden_stage_NN.xs, read through the table 0x424904:
// 8010 width height; 8011 one row of cell types - 0 none, 1 soil, 2 a
// withered tree, 3 a poison mushroom, 4 the start; 8012 / 8013 rows of
// walls before each column / row; 8014 the pallet's flowers by colour; 8015
// / 8016 the camera's two views) is data; the rules, the pallet and the
// pieces drawn are the class's.
//
// Script commands (2110): 1 garden, 2 objects, 3 clear, 4 the camera's view,
// 5 all flowers back, 6 the bloom undone, 7 the camera's close view, 8 / 9
// keep / restore the flowers placed, 10 the 3D grid, 11 the 2D board, 12
// the pallet, 13 the trees left, 20 pick up, 21 put down, 22 the preview
// under the pen, 30 a flower on the start, 31 start, 32 a step, 33 the
// verdict, 34 the poison removed, 35 everything settled, 36 trees still
// opening, 37 a tree playing "open".
#include "system/a5_game.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace picarat::adventure6 {

class Garden final : public adventure5::SpecialPuzzle {
public:
    explicit Garden(adventure5::Game& game);

    bool command(int number, xq::Call& c, std::size_t first) override;
    std::string describe() const override;

    static constexpr int kSize = 15; // the grid's arrays (a garden is smaller)
    static constexpr int kColours = 4;
    static constexpr int kFlowersPerColour = 10;
    static constexpr int kWaves = 128;
    static constexpr int kBursts = 10;

    enum Type : std::int8_t { NoCell = 0, Soil = 1, WitheredTree = 2, Poison = 3, Start = 4 };

    // Wave directions (a flower record's eight effect slots): 0 up-left,
    // 1 up, 2 up-right, 3 left, 4 right, 5 down-left, 6 down, 7 down-right.
    enum Slot : int { UpLeft, Up, UpRight, Left, Right, DownLeft, Down, DownRight, Slots };

    struct Cell {
        std::int8_t type = NoCell; // +0x1c6
        std::int8_t lit = 0;       // +0x1c7: in bloom
        std::int8_t blink = 0;     // +0x1c8: frames a stopped wave shows here
        std::int8_t colour = 0;    // +0x1c9: the flower planted, 0 none
    };

    // A camera view (8015 / 8016).
    struct View {
        std::array<float, 3> eye{}, target{};
        float fov = 0, stereoDistance = 0, stereoStrength = 0, nearZ = 0, farZ = 0;
    };

    // The garden as read (record +0x164; a copy at +0x74c stays as loaded).
    struct Layout {
        std::array<int, kColours + 1> pallet{}; // flowers left by colour (1..4)
        View views[2];
        int width = 0, height = 0;
        std::array<std::array<Cell, kSize>, kSize> cells{};
        // Walls: before column x in row y (+0x54a, wallLeft[y][x]) and before
        // row y in column x (+0x64a, wallAbove[y][x]).
        std::array<std::array<std::uint8_t, 16>, 16> wallLeft{}, wallAbove{};

        Cell& at(int x, int y) { return cells[std::size_t(y)][std::size_t(x)]; }

        const Cell& at(int x, int y) const { return cells[std::size_t(y)][std::size_t(x)]; }
    };

    // A planted flower's record (attached to its object, 0x21c884): its index
    // (a bit in the cells' reach masks), a wave effect per slot and the slots
    // whose wave ended.
    struct Flower {
        int index = 0;
        std::array<std::uint32_t, Slots> waves{};
        std::uint32_t ended = 0;
    };

private:
    using Mask = std::uint64_t;

    // ---- the board
    bool loadGarden(int number);
    bool inGrid(int x, int y) const;
    Flower* flowerAt(int x, int y);
    void plant(int x, int y, int colour);
    void releaseFlower(int x, int y);
    void returnObject(int colour, std::uint32_t object);
    // The wave routines (0x21c510 up, 0x21c2dc down, 0x21c09c left,
    // 0x21be5c right): from (x, y) `distance` cells on for the flower at
    // (fx, fy); `preview` marks the pen's preview instead.
    bool spread(int x, int y, int dx, int dy, int distance, int fx, int fy, int slot, bool preview,
                bool stopAtWall);
    void reach(Flower* f, int slot, int fromX, int fromY, int x, int y, int angle, bool preview);
    void endWave(Flower* f, int slot);
    bool redSpread(int x, int y, int distance, bool preview);
    void previewAt(int x, int y);
    int step(unsigned& events);
    void start();
    void undoBloom();
    void allBack();
    unsigned showStep();

    // ---- presentation
    adventure56::Object3D* object3D(std::uint32_t hash) const;
    adventure56::Object* object2D(std::uint32_t hash) const;
    void playMotion(std::uint32_t object, std::uint32_t motion, bool once);
    void syncMotion(int kind, std::uint32_t object, int state);
    std::uint32_t takeWave();
    void giveBackWave(std::uint32_t object);
    void createObjects();
    void buildGrid3D();
    void buildBoard2D();
    void buildPallet();
    void buildTreesLeft(bool count);
    void cameraView(int which, float frames, int curve);
    bool closeView(float frames);
    std::array<float, 2> objectPosition(std::uint32_t hash) const;

    adventure5::Game& game_;
    int gardenNumber_ = 0;
    Layout layout_, loaded_;
    // Per cell: the flower's object (+0x135c), the wave's distance (+0x1199;
    // 0 waiting, -1 done, -2 / -3 its effects ending), the step's snapshots
    // (+0x127a, +0xd34), the pen's preview (+0x10b8) and the flowers whose
    // waves reached it (+0x2344, last step's +0x2a4c).
    std::array<std::array<std::uint32_t, kSize>, kSize> objects_{};
    std::array<std::array<std::int8_t, kSize>, kSize> wave_{}, waveAtStep_{}, preview_{};
    std::array<std::array<Cell, kSize>, kSize> cellsAtStep_{};
    std::array<std::array<Mask, kSize>, kSize> reached_{}, reachedAtStep_{};
    // The flowers' objects free per colour (+0x185c), their records, the
    // planted flowers by kind for the shared motions (+0x1790 flowers,
    // +0x16e0 trees, +0x170c poison).
    std::array<std::array<std::uint32_t, kFlowersPerColour>, kColours + 1> freeFlowers_{};
    std::map<std::uint32_t, Flower> records_;
    std::vector<std::uint32_t> flowers_, trees_, poisons_;
    std::array<std::uint32_t, kWaves> wavePool_{};
    std::array<std::uint32_t, kBursts> bursts_{}, spores_{};
    // The pen: the colour held (+0x3154), from where (+0x3158 / +0x315c,
    // +0x3160 from the board), the cell previewed (+0x316c / +0x3170).
    int held_ = 0, heldX_ = 0, heldY_ = 0;
    bool heldFromBoard_ = false;
    int hoverX_ = -1, hoverY_ = -1;
    bool perfect_ = false;              // +0x3164
    int treesLeft_ = -1;                // +0x3168
    float boardX_ = 0, boardY_ = 0;     // +0x3174 / +0x3178: the 2D board's corner
    float speed_ = 1, waveFrames_ = 12; // +0x317c / +0x3180
    // 2110 8 / 9 keep the flowers placed per garden in the save (+0x128c4 of
    // the save object); kept here while the session lasts (open item).
    std::map<int, std::vector<std::array<int, 3>>> kept_;
};

} // namespace picarat::adventure6
