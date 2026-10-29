// The first adventure's "River Cross" and "River Cross 2" engines (engines
// 7 and 16, one object: ARM9 02047028..02047484, river 020552f4..02055eac,
// passengers 02055eac..020565a0): getting everyone across the river.
//
// How it plays
// ------------
// Animals wait on the left bank; a raft carries two. The player drags
// animals onto the raft (or off it, back to its bank) and taps the raft to
// cross; crossings are counted. In the first kind (wolves and chicks) a
// bank where wolves outnumber chicks loses its chicks once the raft has
// left; in the second (a farmer who always rows, a wolf, a sheep and a
// cabbage) the wolf eats the sheep, or the sheep the cabbage, on a bank
// without the farmer (with all three together nothing happens). A loss
// shows a message (after a short pause, with a sound) and stops play until
// "Restart". Everyone across solves the puzzle.
//
// The second adventure has no river puzzle; this engine is the first
// adventure's own.
#include "puzzles/a1_engines.hpp"

namespace picarat::adventure1 {

namespace {

class RiverCross final : public Engine {
public:
    RiverCross(PuzzleScene& scene, bool farmer) : farmer_(farmer) {
        // 02047028 and 020552f4; the factory sets the second kind through
        // virtual +0x90 (command 0x8c too).
        SceneKit& kit = scene.kit;
        if (fileExists(kit.assets.root / "bg" / "q12_bg.l2bg")) {
            scene.setEngineBackground("q12_bg"); // 02040ddc
        }
        restart_.setSprite(kit, 6, 6, "?/restart.spr", "gfx");
        restart_.visibleIdle = true;
        restart_.sound = 10;
        digits_ = kit.load("river_count.spr", 0, 0, 1);
        raftSprite_ = kit.load("river_raft.spr", 0, 0, 1);
        fail_ = kit.load("?/river_fail.spr", 0x67, 0x33, 1);
        kit.hold(kLoadFrames); // a loading pause
    }

    // No "Submit" sound is left playing when the result loads, so it does
    // not wait for one (see a1_connect.cpp; measured on puzzle 28).
    int resultLoadFrames() const override { return 38; }

    void bind(ScriptContext& context, PuzzleScene& scene) override {
        SceneKit& kit = scene.kit;
        // Passengers at x, y on the left bank (at most 8): 0x32 wolf, 0x31
        // chick, 0x89 farmer, 0x8a cabbage, 0x8b sheep (02055918 ..
        // 02055ae8).
        auto add = [this, &kit](Kind kind, const char* sprite) {
            return [this, &kit, kind, sprite](CommandArguments& a) {
                const int x = a.integer(), y = a.integer();
                if (passengers_.size() >= 8) {
                    return;
                }
                Passenger p;
                p.kind = kind;
                p.x = x;
                p.y = y;
                p.sprite = kit.load(sprite, 0, 0, 1);
                passengers_.push_back(std::move(p));
                place(passengers_.back());
            };
        };
        context.bind(0x32, add(Wolf, "river_wolf.spr"));
        context.bind(0x31, add(Chick, "river_chicken.spr"));
        context.bind(0x89, add(Farmer, "river_man.spr"));
        context.bind(0x8a, add(Cabbage, "river_cabbage.spr"));
        context.bind(0x8b, add(Sheep, "river_sheep.spr"));
        context.bind(0x8c, [this](CommandArguments& a) { farmer_ = a.integer() == 1; });
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 0204728c.
        SceneKit& kit = scene.kit;
        scene.drawCommon(interactive);
        if (interactive) {
            restart_.update(kit, scene.touch);
        } else {
            restart_.draw(kit);
        }
        river(scene, interactive); // 02055e18
    }

    void play(PuzzleScene& scene) override {
        // 020472c4.
        SceneKit& kit = scene.kit;
        reset(kit);
        scene.intro();
        for (;;) {
            scene.frame([&] { draw(scene, true); });
            if (!lost() && raftTapped(kit, scene.touch) && aboard_ != 0) {
                if (raft_ == AtLeft) {
                    setOff(ToRight);
                    eat(Left, EatenLeft);
                } else if (raft_ == AtRight) {
                    setOff(ToLeft);
                    eat(Right, EatenRight);
                }
                kit.effect(0xac);
                while (raft_ == ToRight || raft_ == ToLeft) {
                    scene.frame([&] { draw(scene, false); });
                }
                eat(Right, EatenRight);
                eat(Left, EatenLeft);
            }
            if (allAcross()) { // 020556c4
                kit.hold(5);
                scene.markSolved();
                break;
            }
            if (restart_.clicked(kit, scene.touch)) {
                reset(kit); // 020558b4
            }
            if (scene.hintClicked()) {
                scene.hintScreen();
            }
            if (scene.quitClicked() && scene.quitDialog()) {
                break;
            }
        }
        scene.fadeOut(); // 02047110 (destructor)
    }

private:
    static constexpr int kLoadFrames = 12; // measured on puzzle 12
    static constexpr int kSeats = 2;       // +0x40

    enum Kind { Wolf = 0, Chick = 1, Sheep = 2, Cabbage = 3, Farmer = 4 };

    // Where a passenger is (+0x1c); the banks double as sides.
    enum Place { Left = 0, Aboard = 1, Right = 2, EatenLeft = 3, EatenRight = 4 };

    enum Raft { AtLeft = 0, ToRight = 1, AtRight = 2, ToLeft = 3 };

    struct Passenger {
        Kind kind = Wolf;              // +0
        int x = 0, y = 0;              // +4 / +8: on the left bank (mirrored on the right)
        int dragX = 0, dragY = 0;      // +0xc / +0x10
        int place = Left;              // +0x1c
        int seatX = 0, seatY = 0;      // +0x20 / +0x24: on the raft
        int seat = 0;                  // +0x28
        bool held = false;             // +0x2c
        SceneKit::SpriteObject sprite; // +0x14
    };

    // 02055f74: on the left bank (the farmer on the raft's first seat).
    void place(Passenger& p) {
        p.place = Left;
        p.held = false;
        if (p.kind == Farmer) {
            p.place = Aboard;
            p.seat = 0;
            ++aboard_;
        }
    }

    // 020558b4.
    void reset(SceneKit& kit) {
        crossings_ = 0;
        raft_ = AtLeft;
        raftX_ = 0x38;
        raftY_ = 0x67;
        kit.select(raftSprite_, 1u);
        aboard_ = 0;
        for (auto& p : passengers_) {
            kit.select(p.sprite, 1u);
            place(p);
        }
    }

    bool lost() const { // 02055710
        return std::any_of(passengers_.begin(), passengers_.end(), [](const Passenger& p) {
            return p.place == EatenLeft || p.place == EatenRight;
        });
    }

    bool allAcross() const { // 020556c4
        return std::all_of(passengers_.begin(), passengers_.end(),
                           [](const Passenger& p) { return p.place == Right || p.kind == Farmer; });
    }

    // 020554a0: wolves outnumber chicks on a side.
    bool outnumbered(int side) const {
        int wolves = 0, chicks = 0;
        for (const auto& p : passengers_) {
            if (p.place == side) {
                wolves += p.kind == Wolf;
                chicks += p.kind == Chick;
            }
        }
        return chicks < wolves;
    }

    // 02055504: who is eaten on a side once the raft has gone.
    void eat(int side, int eaten) {
        auto victims = [&](Kind kind) {
            for (auto& p : passengers_) {
                if (p.place == side && p.kind == kind) {
                    p.place = eaten;
                }
            }
        };
        if (!farmer_) {
            if (outnumbered(side) && raft_ != ToRight && raft_ != ToLeft) {
                victims(Chick);
            }
            return;
        }
        if (raft_ == ToRight || raft_ == ToLeft || (side == Right && raft_ == AtRight) ||
            (side == Left && raft_ == AtLeft)) {
            return; // the farmer is there (or under way)
        }
        bool wolf = false, sheep = false, cabbage = false;
        for (const auto& p : passengers_) {
            if (p.place == side) {
                wolf = wolf || p.kind == Wolf;
                sheep = sheep || p.kind == Sheep;
                cabbage = cabbage || p.kind == Cabbage;
            }
        }
        if (wolf && sheep && !cabbage) {
            victims(Sheep);
        }
        if (!wolf && sheep && cabbage) {
            victims(Cabbage);
        }
    }

    // 02055884: the raft sets off (a crossing counted, at most 999).
    void setOff(int raft) {
        raft_ = raft;
        crossings_ = std::min(crossings_ + 1, 999);
    }

    // 02055750: released after a press on the raft's lower half.
    bool raftTapped(SceneKit& kit, const TouchState& t) const {
        const int w = kit.frameWidth(raftSprite_), h = kit.frameHeight(raftSprite_);
        const int top = raftY_ + (h >> 1);
        return t.released && raftX_ <= t.pressX && t.pressX <= raftX_ + w && top <= t.pressY &&
               t.pressY <= top + (h >> 1);
    }

    // 02055820: the raft lands; its passengers (not the farmer) step off.
    void land(int bank) {
        for (auto& p : passengers_) {
            if (p.place == Aboard && p.kind != Farmer) {
                p.place = bank;
                --aboard_;
            }
        }
    }

    // A passenger's drawn left edge on its bank.
    int bankX(SceneKit& kit, const Passenger& p) const {
        return p.place == Right || p.place == EatenRight ? 0x100 - kit.frameWidth(p.sprite) - p.x
                                                         : p.x;
    }

    // 02056054: a press on the passenger.
    bool pressedOn(SceneKit& kit, const TouchState& t, const Passenger& p) const {
        int x = p.x, y = p.y;
        if (p.place == Right) {
            x = 0x100 - kit.frameWidth(p.sprite) - p.x;
        } else if (p.place == Aboard) {
            x = p.seatX;
            y = p.seatY;
        }
        const int w = kit.frameWidth(p.sprite), h = kit.frameHeight(p.sprite);
        return t.pressed && x <= t.pressX && t.pressX <= x + w && y <= t.pressY &&
               t.pressY <= y + h;
    }

    // 02055fd0: whether a passenger let go at its drag position moves.
    bool moves(const Passenger& p) const {
        if (p.place == Left && p.dragX < 0x38) {
            return false;
        }
        if (p.place == Aboard && p.dragX > 0x38 && raft_ < 2) {
            return false;
        }
        if (p.place == Right && p.dragX > 0xc2) {
            return false;
        }
        if (p.place == Aboard && p.dragX < 0xc2 && (raft_ == AtRight || raft_ == ToLeft)) {
            return false;
        }
        return true;
    }

    // 0205633c: picking up, letting go, dragging.
    void handle(SceneKit& kit, TouchState& t, Passenger& p) {
        if (lost() || p.place == EatenLeft || p.place == EatenRight) {
            return;
        }
        const bool nobodyHeld = std::none_of(passengers_.begin(), passengers_.end(),
                                             [](const Passenger& q) { return q.held; });
        if (pressedOn(kit, t, p) && nobodyHeld && p.kind != Farmer) {
            kit.effect(0xaa);
            p.held = true;
        }
        if (t.released && p.held) {
            kit.effect(0xab);
            p.held = false;
            if (moves(p)) {
                if (p.place == Left && aboard_ < kSeats && raft_ == AtLeft) {
                    p.place = Aboard;
                    p.seat = aboard_++;
                } else if (p.place == Right && aboard_ < kSeats && raft_ == AtRight) {
                    p.place = Aboard;
                    p.seat = aboard_++;
                } else if (p.place == Aboard && raft_ == AtLeft) {
                    p.place = Left;
                    --aboard_;
                } else if (p.place == Aboard && raft_ == AtRight) {
                    p.place = Right;
                    --aboard_;
                }
            }
        }
        if (t.down && p.held && t.steady()) {
            p.dragX = t.x - 2 - (kit.frameWidth(p.sprite) >> 1);
            p.dragY = t.y - 2 - (kit.frameHeight(p.sprite) >> 1);
        }
    }

    void put(SceneKit& kit, SceneKit::SpriteObject& s, unsigned animation, int x, int y) {
        kit.select(s, animation);
        s.x = x;
        s.y = y;
        kit.draw(s);
    }

    // 02055e18: the raft moves on, the passengers take input, then
    // 02055d30 draws.
    void river(PuzzleScene& scene, bool interactive) {
        SceneKit& kit = scene.kit;
        if (raft_ == ToRight) { // 020557c0
            if (raftX_ > 0x87) {
                land(Right);
                raft_ = AtRight;
            } else {
                raftX_ += 2;
            }
        } else if (raft_ == ToLeft) { // 020557f0
            if (raftX_ < 0x39) {
                land(Left);
                raft_ = AtLeft;
            } else {
                raftX_ -= 2;
            }
        }
        if (interactive) {
            for (auto& p : passengers_) {
                const int before = p.place;
                handle(kit, scene.touch, p);
                if (before != Aboard && p.place == Aboard) {
                    break; // one boards a frame
                }
            }
        }
        // 02055b5c: the crossings, without leading zeros.
        if (crossings_ > 99) {
            put(kit, digits_, unsigned(crossings_ % 1000 / 100 + 1), 0x16, 0x25);
        }
        if (crossings_ > 9) {
            put(kit, digits_, unsigned(crossings_ % 100 / 10 + 1), 0x1f, 0x25);
        }
        put(kit, digits_, unsigned(crossings_ % 10 + 1), 0x28, 0x25);
        raftSprite_.x = raftX_;
        raftSprite_.y = raftY_;
        kit.draw(raftSprite_);
        // 02055c84: the passengers aboard, seat by seat, side by side.
        int seatX = raftX_ + 8;
        const int deck = raftY_ + (kit.frameHeight(raftSprite_) >> 1);
        for (int seat = 0; seat < 2; ++seat) {
            for (auto& p : passengers_) {
                if (p.place != Aboard || p.seat != seat) {
                    continue;
                }
                // 020562b8: facing the way the raft goes.
                kit.select(p.sprite, raft_ == AtRight || raft_ == ToLeft ? 2u : 1u);
                p.seatX = seatX;
                p.seatY = deck - kit.frameHeight(p.sprite);
                p.sprite.x = p.held ? p.dragX : p.seatX;
                p.sprite.y = p.held ? p.dragY : p.seatY;
                kit.draw(p.sprite);
                seatX += kit.frameWidth(p.sprite) + 1;
            }
        }
        // 020560e8: on the banks (a chick in danger looks scared).
        for (auto& p : passengers_) {
            switch (p.place) {
            case Left:
            case Right: {
                const bool scared = p.kind == Chick && outnumbered(p.place);
                kit.select(p.sprite, scared ? 5u : p.place == Left ? 1u : 2u);
                p.sprite.x = p.held ? p.dragX : bankX(kit, p);
                p.sprite.y = p.held ? p.dragY : p.y;
                kit.draw(p.sprite);
                break;
            }
            case EatenLeft:
                put(kit, p.sprite, 3, p.x, p.y);
                break;
            case EatenRight:
                kit.select(p.sprite, 4u);
                p.sprite.x = p.held ? p.dragX : bankX(kit, p);
                p.sprite.y = p.held ? p.dragY : p.y;
                kit.draw(p.sprite);
                break;
            default:
                break;
            }
        }
        // A loss: after ten still frames a sound; the message all along.
        if (!lost()) {
            failShown_ = false;
            failWait_ = 0;
            return;
        }
        if (!failShown_) {
            if (raft_ != ToRight && raft_ != ToLeft) {
                ++failWait_;
            }
            if (failWait_ > 10) {
                failShown_ = true;
                kit.effect(0xad);
            }
        }
        kit.draw(fail_);
    }

    bool farmer_ = false;                               // +0x48: the second kind
    std::vector<Passenger> passengers_;                 // +0x1c, count +0x3c
    int crossings_ = 0;                                 // +0
    int raft_ = AtLeft;                                 // +0x10
    int raftX_ = 0x38, raftY_ = 0x67;                   // +0x14 / +0x18
    int aboard_ = 0;                                    // +0x44
    bool failShown_ = false;                            // 0220f124
    int failWait_ = 0;                                  // 0220f125
    SceneKit::SpriteObject digits_, raftSprite_, fail_; // +4 / +8 / +0xc
    Button restart_;                                    // +0x74
};

} // namespace

std::unique_ptr<Engine> createRiverCross(PuzzleScene& scene, bool farmer) {
    return std::make_unique<RiverCross>(scene, farmer);
}

} // namespace picarat::adventure1
