#pragma once
// What the rabbit loops share (story/a5_rabbit*.cpp): the rabbit's record,
// the tables of res/sub/lt5_sub_rabbit_common.xs and _nazo.xs, the hub (loop
// 6000) with the rabbit's motion machine, and the base of the loops the hub
// puts on top. Internal to the rabbit sources; see a5_rabbit.hpp.
#include "story/a5_rabbit.hpp"
#include "import/a56_formats.hpp"
#include "system/a5_game.hpp"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace picarat::adventure5::rabbit {

using adventure56::Object;
using xq::Value;

// ＜ウサギ＞選んだウサギ: the rabbit chosen (1 or 2; 0 is taken as 1).
extern const std::string kChosenRabbit;
// 0x22d4f4: the rabbit's standing motion (001_立ち).
extern const std::string kStandMotion;

std::string format(const char* f, ...);

// ---- the rabbit's record (Game::RabbitRecord)

// 0x280ba0: 2 mastered, 1 learned, else 0.
int trickState(const Game& g, int slot);
// 0x280b0c: 2 sets the mastered bit, 1 the learned one.
void addTrickState(Game& g, int slot, int state);
// 0x280b60 / 0x33f7bc: the trick's "new" mark.
void setFresh(Game& g, int slot, bool on);
bool fresh(const Game& g, int slot);
// 0x33f978 / 0x18597c: the variant of the trick's third text.
int variant(const Game& g, int slot);
void setVariant(Game& g, int slot, int v);
// 0x280adc / 0x18562c: the rabbit's flags (below 128).
bool rabbitFlag(const Game& g, int n);
bool setRabbitFlag(Game& g, int n, bool on);

// ---- the hub's tables

// A trick (8201; 0x20 bytes in the hub's list +0x21ac): its id, name,
// learned / mastered from the start (+8 / +0xc), its save slot (+0x10),
// its kind (+0x14: 100 joy, 110 fun, 120 sorrow, 130 surprise), a
// description and its first row in the text list rbt_txt_a (+0x1c: the
// title; +1 the explanation, +2 the protagonist's words).
struct Trick {
    int id = 0;
    std::string name;
    bool learned = false, mastered = false;
    int slot = 0, kind = 0;
    std::string description;
    int text = 0;
};

// A trick's motion (8221): the trick, its description, the motion's name.
struct TrickMotion {
    int id = 0;
    std::string description, motion;
};

// A stretch of a motion that moves the rabbit (8301: the motion's name and
// six values): between the motion's progress `from` and `to` (0..1) it
// moves by (x, y, z) in its own facing and turns by `angle` degrees. A
// motion may have several (kept per name; the hub's sorted list +0x22c4).
struct MotionStretch {
    float from = 0, to = 0;
    std::array<float, 3> move{};
    float angle = 0;
};

// A show (8231, 0x12d09c: index, name, slots, tricks needed, steps) and
// its steps (8232: the slot counted from 1, a second value (-1: the slot
// has no director's comment, command 21), the trick asked for: -1 none
// (the slot is blocked), 100..130 any trick of that kind, else a trick's
// id).
struct ShowStep {
    int slot = 0, second = 0, trick = 0;
};

struct Show {
    int index = 0;
    std::string name;
    int slots = 0, needed = 0;
    std::vector<ShowStep> steps;
};

// ---- loop 6000

class RabbitHub final : public ScriptLoop {
public:
    // Command 200's handle (the original hands over the hub's address).
    static constexpr std::int32_t kHandle = RabbitLoopId;

    RabbitHub(Story& story, int id, std::vector<Value> values)
        : ScriptLoop(story, id, std::move(values), "mini_rabbit_main") {}

    void start() override;
    void end() override;

    // 0x32b780 only starts Init's thread; +0x15c bit 1 (which begin waits
    // for, 0x347a74) stays set until a frame end (0x2ad24c) sees the thread
    // idle. So a loop begins in the frame after its start at the earliest,
    // and the hub's first frame end puts its sub-loop on top before it has
    // begun: begun, its empty Main would end it at once (as it does when
    // the sub-loop below Commu pops back to it). The port's ScriptLoop runs
    // Init at once; the hub keeps the original's wait.
    bool beginWaits() const override { return ScriptLoop::beginWaits() || !frameEnded_; }

    void afterFrame() override;
    bool command(int number, xq::Call& c, std::size_t first) override;

    // 0x271c54: the trick at an index of the list.
    const Trick* trickAt(int i) const {
        return i >= 0 && i < int(tricks_.size()) ? &tricks_[std::size_t(i)] : nullptr;
    }
    // 0x271c80: the trick of an id.
    const Trick* trick(int id) const;
    int trickCount() const { return int(tricks_.size()); }
    // 0x271cb8: the show of an index.
    const Show* show(int index) const;
    // 0x349d04: a trick motion by its description.
    const TrickMotion* trickMotion(const std::string& description) const;

    // Nazo's judgement for the show (40, 0x2ffec8): passed (+0x21b0) and
    // per slot the trick and its mark (+0x21b4 count, +0x21b8 pairs: 1
    // right, -1 wrong, 0 free).
    struct Mark {
        int trick = 0, mark = 0;
    };

    void setJudgement(bool passed, std::vector<Mark> marks) {
        passed_ = passed;
        marks_ = std::move(marks);
    }
    const std::vector<Mark>& marks() const { return marks_; }
    bool passed() const { return passed_; }
    // 0x349d3c: a trick motion by its motion's name.
    const TrickMotion* trickMotionNamed(const std::string& motion) const;

    // ---- the rabbit's motion machine (state +0x22d0: 1 stands, 2 walks
    // to a point, 3 plays a motion once, 4 turns; 0x22dc9c each frame)

    // 0x22cb74 (110): walk to `to` with `motion`.
    void walkTo(const std::string& motion, const std::array<float, 3>& to);
    // 0x271104 (111): play `motion` once; facing `angle` (radians) first
    // unless it is 2 pi or more.
    void playOnce(const std::string& motion, float angle);
    void resetState() { state_ = 1; }
    // 0x22f648: turn to an angle (state 4).
    void turnTo(float angle) {
        angle_ = angle;
        state_ = 4;
    }
    // 0x22dfac: what a motion's stretches move the rabbit in all.
    std::array<float, 3> motionMove(const std::string& motion) const;
    // 0x271150: anything but standing.
    bool busy() const { return state_ != 1; }
    // 0x22dc9c: one frame (the loops on top call it from their frame end).
    void stepMotion();
    // 0x22e2a4: the protagonist's hand shown or hidden.
    void showHand(bool on);
    // 0x270e10: the hand moves to a point (0x2ab108: at once).
    void moveHand(const std::array<float, 3>& at);
    // 0x22ec00: the hand turned towards a direction.
    void turnHand(const std::array<float, 3>& direction);
    // Sets the standing motion (+0x2320) and restarts the state.
    void setStandMotion(const std::string& motion) {
        standMotion_ = motion;
        restart_ = true;
    }
    const std::string& handName() const { return handName_; }

private:
    void loadTables(const std::string& path);
    std::shared_ptr<adventure56::Model> model(const std::string& path);
    // 0x280978: the motion's stretch at progress t (from <= t < to).
    const MotionStretch* stretchAt(const std::string& motion, float t) const;
    // 0x2936c0: a motion on "usa" (flags as 6810's: 4 restarts).
    void playMotion(Object& usa, const std::string& motion, unsigned flags = 0);
    // The motion's progress 0..1 (0x2bc954) and the frames a stretch of it
    // lasts ((to - from) x length / rate, 0x340a70 / 0x280958).
    float progress(const Object& usa) const;
    float stretchFrames(const Object& usa, const MotionStretch& s) const;
    // 0x2c9e8c / 0x2631d8 / 0x2c9e44: the rotation wrapped into [-pi, pi),
    // its offset cleared.
    static void settleRotation(Object& usa);
    void enterWalk(Object& usa); // 0x22e2c4
    void stepWalk(Object& usa);  // 0x22e95c
    void stepPlay(Object& usa);  // 0x22e6b8
    void stepTurn(Object& usa);  // 0x22e584

    std::vector<Trick> tricks_;             // +0x21ac (8200..8202)
    std::vector<TrickMotion> trickMotions_; // 8220..8222
    std::map<std::string, std::vector<MotionStretch>> stretches_; // 8300..8302
    std::vector<Show> shows_;               // 8230..8234
    std::shared_ptr<adventure56::Model> rabbit_, motions_, shadow_, hand_;
    std::string handName_;    // 0x22d4e8
    bool rabbitMade_ = false; // +0x1a18: the rabbit's model component
    bool frameEnded_ = false;
    bool passed_ = false;     // +0x21b0
    std::vector<Mark> marks_;

    int state_ = 1, previous_ = 1; // +0x22d0 / +0x22d4
    bool restart_ = false;         // +0x22d8: enter the state again
    std::string motion_;           // +0x22fc
    std::string standMotion_;      // +0x2320
    std::array<float, 3> target_{}; // +0x22e0
    float angle_ = 0;               // +0x22ec
    std::array<float, 3> step_{};   // +0x22f0: a walk loop's move (0, rise, stride)
    float lastProgress_ = 0;        // +0x22dc
};

// ---- the loops on top of the hub

// The hub's sub-loops: the hub is the loop 6000 below them (their value 0
// is its handle); what they do not take goes to it.
class RabbitPart : public ScriptLoop {
public:
    RabbitPart(Story& story, int id, std::vector<Value> values, std::string script)
        : ScriptLoop(story, id, std::move(values), std::move(script)) {}

    bool command(int number, xq::Call& c, std::size_t first) override;

protected:
    RabbitHub* hub() const;

    Object* object(const std::string& name) { return game_.scene.object(xq::hash(name)); }

    static std::size_t operands(const xq::Call& c, std::size_t first) {
        return c.count() > first ? c.count() - first : 0;
    }
    static void put(xq::Call& c, std::size_t i, Value v) {
        if (Value* r = i < c.count() ? c.reg(i) : nullptr) {
            *r = std::move(v);
        }
    }
    static bool result(xq::Call& c, std::int32_t v) {
        c.result = Value::ofInt(v);
        return true;
    }
    // 0x2c1bac with the text shown at once (as 5911 with two operands).
    static void setText(Object* o, const std::string* text) {
        if (o) {
            o->text = text ? *text : std::string();
        }
    }
    // A row of a text list (0x2ce6a0: list, row, variant, field 1).
    const std::string* row(std::uint32_t list, int r, int page = 0);
    int rabbitKind() const {
        const int k = game_.variable(kChosenRabbit);
        return k == 0 ? 1 : k;
    }
    void play(Object* o, const std::string& animation) {
        if (o) {
            game_.playAnimation(*o, Value::ofText(animation), 0);
        }
    }
    // An animation set object's cells built into one list (0x2870c4 begin
    // with the first, 0x27b6a4 add, 0x188804 end); the cell's origin at
    // (x, y). *Hypothesis*: the built list shows in place of the playing
    // animation (mrg_piece plays "10" before its first build).
    static void beginCells(Object& o) {
        o.pieces.clear();
        o.playing = -1;
    }
    static void addCell(Object& o, const std::string& cell, float x, float y);
};


std::unique_ptr<Loop> makeNote(Story& story, int id, std::vector<Value> values);
std::unique_ptr<Loop> makeNazo(Story& story, int id, std::vector<Value> values);
std::unique_ptr<Loop> makeCommu(Story& story, int id, std::vector<Value> values);
std::unique_ptr<Loop> makeShow(Story& story, int id, std::vector<Value> values);

} // namespace picarat::adventure5::rabbit
