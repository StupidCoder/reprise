#pragma once
// The fourth adventure's fish tank (loops 6401 and 6400, overlay 37; the
// bag's third minigame, reached through its tank menu 6401).
//
// How it plays
// ------------
// Ten tanks (res/sub/lt4_sub_fish_intr.cb), each a grid of 14 x 9 cells:
// the fish's start (its kind also says which way the fish sets off: right,
// up-right or down-right, left, up-left or down-left), coins, blocks,
// bubbles, hyper bubbles and spots where the player may put bubbles. The
// fish swims from cell to cell, straight or diagonally, and bounces off the
// tank's walls, blocks and bubbles: a wall or block beside it turns that
// half of its direction round, one met head-on across a corner sends it
// back the way it came. The fish eats every coin it swims through; a tank
// is cleared when it has eaten the tank's number of coins before its time
// runs out.
//
// Before a run the player drags the tank's few bubbles onto free spots;
// each change plots the fish's whole path anew (63 bounces' worth) and the
// run replays that plot. After an ordinary bounce the fish gets a burst of
// speed that dies away over 25 frames. A hyper bubble sends it into a rush:
// it swims at 2.1 times its speed, breaks through bubbles instead of
// bouncing off them and leaves fading after-images; each of the next
// bounces slows it a step (1.8, 1.5), and the third ends the rush.
//
// The script (evt/prg/lt4_mini_fish_intr_main) builds the screens, the
// bubbles the player drags, the timer and the windows; the loop keeps the
// tank and the fish and answers 2400 with a sub-command (021092b8): 1 load
// the tanks, 2 the run's numbers, 10 set up a tank, 11 its numbers, 12 back
// to before the run, 20 start, 21 the seconds left, 30 a bubble the player
// may place, 31 put it on the free spot nearest a point, 32 take it off
// again, 33 show or hide a piece, 40 the fish's direction, 41 its speed,
// 42 the speed now and before, 43 the rush's bounces left, 50/51 the kinds
// of piece. The menu (6401) answers 1 load and 2 a tank's numbers.
//
// Reconstructed from overlay 37 (02105220..0210b5c0); see
// .analysis/adventure4/docs/minigames.md.
#include "story/a4_script_loop.hpp"
#include <array>
#include <map>
#include <vector>

namespace picarat::adventure4 {

// The tank menu (6401, ov37 vtable at 0210b5c0's class): a tank's numbers.
class FishMenuLoop final : public ScriptLoop {
public:
    FishMenuLoop(Game& game, int id, std::span<const Value> values)
        : ScriptLoop(game, id, values) {}

    bool command(gss::Call& c, int op) override;

private:
    // 0210544c: a tank's numbers (3 bytes each in the original).
    struct Numbers {
        int bubbles = 0, coins = 0, seconds = 0;
    };

    std::vector<Numbers> tanks_; // +0x12c
};

class FishLoop final : public ScriptLoop {
public:
    FishLoop(Game& game, int id, std::span<const Value> values);
    ~FishLoop() override;

    bool command(gss::Call& c, int op) override;

protected:
    void afterUpdate() override;

private:
    using Point = std::array<std::int32_t, 2>; // 20.12 pixels
    using Cell = std::array<int, 2>;
    static constexpr int kColumns = 14, kRows = 9;
    using Grid = std::array<std::array<int, kRows>, kColumns>; // [x][y]

    // A tank (0x102 bytes: the numbers, then the cells column by column).
    struct Tank {
        int bubbles = 0;           // [0]: the bubbles the player may place
        int coins = 0;             // [1]: the coins to eat
        int seconds = 0;           // [2]: the time limit
        int width = 0, height = 0; // [4], [5]
        std::array<std::array<int, kRows>, kColumns> cells{}; // the kinds
    };

    // A kind of piece (2400 51; 12 bytes: kind, the hash of a block run on
    // the fish when it is hit (none in this game's script), of the hit's
    // block, of the sequence).
    struct Kind {
        int kind = 0;
        std::string sequence, fishBlock, hitBlock;
    };

    // A piece of the tank (0xdc bytes each).
    struct Piece {
        int index = 0;              // +0
        Cell cell{};                // +2, +4
        const Kind* kind = nullptr; // +0xd4
        AnimationPlayer player;     // +8
        bool shown = false;         // +0xd8
    };

    // A step of the plotted path (16 bytes each, at most 512 in the
    // original): whether the fish bounced and how often, where it was,
    // where it goes and its direction then.
    struct Step {
        bool bounced = false;
        int bounces = 0;
        Cell from{}, to{}, direction{};
    };

    // A hit the plot met (8 bytes each): the piece and the step.
    struct Hit {
        int piece = -1;
        int step = 0;
    };

    // An after-image of the rush (0xd4 bytes each, eight).
    struct Trail {
        Point at{};
        Cell direction{};
        const Animation::Sequence* sequence = nullptr;
    };

    void loadTanks();                       // 021094d0 (1)
    bool setUp(const std::string& picture); // 02109804 (10)
    void clearTank();                       // 02107528
    bool addPiece(int kind, Cell cell);     // 02107604
    bool placeFish();                       // 02107818
    bool plot();                            // 021089c8
    bool plotStep();                        // 02107d1c
    bool replayStep();                      // 02108294
    void enterTarget(bool live);            // 02108518
    bool hit(int piece, bool live);         // 02108bf8
    void hitShow(const Piece& p);           // 02108e94
    void swim(std::int32_t elapsed);        // 02106c0c
    void draw();                            // 02106538
    void startBounce();                     // the bounce's half of 02107d1c / 02108294

    // 021091c8: the hit blocks' arguments: the rush's bounces left and
    // whether the rush began with this hit.
    std::vector<Value> hitArguments() const;
    int rushLeft() const;      // 02108288(3 - bounces, 0) in the rush, else 0
    bool inside(Cell c) const; // 02109240
    const Piece* pieceAt(Cell c) const;
    Piece* pieceAt(Cell c);
    const Kind* kindOf(int kind) const;
    Chara* fish() const;
    const Tank* tank() const;

    Piece& fishPiece() { return pieces_[std::size_t(fish_)]; }

    std::vector<Tank> tanks_;           // +0x130
    std::map<int, Kind> kinds_;         // +0x140 / +0x150 (sorted by kind)
    std::vector<Piece> pieces_;         // +0x18c
    Grid grid_{}, initial_{}, saved_{}; // +0x19c, +0x298, +0x394: piece or -1
    int stage_ = 0;                     // +0x188 (0..9)
    // The run's numbers (2400 2, +0x160..+0x17c): the burst after a bounce
    // and how many frames it lasts, the rush's speeds after two, one and no
    // bounces (the last is its start); three more are kept but unused.
    std::array<float, 8> numbers_{};
    int state_ = 0;                // +0xbcc: 1 set up, 2 running, 3 over
    float elapsed_ = 0;            // +0xbd0: frames since the start
    int eaten_ = 0;                // +0xbd4: coins
    bool warned_ = false;          // +0xbd8: the end is near
    int stepIndex_ = 0;            // +0xbda
    std::vector<Step> steps_;      // +0x13e0
    std::vector<Hit> hits_;        // +0x33e0 (+0xbdc the count)
    std::size_t hitIndex_ = 0;     // +0xbdd: hits replayed
    std::int32_t lastElapsed_ = 0; // +0x47cc
    // The water's shimmer (+0x47d0 the picture, +0x47d4 its phase, +0x47d8
    // its alpha).
    const ImageResource* shimmer_ = nullptr;
    std::int32_t shimmerPhase_ = -32, shimmerAlpha_ = 0;

    // The fish (+0x490..+0xbc8).
    int fish_ = -1;                   // +0x494: its piece
    Cell target_{};                   // +0x498: the cell it swims to
    Cell lastDirection_{};            // +0x49c
    Cell direction_{};                // +0x4a0
    Point from_{}, to_{};             // +0x4a4, +0x4b4 (+0x4ac a midpoint, unused)
    float progress_ = 0;              // +0x4bc: frames into the cell
    float framesPerCell_ = 0;         // +0x4c0
    float speed_ = 1, lastSpeed_ = 1; // +0x4c4, +0x4c8
    TweenValue burst_;                // +0x4cc
    bool rush_ = false;               // +0x4f4
    int rushBounces_ = 0;             // +0x4f5
    int bounceCount_ = 0;             // +0x4f6 (counted, not read)
    bool bouncing_ = false;           // +0x4f8
    float trailTime_ = 0;             // +0x4fc
    std::array<Trail, 8> trail_{};    // +0x500
    TweenValue trailAlpha_;           // +0xba0
    bool rushBegun_ = false;          // +0xbc8
};

} // namespace picarat::adventure4
