// The first adventure's "Match" engine (engine 2, ARM9 02043038..020434e8,
// logic 02050170..02050e44, matches 0204f718..02050170) and its "Match
// Tutorial" (engine 1, 02042ca0..02043038, its match 0204ee9c..0204f718):
// moving matches.
//
// How it plays
// ------------
// Matches lie on the table in a picture. The player may change a limited
// number of them (the counter at the bottom): dragging a match by its
// middle moves it, dragging one of its ends turns it about its middle in
// steps of 15 degrees. Changing a match costs one move the first time; a
// match put back exactly where it started gives its move back, and so does
// one moved less than 10 pixels away from its start (it springs back).
// With no moves left, touching an unchanged match shows a "no more moves"
// panel. "Submit" checks the arrangement: every target position needs a
// match on it (a match counts the same turned half way round), first
// within one pixel and one degree, then within each target's own
// tolerances. "Restart" puts every match back.
//
// The tutorial has one large match to play with and a "to the puzzle" bar;
// nothing is judged (the flow then reports a wrong answer, as the original
// does when the tutorial is played as a puzzle).
//
// The second adventure carries this engine unused (overlay 28, similar
// code); its puzzles never play it, so this file is the first adventure's.
#include "puzzles/a1_engines.hpp"
#include "puzzles/a1_moves.hpp"
#include "sound/a1_sound.hpp"
#include <cmath>

namespace picarat::adventure1 {

namespace {

constexpr float kPi = 3.14159012f; // 0204fbc8 (0x40490fd0)

// An angle in the original's units (65536 a turn) as degrees in 0..360.
float degrees(int turns) {
    float a = float(turns) * 360.0f / 65536.0f;
    while (a > 360.0f) {
        a -= 360.0f;
    }
    while (a < 0.0f) {
        a += 360.0f;
    }
    return a;
}

// Degrees to the original's units (double arithmetic, truncated: 0205023c).
int turnsOf(float deg) {
    return int(double(deg) * 65536.0 / 360.0);
}

// The two kinds of match differ in size and limits.
struct MatchShape {
    const char* sprite;
    float arm;       // centre to the end handles
    int grab;        // half the middle's square
    int handle;      // half an end handle's square
    int left, right; // where the middle may be dragged
    int moveOffset;  // the "move" highlight's offset
    int dragOffset;  // the tutorial's match sits 4 pixels up-left of the stylus
};

constexpr MatchShape kMatch{"match.spr", 21.0f, 12, 8, 0x20, 0xea, -2, 0};
constexpr MatchShape kBigMatch{"matchtutorial.spr", 30.0f, 16, 12, 0x30, 0xda, 0, 4};

// One match (0204f718 / 0204ee9c, 0x18 bytes).
struct Stick {
    int x = 0, y = 0;              // +0 / +2: its middle
    int savedX = 0, savedY = 0;    // +4 / +6: where the move started
    int angle = 0;                 // +8 (65536 a turn)
    int savedAngle = 0;            // +0xc
    bool dragged = false;          // +0x11: held by the middle
    bool turnedA = false;          // +0x12: held by the end at angle + 180
    bool turnedB = false;          // +0x13: held by the end at angle
    SceneKit::SpriteObject sprite; // +0x14

    // An end's offset from the middle.
    std::pair<int, int> end(const MatchShape& shape, float addDegrees) const {
        const float rad = kPi * (degrees(angle) + addDegrees) / 180.0f;
        return {int(shape.arm * std::cos(rad)), int(shape.arm * std::sin(rad))};
    }

    // 0204fbdc / 0204f19c: the press began on the middle.
    bool middleHit(const MatchShape& shape, const TouchState& t) const {
        return pressedIn(t, x - shape.grab, y - shape.grab, 2 * shape.grab);
    }

    // 0204fc08 / 0204f1c8 (end A) and 0204fd44 / 0204f2a4 (end B).
    bool endHit(const MatchShape& shape, const TouchState& t, bool a) const {
        const auto [dx, dy] = end(shape, a ? 180.0f : 0.0f);
        return pressedIn(t, x - shape.handle + dx, y - shape.handle + dy, 2 * shape.handle);
    }

    // 0204fe70 / 0204f378: the direction from the middle to the stylus in
    // whole steps of 15 degrees (the end held points at the stylus).
    float pointing(const TouchState& t) const {
        const int dx = t.x - x, dy = t.y - y;
        const float length = float(std::sqrt(double(dx * dx + dy * dy)));
        const float cosine = float(dx) / length * 1.0f + float(dy) / length * 0.0f;
        float deg = 180.0f * float(std::acos(double(cosine))) / kPi;
        if (dy < 0) {
            deg = 360.0f - deg;
        }
        if (turnedA) {
            deg += 180.0f;
        }
        return float(int(deg) / 15 * 15);
    }

    // 0204ffb0 / 0204f4b8 (the stylus part): drag or turn the match held.
    void follow(const MatchShape& shape, TouchState& t, bool moved) {
        if (!t.down || !t.steady()) {
            return;
        }
        if (dragged) {
            x = (std::clamp(t.x, shape.left, shape.right) - shape.dragOffset) & 0xff;
            y = (std::clamp(t.y, 0x36, 0x9e) - shape.dragOffset) & 0xff;
        } else if ((turnedA || turnedB) && moved && (t.x != x || t.y != y)) {
            // (The stylus exactly on the middle would give the original no
            // direction - a division by zero; the match keeps its angle.)
            angle = turnsOf(pointing(t));
        }
    }

    // 0204f944 / 0204ef44: the match turned about its middle, the "move"
    // highlight while dragged, the handle held while turned.
    void draw(SceneKit& kit, const MatchShape& shape) {
        kit.select(sprite, "match");
        sprite.x = x;
        sprite.y = y;
        kit.drawRotated(sprite, unsigned(angle) & 0xffff);
        if (dragged) {
            kit.select(sprite, "move");
            sprite.x = x + shape.moveOffset - (kit.frameWidth(sprite) >> 1);
            sprite.y = y + shape.moveOffset - (kit.frameHeight(sprite) >> 1);
            kit.draw(sprite);
        }
        for (const bool a : {false, true}) {
            if (a ? turnedA : turnedB) {
                const auto [dx, dy] = end(shape, a ? 180.0f : 0.0f);
                kit.select(sprite, a ? "rotate" : "rotate2");
                sprite.x = x + dx;
                sprite.y = y + dy;
                kit.drawRotated(sprite, unsigned(angle) & 0xffff);
            }
        }
    }

private:
    static bool pressedIn(const TouchState& t, int left, int top, int size) {
        // 0200ea58: inclusive edges.
        return left <= t.pressX && t.pressX <= left + size && top <= t.pressY &&
               t.pressY <= top + size;
    }
};

// Whether the stylus moved since the last frame (the flags 0200eb38 sets
// at +0x30..+0x33; a new press counts as a move).
bool stylusMoved(const TouchState& t) {
    return t.previousX != t.x || t.previousY != t.y;
}

class Match final : public Engine {
public:
    explicit Match(PuzzleScene& scene) {
        // 02043038 and 02050170.
        SceneKit& kit = scene.kit;
        const std::string picture = scene.id() == 0x19 ? "q25_bg" : "match_bg";
        if (fileExists(kit.assets.root / "bg" / (picture + ".l2bg"))) {
            scene.setEngineBackground(picture); // 02040ddc
        }
        submit_.setSprite(kit, 0x16, 6, "?/mtut_off.spr", "gfx");
        submit_.visibleIdle = true;
        submit_.sound = 0xb;
        restart_.setSprite(kit, 99, 6, "?/mtut_off.spr", "gfx2");
        restart_.visibleIdle = true;
        restart_.sound = 10;
        counter_.load(kit);
        kit.hold(kLoadFrames); // a loading pause
    }

    void bind(ScriptContext& context, PuzzleScene& scene) override {
        SceneKit& kit = scene.kit;
        // 0x27 (virtual +0x6c, 020432b8): the moves allowed.
        context.bind(0x27, [this](CommandArguments& a) { allowed_ = moves_ = a.integer() & 0xff; });
        // 0x2a (virtual +0x78 -> 0205023c): a match's middle and angle.
        context.bind(0x2a, [this, &kit](CommandArguments& a) {
            Start s;
            s.x = a.integer();
            s.y = a.integer();
            s.angle = a.real();
            Stick stick;
            stick.sprite = kit.load(kMatch.sprite, 0, 0, 1);
            stick.x = s.x;
            stick.y = s.y;
            stick.angle = turnsOf(s.angle);
            sticks_.push_back(std::move(stick));
            starts_.push_back(s);
            changed_.push_back(false);
        });
        // 0x2b (virtual +0x7c -> 02050300): a target: middle, angle and the
        // distance and angle tolerances of the second pass.
        context.bind(0x2b, [this](CommandArguments& a) {
            Target t;
            t.x = a.integer();
            t.y = a.integer();
            t.angle = a.real();
            t.distance = a.real();
            t.turn = a.real();
            targets_.push_back(t);
        });
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 020432f4.
        SceneKit& kit = scene.kit;
        scene.drawCommon(interactive);
        if (interactive) {
            input(scene);
            submit_.update(kit, scene.touch);
            restart_.update(kit, scene.touch);
        } else {
            drawSticks(kit);
            submit_.draw(kit);
            restart_.draw(kit);
        }
    }

    void play(PuzzleScene& scene) override {
        // 020433b4.
        SceneKit& kit = scene.kit;
        begin();
        scene.intro();
        for (;;) {
            scene.frame([&] { draw(scene, true); });
            if (submit_.clicked(kit, scene.touch)) {
                if (judge()) { // 02050614
                    scene.markSolved();
                } else {
                    scene.markWrong();
                }
                break;
            }
            if (restart_.clicked(kit, scene.touch)) {
                restart(); // 02050358
            }
            if (scene.hintClicked()) {
                scene.hintScreen();
            }
            if (scene.quitClicked() && scene.quitDialog()) {
                break;
            }
        }
        scene.fadeOut(); // 0200fce8(1, 1)
        NoMovesPanel::clear(kit);
    }

private:
    static constexpr int kLoadFrames = 7; // measured on puzzle 1

    struct Start {
        int x = 0, y = 0; // +0xc8 / +0x148
        float angle = 0;  // +0x348
    };

    struct Target {
        int x = 0, y = 0;   // +0x1c8 / +0x248
        float angle = 0;    // +0x2c8
        float distance = 0; // +0x3c8
        float turn = 0;     // +0x448
    };

    // 02050414: the arrangement now is where moves start from.
    void begin() {
        for (auto& s : sticks_) {
            s.savedAngle = s.angle;
            s.savedX = s.x;
            s.savedY = s.y;
        }
        selected_ = -1;
        dragging_ = false;
        refused_ = false;
    }

    // 02050358: every match back to its start, all moves again.
    void restart() {
        selected_ = -1;
        moves_ = allowed_;
        counter_.stop();
        for (std::size_t i = 0; i < sticks_.size(); ++i) {
            sticks_[i].angle = turnsOf(starts_[i].angle);
            sticks_[i].x = starts_[i].x;
            sticks_[i].y = starts_[i].y;
            changed_[i] = false;
        }
        begin();
    }

    // Takes hold of match i; changing it costs a move the first time.
    void take(std::size_t i) {
        if (!changed_[i]) {
            changed_[i] = true;
            moves_ = std::max(moves_ - 1, 0);
        }
    }

    // 020507b8: presses, releases and the matches held, then drawing.
    void input(PuzzleScene& scene) {
        SceneKit& kit = scene.kit;
        TouchState& t = scene.touch;
        const int movesBefore = moves_;
        panel_.update(kit, refused_); // the "no more moves" panel
        if (t.pressed) {
            press(kit, t);
        }
        if (t.released) {
            release();
        }
        const bool moved = stylusMoved(t);
        for (auto& s : sticks_) {
            // 0204ffb0: a release lets go (with a sound for a dragged match).
            if (t.released) {
                if (s.dragged) {
                    kit.effect(0x96);
                }
                s.dragged = s.turnedA = s.turnedB = false;
            }
            s.follow(kMatch, t, moved);
        }
        if (movesBefore != moves_) {
            counter_.hop();
        }
        drawSticks(kit);
    }

    void press(SceneKit& kit, const TouchState& t) {
        const std::size_t n = sticks_.size();
        auto grab = [&](std::size_t i) {
            take(i);
            sticks_[i].dragged = true;
            dragging_ = true;
            selected_ = int(i);
            lastDragged_ = i;
            kit.effect(0x98);
        };
        // A changed match is taken by its middle first, then any match
        // while moves remain.
        for (std::size_t i = 0; i < n; ++i) {
            if (changed_[i] && sticks_[i].middleHit(kMatch, t)) {
                grab(i);
                break;
            }
        }
        for (std::size_t i = 0; i < n && selected_ == -1; ++i) {
            if (sticks_[i].middleHit(kMatch, t) && (moves_ > 0 || changed_[i])) {
                grab(i);
                break;
            }
            if (sticks_[i].middleHit(kMatch, t) && moves_ == 0) {
                refused_ = true;
            }
        }
        // Then the ends: of changed matches freely, of others for a move.
        for (std::size_t i = 0; i < n && selected_ == -1; ++i) {
            if (!changed_[i]) {
                continue;
            }
            for (const bool a : {false, true}) {
                if (sticks_[i].endHit(kMatch, t, a)) {
                    (a ? sticks_[i].turnedA : sticks_[i].turnedB) = true;
                    selected_ = int(i);
                    refused_ = false;
                    break;
                }
            }
        }
        for (std::size_t i = 0; i < n && selected_ == -1; ++i) {
            for (const bool a : {false, true}) {
                if (sticks_[i].endHit(kMatch, t, a) && moves_ > 0) {
                    (a ? sticks_[i].turnedA : sticks_[i].turnedB) = true;
                    changed_[i] = true;
                    --moves_;
                    selected_ = int(i);
                    break;
                }
            }
        }
    }

    void release() {
        refused_ = false;
        if (selected_ != -1) {
            // Back exactly where it started: the move is returned.
            Stick& s = sticks_[std::size_t(selected_)];
            if (s.angle == s.savedAngle && s.x == s.savedX && s.y == s.savedY) {
                changed_[std::size_t(selected_)] = false;
                ++moves_;
                dragging_ = false;
            }
        }
        selected_ = -1;
        if (dragging_) {
            // 0204f7b4: moved 10 pixels or less, it springs back.
            Stick& s = sticks_[lastDragged_];
            const int dx = s.savedX - s.x, dy = s.savedY - s.y;
            const float distance = float(std::sqrt(double(float(dx * dx) + float(dy * dy))));
            if (distance <= 10.0f) {
                s.angle = s.savedAngle;
                s.x = s.savedX;
                s.y = s.savedY;
                changed_[lastDragged_] = false;
                ++moves_;
            }
        }
        dragging_ = false;
    }

    // 02050ca0: shadows, matches, then the moves left.
    void drawSticks(SceneKit& kit) {
        for (auto& s : sticks_) {
            // 0204f8cc: a faint copy where the match started, darker while
            // it is dragged.
            kit.select(s.sprite, s.dragged ? "dark" : "shadow");
            s.sprite.x = s.savedX + 1;
            s.sprite.y = s.savedY + 1;
            kit.drawRotated(s.sprite, unsigned(s.savedAngle) & 0xffff);
        }
        for (auto& s : sticks_) {
            s.draw(kit, kMatch);
        }
        counter_.draw(kit, moves_);
    }

    // 02050470: a match lies at `target` degrees (either way round)
    // within `tolerance`.
    static bool alongside(const Stick& s, float target, float tolerance) {
        float d = target - degrees(s.angle);
        if (d < 0.0f) {
            d = 0.0f - d;
        }
        if (d >= 180.0f) {
            d -= 180.0f;
        }
        if (d >= 90.0f) {
            d = 180.0f - d;
        }
        return d <= tolerance;
    }

    static float distance(const Stick& s, const Target& t) {
        // 0200c15c.
        const float dx = float(s.x - t.x), dy = float(s.y - t.y);
        return float(std::sqrt(double(dx * dx + dy * dy)));
    }

    // 02050614: every target taken, first exactly, then within its own
    // tolerances (each match takes the first target it fits per pass).
    bool judge() const {
        std::vector<bool> taken(targets_.size(), false);
        for (const auto& s : sticks_) {
            for (std::size_t j = 0; j < targets_.size(); ++j) {
                if (distance(s, targets_[j]) < 1.0f && alongside(s, targets_[j].angle, 1.0f)) {
                    taken[j] = true;
                    break;
                }
            }
        }
        for (const auto& s : sticks_) {
            for (std::size_t j = 0; j < targets_.size(); ++j) {
                const Target& t = targets_[j];
                if (!taken[j] && distance(s, t) < t.distance && alongside(s, t.angle, t.turn)) {
                    taken[j] = true;
                    break;
                }
            }
        }
        return std::all_of(taken.begin(), taken.end(), [](bool b) { return b; });
    }

    std::vector<Stick> sticks_; // +0 (at most 32)
    std::vector<Start> starts_;
    std::vector<Target> targets_; // count +0x4c8
    std::vector<bool> changed_;   // +0x94
    int allowed_ = 0, moves_ = 0; // +0x90 / +0x8c
    int selected_ = -1;           // +0x88
    std::size_t lastDragged_ = 0; // 0220f120
    bool dragging_ = false;       // +0xb8
    bool refused_ = false;        // +0xba
    MovesCounter counter_;        // +0xb4, +0xbc..+0xc4
    NoMovesPanel panel_;
    Button submit_, restart_; // +0x78 / +0x7c
};

class MatchTutorial final : public Engine {
public:
    explicit MatchTutorial(PuzzleScene& scene) {
        // 02042ca0: its own pictures on both screens, two captions, one
        // large match and a bar at the bottom; the sounds of scene kind 9
        // (02004b08) and the music faded out (02005114).
        SceneKit& kit = scene.kit;
        kit.lowerBackground = "mtut_bottom";
        kit.upperBackground = kit.localized("?/mtut_top");
        // With sound: 3 frames loading its pictures, then the sound object
        // waits for the music to fade and the new music loads (ROM frames
        // 1005..1047 of `p0`).
        if (scene.sound) {
            kit.hold(3);
            scene.sound->setupScene(9, [&scene] { scene.soundWait(); });
            kit.hold(11); // its music and effects load
            scene.sound->fadeOutMusic();
        }
        kit.clearText(SceneKit::Upper);
        kit.clearText(SceneKit::Lower);
        stick_.sprite = kit.load(kBigMatch.sprite, 0, 0, 1);
        stick_.x = 0x78;
        stick_.y = 0x60;
        next_.setRect(0, 0xac, 0x100, 0x14);
        next_.sound = 3;
        caption_ = kit.load("?/mtut_mes.spr", 0x19, 9, 1);
        bar_ = kit.load("?/mtut_mes.spr", 0x40, 0xaa, 2);
        if (!scene.sound) {
            kit.hold(kLoadFrames); // a loading pause
        }
    }

    void bind(ScriptContext&, PuzzleScene&) override {}

    // Without sound: the result loads in 81 frames after the tutorial
    // (measured on `p0`).
    int resultLoadFrames() const override { return 81; }

    void draw(PuzzleScene& scene, bool) override { frame(scene, false); } // not called

    void play(PuzzleScene& scene) override {
        // 02042f48: no question; fade in on the first picture, play until
        // the bar is touched.
        SceneKit& kit = scene.kit;
        scene.frame([&] {
            kit.draw(caption_);
            kit.draw(bar_);
            stick_.draw(kit, kBigMatch);
        });
        fadeIn(kit, Screens::Both);
        for (;;) {
            scene.frame([&] { frame(scene, true); });
            if (next_.clicked(kit, scene.touch)) {
                break;
            }
        }
        scene.fadeOut(); // 0200fce8(1, 1)
    }

private:
    static constexpr int kLoadFrames = 44; // without sound (measured, `p0`)

    void frame(PuzzleScene& scene, bool interactive) {
        SceneKit& kit = scene.kit;
        TouchState& t = scene.touch;
        if (interactive) {
            // 0204f4b8.
            if (t.pressed) {
                if (stick_.middleHit(kBigMatch, t)) {
                    kit.effect(0x98);
                    stick_.dragged = true;
                } else if (stick_.endHit(kBigMatch, t, false)) {
                    stick_.turnedB = true;
                } else if (stick_.endHit(kBigMatch, t, true)) {
                    stick_.turnedA = true;
                }
            }
            if (t.released) {
                if (stick_.dragged) {
                    kit.effect(0x96);
                }
                stick_.dragged = stick_.turnedA = stick_.turnedB = false;
            }
            stick_.follow(kBigMatch, t, stylusMoved(t));
        }
        stick_.draw(kit, kBigMatch);
        kit.draw(caption_);
        kit.draw(bar_);
        if (interactive) {
            next_.update(kit, t);
        }
    }

    Stick stick_;                          // +0x74
    Button next_;                          // +0x78
    SceneKit::SpriteObject caption_, bar_; // +0x7c / +0x80
};

} // namespace

std::unique_ptr<Engine> createMatch(PuzzleScene& scene) {
    return std::make_unique<Match>(scene);
}

std::unique_ptr<Engine> createMatchTutorial(PuzzleScene& scene) {
    return std::make_unique<MatchTutorial>(scene);
}

} // namespace picarat::adventure1
