#pragma once
// The fourth adventure's model railway (loop 6200, overlay 39
// lt4CLoopMiniTrain; the bag's train minigame).
//
// How it plays
// ------------
// Ten courses, each a board of square cells (at most 12 x 9) read from
// res/sub/lt4_sub_train.cb: empty lots in three colours, houses, mountains
// and trees (obstacles), stations (crossed only along their axis), fuel
// cans, printed rails and roads, the start of each of the player's trains
// (one or two) and the goal. In the editor (lower screen, seen from above)
// the player lays track by dragging from a train's head: every drag past
// half a cell extends that train's track by one cell in the drag's
// direction and costs one unit of fuel; a cell holding a fuel can gives
// five back when the track leaves it. Dragging back onto the previous cell
// takes the last piece up again (and the fuel back). A straight piece,
// printed or laid, can be crossed at right angles once (a crossing); roads
// can be crossed the same way (a level crossing). Track can't be laid onto
// obstacles, another train's start or head, the fixed trains' goal, or out
// of a cell the train has reached the goal in.
//
// "Abfahrt!" runs the course in an isometric view (lower screen, the
// camera on a leading train) with a map on the upper screen: the player's
// trains follow their track, fixed trains their printed rails and road
// cars their roads (looping round, or driving off the board and starting
// again), each car one cell behind the one in front, 32 steps a cell. A
// train counts the stations its engine passes and picks up fuel cans. Two
// vehicles meeting in a cell, or one finding the next cell taken, stop in
// the middle of their cell - an accident; a train that runs out of track
// short of the goal is one too. When every train of the player's stands in
// the goal the course is won if all the course's stations were passed,
// missed otherwise. The script (evt/prg/lt4_mini_train) does the menus,
// the countdown and the result windows.
//
// The script talks to the loop with 2400 and a sub-command (02109424):
// 0/1 the mode (1 course select, 2 editor, 3 run; 1 also marks a mode
// change), 2/3 the course (1..10), 4 forget the board's pictures, 5/6 the
// run's state (0 paused, 1 running, 2 result), 7..10 the accident's camera
// (shake, then glide to the crashed vehicle), 100/200/300 set up the
// select screen, the editor and the run, 201 clear the track.
//
// Reconstructed from overlay 39 (02105220..0210bd20); see
// .analysis/adventure4/docs/minigames.md.
#include "story/a4_script_loop.hpp"
#include <array>
#include <vector>

namespace picarat::adventure4 {

class TrainLoop final : public ScriptLoop {
public:
    TrainLoop(Game& game, int id, std::span<const Value> values);

    void resume() override;
    bool command(gss::Call& c, int op) override;

protected:
    void afterPen() override;
    void afterUpdate() override;

private:
    using Point = std::array<std::int32_t, 2>; // 20.12 pixels

    // A cell (16 bytes in the original: kind, colour, object, state,
    // occupant).
    struct Cell {
        int kind = 0;   // 5200 operand 1 (see kinds below)
        int colour = 0; // the lot's colour (1..3; 99 the fixed trains' goal)
        int object = 0; // what stands on it: 1 house, 2 mountain, 3 tree,
                        // -4/5 a station (vertical/horizontal), 6 fuel;
                        // negative: mirrored
        // The piece laid on it (0 none, 1..6 like the rails, 7 a crossing,
        // -1 a station passed through).
        int state = 0;
        int occupant = -1, occupantCar = 0; // during a run
    };

    // A vehicle's start (5212 player train, 5213 fixed train, 5214 road car).
    struct Start {
        int cell = 0, cars = 1, direction = 0;
    };

    struct Course {
        int width = 0, height = 0, fuel = 0;
        int players = 0, fixed = 0, roads = 0;
        std::vector<Cell> cells;
        std::vector<Start> starts; // players, then fixed trains, then road cars
        int stations = 0;          // +0x293d
        int blocked = -1;          // +0x293c: the road car stopped at its start
    };

    // A car's pictures and places (0xe0 bytes each in the original).
    struct Car {
        const Animation::Sequence* look = nullptr; // lower screen (isometric)
        const Animation::Sequence* map = nullptr;  // upper screen
        bool mirror = false;                       // +200
        Point board{};                             // +0xd0 (board pixels)
        Point screen{};                            // +0xd8 (isometric)
        bool drawn = false;                        // +0x7e0 (per frame)
    };

    // A vehicle of the course (0xa4c bytes each).
    struct Vehicle {
        int cars = 1, start = 0, direction = 0; // +0x7e9, +0x7ea, +0xa48
        bool road = false;                      // flag 4
        int length = 0;                         // +0x7eb: the last path index
        std::array<int, 68> path{};             // +0x7ec: cells
        std::array<Point, 66> points{};         // +0x830: cell edges
        int index = 0, step = 0;                // +0xa40, +0xa42
        int stopStep = 0;                       // +0xa44
        // +0xa41: 1 stopping, 2 crashed, 8 loops (road), 0x10 axis (road),
        // 0x20 just started, 0x40 cars lined up at the goal.
        unsigned flags = 0;
        std::array<Car, 9> car{};
    };

    // A picture of a cell (the 0xe0/200-byte animation players).
    struct Look {
        const AnimationResource* animation = nullptr;
        const Animation::Sequence* sequence = nullptr;
        bool mirror = false;
        Point screen{};
    };

    void loadCourses();

    Course& course() { return courses_[std::size_t(stageShown_ - 1)]; }

    // Setup commands.
    void setUpEditor();     // 02109ad4 (200)
    void resetBoard();      // 021088ac (201, a new course)
    void setUpRun();        // 0210a10c (300)
    void forgetPictures();  // 02109644 (4)
    void buildFixedPaths(); // 02108cf0
    void buildRoadPaths();  // 02108efc

    // The editor.
    void penIdle();                       // 02105a64
    void penDrag();                       // 02105b24
    void applyDrag();                     // 02105ebc
    bool extend();                        // 02107904
    int pieceFor(int from, int at) const; // 02107f08
    void showFuel();                      // fuel bar and counter
    void refreshCell(int cell, bool run); // 02108300

    // The run.
    void runStep();                                                   // 02106044
    void checkResult();                                               // 02108590
    void restartRoadCar(int v);                                       // 02109178
    void advance(Vehicle& v);                                         // 02105464
    static bool moving(const Vehicle& v);                             // 0210573c
    static void stopAt(Vehicle& v, int step);                         // 021057ac
    void follow();                                                    // 02105808
    void trainLook(Car& car, Point delta, int vehicle, int carIndex); // 02108a80
    void carLook(Car& car, Point delta);                              // 02108c1c

    // Geometry.
    Point centre(int originX, int originY, int cell) const;        // 02107744
    Point edge(int cell, int direction) const;                     // 02107fb0
    static Point project(Point board);                             // 0210825c
    static Point curve(Point a, Point c, Point b, std::int32_t t); // 02108064
    bool penOver(int cell) const;                                  // 021077d0

    // Drawing (the draw hooks 02106c60, 02106dfc, 02107410).
    void drawEditor();
    void drawRun();
    void drawCellAt(int order);         // 02107014
    void drawCars(Vehicle& v, int car); // 021054d4
    void drawMap();
    void sprite(const Look& look, Point at);

    const Animation::Sequence* sequence(const AnimationResource* a, const std::string& name) const;
    Chara* object(const std::string& name) const;

    std::vector<Course> courses_;
    int mode_ = 0;          // +0x120
    bool changing_ = false; // +0x124
    int stage_ = 0;         // +0x12c (1..10)
    int stageShown_ = 0;    // +0x12d
    std::vector<Vehicle> vehicles_;
    int originX_ = 0, originY_ = 0;        // +0x148
    int pen_ = 0;                          // +0x14c: 0 idle, 1 dragging a train's head
    int selected_ = -1;                    // +0x150
    std::array<int, 2> heads_{};           // +0x151
    int target_ = 0;                       // +0x153
    int drag_ = -1;                        // +0x154: 0 right, 1 left, 2 down, 3 up
    int fuel_ = 0;                         // +0x158
    int runState_ = 0;                     // +0x15c
    std::array<bool, 108> drawn_{};        // +0x160
    std::vector<int> order_;               // +0x1cc: the cells by diagonals
    int stations_ = 0;                     // +0x238
    int camera_ = 0;                       // +0x23c: the vehicle the camera follows
    Point view_{}, viewFrom_{}, viewTo_{}; // +0x240, +0x248, +0x250
    int viewStep_ = 0;                     // +0x264
    // The pictures: board cells and what stands on them (lower screen),
    // and the upper screen's map pieces and fuel cans.
    std::array<Look, 108> tiles_{}, objects_{}, mapTiles_{}, mapObjects_{};
};

} // namespace picarat::adventure4
