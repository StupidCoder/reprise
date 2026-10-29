#pragma once
// The sixth adventure's Nut Roller ("Walnussbahn", loop 6300,
// lt6CLoopMiniRisu: create 0x10507c, vtable 0x3c7ae8, command method
// 0x2c38bc, update 0x2c3070). A squirrel runs along a raised track around
// a field, picks up the walnut, acorns and rocks lying on the track and
// throws them into the field, where they roll until something stops them;
// the course is won when the walnut comes to rest in the tent.
//
// The rules (recovered from the class's code)
// -------------------------------------------
// The course is a grid of cells read from res/sub/risu_stage_NN.xs
// (8020 width height, 8021 kind count, 8022 per kind its type and four
// blocked sides, 8023 per cell kind, object, height; 8024 / 8025 the two
// camera views, 8026 the following camera's stereo distance, near and
// far). Kind 0 is water. The kinds' types: 1 ground, 2 a hole a ball sinks
// into (a rock fills it), 3..6 low curves, 10..13 high curves (both turn a
// ball that is below the cell's surface), 7 / 8 bridges across / along
// (they collapse once a ball has crossed), 9 the squirrel's track. Objects:
// 1 the squirrel, 2 the tent, 3 the walnut, 4 an acorn, 5 a rock, 6 a
// bridge, 7 a tree, 8 a hedge, 9 bamboo; 1 and 3..5 move ("pieces"), the
// others are fixtures.
//
// A ball rolls one cell per step (10 frames at the scripts' speeds) in its
// direction while the next cell lets it in: not higher than the ball unless
// a curve's ramp leads up, no fixture other than a bridge or the tent, the
// side not blocked. A ball in front is pushed instead (it takes over the
// direction; the pusher stops). Each step a ball also falls to the height
// of the cell under it (or onto the ball below it); into water it is lost,
// into a hole it sinks two levels (a rock fills the hole, an acorn grows a
// tree there, the walnut is stuck). Rocks and acorns landing on the walnut
// crush it. The walnut lost, stuck or crushed fails the course.
//
// The scripts (seq/mini_risu) run the rest: the menus, the squirrel's
// input, the throw's arc, sounds and the result. Script commands (2110):
// 1 stage, 2 / 102 forget the objects, 3 / 4 a camera view over frames,
// 5 the fixtures' resting motions, 6 restart, 7 / 8 / 101 the undo ring,
// 9 the shadows, 10 squirrel step or turn, 11 pick up, 12 put down or
// throw, 13 what is held, 14 the squirrel's resting motion, 15 a ball in
// front, 20 one frame of rolling, 21 the walnut in the tent, 22 anything
// still moving, 23 the course failed, 30 the timing values, 100 the water
// splashes.
#include "system/a5_game.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace picarat::adventure6 {

class NutRoller final : public adventure5::SpecialPuzzle {
public:
    explicit NutRoller(adventure5::Game& game);

    bool command(int number, xq::Call& c, std::size_t first) override;
    void update() override;
    std::string describe() const override;

    static constexpr int kMaxColumns = 20, kMaxRows = 20;
    static constexpr int kMaxPieces = 32;
    static constexpr int kUndoDepth = 30;

    // The grid's objects (8023's second value).
    enum Object : std::uint8_t {
        None = 0,
        Squirrel = 1,
        Tent = 2,
        Walnut = 3,
        Acorn = 4,
        Rock = 5,
        Bridge = 6,
        Tree = 7,
        Hedge = 8,
        Bamboo = 9,
        ObjectKinds = 10
    };

    // Kind types (8022's first value).
    enum Type : std::uint8_t {
        Ground = 1,
        Hole = 2,
        BridgeAcross = 7, // blocks left and right
        BridgeAlong = 8,  // blocks up and down
        Track = 9
    };

    // Directions: 0 up (-z), 1 down (+z), 2 left (-x), 3 right (+x).
    static constexpr int kNoDirection = -1;

    struct Kind {
        std::uint8_t type = 0;
        bool blocked[4] = {};
    };

    struct Cell {
        std::uint8_t kind = 0, fixture = None, piece = None, height = 0;
        std::uint32_t fixtureObject = 0; // the fixture's 3D object (name hash)
    };

    // A camera view (8024 / 8025): eye, target, field of view (radians),
    // two stereo values, near and far.
    struct View {
        std::array<float, 3> eye{}, target{};
        float fov = 0, stereoDistance = 0, stereoStrength = 0, nearZ = 0, farZ = 0;
    };

    struct Stage {
        int width = 0, height = 0;
        std::array<Kind, 17> kinds{};
        std::array<std::array<Cell, kMaxColumns>, kMaxRows> cells{};
        std::array<int, 17> counts{}; // objects of each kind on the grid
        View views[2];
        float followStereo = 0, followNear = 0, followFar = 0; // 8026

        Cell& at(int x, int y) { return cells[std::size_t(y)][std::size_t(x)]; }

        const Cell& at(int x, int y) const { return cells[std::size_t(y)][std::size_t(x)]; }

        const Kind& kindOf(int x, int y) const { return kinds[at(x, y).kind % kinds.size()]; }
    };

    // A moving piece: the squirrel or a ball.
    struct Piece {
        Object type = None;
        std::uint32_t object = 0; // its 3D object
        int x = 0, y = 0, h = 0;
        int dir = 1;
        // Balls: rolling on (+0x18), sinking into a hole next step (+0x1c),
        // stopping next step (+0x20), changed this step (+0x24), the step's
        // events and the last step's (+0x2c / +0x28), the turn (quaternion
        // x, y, z, w; +0x30) and its matrix (+0x40).
        bool rolling = false, sinking = false, stopping = false, changed = false;
        unsigned events = 0, lastEvents = 0;
        std::array<float, 4> turn{0, 0, 0, 1};
        std::array<float, 9> matrix{1, 0, 0, 0, 1, 0, 0, 0, 1};
        // The squirrel: the ball it holds (an index into the pieces).
        int held = -1;

        bool isBall() const { return type == Walnut || type == Acorn || type == Rock; }
    };

private:
    struct Timing {
        // 2110 30 (the scripts: 12 6 10 12 600 4 4 60 0.628319): frames of
        // a squirrel step, of its turn, of a ball's step, per fall (times
        // the drop / 5 + 1), the following camera's distance, the margins it
        // keeps from the grid's sides, its frames, a ball's turn per frame
        // (radians).
        int walkFrames = 12, turnFrames = 6, rollFrames = 10, fallFrames = 12;
        int followDistance = 600, marginX = 4, marginY = 4, followFrames = 60;
        float spin = 0.628319f;
    };

    // What an undo step keeps of a piece (0x2c3294 / 0x2c69f4).
    struct PieceState {
        int x = 0, y = 0, h = 0, dir = 1;
        std::array<float, 3> position{}, rotation{};
        bool visible = true;
        std::array<float, 9> matrix{1, 0, 0, 0, 1, 0, 0, 0, 1};
    };

    struct Snapshot {
        Stage stage;
        std::vector<PieceState> pieces;
        std::array<bool, 32> bridges{}; // the bridge objects shown
        int trees = 0;                  // trees grown
    };

    // ---- rules
    bool loadStage(int number);
    void createObjects();
    void addPiece(Object type, int x, int y, int h, std::uint32_t object);
    bool inGrid(int x, int y) const;
    int heightAt(int x, int y, const Piece* except) const;
    bool atRest(const Piece& p) const;
    bool shown(const Piece& p) const;
    unsigned ballCanMove(Piece& p, int dir, bool test);
    unsigned ballMove(Piece& p, int dir, bool test);
    unsigned ballSettle(Piece& p, bool resting, bool test);
    unsigned ballSpin(Piece& p);
    unsigned ballSinks(Piece& p);
    void ballInWater(Piece& p);
    void walnutCrushed(Piece& p);
    bool squirrelCanMove(const Piece& s, int dir) const;
    unsigned squirrelStep(Piece& s, int dir);
    int pickUp(Piece& s);
    int putDown(Piece& s, std::array<float, 3>& throwFrom);
    int simulate(unsigned& events);
    bool goalReached();
    bool anythingMoving(unsigned& events);
    Piece* squirrel();
    const Piece* squirrel() const;
    Piece* pieceAt(int x, int y, int h);

    // ---- undo (0x2c510c / 0x2c5d30 / 0x2c5f64)
    Snapshot capture() const;
    void restore(const Snapshot& s);
    void pushUndo();
    void popUndo();

    // ---- presentation
    adventure56::Object3D* object(std::uint32_t hash) const;
    static std::uint32_t poolName(Object type, int index, std::string* name = nullptr);
    void playMotion(std::uint32_t object, const char* motion, bool once);
    void moveTo(std::uint32_t object, const std::array<float, 3>& to, float frames);
    void splash(int x, int y, int h);
    void createSplashes();
    void createShadows();
    void updateShadows();
    void showTurn(const Piece& p);
    void cameraView(int which, float frames);
    void startFollowing(int piece);
    void follow();
    void stepCameraClip();

    adventure5::Game& game_;
    Timing timing_;
    Stage stage_, initial_;
    std::vector<Piece> pieces_;
    int squirrel_ = -1;
    int treesGrown_ = 0;
    int trees_ = 0;       // tree objects made for the course
    bool failed_ = false; // +0x363c: the course is lost (2110 23 reads it once)
    // The camera: the view chosen (+0x3628), the piece followed (+0x3624),
    // the first follow frame (+0x3638) and the eye's offset from the
    // target while following (+0x362c).
    int cameraMode_ = 0;
    int followed_ = -1;
    bool followStarts_ = false;
    std::array<float, 3> followOffset_{};
    adventure56::Tween<1> nearZ_, farZ_; // the camera's clip distances moving
    // The undo ring (+0x3618): the newest state apart, the older ones in the
    // ring (count +0x361c, index +0x3620).
    std::vector<Snapshot> ring_;
    Snapshot newest_;
    int ringCount_ = 0, ringIndex_ = 0;
};

} // namespace picarat::adventure6
