// The first adventure's "Tile" engine (engine 15, ARM9 02049d20..0204ab48,
// tile pieces 0205a710..0205ac40): pictures to drag into place.
//
// How it plays
// ------------
// Picture tiles lie beside empty places. The player drags a tile with the
// stylus; it follows centred on the stylus and, when released, snaps to the
// nearest place (its starting place counts too); a tile dropped on an
// occupied place swaps with the tile there. In puzzle 116 four buttons turn
// the tile lying on their place by a quarter. "Restart" puts every tile back
// (some puzzles hide it). "Submit" compares the arrangement (and the turns)
// with the accepted answers. After a wrong answer the next try starts with
// the arrangement as it was.
//
// The rules of the places are the second adventure's tile puzzle
// (puzzles/slot_puzzle.*), one version earlier: SlotPuzzle runs them with
// Rules::Adventure1. Turning, restarting and the memory between tries are
// this engine's own.
#include "puzzles/a1_engines.hpp"
#include "puzzles/slot_puzzle.hpp"

namespace picarat::adventure1 {

namespace {

class Tile final : public Engine {
public:
    explicit Tile(PuzzleScene& scene) : places_(SlotPuzzle::Rules::Adventure1) {
        // 02049d20.
        SceneKit& kit = scene.kit;
        const std::string picture = "q" + std::to_string(scene.id()) + "_bg";
        if (fileExists(kit.assets.root / "bg" / (picture + ".l2bg"))) {
            scene.setEngineBackground(picture); // 02040ddc
        }
        submit_.setSprite(kit, 0xba, 0x9d, "?/submit.spr", "gfx");
        submit_.visibleIdle = true;
        submit_.sound = 0xb;
        restart_.setSprite(kit, 6, 6, "?/restart.spr", "gfx");
        restart_.visibleIdle = true;
        restart_.sound = 10;
    }

    void bind(ScriptContext& context, PuzzleScene& scene) override {
        SceneKit& kit = scene.kit;
        // 0x73 (virtual +0x10c, 0204a444): a tile at x, y with its sprite and
        // animation (0205a710); its starting place becomes a place.
        context.bind(0x73, [this, &kit](CommandArguments& a) {
            const int x = a.integer(), y = a.integer();
            const std::string sprite = a.string();
            const int animation = a.integer();
            if (int(places_.tiles().size()) >= 0x18) {
                return;
            }
            places_.addTile(x, y, sprite, animation);
            const int index = int(places_.tiles().size()) - 1;
            SceneKit::SpriteObject s = kit.load(sprite, x, y, unsigned(animation));
            s.x = x;
            s.y = y;
            places_.tile(index).width = kit.frameWidth(s);
            places_.tile(index).height = kit.frameHeight(s);
            sprites_.push_back(s);
        });
        // 0x74 (virtual +0x110, 0204a3bc): an empty place.
        context.bind(0x74, [this](CommandArguments& a) {
            const int x = a.integer();
            places_.addSlot(x, a.integer(), false);
        });
        // 0x75 (virtual +0x114, 0204a504 -> 0205a900): a tile's place in the
        // next accepted arrangement.
        context.bind(0x75, [this](CommandArguments& a) {
            const int tile = a.integer();
            places_.addTileAnswer(tile, a.integer());
        });
        // 0x76 (virtual +0x118, 0204ab38): the number of arrangements.
        context.bind(0x76, [this](CommandArguments& a) { places_.setAnswerCount(a.integer()); });
        // 0x39 (virtual +0xc4, 0204a5bc): the next tile's required turn in
        // degrees (at most 24).
        context.bind(0x39, [this](CommandArguments& a) {
            const float turn = a.real();
            if (targets_.size() < 0x18) {
                targets_.push_back(turn);
            }
        });
        // 0xb5 (virtual +0x11c, 0204a534): a button turning the tile on the
        // place of the same number (at most 24; sound 0xb1).
        context.bind(0xb5, [this](CommandArguments& a) {
            const int x = a.integer(), y = a.integer(), w = a.integer(), h = a.integer();
            if (turnButtons_.size() >= 0x18) {
                return;
            }
            Button b;
            b.setRect(x, y, w, h);
            b.sound = 0xb1;
            turnButtons_.push_back(b);
        });
        // 0xbe (virtual +0x38, 0204ab2c): no restart button.
        context.bind(0xbe, [this](CommandArguments&) { restartShown_ = false; });
        // 0xaa (virtual +0x1c, 0204ab40): 1 draws turned tiles turned.
        context.bind(0xaa, [this](CommandArguments& a) { turnedDrawing_ = a.integer() == 1; });
        // Loading the tiles' sprites holds the black screen (measured on
        // puzzle 112; a loading pause).
        kit.hold(kLoadFrames);
    }

    void draw(PuzzleScene& scene, bool interactive) override {
        // 0204a7f8.
        SceneKit& kit = scene.kit;
        TouchState& touch = scene.touch;
        scene.drawCommon(interactive);
        TouchFilter f = filter(touch, interactive);
        if (!interactive) {
            submit_.draw(kit);
            if (restartShown_) {
                restart_.draw(kit);
            }
        } else {
            for (auto& b : turnButtons_) {
                b.update(kit, touch);
            }
            submit_.update(kit, touch);
            if (restartShown_) {
                restart_.update(kit, touch);
            }
            // 0205a7f4: the press position in the tile's frame, 2 pixels up
            // and left of the sprite.
            const bool picked = places_.pick(f, [&](int i) {
                const auto& t = places_.tiles()[std::size_t(i)];
                const int left = t.x - 2, top = t.y - 2;
                return left <= touch.pressX && touch.pressX <= left + t.width &&
                       top <= touch.pressY && touch.pressY <= top + t.height;
            });
            if (picked) {
                kit.effect(scene.id() == 0x3f ? 0xd2 : 0xaf);
            }
        }
        // Tiles at rest in their order, then the dragged one.
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < int(places_.tiles().size()); ++i) {
                if (places_.tiles()[std::size_t(i)].dragging != (pass == 1)) {
                    continue;
                }
                drawTile(scene, i, f, interactive);
            }
        }
    }

    void play(PuzzleScene& scene) override {
        // 0204a988.
        SceneKit& kit = scene.kit;
        restore(scene.state()); // 0204a2a8
        scene.intro();
        for (;;) {
            scene.frame([&] { draw(scene, true); });
            for (int i = 0; i < int(turnButtons_.size()); ++i) {
                if (!turnButtons_[std::size_t(i)].clicked(kit, scene.touch)) {
                    continue;
                }
                // The tile lying on place i turns a quarter (back to 0 at
                // 360 degrees).
                int tile = 0;
                while (tile < int(places_.tiles().size()) &&
                       places_.tiles()[std::size_t(tile)].slot != i) {
                    ++tile;
                }
                if (tile < int(turns_.size())) {
                    float& turn = turns_[std::size_t(tile)];
                    turn = turn + 90.0f;
                    if (turn >= 360.0f) {
                        turn = 0;
                    }
                }
            }
            if (scene.hintClicked()) {
                scene.hintScreen();
            }
            if (scene.quitClicked() && scene.quitDialog()) {
                break;
            }
            if (submit_.clicked(kit, scene.touch)) {
                if (solved()) {
                    scene.markSolved();
                } else {
                    scene.markWrong();
                }
                break;
            }
            if (restartShown_ && restart_.clicked(kit, scene.touch)) {
                restart(); // 0204a334
            }
        }
        // 02049ebc (destructor): both screens to black, the arrangement kept.
        scene.fadeOut();
        save(scene.state()); // 0204a218
    }

private:
    static constexpr int kLoadFrames = 11;

    // The stylus as the tile pieces read it.
    static TouchFilter filter(TouchState& t, bool interactive) {
        TouchFilter f;
        if (interactive) {
            if (t.pressed && t.steady()) {
                f.state = 1;
            } else if (t.down && t.steady()) {
                f.state = 2;
            } else if (t.released) {
                f.state = 3;
            }
        }
        f.x = t.x;
        f.y = t.y;
        f.pressX = t.pressX;
        f.pressY = t.pressY;
        return f;
    }

    // 0205aa50 and 0205a98c: a tile follows the stylus or drops (sound 0xb0,
    // 0xd3 in puzzle 63), then is drawn, turned when the puzzle asks so.
    // Loading the tiles holds the black screen 11 frames (kLoadFrames,
    // measured 12 on puzzle 112 and 9 on 116).
    void drawTile(PuzzleScene& scene, int index, const TouchFilter& f, bool interactive) {
        SceneKit& kit = scene.kit;
        const auto& t = places_.tiles()[std::size_t(index)];
        if (t.x == -1 && t.y == -1 && !t.dragging) {
            return;
        }
        if (interactive && places_.step(index, f)) {
            // A dropped tile is not drawn in that frame (0205aa50 returns).
            kit.effect(scene.id() == 0x3f ? 0xd3 : 0xb0);
            return;
        }
        auto& s = sprites_[std::size_t(index)];
        const float turn = index < int(turns_.size()) ? turns_[std::size_t(index)] : 0.0f;
        if (turnedDrawing_ && turn != 0.0f) {
            s.x = t.x + (t.width >> 1);
            s.y = t.y + (t.height >> 1);
            kit.drawRotated(s, unsigned(int(turn * 65536.0f / 360.0f)) & 0xffff);
            return;
        }
        s.x = t.x;
        s.y = t.y;
        kit.draw(s);
    }

    // 0204a604: every required turn, then one of the arrangements.
    bool solved() const {
        for (std::size_t i = 0; i < targets_.size(); ++i) {
            const float turn = i < turns_.size() ? turns_[i] : 0.0f;
            if (turn != targets_[i]) {
                return false;
            }
        }
        return places_.solved();
    }

    void restart() {
        // 0204a334: every tile home, unturned.
        places_.reset();
        turns_.fill(0);
    }

    void save(PuzzleState& state) {
        auto& m = state.tiles;
        m.valid = true;
        for (int i = 0; i < int(places_.tiles().size()) && i < 24; ++i) {
            const auto& t = places_.tiles()[std::size_t(i)];
            m.x[std::size_t(i)] = t.x;
            m.y[std::size_t(i)] = t.y;
            m.slot[std::size_t(i)] = t.slot;
        }
        m.occupied = places_.occupancy();
    }

    void restore(PuzzleState& state) {
        const auto& m = state.tiles;
        if (!m.valid) {
            return;
        }
        for (int i = 0; i < int(places_.tiles().size()) && i < 24; ++i) {
            auto& t = places_.tile(i);
            t.x = m.x[std::size_t(i)];
            t.y = m.y[std::size_t(i)];
            t.slot = m.slot[std::size_t(i)];
        }
        places_.occupancy() = m.occupied;
    }

    SlotPuzzle places_;
    std::vector<SceneKit::SpriteObject> sprites_; // the pieces' +0x44
    std::vector<float> targets_;                  // +0x534, count +0x634
    std::array<float, 24> turns_{};               // +0xdc
    std::vector<Button> turnButtons_;             // +0x4d0, count +0x530
    Button submit_, restart_;                     // +0x74 / +0x78
    bool restartShown_ = true;                    // +0x685
    bool turnedDrawing_ = false;                  // +0x680
};

} // namespace

std::unique_ptr<Engine> createTile(PuzzleScene& scene) {
    return std::make_unique<Tile>(scene);
}

} // namespace picarat::adventure1
