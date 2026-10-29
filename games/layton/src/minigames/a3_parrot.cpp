// The third adventure's parrot loops. See a3_parrot.hpp.
#include "minigames/a3_parrot.hpp"
#include "puzzles/a3_engines.hpp"
#include "sound/a3_sound.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace picarat::adventure3 {

namespace {
// Overlay 25's names (the strings of its code).
constexpr std::uint32_t kMenuProgram = 0x0211e618; // evt/prg/lt3_mini_parrot_menu.cbin
constexpr std::uint32_t kMenuAnimA = 0x0211e63c, kMenuAnimAFile = 0x0211e648;
constexpr std::uint32_t kMenuAnimB = 0x0211e65c, kMenuAnimBFile = 0x0211e668;
constexpr std::uint32_t kMenuPictureLower = 0x0211e67c, kMenuPictureUpper = 0x0211e690;
constexpr std::uint32_t kMenuPicturePicked = 0x0211e6ec; // the upper screen once one is picked
constexpr std::uint32_t kMenuTexts = 0x0211e454;         // "menu_txt": txt/?/90 part 100000
constexpr std::uint32_t kStageFlags = 0x0211e6b0;        // ＜オウム＞ステージ１ [n]
constexpr std::uint32_t kClearFlags = 0x0211e724;        // ＜オウムクリアー＞ [n]
constexpr std::uint32_t kCharacterButton = 0x0211e6c8;   // btn_chr%d
constexpr std::uint32_t kStartButton = 0x0211e6d4, kItem = 0x0211e6e0;
constexpr std::uint32_t kTitle = 0x0211e700, kText = 0x0211e70c, kRule = 0x0211e718;
constexpr std::uint32_t kIconOff = 0x0211e748, kIconOffNew = 0x0211e738, kIcon = 0x0211e758;
} // namespace

void ParrotMenuLoop::start() {
    // 0211db78: the program (the game also keeps the sound set to return
    // to when it is covered, 0211dbe0 - not reproduced).
}

void ParrotMenuLoop::activate() {
    // 0211dc20: its two animations (lower, upper), both screens' pictures,
    // its texts; MAIN starts once everything has loaded.
    scene_ = makeScene();
    scene_->load(story_.codeName(kMenuProgram, 25).text);
    for (const auto& f : Story::residentScripts()) {
        scene_->load(f);
    }
    scene_->loadAnimation(story_.codeName(kMenuAnimA, 25).text, 0,
                          story_.codeName(kMenuAnimAFile, 25).text);
    scene_->loadAnimation(story_.codeName(kMenuAnimB, 25).text, 2,
                          story_.codeName(kMenuAnimBFile, 25).text);
    for (int s = 0; s < 2; ++s) { // 020f2b14 -> 020c28a4
        const std::string path =
            story_.codeName(s == 0 ? kMenuPictureLower : kMenuPictureUpper, 25).text;
        scene_->setScreenPicture(s, story_.assets.exists(path) ? &story_.assets.picture(path)
                                                               : nullptr);
    }
    scene_->loadTextTable(story_.codeName(kMenuTexts, 25).text, 90, 100000);
    picked_ = 0;
    state_ = State::Loading;
}

void ParrotMenuLoop::deactivate() {
    scene_.reset();
    state_ = State::Idle;
}

bool ParrotMenuLoop::busy() const {
    return state_ == State::Loading || Loop::busy();
}

void ParrotMenuLoop::frame(const lscr::SceneInput& input) {
    if (!scene_) {
        return;
    }
    if (state_ == State::Loading) {
        // 0211dd18: MAIN with the request's arguments.
        const std::vector<lscr::Value> vars{lscr::Value::of(args_[0]), lscr::Value::of(args_[1]),
                                            lscr::Value::of(args_[2]), lscr::Value::of(args_[3])};
        scene_->start("MAIN", vars);
        state_ = State::Running;
    }
    if (state_ == State::Running && !scene_->running()) {
        // 0211dcdc: MAIN over - the menu ends.
        state_ = State::Finished;
        story_.request(Story::Request::Pop);
        return;
    }
    scene_->frame(input);
}

lscr::Result ParrotMenuLoop::command(int index, lscr::Call& call) {
    lscr::Scene& sc = *scene_;
    auto name = [&](std::uint32_t address) {
        return story_.codeName(address, 25).text;
    };
    auto format = [&](std::uint32_t address, int n) {
        char out[64];
        std::snprintf(out, sizeof out, name(address).c_str(), n);
        return std::string(out);
    };
    auto show = [&](const std::string& object, bool on) {
        lscr::Object* o = sc.find(object);
        if (o) {
            o->switches[0].on = on;
        }
        return o;
    };
    if (index == 0) {
        // 1280 (0211dd94): the characters whose stage is not open yet go,
        // and the start button and the item until one is picked.
        for (int n = 1; n <= 12; ++n) {
            if (!story_.state.flag(story_.codeName(kStageFlags, 25), n - 1)) {
                show(format(kCharacterButton, n), false);
            }
        }
        show(name(kStartButton), false);
        show(name(kItem), false);
        return lscr::Result::Next;
    }
    if (index != 1 || call.count() < 1) {
        return lscr::Result::Next;
    }
    // 1281 (0211de88): pick character {n}.
    const int n = call.integer(0);
    if (picked_ == 0) {
        // The first pick: the upper screen's picture, the start button
        // sliding up into place (30 pixels, 8 frames) and the three
        // messages of the upper screen.
        const std::string upper = name(kMenuPicturePicked);
        sc.setScreenPicture(1,
                            story_.assets.exists(upper) ? &story_.assets.picture(upper) : nullptr);
        if (lscr::Object* start = show(name(kStartButton), true)) {
            start->offset.set({0, 30 * lscr::fx::kOne});
            start->position.to(1, {0, -30 * lscr::fx::kOne}, 8 * lscr::fx::kOne);
        }
        show(name(kItem), true);

        struct Message {
            std::uint32_t name;
            int lineSpacing, x, y;
        };

        for (const Message& m :
             {Message{kTitle, 1, 30, 12}, Message{kText, 7, 18, 46}, Message{kRule, 1, 18, 163}}) {
            if (lscr::Object* msg = sc.createMessage(name(m.name), lscr::Screen::Upper, 256, 0)) {
                msg->text.style.characterSpacing = 1; // 02069c74 (1, spacing)
                msg->text.style.lineSpacing = m.lineSpacing;
                msg->position.set({m.x * lscr::fx::kOne, m.y * lscr::fx::kOne});
            }
        }
    } else if (n == picked_) {
        return lscr::Result::Next;
    } else if (lscr::Object* before = sc.find(format(kCharacterButton, picked_))) {
        // The one picked before shows its plain icon again ("...b_off"
        // until its delivery has been made).
        const bool cleared = story_.state.flag(story_.codeName(kClearFlags, 25), picked_);
        lscr::play(*before,
                   lscr::sequenceOf(*before, lscr::Value::ofText(format(
                                                 cleared ? kIconOff : kIconOffNew, picked_ - 1))),
                   0);
    }
    if (lscr::Object* item = sc.find(name(kItem))) {
        lscr::play(*item, lscr::sequenceOf(*item, lscr::Value::ofText(format(kIcon, n - 1))), 0);
    }
    picked_ = n;
    return lscr::Result::Next;
}

} // namespace picarat::adventure3

// --- The parrot game (overlay 26) ------------------------------------------------

namespace picarat::adventure3 {

namespace {
using lscr::fx::div;
using lscr::fx::kOne;
using lscr::fx::mul;
using Vec = std::array<int, 2>; // fixed point

// Overlay 26's names.
constexpr std::uint32_t kGameProgram = 0x021264d8;    // evt/prg/lt3_mini_parrot_game.cbin
constexpr std::uint32_t kStageData = 0x021264b8;      // rc/mode/lt3_mini_parrot.cbin
constexpr std::uint32_t kPracticeCourse = 0x021264a0; // ＜オウム＞練習コース
constexpr std::uint32_t kWork5 = 0x021264fc, kWork6 = 0x02126508, kWork7 = 0x02126514,
                        kWork15 = 0x02126520;
constexpr std::uint32_t kPictureLower = 0x02126550, kPictureUpper = 0x02126568,
                        kObjectAnimation = 0x02126580;
constexpr std::uint32_t kAnimObjA = 0x02126164, kAnimObjB = 0x02126170, kAnimCommonA = 0x0212614c,
                        kAnimCommonAFile = 0x02126598, kAnimCommonB = 0x02126158,
                        kAnimCommonBFile = 0x021265ac;
constexpr std::uint32_t kTextWin = 0x021260d0, kTextStage = 0x021260c0;
constexpr std::uint32_t kTutorialPhase = 0x021265c0; // オウムチュートリアル
constexpr std::uint32_t kTutorialQuit = 0x021265d8;  // オウム・ヒゲマフラーやめる
constexpr std::uint32_t kResetSection = 0x021261bc, kSuccessSection = 0x02126220,
                        kFailSection = 0x0212620c;
constexpr std::uint32_t kDead = 0x02126134, kMessageOver = 0x021260ec, kMessageFail = 0x021260f8;
constexpr std::uint32_t kFly = 0x021260c8, kTrail1 = 0x02126140, kTrail2 = 0x021260e0,
                        kPerch = 0x021260d8;
constexpr std::uint32_t kKnotObject = 0x0212617c, kKnotValid = 0x02126188,
                        kKnotInvalid = 0x021261ac;

// Sounds (thunk_020601ec).
constexpr int kSoundRope = 0x2b5, kSoundBounceRope = 0x2b6, kSoundBounceWall = 0x2b7,
              kSoundHop = 0x2b8, kSoundRim = 0x2b9, kSoundReset = 0x2ba, kSoundRefuse = 4;

// The overlay's constants (0x02126280, filled by 02126238).
constexpr int kLineWidth = 0x2000;  // +0x0c: 2.0
constexpr int kTrailSpeed = 0x54a3; // +0x14: speed squared above which the trail shows
constexpr int kRadius = 0x8000;     // +0x28: the parrot's body and head, 8 pixels round
constexpr Vec kHead{0, -0x10000};   // +0x48: the head, 16 pixels above the body
constexpr int kStageCap = 13;       // +0x34

int square(int v) {
    return mul(v, v); // 02120250
}

int lengthSquared(const Vec& v) {
    return square(v[0]) + square(v[1]);
}

// 02005c5c: the square root of a fixed-point value (the hardware's 64-bit
// root of v << 32, scaled back).
int fxSqrt(int v) {
    if (v <= 0) {
        return 0;
    }
    const auto r = std::uint64_t(std::sqrt(double(std::uint64_t(v) << 32)));
    return int((r + 0x200) >> 10);
}

// 02005c90: one over the square root (hypothesis on the rounding).
int fxInvSqrt(int v) {
    if (v <= 0) {
        return 0;
    }
    return int(std::lround(4096.0 / std::sqrt(v / 4096.0)));
}

// 02125154: the circle of radius r round p touches segment a-b.
bool touches(const Vec& p, const Vec& a, const Vec& b, int r) {
    if (a == b) {
        return false;
    }
    const Vec d{b[0] - a[0], b[1] - a[1]};
    int t = div(mul(p[0] - a[0], d[0]) + mul(p[1] - a[1], d[1]), lengthSquared(d));
    t = std::min(std::max(t, 0), kOne);
    const Vec c{a[0] + mul(d[0], t), a[1] + mul(d[1], t)};
    return lengthSquared({p[0] - c[0], p[1] - c[1]}) < square(r);
}

// 02123fdc: segments p1-p2 and p3-p4 (whole pixels) meet, touching or
// overlapping counted.
bool crosses(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4) {
    const int dx34 = x4 - x3, dy34 = y4 - y3, dx12 = x2 - x1, dy12 = y2 - y1;
    const std::int64_t denom = std::int64_t(dx12) * dy34 - std::int64_t(dy12) * dx34;
    if (denom == 0) {
        const std::int64_t c = std::int64_t(dx12) * (y3 - y1) - std::int64_t(dy12) * (x3 - x1);
        if (c != 0) {
            return false;
        }
        // Collinear: they meet unless they lie apart along the line.
        auto dot = [](int ax, int ay, int bx, int by) {
            return std::int64_t(ax) * bx + std::int64_t(ay) * by;
        };
        return dot(x3 - x1, y3 - y1, x3 - x2, y3 - y2) < 1 ||
               dot(x4 - x1, y4 - y1, x4 - x2, y4 - y2) < 1 ||
               dot(x1 - x3, y1 - y3, x1 - x4, y1 - y4) < 1;
    }
    const std::int64_t s = std::int64_t(dx34) * (y1 - y3) - std::int64_t(dy34) * (x1 - x3);
    const std::int64_t t = std::int64_t(dx12) * (y1 - y3) - std::int64_t(dy12) * (x1 - x3);
    if (s * denom < 0 || t * denom < 0) {
        return false;
    }
    return std::abs(s) <= std::abs(denom) && std::abs(t) <= std::abs(denom);
}
} // namespace

// The stage (rc/mode/lt3_mini_parrot, the rows between 4100 {stage} and
// 4101; the loop's record at +0x7bc).
struct ParrotStage {
    int originX = 0, originY = 0, columns = 0, rows = 0, cell = 8, maxRopes = 0; // 4110, 4112
    Vec goalA{}, goalB{};                             // 4111: the goal ledge
    int restitution = kOne;                           // +0x24
    Vec start{};                                      // +0x28: the perch
    int hopX = 0, hopY = 0;                           // +0x30: the hop from a perch, across and up
    int hopFrames = 0;                                // +0x38 (fixed point)
    Vec gravity{};                                    // +0x3c
    int perchFrames = 0;                              // +0x44: the pause on a rope
    std::uint16_t drawing = 0, rope = 0, refused = 0; // 4112: +0x48, +0x4a, +0x4c
    int timeLimit = 0;                                // +0x4e: frames
    int screenGap = 0;                                // +0x50: the space between the screens
    std::vector<std::pair<int, int>> walls;           // 4121: grid points
    std::vector<int> knots;                           // 4131: the pegs' grid points

    // 02123d20: a grid point's place; 0211dbf8: the grid point nearest a
    // place, -1 off the grid.
    Vec place(int point) const {
        const int x = originX + cell * (point % (columns + 1)),
                  y = originY + cell * (point / (columns + 1));
        return {x * kOne, y * kOne};
    }

    int point(const Vec& p) const {
        const int x = (p[0] >> 12) - originX + (cell >> 1),
                  y = (p[1] >> 12) - originY + (cell >> 1);
        if (x < 0 || y < 0) {
            return -1;
        }
        const int ix = x / cell, iy = y / cell;
        if (ix > columns || iy > rows) {
            return -1;
        }
        return (columns + 1) * iy + ix;
    }

    static ParrotStage load(Assets& assets, const std::string& path, int stage);
};

ParrotStage ParrotStage::load(Assets& assets, const std::string& path, int stage) {
    // 0211ee28 runs the file with its own commands (0211def0..0211e7d8).
    ParrotStage s;
    const auto data = assets.script(path);
    bool chosen = false;
    for (const auto& c : data.commands) {
        auto num = [&](std::size_t i) -> double {
            if (i >= c.operands.size()) {
                return 0;
            }
            const auto& o = c.operands[i];
            if (o.type == 1) {
                float f;
                std::memcpy(&f, &o.value, 4);
                return f;
            }
            return double(std::int32_t(o.value));
        };
        auto fixed = [&](double v) {
            return lscr::fx::fromFloat(float(v));
        };
        auto grid = [&](double gx, double gy) {
            return Vec{fixed(float(s.originX) + float(s.cell) * float(gx)),
                       fixed(float(s.originY) + float(s.cell) * float(gy))};
        };
        auto colour = [&](std::size_t i) {
            auto ch = [&](std::size_t k) {
                return std::clamp(int(num(k)) >> 3, 0, 31);
            };
            return std::uint16_t(0x8000 | ch(i) | ch(i + 1) << 5 | ch(i + 2) << 10); // 0211dae0
        };
        switch (c.opcode) {
        case 4100:
            chosen = int(num(0)) == stage;
            break;
        case 4110:
            if (chosen) {
                s.originX = int(num(0));
                s.originY = int(num(1));
                s.columns = int(num(2));
                s.rows = int(num(3));
                s.cell = int(num(4));
            }
            break;
        case 4111:
            if (chosen && c.operands.size() >= 14) {
                s.goalA = grid(num(0), num(1));
                s.goalB = grid(num(2), num(3));
                s.start = {fixed(num(4)), fixed(num(5))};
                s.hopX = fixed(num(6));
                s.hopY = fixed(num(7));
                s.hopFrames = fixed(num(8));
                s.restitution = fixed(num(9));
                s.perchFrames = int(num(10));
                s.gravity = {0, fixed(num(11))};
                s.screenGap = int(num(12));
                s.timeLimit = int(num(13)) * 60;
            }
            break;
        case 4112:
            if (chosen && c.operands.size() >= 10) {
                s.maxRopes = int(num(0));
                s.rope = colour(1);
                s.drawing = colour(4);
                s.refused = colour(7);
            }
            break;
        case 4121:
            if (chosen && c.operands.size() >= 4) {
                s.walls.push_back({s.point(grid(num(0), num(1))), s.point(grid(num(2), num(3)))});
            }
            break;
        case 4131:
            if (chosen && c.operands.size() >= 2) {
                s.knots.push_back(s.point(grid(num(0), num(1))));
            }
            break;
        default:
            break;
        }
    }
    return s;
}

class ParrotGameLoop::Engine final : public lscr::SceneEngine {
public:
    Engine(Story& story, ParrotStage stage, int number)
        : story_(story), stage_(std::move(stage)), number_(number) {
        time_ = stage_.timeLimit;
    }

    bool live = false; // MAIN runs (the loop's state 2)

    void command(lscr::Scene& scene, int hook, lscr::Call& c) override;
    void afterInput(lscr::Scene& scene) override;
    void update(lscr::Scene& scene) override;
    void draw(const lscr::Scene& scene, Surface& screen, lscr::Screen which) const override;

    // Research (the automatic player).
    const ParrotStage& stage() const { return stage_; }

    bool allowed(int a, int b) const {
        const Vec pa = stage_.place(a), pb = stage_.place(b);
        return a != b && !blocked(pa, pb) && !onParrot(pa, pb);
    }

    bool trial(lscr::Scene& scene, const std::vector<std::pair<int, int>>& ropes) const;

private:
    // The loop's flags (+0x9ac).
    enum : unsigned {
        kDrawing = 0x1,   // a rope's end follows the pen
        kMoving = 0x2,    // a whole rope taken (put back when the pen leaves it)
        kPicked = 0x4,    // just taken: lifted unmoved, it goes
        kAnimating = 0x8, // a rope going (or a refused one drawing back)
        kRefused = 0x10,  // the rope as it stands may not be put there
        kFromRope = 0x20, // taken from a rope that was there
        kFlying = 0x40,
        kPerched = 0x80,
        kHopping = 0x200,
        kOver = 0x400, // failed: the result shows, a touch starts over
        kFull = 0x800, // "no more ropes" shows
        kDying = 0x1000,
        kPaused = 0x2000, // 1285 / until 1281
        kDelivered = 0x4000,
        kLocked = 0x8000, // 1287
    };

    struct Rope {
        int a = 0, b = 0;   // grid points, a < b
        int bendAt = 0;     // +8: where it was hit (0..1)
        int bendFrames = 0; // +0x0c: counted down as it is drawn
        Vec impact{};       // +0x10: the parrot's velocity then
    };

    struct Segment {
        Vec a, b;
        int rope; // index into ropes_, -1 a wall
    };

    std::string name(std::uint32_t address) const { return story_.codeName(address, 26).text; }

    void sound(lscr::Scene& scene, int id) const {
        if (scene.host.sound && !silent_) {
            scene.host.sound->playEffect(id);
        }
    }

    void show(lscr::Object* o, bool on) const {
        if (o) {
            o->switches[0].on = on;
        }
    }

    void setNumber(lscr::Object* o, int v) const {
        if (o) {
            o->user[0] = lscr::Value::of(v);
        }
    }

    void play(lscr::Scene&, lscr::Object* o, const std::string& sequence, int flags) const {
        if (o) {
            lscr::play(*o, lscr::sequenceOf(*o, lscr::Value::ofText(sequence)), flags);
        }
    }

    void call(lscr::Scene& scene, std::uint32_t section) const { // 020f1a28
        if (lscr::Thread* t = scene.mainThread()) {
            t->call(name(section), {});
        }
    }

    void showRopesLeft() const { // the "ropes left" number
        setNumber(lineNumber_, stage_.maxRopes - int(ropes_.size()));
    }

    void showTime(int value) const { setNumber(timeNumber_, value); }

    void knotsAll(lscr::Scene& scene, bool valid);
    void knot(lscr::Scene& scene, int point, bool valid);
    std::vector<Segment> segments(bool ropes) const;

    // Rope editing (the pen, 021218c4 / 02121b34 / 02122154).
    int knotNear(const Vec& p) const;
    int ropeAtEnd(const Vec& pen) const;
    int ropeAt(const Vec& pen) const;
    void pick(int rope, int end);
    void add(lscr::Scene& scene, const Vec& a, const Vec& b);
    void remove(lscr::Scene& scene, const Vec& a, const Vec& b);
    void drawBack(lscr::Scene& scene);
    bool blocked(const Vec& a, const Vec& b) const;
    bool onParrot(const Vec& a, const Vec& b) const;
    void penDown(lscr::Scene& scene, int x, int y);
    void penDrag(lscr::Scene& scene, int x, int y);
    void penUp(lscr::Scene& scene);

    // The flight (021245cc and its parts).
    void move(lscr::Scene& scene);
    int step(lscr::Scene& scene);
    bool collide(lscr::Scene& scene, int& code);
    void pushOut(Vec p, const Vec& a, const Vec& b, int k);
    void bounce(int& code, const Vec& p, const Vec& a, const Vec& b, int rope);
    int goal(lscr::Scene& scene);
    void resetParrot();
    void showParrot(lscr::Scene& scene, int code);
    void upperCopies();
    void preview(lscr::Scene& scene);
    void setup(lscr::Scene& scene);
    void layout(lscr::Scene& scene, int x, int y, const std::string& icon,
                const std::string& number, int a, int b, int c, int d);
    void layout3(lscr::Scene& scene, int x, int y, const std::string& icon,
                 const std::string& number, const std::string& second, int a, int b, int c, int d);

    Story& story_;
    ParrotStage stage_;
    int number_ = 0;
    unsigned flags_ = 0;
    std::vector<Rope> ropes_; // +0x96c
    Vec pos_{}, vel_{};       // +0x980, +0x988
    int contacts_ = 0;        // +0x990: frames in a row touching something
    int pause_ = 0;           // +0x994
    int hopLeft_ = 0;         // +0x998
    Vec hopFrom_{};           // +0x99c
    int time_ = 0;            // +0x9a8 (frames)
    int perch_ = -1;          // +0x968: the rope perched on
    Vec dragStart_{}, dragEnd_{}, oldStart_{}, oldEnd_{}; // +0x88c.., +0x89c..
    int pickTimer_ = 0;                                   // +0x8b4
    int messageTimer_ = 0;                                // +0x8b0
    lscr::Tween<2> goingA_, goingB_;                      // +0x8b8 / +0x910: a rope's ends going
    // Objects (+0x83c..+0x864).
    lscr::Object *parrot_ = nullptr, *trail1_ = nullptr, *trail2_ = nullptr, *upper_ = nullptr,
                 *upperTrail1_ = nullptr, *upperTrail2_ = nullptr, *lineNumber_ = nullptr,
                 *timeNumber_ = nullptr, *full_ = nullptr, *failed_ = nullptr;
    std::array<lscr::Object*, 9> dots_{};

    // The overlay's own state (0x021266a0..): the trail ring of eleven places
    // (021267c4), how long it still shows, whether the upper screen's
    // copies show (0x02126280), steps since the last bounce sound, the goal
    // rim's bump.
    struct TrailPlace {
        Vec p{};
        int mirror = 0;
    };

    std::array<TrailPlace, 11> ring_{};
    int ringAt_ = 0, trailFrames_ = 0;
    bool ringFull_ = false, upperShown_ = true, rimBumped_ = false;
    int sinceSound_ = 0;
    bool silent_ = false; // a flight worked out without being shown
};

void ParrotGameLoop::Engine::knot(lscr::Scene& scene, int point, bool valid) {
    // 021227c0: "knot_%03d" shows valid or not.
    char n[32];
    std::snprintf(n, sizeof n, name(kKnotObject).c_str(), point);
    play(scene, scene.find(n), name(valid ? kKnotValid : kKnotInvalid), 0);
}

void ParrotGameLoop::Engine::knotsAll(lscr::Scene& scene, bool valid) {
    for (auto it = stage_.knots.rbegin(); it != stage_.knots.rend(); ++it) {
        knot(scene, *it, valid);
    }
}

std::vector<ParrotGameLoop::Engine::Segment> ParrotGameLoop::Engine::segments(bool ropes) const {
    std::vector<Segment> out;
    if (ropes) {
        for (std::size_t i = 0; i < ropes_.size(); ++i) {
            out.push_back({stage_.place(ropes_[i].a), stage_.place(ropes_[i].b), int(i)});
        }
    } else {
        for (const auto& [a, b] : stage_.walls) {
            out.push_back({stage_.place(a), stage_.place(b), -1});
        }
    }
    return out;
}

int ParrotGameLoop::Engine::knotNear(const Vec& p) const {
    // 02123c24: the peg nearest the pen within 12 pixels.
    float best = 12.0f * 12.0f;
    int found = -1;
    for (auto it = stage_.knots.rbegin(); it != stage_.knots.rend(); ++it) {
        const Vec k = stage_.place(*it);
        const float d = float(square(p[0] - k[0]) + square(p[1] - k[1])) / 4096.0f;
        if (d < best) {
            best = d;
            found = *it;
        }
    }
    return found;
}

int ParrotGameLoop::Engine::ropeAtEnd(const Vec& pen) const {
    // 02123350: a rope with an end at the grid point under the pen - for a
    // one-cell rope only when the pen is near that end, not its middle.
    const Vec v = stage_.place(stage_.point(pen));
    const int cell2 = stage_.cell * stage_.cell;
    for (int i = int(ropes_.size()) - 1; i >= 0; --i) {
        const Vec a = stage_.place(ropes_[size_t(i)].a), b = stage_.place(ropes_[size_t(i)].b);
        if (v != a && v != b) {
            continue;
        }
        const int len2 = (square(a[0] - b[0]) + square(a[1] - b[1])) >> 12;
        auto nearEnd = [&](int axis) {
            const int lo = std::min(a[size_t(axis)], b[size_t(axis)]),
                      hi = std::max(a[size_t(axis)], b[size_t(axis)]);
            return pen[size_t(axis)] < lo + kOne || hi - kOne < pen[size_t(axis)];
        };
        if (len2 == cell2) {
            if (nearEnd(a[0] == b[0] ? 1 : 0)) {
                return i;
            }
        } else if (len2 != 2 * cell2 || (nearEnd(0) && nearEnd(1))) {
            return i;
        }
    }
    return -1;
}

int ParrotGameLoop::Engine::ropeAt(const Vec& pen) const {
    // 02123620: a rope passing within half a cell of the pen.
    const int r = lscr::fx::fromFloat(float(stage_.cell) / 1.99f);
    for (int i = int(ropes_.size()) - 1; i >= 0; --i) {
        if (touches(pen, stage_.place(ropes_[size_t(i)].a), stage_.place(ropes_[size_t(i)].b), r)) {
            return i;
        }
    }
    return -1;
}

void ParrotGameLoop::Engine::pick(int rope, int end) {
    // 02123890: the rope comes off; its end `end` stays, the other follows.
    const Rope r = ropes_[size_t(rope)];
    const int ends[2] = {r.a, r.b};
    dragStart_ = stage_.place(ends[end]);
    dragEnd_ = stage_.place(ends[1 - end]);
    oldStart_ = dragStart_;
    oldEnd_ = dragEnd_;
    ropes_.erase(ropes_.begin() + rope);
    pickTimer_ = 10;
}

void ParrotGameLoop::Engine::add(lscr::Scene&, const Vec& a, const Vec& b) {
    // 0212376c: a rope between the two grid points (the smaller first). Its
    // sound comes from whatever its callers leave on the stack - none here.
    const int pa = stage_.point(a), pb = stage_.point(b);
    ropes_.push_back({std::min(pa, pb), std::max(pa, pb)});
    showRopesLeft();
}

void ParrotGameLoop::Engine::remove(lscr::Scene& scene, const Vec& a, const Vec& b) {
    // 02123954: a rope taken away - both ends draw into its middle (7
    // frames).
    const Vec mid{div(a[0] + b[0], 0x2000), div(a[1] + b[1], 0x2000)};
    goingA_.set(a);
    goingA_.to(2, mid, 0x7000);
    goingB_.set(b);
    goingB_.to(2, mid, 0x7000);
    flags_ |= kAnimating;
    sound(scene, kSoundRefuse);
    showRopesLeft();
}

void ParrotGameLoop::Engine::drawBack(lscr::Scene& scene) {
    // 02124390: a refused new rope draws back into its first peg.
    const int frames = dragStart_ == dragEnd_ ? 0 : 0x7000;
    goingA_.set(dragStart_);
    goingA_.to(2, dragStart_, frames);
    goingB_.set(dragEnd_);
    goingB_.to(2, dragStart_, frames);
    dragStart_ = {0, 0};
    flags_ |= kAnimating;
    sound(scene, kSoundRefuse);
}

bool ParrotGameLoop::Engine::blocked(const Vec& a, const Vec& b) const {
    // 02123eb8: the line a-b (whole pixels) meets a rope, a wall, a peg
    // or the goal.
    const int x1 = a[0] >> 12, y1 = a[1] >> 12, x2 = b[0] >> 12, y2 = b[1] >> 12;
    for (bool ropes : {true, false}) {
        for (const auto& s : segments(ropes)) {
            if (crosses(x1, y1, x2, y2, s.a[0] >> 12, s.a[1] >> 12, s.b[0] >> 12, s.b[1] >> 12)) {
                return true;
            }
        }
    }
    // 02124250: a peg strictly inside it.
    const int left = std::min(x1, x2), right = std::max(x1, x2), top = std::min(y1, y2),
              bottom = std::max(y1, y2);
    for (auto it = stage_.knots.rbegin(); it != stage_.knots.rend(); ++it) {
        const Vec k = stage_.place(*it);
        const int kx = k[0] >> 12, ky = k[1] >> 12;
        if (x1 == x2) {
            if (kx == x1 && top < ky && ky < bottom) {
                return true;
            }
        } else if (y1 == y2) {
            if (ky == y1 && left < kx && kx < right) {
                return true;
            }
        } else if (left < kx && kx < right && top < ky && ky < bottom &&
                   (x2 - x1) * (ky - y1) - (kx - x1) * (y2 - y1) == 0) {
            return true;
        }
    }
    return crosses(x1, y1, x2, y2, stage_.goalA[0] >> 12, stage_.goalA[1] >> 12,
                   stage_.goalB[0] >> 12, stage_.goalB[1] >> 12);
}

bool ParrotGameLoop::Engine::onParrot(const Vec& a, const Vec& b) const {
    return touches(pos_, a, b, kRadius) ||
           touches({pos_[0] + kHead[0], pos_[1] + kHead[1]}, a, b, kRadius);
}

void ParrotGameLoop::Engine::penDown(lscr::Scene& scene, int x, int y) {
    // 021218c4.
    const Vec pen{x * kOne, y * kOne};
    const int k = knotNear(pen);
    const Vec kp = k >= 0 ? stage_.place(k) : Vec{};
    if (const int r = ropeAtEnd(pen); r >= 0) {
        pick(r, stage_.place(ropes_[size_t(r)].a) == kp ? 1 : 0);
        flags_ |= kDrawing | kPicked | kFromRope;
        return;
    }
    if (const int r = ropeAt(pen); r >= 0) {
        pick(r, 0);
        flags_ |= kMoving | kPicked | kFromRope;
        return;
    }
    if (k < 0) {
        return;
    }
    if (int(ropes_.size()) < stage_.maxRopes) {
        dragStart_ = kp;
        dragEnd_ = kp;
        sound(scene, kSoundRope);
        flags_ |= kDrawing | kRefused;
        return;
    }
    // All ropes are out: the sign for 30 frames.
    flags_ |= kFull;
    show(full_, true);
    messageTimer_ = 30;
    sound(scene, kSoundRefuse);
}

void ParrotGameLoop::Engine::penDrag(lscr::Scene& scene, int x, int y) {
    // 02121b34.
    const Vec pen{x * kOne, y * kOne};
    if (flags_ & kMoving) {
        // A whole rope cannot be moved here: off it, the pen puts it back.
        if (!touches(pen, dragStart_, dragEnd_, lscr::fx::fromFloat(float(stage_.cell) / 1.99f))) {
            add(scene, dragStart_, dragEnd_);
            flags_ &= ~(kMoving | kPicked | kRefused | kFromRope);
        }
        return;
    }
    if (!(flags_ & kDrawing)) {
        return;
    }
    const Vec before = dragEnd_;
    // 02123dd4: the end snaps to a peg within 12 pixels.
    const int k = knotNear(pen);
    dragEnd_ = k >= 0 ? stage_.place(k) : pen;
    const int end = stage_.point(dragEnd_);
    bool ok = !blocked(dragStart_, dragEnd_) && !onParrot(dragStart_, dragEnd_) && end >= 0 &&
              dragStart_ != dragEnd_;
    if (ok) {
        ok = std::find(stage_.knots.begin(), stage_.knots.end(), end) != stage_.knots.end();
    }
    flags_ = ok ? flags_ & ~kRefused : flags_ | kRefused;
    if (flags_ & kPicked) {
        if (dragEnd_ == before && pickTimer_ > 0) {
            return;
        }
        sound(scene, kSoundRope);
        flags_ &= ~kPicked;
    }
}

void ParrotGameLoop::Engine::penUp(lscr::Scene& scene) {
    // 02122154.
    if (flags_ & kPicked) {
        remove(scene, dragStart_, dragEnd_);
        pickTimer_ = 0;
        flags_ &= ~(kDrawing | kMoving | kPicked | kFromRope);
        return;
    }
    if (flags_ & kMoving) {
        if (!(flags_ & kRefused)) {
            add(scene, dragStart_, dragEnd_);
        } else {
            add(scene, oldStart_, oldEnd_);
        }
        flags_ &= ~(kMoving | kRefused | kFromRope);
        return;
    }
    if (flags_ & kDrawing) {
        if (!(flags_ & kRefused)) {
            add(scene, dragStart_, dragEnd_);
        } else if (!(flags_ & kFromRope)) {
            drawBack(scene);
        } else {
            add(scene, oldStart_, oldEnd_);
        }
        flags_ &= ~(kDrawing | kRefused | kFromRope);
    }
}

void ParrotGameLoop::Engine::move(lscr::Scene& scene) {
    // 021246d0: hopping - a parabola from where it sat, `hopX` across and
    // `hopY` up over `hopFrames` (the way it faces) - else free flight.
    if ((flags_ & kHopping) && !(flags_ & kDying)) {
        hopLeft_ -= kOne;
        if (hopLeft_ == stage_.hopFrames - 0x4000 && (flags_ & kFlying)) {
            sound(scene, kSoundHop);
        }
        if (hopLeft_ < 1) {
            flags_ &= ~kHopping;
        }
        const Vec before = pos_;
        const int a = stage_.hopX, b = stage_.hopY, total = stage_.hopFrames;
        const int across = mul(div(a, square(total)), square(total - hopLeft_));
        const int up = mul(div(b, square(a)), square(across - a)) - b;
        const Vec d{vel_[0] < 0 ? -across : across, up};
        pos_ = {hopFrom_[0] + d[0], hopFrom_[1] + d[1]};
        vel_ = {pos_[0] - before[0], pos_[1] - before[1]};
        return;
    }
    vel_[0] += stage_.gravity[0];
    vel_[1] += stage_.gravity[1];
    pos_[0] += vel_[0];
    pos_[1] += vel_[1];
}

void ParrotGameLoop::Engine::pushOut(Vec p, const Vec& a, const Vec& b, int k) {
    // 02125320: back along the velocity in halving steps until the circle at
    // p only just touches a-b (k: the divisor of the next step; negative
    // backs out).
    const Vec d{div(vel_[0], k), div(vel_[1], k)};
    if (lengthSquared(d) < 2) {
        return;
    }
    pos_[0] += d[0];
    pos_[1] += d[1];
    const Vec q{p[0] + d[0], p[1] + d[1]};
    const int next = std::abs(k) * 2;
    pushOut(q, a, b, touches(q, a, b, kRadius) ? -next : next);
}

void ParrotGameLoop::Engine::bounce(int& code, const Vec& p, const Vec& a, const Vec& b, int rope) {
    // 02125560: off segment a-b at the parrot's point p.
    const bool slanted = a[0] != b[0], level = a[1] == b[1];
    const Vec ab{b[0] - a[0], b[1] - a[1]};
    const int t = div(mul(p[0] - a[0], ab[0]) + mul(p[1] - a[1], ab[1]), lengthSquared(ab));
    const bool within = t >= 0 && t <= kOne;
    auto pluck = [&](int at) {
        if (rope >= 0) {
            Rope& r = ropes_[size_t(rope)];
            r.bendAt = at;
            r.bendFrames = 10;
            r.impact = vel_;
        }
    };
    if (p == pos_ && level && p[1] < a[1] && within) {
        // Down on top of a level rope (or wall): it perches.
        pluck(t);
        perch_ = rope;
        vel_ = {lscr::fx::fromFloat(vel_[0] < 0 ? -0.001f : 0.001f), 0};
        code = 1;
        return;
    }
    Vec n;
    if (!within || (slanted && !level)) {
        const int tc = std::clamp(t, 0, kOne);
        n = {p[0] - (a[0] + mul(ab[0], tc)), p[1] - (a[1] + mul(ab[1], tc))};
    } else if (slanted) {
        n = a[1] < p[1] ? Vec{0, kOne} : Vec{0, -kOne};
    } else {
        n = a[0] < p[0] ? Vec{kOne, 0} : Vec{-kOne, 0};
    }
    if (0 < t && t < kOne) {
        pluck(t);
    }
    // The velocity mirrored in the normal...
    const int s = mul(n[0], -vel_[0]) + mul(n[1], -vel_[1]);
    const int inv = div(kOne, lengthSquared(n));
    vel_[0] += mul(mul(mul(n[0], s), inv), 0x2000);
    vel_[1] += mul(mul(mul(n[1], s), inv), 0x2000);
    // ...less what the rope does not give back.
    const int k = fxInvSqrt(lengthSquared(n));
    const Vec unit{mul(n[0], k), mul(n[1], k)};
    const int along = mul(vel_[0], unit[0]) + mul(vel_[1], unit[1]);
    const Vec normal{mul(unit[0], along), mul(unit[1], along)};
    const int keep = kOne - stage_.restitution;
    vel_[0] -= mul(normal[0], keep);
    vel_[1] -= mul(normal[1], keep);
    if ((slanted || !within) && -0x7a < vel_[1] && vel_[1] < 0 && lengthSquared(normal) < 8) {
        code = 5; // it rests there: stuck
        return;
    }
    code = std::abs(a[1] - b[1]) < std::abs(a[0] - b[0]) ? 2 : 3;
}

bool ParrotGameLoop::Engine::collide(lscr::Scene& scene, int& code) {
    // 021248b8: the body and the head against every rope, then every wall;
    // each touch is backed out of, the last one bounces.
    ++sinceSound_;
    bool touched = false;
    Segment hit{};
    Vec at{};
    bool ropeList = false;
    for (bool ropes : {true, false}) {
        const auto list = segments(ropes);
        for (auto s = list.rbegin(); s != list.rend(); ++s) {
            if (touches(pos_, s->a, s->b, kRadius)) {
                pushOut(pos_, s->a, s->b, -0x2000);
                hit = *s;
                at = pos_;
                touched = true;
                ropeList = ropes;
            }
            const Vec head{pos_[0] + kHead[0], pos_[1] + kHead[1]};
            if (touches(head, s->a, s->b, kRadius)) {
                pushOut(head, s->a, s->b, -0x2000);
                hit = *s;
                at = {pos_[0] + kHead[0], pos_[1] + kHead[1]};
                touched = true;
                ropeList = ropes;
            }
        }
    }
    if (touched) {
        bounce(code, at, hit.a, hit.b, ropeList ? hit.rope : -1);
        if (sinceSound_ > 7) {
            const bool level = hit.a[1] == hit.b[1];
            const int lo = std::min(hit.a[0], hit.b[0]), hi = std::max(hit.a[0], hit.b[0]);
            if (level && lo <= pos_[0] && pos_[0] <= hi && pos_[1] < hit.a[1]) {
                sound(scene, ropeList ? kSoundBounceRope : kSoundBounceWall);
            } else {
                sound(scene, kSoundRim);
            }
            sinceSound_ = 0;
        }
    }
    contacts_ = touched ? contacts_ + 1 : 0;
    if (contacts_ > 10) {
        code = 5;
    }
    return touched;
}

int ParrotGameLoop::Engine::goal(lscr::Scene& scene) {
    // 02124cdc: coming down onto the goal ledge.
    if (vel_[1] < 1) {
        return 0;
    }
    const Vec a = stage_.goalA, d{stage_.goalB[0] - a[0], stage_.goalB[1] - a[1]};
    auto along = [&](const Vec& p) {
        return div(mul(p[0] - a[0], d[0]) + mul(p[1] - a[1], d[1]), lengthSquared(d));
    };
    const int tc = std::clamp(along(pos_), 0, kOne);
    const Vec c{a[0] + mul(d[0], tc), a[1] + mul(d[1], tc)};
    if (square(pos_[0] - c[0]) + square(pos_[1] - c[1]) > square(kRadius)) {
        rimBumped_ = false;
        return 0;
    }
    const Vec saved = pos_;
    pushOut(pos_, a, stage_.goalB, -kOne);
    const bool fromBelow = c[1] < pos_[1] - vel_[1] + kRadius;
    const int t = along(pos_);
    pos_ = saved;
    const bool wrongSide = vel_[0] > 0 ? t < 0x800 : vel_[0] < 0;
    if (wrongSide && fromBelow) {
        return 0;
    }
    if (-0xcc < t && t < 0x10cc) {
        if (!fromBelow) {
            pushOut(pos_, a, stage_.goalB, -kOne);
            sound(scene, kSoundBounceWall);
            return 6;
        }
        return 0;
    }
    if (!rimBumped_) {
        // Its end: one bump.
        rimBumped_ = true;
        pushOut(pos_, a, stage_.goalB, -kOne);
        int ignored = 0;
        bounce(ignored, pos_, a, stage_.goalB, -1);
        sound(scene, kSoundRim);
    }
    return 0;
}

int ParrotGameLoop::Engine::step(lscr::Scene& scene) {
    // 021245cc: one frame of flight - 1 perched, 2 / 3 bounced, 4 gone off
    // the screens, 5 stuck, 6 delivered.
    move(scene);
    int code = 0;
    if (collide(scene, code)) {
        flags_ &= ~kHopping;
        hopLeft_ = 0;
        return code;
    }
    if (pos_[1] > 4 * kRadius + 192 * kOne || pos_[0] > 2 * kRadius + 256 * kOne ||
        pos_[0] < -2 * kRadius) {
        return 4;
    }
    return goal(scene);
}

void ParrotGameLoop::Engine::resetParrot() {
    pos_ = stage_.start;
    vel_ = {0, 0};
}

void ParrotGameLoop::Engine::upperCopies() {
    // 02122dd4: the upper screen's copies show while the parrot is near the
    // top of the lower screen or above it.
    const bool high = !((pos_[1] >> 12) > 23);
    if (high != upperShown_) {
        show(upper_, high);
        show(upperTrail1_, high);
        show(upperTrail2_, high);
        upperShown_ = high;
    }
}

void ParrotGameLoop::Engine::showParrot(lscr::Scene& scene, int code) {
    // 02122834: the parrot's sprites, on the lower screen and copied onto the
    // upper one (the upper screen 192 pixels plus the gap above), and its
    // trail when it flies fast.
    if (!parrot_ || !trail1_ || !trail2_ || !upper_ || !upperTrail1_ || !upperTrail2_) {
        return;
    }
    const Vec up{0, (192 + stage_.screenGap) * kOne};
    if (code != 7) {
        parrot_->position.set(pos_);
        upper_->position.set({pos_[0] + up[0], pos_[1] + up[1]});
    }
    const int mirror = vel_[0] < 0 ? 1 : 0;
    parrot_->mirror = upper_->mirror = mirror;
    if (code == 1) {
        play(scene, parrot_, name(kPerch), 4);
        play(scene, upper_, name(kPerch), 4);
    } else if (code == 0 || code == 2 || code == 3) {
        if (lengthSquared(vel_) > kTrailSpeed) {
            if (trailFrames_ == 0) {
                for (auto* o : {parrot_, upper_}) {
                    play(scene, o, name(kFly), 0);
                }
                play(scene, trail1_, name(kTrail1), 0);
                play(scene, upperTrail1_, name(kTrail1), 0);
                play(scene, trail2_, name(kTrail2), 0);
                play(scene, upperTrail2_, name(kTrail2), 0);
                ring_ = {};
                ringAt_ = 0;
                ringFull_ = false;
            }
            trailFrames_ = std::min(trailFrames_ + 2, 11);
        } else {
            play(scene, parrot_, name(kFly), 4);
            play(scene, upper_, name(kFly), 4);
        }
    }
    if (!ringFull_ && ringAt_ == 1) {
        for (auto* o : {trail1_, trail2_, upperTrail1_, upperTrail2_}) {
            show(o, true);
        }
    }
    trailFrames_ = std::max(trailFrames_ - 1, 0);
    if (trailFrames_ == 0) {
        for (auto* o : {trail1_, trail2_, upperTrail1_, upperTrail2_}) {
            show(o, false);
        }
        return;
    }
    ringAt_ = (ringAt_ + 1) % 11;
    ring_[size_t(ringAt_)] = {pos_, mirror};
    if (ringAt_ == 0) {
        ringFull_ = true;
    }
    const int k1 = trailFrames_ < 9 ? trailFrames_ >> 1 : 5, k2 = trailFrames_ < 9 ? k1 << 1 : 10;
    const int i1 = (ringFull_ || ringAt_ - k1 > 0) ? (ringAt_ - k1 + 11) % 11 : 1;
    const int i2 = (ringFull_ || ringAt_ - k2 > 0) ? (ringAt_ - k2 + 11) % 11 : 1;
    trail1_->position.set(ring_[size_t(i1)].p);
    trail2_->position.set(ring_[size_t(i2)].p);
    upperTrail1_->position.set({ring_[size_t(i1)].p[0] + up[0], ring_[size_t(i1)].p[1] + up[1]});
    upperTrail2_->position.set({ring_[size_t(i2)].p[0] + up[0], ring_[size_t(i2)].p[1] + up[1]});
    trail1_->mirror = upperTrail1_->mirror = ring_[size_t(i1)].mirror;
    trail2_->mirror = upperTrail2_->mirror = ring_[size_t(i2)].mirror;
}

void ParrotGameLoop::Engine::preview(lscr::Scene& scene) {
    // 021231b8: the first flight's path as nine dots, one every ten pixels
    // flown (the first two left out); without its sounds (hypothesis).
    silent_ = true;
    resetParrot();
    flags_ |= kHopping;
    hopLeft_ = stage_.hopFrames;
    hopFrom_ = pos_;
    Vec last = pos_;
    for (int dot = -2; dot < 9; ++dot) {
        int flown = 0;
        for (int guard = 0; flown < 0xa001 && guard < 4000; ++guard) {
            step(scene);
            flown += fxSqrt(square(pos_[0] - last[0]) + square(pos_[1] - last[1]));
            last = pos_;
        }
        if (dot >= 0 && dots_[size_t(dot)]) {
            dots_[size_t(dot)]->position.set(pos_);
        }
    }
    flags_ &= ~kHopping;
    resetParrot();
    silent_ = false;
}

bool ParrotGameLoop::Engine::trial(lscr::Scene& scene,
                                   const std::vector<std::pair<int, int>>& ropes) const {
    // Research: the flight with these ropes, in a silent copy - as update()
    // flies it, without the sprites.
    Engine e(*this);
    e.silent_ = true;
    e.ropes_.clear();
    e.resetParrot();
    for (const auto& [a, b] : ropes) {
        // Drawn in turn: from a peg no rope ends at, past nothing.
        const bool aTaken = std::any_of(e.ropes_.begin(), e.ropes_.end(),
                                        [a](const Rope& r) { return r.a == a || r.b == a; });
        if (aTaken || int(e.ropes_.size()) >= stage_.maxRopes || !e.allowed(a, b)) {
            return false;
        }
        e.ropes_.push_back({std::min(a, b), std::max(a, b)});
    }
    e.flags_ = kFlying | kHopping;
    e.hopLeft_ = stage_.hopFrames;
    e.hopFrom_ = e.pos_;
    e.contacts_ = e.pause_ = 0;
    for (int time = stage_.timeLimit; time > 0; --time) {
        if (e.flags_ & kPerched) {
            if (--e.pause_ < 1) {
                e.flags_ = (e.flags_ & ~kPerched) | kHopping;
                e.hopLeft_ = stage_.hopFrames;
                e.hopFrom_ = e.pos_;
            }
            continue;
        }
        const int code = e.step(scene);
        if (code == 1) {
            e.flags_ |= kPerched;
            e.pause_ = stage_.perchFrames;
        } else if (code == 4 || code == 5) {
            return false;
        } else if (code == 6) {
            return true;
        }
    }
    return false;
}

void ParrotGameLoop::Engine::layout(lscr::Scene& scene, int x, int y, const std::string& icon,
                                    const std::string& number, int a, int b, int c, int d) {
    // 02122ed8: an icon and its number centred on x.
    lscr::Object* i = scene.find(icon);
    lscr::Object* n = scene.find(number);
    if (!i || !n) {
        return;
    }
    const auto box = lscr::touchBox(*i);
    const int total = a + c + ((box[2] - box[0]) - b);
    const int left = (x - (total >> 1)) * kOne;
    i->position.set({left, y * kOne});
    n->position.set({left + total * kOne, (y - d) * kOne});
}

void ParrotGameLoop::Engine::layout3(lscr::Scene& scene, int x, int y, const std::string& icon,
                                     const std::string& number, const std::string& second, int a,
                                     int b, int c, int d) {
    // 02122ffc: an icon, a number and a second sign centred on x.
    lscr::Object* i = scene.find(icon);
    lscr::Object* n = scene.find(number);
    lscr::Object* s = scene.find(second);
    if (!i || !n || !s) {
        return;
    }
    const auto bi = lscr::touchBox(*i), bs = lscr::touchBox(*s);
    const int wi = bi[2] - bi[0], ws = bs[2] - bs[0];
    const int left = (x - int(unsigned((c * 2 - b) + wi + a + ws) >> 1)) * kOne;
    const int mid = left + (a + c + (wi - b)) * kOne;
    i->position.set({left, y * kOne});
    n->position.set({mid, (y - d) * kOne});
    s->position.set({mid + c * kOne, y * kOne});
}

void ParrotGameLoop::Engine::setup(lscr::Scene& scene) {
    // 1280 (02120c90): the game's objects, the pegs, the numbers, the
    // preview of the first flight.
    flags_ |= kPaused;
    auto find = [&](const char* n) {
        return scene.find(n);
    };
    parrot_ = find("bound_obj_a");
    trail1_ = find("bound_obj_a_z1");
    trail2_ = find("bound_obj_a_z2");
    upper_ = find("bound_obj_b");
    upperTrail1_ = find("bound_obj_b_z1");
    upperTrail2_ = find("bound_obj_b_z2");
    lineNumber_ = find("line_num");
    timeNumber_ = find("time_num");
    setNumber(lineNumber_, stage_.maxRopes);
    showTime(stage_.timeLimit / 60);
    full_ = find("msg_max");
    failed_ = find("msg_fail");
    for (int i = 0; i < 9; ++i) {
        char n[16];
        std::snprintf(n, sizeof n, "pnt_%02d", i);
        dots_[size_t(i)] = scene.find(n);
        show(dots_[size_t(i)], false);
    }
    for (auto it = stage_.knots.rbegin(); it != stage_.knots.rend(); ++it) {
        char n[32];
        std::snprintf(n, sizeof n, name(kKnotObject).c_str(), *it);
        if (lscr::Object* o = scene.create(lscr::Object::Kind::Sprite, n, "anm_cmn_a")) {
            play(scene, o, name(kKnotValid), 0);
            o->position.set(stage_.place(*it));
        }
    }
    layout(scene, 0x80, 0x43, "stage_i", "stage_n", 0xc, 0, 2, 0);
    layout(scene, 0x80, 0x5e, "line_rst_i", "line_rst_n", 0xc, 4, 2, 0);
    layout(scene, 0x24, 0xc4, "line_rst", "line_num", 0xe, 0, 2, 1);
    layout3(scene, 0x80, 0x71, "time_rst_i", "time_rst_n", "time_rst_is", 0xc, 4, 2, 0);
    layout3(scene, 0x35, 0xc4, "time_rst", "time_num", "time_rst_s", 0xe, 0, 2, 1);
    setNumber(scene.find("stage_n"), number_ > 12 ? 0 : number_);
    setNumber(scene.find("line_rst_n"), stage_.maxRopes);
    setNumber(scene.find("time_rst_n"), stage_.timeLimit / 60);
    preview(scene);
}

void ParrotGameLoop::Engine::command(lscr::Scene& scene, int hook, lscr::Call& c) {
    switch (hook) {
    case 0:
        setup(scene);
        break;
    case 1: // 1281 (021211b0): ready - the parrot on its perch, the dots, the pegs
        flags_ &= ~kPaused;
        resetParrot();
        if (parrot_ && upper_) {
            parrot_->position.set(pos_);
            upper_->position.set({pos_[0], pos_[1] + (192 + stage_.screenGap) * kOne});
        }
        showRopesLeft();
        for (auto* d : dots_) {
            show(d, true);
        }
        knotsAll(scene, true);
        break;
    case 2: // 1282 (021213b0): all ropes away, everything afresh
        dragStart_ = dragEnd_ = {0, 0};
        messageTimer_ = pickTimer_ = 0;
        flags_ = 0;
        goingA_.set({0, 0});
        goingB_.set({0, 0});
        pos_ = vel_ = hopFrom_ = {0, 0};
        contacts_ = pause_ = hopLeft_ = 0;
        ropes_.clear();
        time_ = stage_.timeLimit;
        show(full_, false);
        setNumber(lineNumber_, stage_.maxRopes);
        break;
    case 3: // 1283 (02121570): fly - the pegs used light up
        resetParrot();
        contacts_ = pause_ = 0;
        time_ = stage_.timeLimit;
        knotsAll(scene, false);
        for (auto it = ropes_.rbegin(); it != ropes_.rend(); ++it) {
            knot(scene, it->a, true);
            knot(scene, it->b, true);
        }
        for (auto* d : dots_) {
            show(d, false);
        }
        flags_ = (flags_ & ~kLocked) | kFlying | kHopping;
        hopLeft_ = stage_.hopFrames;
        hopFrom_ = pos_;
        break;
    case 4: // 1284 (021216dc): stop the flight
        if (flags_ & kFlying) {
            flags_ &= ~(kFlying | kHopping);
            resetParrot();
            contacts_ = pause_ = hopLeft_ = 0;
            hopFrom_ = {0, 0};
            time_ = stage_.timeLimit;
            call(scene, kResetSection);
        }
        knotsAll(scene, true);
        for (auto* o : {trail1_, trail2_, upperTrail1_, upperTrail2_}) {
            show(o, false);
        }
        break;
    case 5: // 1285 (0212180c): pause {on}
        if (c.count() > 0) {
            flags_ = c.integer(0) ? flags_ | kPaused : flags_ & ~kPaused;
        }
        break;
    case 6: // 1286 (02121844): the time shown afresh
        showTime(stage_.timeLimit / 60);
        break;
    case 7: // 1287 (021218b0)
        flags_ |= kLocked;
        break;
    default:
        break;
    }
}

void ParrotGameLoop::Engine::afterInput(lscr::Scene& scene) {
    // 0211f60c (vtable 0xe4): the pen.
    if (!live || (flags_ & kPaused)) {
        return;
    }
    const auto& in = scene.input();
    if (flags_ & kOver) {
        if (messageTimer_ > 0 || !in.pressed) {
            return;
        }
        flags_ &= ~(kOver | kDying);
        sound(scene, kSoundReset);
        call(scene, kResetSection);
        return;
    }
    if (flags_ & kFull) {
        if (messageTimer_ > 0 || !in.pressed) {
            return;
        }
        show(full_, false);
        flags_ &= ~kFull;
        return;
    }
    if ((flags_ & kAnimating) && !goingA_.active() && !goingB_.active()) {
        flags_ &= ~kAnimating;
        goingA_.set({0, 0});
        goingB_.set({0, 0});
    }
    if (flags_ & (kFlying | kLocked | kOver | kDelivered)) {
        return;
    }
    if (in.pressed) {
        penDown(scene, in.x, in.y);
    } else if (in.held) {
        penDrag(scene, in.x, in.y); // the raw pen, as for a press
    } else if (in.released) {
        penUp(scene);
    }
}

void ParrotGameLoop::Engine::update(lscr::Scene& scene) {
    // 0211fa28 (vtable 0xec): one frame.
    if (!live || (flags_ & kPaused)) {
        return;
    }
    upperCopies();
    if (flags_ & kAnimating) {
        goingA_.step(kOne);
        goingB_.step(kOne);
        return;
    }
    if (flags_ & kPicked) {
        pickTimer_ = std::max(0, pickTimer_ - 1);
        return;
    }
    if (flags_ & kFull) {
        messageTimer_ = std::max(0, messageTimer_ - 1);
        return;
    }
    if (flags_ & kPerched) {
        pause_ = std::max(0, pause_ - 1);
        if (pause_ < 1) {
            flags_ &= ~kPerched;
            flags_ |= kHopping;
            hopLeft_ = stage_.hopFrames;
            hopFrom_ = pos_;
        }
        // It sits on the rope as the rope still swings.
        int bob = 0;
        if (perch_ >= 0 && perch_ < int(ropes_.size())) {
            const Rope& r = ropes_[size_t(perch_)];
            const int f = r.bendFrames * kOne;
            const int amp = std::min(fxSqrt(lengthSquared(r.impact)), 0x3000);
            bob = mul(div(mul(-f, f - 10 * kOne), 25 * kOne), amp);
        }
        if (parrot_ && upper_) {
            parrot_->position.set({pos_[0], pos_[1] + bob});
            upper_->position.set({pos_[0], pos_[1] + (192 + stage_.screenGap) * kOne + bob});
        }
        showParrot(scene, 7);
        time_ = std::max(0, time_ - 1);
        showTime((time_ + 59) / 60);
        return;
    }
    if (flags_ & kOver) {
        messageTimer_ = std::max(0, messageTimer_ - 1);
        if (flags_ & kDying) {
            pause_ = std::max(0, pause_ - 1);
            if (pause_ == 0) {
                move(scene);
                if (pos_[1] > 192 * kOne + 4 * kRadius) {
                    flags_ &= ~kDying;
                }
            }
        }
        showParrot(scene, 8);
        return;
    }
    if (flags_ & kDelivered) {
        showParrot(scene, 8);
        return;
    }
    if (!(flags_ & kFlying)) {
        return;
    }
    const int code = step(scene);
    showParrot(scene, code);
    time_ = std::max(0, time_ - 1);
    showTime((time_ + 59) / 60);
    if (time_ < 1 || code == 5) {
        // It gives up: up and away.
        flags_ |= kDying;
        pause_ = 30;
        vel_ = {0, -0x1800};
        play(scene, parrot_, name(kDead), 0);
        play(scene, upper_, name(kDead), 0);
    }
    if (code == 1) {
        flags_ |= kPerched;
        pause_ = stage_.perchFrames;
    }
    if (code == 4 || (flags_ & kDying)) {
        messageTimer_ = 30;
        flags_ &= ~(kFlying | kPerched | kHopping);
        flags_ |= kOver;
        play(scene, failed_, name(time_ < 1 ? kMessageOver : kMessageFail), 0);
        call(scene, kFailSection);
    } else if (code == 6) {
        flags_ &= ~(kFlying | kHopping);
        flags_ |= kDelivered;
        call(scene, kSuccessSection);
    }
}

void ParrotGameLoop::Engine::draw(const lscr::Scene&, Surface& screen, lscr::Screen which) const {
    // 0212098c (vtable 0xf8): the ropes (a rope just hit swings, bent at
    // where it was hit), the rope being drawn and a rope going.
    if (which != lscr::Screen::Lower) {
        return;
    }
    auto line = [&](const Vec& a, const Vec& b, std::uint16_t colour) {
        drawThickLine(screen, a, b, kLineWidth, colour);
    };
    auto end = [&](const Vec& p, std::uint16_t colour) { // 0211de10: a 4 x 4 square
        drawBox(screen, p[0] - 0x2000, p[1] - 0x2000, p[0] + 0x2000, p[1] + 0x2000, colour);
    };
    for (std::size_t i = 0; i < ropes_.size(); ++i) {
        Rope& r = const_cast<Rope&>(ropes_[i]); // 02122310 counts the swing down as it draws
        const Vec a = stage_.place(r.a), b = stage_.place(r.b);
        const std::uint16_t colour = stage_.rope;
        if (r.bendFrames < 1) {
            line(a, b, colour);
        } else {
            const int f = r.bendFrames * kOne;
            const int amp = std::min(fxSqrt(lengthSquared(r.impact)), 0x3000);
            int k;
            if (int(i) == perch_) {
                k = div(mul(-f, f - 0xa000), square(div(0xa000, 0x2000)));
            } else if (f < 0x4001) {
                k = div(mul(f, f - 0x4000), 0x8000);
            } else {
                k = div(mul(-(f - 0x4000), f - 0xa000), 0x9000);
            }
            const int bend = mul(k, amp);
            const Vec on{a[0] + mul(b[0] - a[0], r.bendAt), a[1] + mul(b[1] - a[1], r.bendAt)};
            const int inv = fxInvSqrt(lengthSquared(r.impact));
            const Vec dir{mul(r.impact[0], inv), mul(r.impact[1], inv)};
            const Vec p{on[0] + mul(dir[0], bend), on[1] + mul(dir[1], bend)};
            line(a, p, colour);
            line(p, b, colour);
            --r.bendFrames;
        }
        end(a, colour);
        end(b, colour);
    }
    if ((flags_ & kFromRope) && !(flags_ & kPicked)) {
        line(oldStart_, oldEnd_, 0xffff);
        end(oldStart_, 0xffff);
        end(oldEnd_, 0xffff);
    }
    if (flags_ & (kDrawing | kMoving)) {
        const std::uint16_t c = flags_ & kRefused ? stage_.refused : stage_.drawing;
        line(dragStart_, dragEnd_, c);
        end(dragStart_, c);
        end(dragEnd_, c);
    }
    if (flags_ & kAnimating) {
        line(goingA_.value, goingB_.value, stage_.drawing);
        end(goingA_.value, stage_.drawing);
        end(goingB_.value, stage_.drawing);
    }
}

ParrotGameLoop::ParrotGameLoop(Story& story, LoopArgs args)
    : Loop(story, "MINI_PARROT_GAME", args) {}

ParrotGameLoop::~ParrotGameLoop() = default;

void ParrotGameLoop::start() {
    // 0211ece0: the stage asked for; 0 the practice course - the next of
    // three in turn (stages 13..15).
    auto& state = story_.state;
    stage_ = args_[0];
    if (stage_ == 0) {
        const lscr::Value course = story_.codeName(kPracticeCourse, 26);
        const int at = state.variable(course);
        state.setVariable(course, (at + 1) % 3, 0, true);
        stage_ = at + 13;
    }
    for (std::uint32_t w : {kWork5, kWork6, kWork7}) {
        state.setVariable(story_.codeName(w, 26), 0, 0, true);
    }
    state.setVariable(story_.codeName(kWork15, 26), std::min(stage_, kStageCap), 0, true);
}

void ParrotGameLoop::activate() {
    // 0211ef7c: the stage's pictures and sprites, the common sprites, the
    // texts, the program; MAIN once everything has loaded.
    auto name = [&](std::uint32_t address) {
        return story_.codeName(address, 26).text;
    };
    auto format = [&](std::uint32_t address, int n) {
        char out[96];
        std::snprintf(out, sizeof out, name(address).c_str(), n);
        return std::string(out);
    };
    scene_ = makeScene();
    engine_ = std::make_unique<Engine>(
        story_, ParrotStage::load(story_.assets, name(kStageData), stage_), stage_);
    scene_->engine = engine_.get();
    scene_->load(name(kGameProgram));
    for (const auto& f : Story::residentScripts()) {
        scene_->load(f);
    }
    for (int s = 0; s < 2; ++s) {
        const std::string path = format(s == 0 ? kPictureLower : kPictureUpper, stage_);
        scene_->setScreenPicture(s, story_.assets.exists(path) ? &story_.assets.picture(path)
                                                               : nullptr);
    }
    const std::string objects = format(kObjectAnimation, stage_ > 12 ? 0 : stage_);
    scene_->loadAnimation(name(kAnimObjA), 0, objects);
    scene_->loadAnimation(name(kAnimObjB), 2, objects);
    scene_->loadAnimation(name(kAnimCommonA), 0, name(kAnimCommonAFile));
    scene_->loadAnimation(name(kAnimCommonB), 2, name(kAnimCommonBFile));
    scene_->loadTextTable(name(kTextWin), 40, 7300);
    scene_->loadTextTable(name(kTextStage), 90, 100000);
    state_ = State::Loading;
}

void ParrotGameLoop::deactivate() {
    if (scene_) {
        scene_->engine = nullptr;
    }
    scene_.reset();
    engine_.reset();
    state_ = State::Idle;
}

void ParrotGameLoop::end() {
    // 0211ee98: the work variables back to 0.
    for (std::uint32_t w : {kWork5, kWork6, kWork7, kWork15}) {
        story_.state.setVariable(story_.codeName(w, 26), 0, 0, true);
    }
}

bool ParrotGameLoop::busy() const {
    return state_ == State::Loading || Loop::busy();
}

void ParrotGameLoop::frame(const lscr::SceneInput& input) {
    if (!scene_) {
        return;
    }
    if (state_ == State::Loading) {
        const std::vector<lscr::Value> vars{lscr::Value::of(args_[0]), lscr::Value::of(args_[1]),
                                            lscr::Value::of(args_[2]), lscr::Value::of(args_[3])};
        scene_->start("MAIN", vars);
        engine_->live = true;
        state_ = State::Running;
    }
    if (state_ == State::Running &&
        (!scene_->running() || story_.state.variable(story_.codeName(kWork7, 26)) == 1)) {
        // 0211f60c: MAIN over (or the script asks to leave) - the game ends;
        // quitting the first stage during the tutorial plays its event.
        state_ = State::Finished;
        engine_->live = false;
        story_.request(Story::Request::Pop);
        const Phase* tutorial = story_.data.phase(story_.codeName(kTutorialPhase, 26).text);
        const int now = story_.state.variable(story_.codeName(0x020f6d6c));
        if (stage_ == 1 && tutorial && now <= tutorial->id) {
            const EventInfo* e = story_.data.event(story_.codeName(kTutorialQuit, 26).text);
            story_.request(Story::Request::Push, "EVENT", {e ? e->id : -1, -1, 0, 0});
        }
        return;
    }
    scene_->frame(input);
}

std::vector<int> ParrotGameLoop::pegs() const {
    return engine_ ? engine_->stage().knots : std::vector<int>{};
}

std::array<int, 2> ParrotGameLoop::pegPlace(int point) const {
    const auto p = engine_ ? engine_->stage().place(point) : Vec{};
    return {p[0] >> 12, p[1] >> 12};
}

int ParrotGameLoop::ropeLimit() const {
    return engine_ ? engine_->stage().maxRopes : 0;
}

bool ParrotGameLoop::ropeAllowed(int a, int b) const {
    return engine_ && engine_->allowed(a, b);
}

bool ParrotGameLoop::delivers(const std::vector<std::pair<int, int>>& ropes) const {
    return engine_ && scene_ && engine_->trial(*scene_, ropes);
}

lscr::Result ParrotGameLoop::command(int index, lscr::Call& call) {
    if (scene_ && engine_) {
        engine_->command(*scene_, index, call);
    }
    return lscr::Result::Next;
}

void openParrotStages(Story& story) {
    for (int n = 1; n <= kParrotStages; ++n) {
        story.state.setFlag(story.codeName(kStageFlags, 25), true, n - 1);
    }
}

std::vector<std::uint8_t> parrotDelivered(const Story& story) {
    std::vector<std::uint8_t> delivered(kParrotStages);
    for (int n = 1; n <= kParrotStages; ++n) {
        delivered[std::size_t(n - 1)] =
            story.state.flag(story.codeName(kClearFlags, 25), n) ? 1 : 0;
    }
    return delivered;
}

void setParrotDelivered(Story& story, const std::vector<std::uint8_t>& delivered) {
    for (int n = 1; n <= kParrotStages && n <= int(delivered.size()); ++n) {
        story.state.setFlag(story.codeName(kClearFlags, 25), delivered[std::size_t(n - 1)] != 0, n);
    }
}

} // namespace picarat::adventure3
