// Playing one puzzle of the first adventure: the title card, the puzzle scene
// and the result (ARM9 02001044). See a1_puzzle.hpp.
//
// The title card (QuestionIntroObject, 0203c85c / 0203db80): after a pause
// in black, a panel with the word "puzzle" and the puzzle's number fades in on
// the lower screen and slides up to the upper screen, where the title
// appears under it. Unless the puzzle is already solved, the lower screen
// then shows what the puzzle is worth: after wrong answers the value counts
// down from the previous one, beside the full value and the player's total.
// A touch moves on.
//
// The result (QuestionEndObject, 0203e248 / 0203f1c4): the judgement
// pictures (shared with the second adventure, puzzles/judgment.hpp) end on
// "correct" or "wrong", which slides up to the upper screen. A newly solved
// puzzle's picarats then count into the total. The answer screen shows the
// puzzle's text for a right or wrong answer (with the answer picture above,
// if the puzzle has one). After a wrong answer the player picks: try again,
// look at the hints and try again, or stop for now.
#include "puzzles/a1_puzzle.hpp"
#include "puzzles/judgment.hpp"
#include "sound/a1_sound.hpp"
#include <cmath>

namespace picarat::adventure1 {

namespace {
constexpr std::uint8_t kSolved = 2, kFailure1 = 0x40, kFailure2 = 0x80;

// Digit d of a sprite of digits is animation d + 1.
void drawDigit(SceneKit& kit, SceneKit::SpriteObject& sprite, int digit, int x, int y) {
    kit.select(sprite, unsigned(digit + 1));
    sprite.x = x;
    sprite.y = y;
    kit.draw(sprite);
}
} // namespace

PuzzlePlay::PuzzlePlay(GraphicsAssets& assets, PresentFrame present, const Catalogue& catalogue,
                       const std::string& language, unsigned id, PuzzleState& state,
                       Adventure1Sound* sound)
    : kit(assets, std::move(present), nullptr, SceneKind::PuzzleCard), catalogue_(catalogue),
      id_(id), state_(state), sound_(sound) {
    kit.sceneSound = sound;
    kit.language = language;
    // This game's scenes show a frame's sprites and scroll with its text
    // (measured on the puzzle scene and the title card).
    kit.spriteLatency = false;
    kit.depthOrder = true;
    // The 3D camera the game sets up once (020040d0, 0209b2f0, 020041e0).
    kit.ds3D = SceneKit::Camera3D{dspipe::spriteCamera()};
}

PuzzleScene::Outcome PuzzlePlay::play() {
    // 02001044: title card, puzzle, result, again while the result asks
    // for another try; quitting skips the result.
    PuzzleScene::Outcome outcome;
    state_.retries = 0;
    state_.tiles.valid = false; // +0x720
    if (sound_) {
        sound_->rememberMusic(); // 02004f10
    }
    do {
        titleCard();
        PuzzleScene scene(kit, touch, catalogue_, id_, state_);
        scene.sound = sound_;
        outcome = scene.run();
        state_.retry = false;
        if (outcome != PuzzleScene::Outcome::Quit) {
            result(scene.resultLoadFrames());
        }
    } while (state_.retry && outcome != PuzzleScene::Outcome::Quit);
    state_.tiles.valid = false;
    return outcome;
}

void PuzzlePlay::titleCard() {
    const PuzzleInfo& info = *catalogue_.find(id_);
    const std::uint8_t progress = state_.puzzles[id_];
    const bool solved = progress & kSolved;
    // 0203c85c: the number of wrong answers so far (0..3, from two bits)
    // picks the value now and the one before (0203d1c0).
    const unsigned failures = (progress & kFailure1 ? 1 : 0) + (progress & kFailure2 ? 2 : 0);
    const auto& p = info.picarats;
    const int full = int(p[0]);
    const int now = failures == 0 ? int(p[0]) : failures == 1 ? int(p[1]) : int(p[2]);
    int shown = failures == 0   ? int(p[0])
                : failures == 1 ? int(p[0])
                : failures == 2 ? int(p[1])
                                : int(p[2]);
    state_.puzzles[id_] |= 1; // seen
    // 0200517c(0x3c): the music fades out over a second, meanwhile
    // 02003d64(0x3c): a pause in black before anything is set up.
    if (sound_) {
        sound_->stopMusic(0x3c);
    }
    kit.setBrightness(-16);
    for (int i = 0; i < 60; ++i) {
        kit.present();
    }
    // Measured: when the result asked for another try, loading the title
    // card takes 67 frames longer (the game waits on the card; puzzle 23,
    // research/runs/p23rh; which reads take the time is not established).
    if (state_.retries > 0) {
        kit.hold(67);
    }
    // The panel on both screens; the upper one scrolled out of view
    // (layer state +0x48 = -192).
    kit.lowerBackground = kit.localized("q_alt_sub_bg");
    kit.upperBackground = kit.localized("q_alt_sub_bg");
    kit.upperScrollY = -192;
    kit.lowerScrollY = 0;
    kit.clearText(SceneKit::Upper);
    kit.clearText(SceneKit::Lower);
    auto lowerDigits = kit.load("qintro_numbers.spr", 0, 0, 1);
    auto upperDigits = kit.load("qintro_numbers.sbj", 0, 0, 1);
    auto lowerLabel = kit.load("?/pazzle_mes.spr", 0x54, 0x20, 1);
    auto upperLabel = kit.load("?/pazzle_mes.sbj", 0x54, 0x20, 1);
    auto big = kit.load("picarat_number_big.spr", 0, 0, 1);
    auto small = kit.load("picarat_number_small.spr", 0, 0, 1);
    auto fullDigits = kit.load("picarat_get_number_small.spr", 0, 0, 1);
    // The constructor's end: the sounds of a puzzle's title card (02004b08
    // kind 5: the puzzle music and the effects) and voice group 1.
    if (sound_) {
        const auto wait = [this] {
            kit.present();
        };
        sound_->setupScene(5, wait);
        sound_->loadVoice(1, wait);
    }
    const int number = int(info.number);
    // 0203d5b8 / 0203d470: "puzzle" and the three digits, on the lower or
    // upper screen, moved by (dx, dy).
    auto panel = [&](SceneKit::SpriteObject& digits, SceneKit::SpriteObject& label, int dy) {
        drawDigit(kit, digits, number % 1000 / 100, 0x6e, dy + 0x35);
        drawDigit(kit, digits, number % 100 / 10, 0x8c, dy + 0x35);
        drawDigit(kit, digits, number % 10, 0xaa, dy + 0x35);
        label.x = 0x54;
        label.y = dy + 0x20;
        kit.draw(label);
    };
    // 0203d2e8: the title, centred at row 0x79 of the upper screen (none
    // for an entry without one, such as the match tutorial: measured).
    auto title = [&]() {
        kit.clearText(SceneKit::Upper);
        drawCentredLine(kit, SceneKit::Upper, 0x79, info.title);
    };
    // 0203d714: the value now (big digits), the full value and the total.
    auto worth = [&](bool open) {
        if (open) {
            if (full == 100) {
                drawDigit(kit, fullDigits, 1, 0x98, 0x4c);
                drawDigit(kit, fullDigits, 0, 0xb1, 0x4c);
                drawDigit(kit, fullDigits, 0, 0xca, 0x4c);
            } else {
                if (full > 9) {
                    drawDigit(kit, fullDigits, full % 100 / 10, 0x9b, 0x4c);
                }
                drawDigit(kit, fullDigits, full % 10, 0xb5, 0x4c);
            }
            if (shown > 99) {
                drawDigit(kit, big, shown % 1000 / 100, 0xf, 0x37);
            }
            if (shown > 9) {
                drawDigit(kit, big, shown % 100 / 10, 0x35, 0x37);
            }
            drawDigit(kit, big, shown % 10, 0x5b, 0x37);
        }
        const int score = int(state_.score);
        const int xs[] = {0x94, 0x9e, 0xa8, 0xb2, 0xbc};
        const int powers[] = {10000, 1000, 100, 10, 1};
        for (int i = 0; i < 5; ++i) {
            if (score >= powers[i] || i == 4) {
                drawDigit(kit, small, score / powers[i] % 10, xs[i], 0xaa);
            }
        }
    };
    // 0203db80.
    auto frame = [&](auto draw) {
        kit.poll();
        touch.update(kit.hostInput().down, kit.hostInput().x, kit.hostInput().y);
        draw();
        kit.present();
    };
    frame([&] { panel(lowerDigits, lowerLabel, 0); });
    kit.effect(0); // 020053ac(0)
    // 0200f7b4(1, 1): from black in steps of 1, the last frame shown.
    for (int b = -15; b <= 0; ++b) {
        kit.setBrightness(b);
        kit.present();
    }
    for (int i = 0; i < 0x16; ++i) {
        kit.present();
    }
    // 0203d234: the panel slides up by 16 pixels a frame.
    for (int i = 0; i <= 0xc0; i += 0x10) {
        frame([&] {
            kit.lowerScrollY = i;
            panel(lowerDigits, lowerLabel, -i);
            panel(upperDigits, upperLabel, 0xc0 - i);
            kit.upperScrollY = i - 0xc0;
            // 0203d2e8 is called with the panel's offset every step, but
            // the title only shows from the last step on (measured; why
            // the earlier positions stay invisible is not established).
            if (i == 0xc0) {
                title();
            }
        });
    }
    kit.setLowerBrightness(-16);
    kit.lowerScrollY = 0;
    kit.upperScrollY = 0;
    frame([&] {
        title();
        panel(upperDigits, upperLabel, 0);
    });
    frame([&] { panel(upperDigits, upperLabel, 0); });
    for (int i = 0; i < 0x14; ++i) {
        kit.present();
    }
    state_.worthShown = !solved; // +0x644
    if (!solved) {
        kit.lowerBackground = kit.localized("?/picarat_bg");
        frame([&] {
            panel(upperDigits, upperLabel, 0);
            worth(true);
        });
        // Three more frames pass before the fade starts (measured; loading
        // the picture, 02009d80, hypothesis).
        for (int i = 0; i < 3; ++i) {
            kit.present();
        }
        // 0200fb90(1): the lower screen from black.
        for (int b = -15; b <= 0; ++b) {
            kit.setLowerBrightness(b);
            frame([&] {
                panel(upperDigits, upperLabel, 0);
                worth(true);
            });
        }
        // The value counts down to the one now, until a touch: one per two
        // frames below 15 to go, one per frame below 25, else two
        // (the difference taken at the start). The looped effect 0x13
        // meanwhile (0200552c), stopped by any step that finds the value
        // reached (02005594) and after the loop.
        const int distance = shown - now;
        if (failures < 3 && sound_) {
            sound_->startLoopedEffect(0x13);
        }
        for (;;) {
            if (failures < 3) {
                const int steps = distance < 15 ? 1 : distance < 25 ? 1 : 2;
                for (int s = 0; s < steps; ++s) {
                    if (now < shown) {
                        --shown;
                    } else if (sound_) {
                        sound_->stopLoopedEffect();
                    }
                }
                if (distance < 15) {
                    kit.present(); // 02003d50: one more frame
                }
            }
            frame([&] {
                panel(upperDigits, upperLabel, 0);
                worth(true);
            });
            // (The original also leaves on a key press after the keys were
            // seen up, 02003ae8.)
            if (touch.released) {
                break;
            }
        }
        if (failures < 3 && sound_) {
            sound_->stopLoopedEffect();
        }
        shown = now;
        frame([&] {
            panel(upperDigits, upperLabel, 0);
            worth(true);
        });
    }
    // 0200fce8(1, 1): both screens to black; then the puzzle music
    // (0200530c(3)).
    for (int b = -1; b >= -16; --b) {
        kit.setBrightness(b);
        kit.present();
    }
    if (sound_) {
        sound_->playMusic(3);
    }
    kit.clearText(SceneKit::Upper);
    kit.clearText(SceneKit::Lower);
    kit.lowerBackground.clear();
    kit.upperBackground.clear();
}

namespace {
// ARM9 0200bde0's tables: the picture numbers for a right and a wrong answer
// and the fade frames, 32-bit each (0 = pause, -1 = end), turned into the
// rows the shared judgment runs on ({right, wrong, frames}, 16-bit).
Bytes judgmentTable(const std::filesystem::path& pack) {
    constexpr std::uint32_t base = 0x02000000, right = 0x020d2488, wrong = 0x020d2520,
                            frames = 0x020d24d4;
    const Bytes arm9 = readFile(pack / "code/arm9.bin");
    Bytes table;
    for (std::uint32_t i = 0; i < 19; ++i) {
        for (std::uint32_t address : {right, wrong, frames}) {
            const std::uint32_t v = read32(arm9, address - base + 4 * i);
            table.push_back(std::uint8_t(v));
            table.push_back(std::uint8_t(v >> 8));
        }
    }
    return table;
}

// The first adventure's judgment host: the pictures go to both screens'
// backgrounds (the upper one scrolled out of view), brightness is the lower
// screen's (0x0400006c).
struct JudgmentScreens final : JudgmentHost {
    SceneKit& kit;

    explicit JudgmentScreens(SceneKit& k) : kit(k) {}

    bool aborted() override { return false; }

    void image(const std::string& path, bool) override {
        // Loading a picture from the card holds the previous one on screen
        // for two frames (measured, puzzle 23; a loading pause).
        kit.present();
        kit.present();
        kit.lowerBackground = kit.localized(path);
        kit.upperBackground = kit.localized(path);
    }

    void brightness(int value) override { kit.setLowerBrightness(value); }

    void upperBrightness(int value) override { kit.setUpperBrightness(value); }

    void present() override { kit.present(); }

    void wait(int frames) override {
        for (int i = 0; i < std::max(1, frames); ++i) {
            kit.present();
        }
    }

    void slide(int, int) override {}

    void sound(unsigned id) override { kit.effect(int(id)); }

    void voice(unsigned) override {}
};
} // namespace

void PuzzlePlay::voiceLine(std::uint32_t table, std::uint32_t random) {
    // 0203ee2c / 0203ee98: entry random % 3 (+3 for the second judgement
    // set) of a byte table of voice groups in the program; load it
    // (02004f3c) and say its line (02005380).
    const int r = int(random);
    const unsigned index = unsigned(r % 3) + (state_.secondJudge ? 3u : 0u);
    const Bytes arm9 = readFile(kit.assets.root / "code/arm9.bin");
    sound_->loadVoice(arm9.at(table - 0x02000000 + index), [&] { kit.present(); });
    sound_->voice();
}

void PuzzlePlay::result(int loadFrames) {
    const PuzzleInfo& info = *catalogue_.find(id_);
    std::uint8_t& progress = state_.puzzles[id_];
    const bool solved = state_.solved;
    // 0203e248: the wrong answers so far (before this one) pick the value;
    // a wrong answer counts one more (bits 0x40/0x80: 1, 2, 3 = both).
    const unsigned failures = (progress & kFailure1 ? 1 : 0) + (progress & kFailure2 ? 2 : 0);
    if (!(progress & kSolved) && !solved) {
        const unsigned next = failures + 1;
        progress = std::uint8_t((progress & ~(kFailure1 | kFailure2)) |
                                (next == 1   ? kFailure1
                                 : next == 2 ? kFailure2
                                             : kFailure1 | kFailure2));
    }
    // 0203ed88 (picarats from pscript, by the failures before this answer).
    const int earned = int(info.picarats[std::min(failures, 2u)]);
    kit.clearText(SceneKit::Upper);
    kit.clearText(SceneKit::Lower);
    kit.upperBackground = solved ? "qend_o" : "qend_x";
    kit.lowerBackground.clear();
    kit.upperScrollY = -192;
    kit.lowerScrollY = 0;
    auto prompt = kit.load("?/qend_touch.spr", 0, 0, 1);
    kit.select(prompt, "touch");
    prompt.x = (0x100 - kit.frameWidth(prompt)) >> 1;
    prompt.y = (0xc0 - kit.frameHeight(prompt)) >> 1;
    auto earnedDigits = kit.load("picarat_get_number_small.spr", 0, 0, 1);
    auto totalDigits = kit.load("picarat_get_number_big.spr", 0, 0, 1);
    auto numbers = kit.load("q_numbers.spr", 0, 0, 1);
    auto numbers2 = kit.load("q_numbers2.spr", 0, 0, 1);
    auto frame = [&](auto draw) {
        kit.poll();
        touch.update(kit.hostInput().down, kit.hostInput().x, kit.hostInput().y);
        draw();
        kit.present();
    };
    auto waitFrames = [&](int n) {
        for (int i = 0; i < n; ++i) {
            kit.present();
        }
    };
    // The constructor loads the result's sounds (02004b08: 0xa right, 0xb
    // wrong), which first waits, frame by frame, until the puzzle's
    // effects have ended (174 frames after "Submit", measured on puzzles 1
    // and 23); then a spoken line (0203ee2c: one of three voice groups at
    // random, the second judgement set's with command 0x67). Without sound
    // the measured wait (Engine::resultLoadFrames) stands in.
    // After the wait, measured on puzzles 23 and 13 (ROM frames 866..904 /
    // 1763..1800): 7 frames loading the effect group, 6 loading
    // the voice and pictures, 20 frames that run, 6 more loading (loading
    // pauses: the main loop, and with it the sound object's frame, stands
    // still). After the match tutorial the cartridge runs 19 and loads 3
    // (frames 1398..1425 of `p0`; not modelled).
    const std::uint32_t lineRandom = gameRandom(state_.random);
    if (sound_) {
        sound_->setupScene(solved ? 0xa : 0xb, [&] { kit.present(); });
        kit.hold(7);
        voiceLine(0x020d6490, lineRandom);
        kit.hold(6);
        waitFrames(20);
        kit.hold(6);
    } else {
        waitFrames(loadFrames);
    }
    // 0203f1c4: the music down to nothing over 105 frames (020052f4).
    if (sound_) {
        sound_->moveMusicVolume(0, 0x69);
    }
    frame([] {});
    for (int b = -15; b <= 0; ++b) { // 0200f7b4(1, 1)
        kit.setBrightness(b);
        kit.present();
    }
    kit.effect(0x1f);
    // 0200bde0: the pictures; mode bit 1 (the second set) when state +0x4f0
    // is 1 (command 0x67).
    JudgmentScreens screens(kit);
    JudgmentPictures pictures;
    pictures.path = [](char set, unsigned index, bool verdict) {
        return std::string(verdict ? "?/" : "") + "judge_" + set + std::to_string(index) + "_bg";
    };
    pictures.correctSound = 0x20;
    pictures.wrongSound = 0x21;
    playJudgmentPictures(judgmentTable(kit.assets.root),
                         (solved ? 0u : 1u) | (state_.secondJudge ? 2u : 0u), pictures, screens);
    waitFrames(0x16);
    // 0203edb0: the verdict slides up to the upper screen.
    for (int i = 0; i <= 0xc0; i += 0x10) {
        frame([&] {
            kit.lowerScrollY = i;
            kit.upperScrollY = i - 0xc0;
        });
    }
    waitFrames(0x25);
    // 0203ee98: a spoken line, one of three at random (020b31f8), from the
    // right or the wrong answer's groups.
    const std::uint32_t spoken = gameRandom(state_.random);
    if (sound_) {
        voiceLine(solved ? 0x020d6498 : 0x020d64a0, spoken);
    }
    waitFrames(0x25);
    kit.setLowerBrightness(-16);
    kit.lowerScrollY = 0;
    kit.upperScrollY = 0;
    kit.lowerBackground.clear();
    // 0203ef14: the earned picarats (two digits) and the total.
    auto drawWorth = [&](int left, int total) {
        if (left > 9) {
            drawDigit(kit, earnedDigits, left % 100 / 10, 0x68, 0x2a);
        }
        drawDigit(kit, earnedDigits, left % 10, 0x82, 0x2a);
        const int xs[] = {0x2c, 0x4b, 0x6a, 0x89, 0xa8};
        const int powers[] = {10000, 1000, 100, 10, 1};
        for (int i = 0; i < 5; ++i) {
            if (total >= powers[i] || i == 4) {
                drawDigit(kit, totalDigits, total / powers[i] % 10, xs[i], 0x80);
            }
        }
    };
    if (solved && state_.worthShown) {
        int left = earned;
        int total = int(state_.score);
        state_.score += std::uint32_t(earned);
        kit.lowerBackground = kit.localized("?/picarat_get_bg");
        frame([&] { drawWorth(left, total); });
        for (int b = -15; b <= 0; ++b) { // 0200fb90(1)
            kit.setLowerBrightness(b);
            frame([&] { drawWorth(left, total); });
        }
        if (sound_) {
            sound_->startLoopedEffect(0x13); // 0200552c
        }
        for (;;) {
            const int steps = earned < 25 ? 1 : 2;
            for (int i = 0; i < steps; ++i) {
                if (left > 0) {
                    --left;
                    ++total;
                }
            }
            if (earned < 15) {
                kit.present();
            }
            frame([&] { drawWorth(left, total); });
            if (touch.released) {
                total += left;
                left = 0;
                break;
            }
            if (left == 0) {
                break;
            }
        }
        if (sound_) {
            sound_->stopLoopedEffect(); // 02005594
        }
        do {
            frame([&] { drawWorth(left, total); });
        } while (!touch.released);
        // 0200ff00 (lower) without an answer picture, else both.
        for (int b = -1; b >= -16; --b) {
            if (state_.answerPicture) {
                kit.setBrightness(b);
            } else {
                kit.setLowerBrightness(b);
            }
            kit.present();
        }
    } else if (solved && state_.answerPicture) {
        for (int b = -1; b >= -16; --b) { // 0200fe78(1): the upper screen
            kit.setUpperBrightness(b);
            kit.present();
        }
    }
    // The answer screen.
    const std::string number = std::to_string(id_);
    if (!solved) {
        if (state_.wrongPicture) {
            kit.upperBackground = "q" + number + "f_bg";
        }
        kit.lowerBackground = "qend_awrong";
    } else {
        if (state_.answerPicture) {
            kit.upperBackground = kit.localized(std::string(state_.answerLocalized ? "?/" : "") +
                                                "q" + number + "a_bg");
        }
        kit.lowerBackground = "qend_acorrect";
    }
    // 020536b4: the header on the lower screen (number and coins).
    auto header = [&] {
        const int n = int(info.number);
        drawDigit(kit, numbers, n % 1000 / 100, 0x19, 6);
        drawDigit(kit, numbers, n % 100 / 10, 0x20, 6);
        drawDigit(kit, numbers, n % 10, 0x27, 6);
        const int coins = state_.coins;
        if (coins > 99) {
            drawDigit(kit, numbers2, coins % 1000 / 100, 0xe3, 6);
        }
        if (coins > 9) {
            drawDigit(kit, numbers2, coins % 100 / 10, 0xea, 6);
        }
        drawDigit(kit, numbers2, coins % 10, 0xf1, 6);
    };
    kit.clearText(SceneKit::Upper);
    kit.clearText(SceneKit::Lower);
    // Loading the pictures: three frames (measured, puzzle 23).
    waitFrames(3);
    kit.poll();
    if (solved) {
        header();
    }
    kit.present();
    if (solved && state_.answerPicture) {
        waitFrames(0x1e);
    }
    for (int b = -15; b <= 0; ++b) { // 0200f7b4(1, 1)
        kit.setBrightness(b);
        kit.present();
    }
    // The music back up over 60 frames (02005144); the answer text, typed
    // out two characters a frame on the lower screen (020535cc), until a
    // touch.
    if (sound_) {
        sound_->fadeInMusic();
    }
    const std::string text = catalogue_.text(id_, solved ? "c" : "f");
    int typingSounds = 0;
    for (std::size_t typed = 0; typed < text.size(); typed += 2) {
        bool stop = false;
        // The typing sound holds frames as in the question.
        typingTick(sound_, text, typed, typingSounds, [&] { kit.present(); });
        frame([&] {
            if (solved) {
                header();
            }
            drawTextBox(kit, SceneKit::Lower, 4, 0x17, text, typed, typed + 2);
        });
        stop = touch.released;
        if (stop) {
            break;
        }
    }
    kit.clearText(SceneKit::Lower);
    frame([&] {
        if (solved) {
            header();
        }
        drawTextBox(kit, SceneKit::Lower, 4, 0x17, text); // 0205358c
    });
    // The pulsing prompt: unlike the question's intro, the alpha (of 31)
    // is int(15 sin(angle)) itself, so the prompt fades out between pulses
    // (it is drawn only above 1).
    float angle = 0;
    int shown = 0;
    for (;;) {
        frame([&] {
            if (solved) {
                header();
            }
            const int alpha = int(15.0f * float(std::sin(double(angle))));
            angle += 0.05f;
            if (shown + 1 <= 30 || alpha <= 1) {
                shown += 2;
            } else {
                kit.draw(prompt, alpha);
                shown += 1;
            }
        });
        if (touch.released) {
            break;
        }
    }
    kit.effect(3);
    for (int b = -1; b >= -16; --b) { // wrong: 0200ff00 (lower), right: both
        if (solved) {
            kit.setBrightness(b);
        } else {
            kit.setLowerBrightness(b);
        }
        kit.present();
    }
    state_.retry = false;
    state_.answerPicture = 0;
    state_.wrongPicture = 0;
    state_.answerLocalized = false;
    kit.clearText(SceneKit::Lower);
    kit.clearText(SceneKit::Upper);
    if (solved) {
        state_.retries = 0;
        return;
    }
    // (In the story, the new game's first puzzle, 24, is tried again
    // without asking; not part of playing a puzzle on its own.)
    // The retry menu: "1" try again (0x34, 0x1b), "2" the hints first
    // (0x34, 0x4e), "3" stop for now (0x34, 0x81), on ?/ato_btn.spr.
    kit.lowerBackground = "qend_retry";
    Button again, hints, stop;
    for (auto* b : {&again, &hints, &stop}) {
        const int y = b == &again ? 0x1b : b == &hints ? 0x4e : 0x81;
        b->setSprite(kit, 0x34, y, "?/ato_btn.spr", b == &again ? "1" : b == &hints ? "2" : "3");
        b->visibleIdle = true;
        b->pressDx = b->pressDy = 1;
        b->sound = 3;
    }
    auto drawMenu = [&] {
        again.update(kit, touch);
        hints.update(kit, touch);
        stop.update(kit, touch);
    };
    frame(drawMenu);
    for (int b = -15; b <= 0; ++b) { // 0200fb90(1)
        kit.setLowerBrightness(b);
        frame(drawMenu);
    }
    bool fadeMusic = false;
    for (;;) {
        frame(drawMenu);
        if (again.clicked(kit, touch)) {
            ++state_.retries;
            state_.retry = true;
            fadeMusic = true;
            break;
        }
        if (stop.clicked(kit, touch)) {
            state_.retries = 0;
            state_.retry = false;
            break;
        }
        if (hints.clicked(kit, touch)) {
            // 0203fa5c: a try more, the retry flag, the lower screen to
            // black (0200ff00(1)), then the hint view on its own object
            // (0205fb58) under the result's upper picture. Leaving fades
            // the lower screen out again and shows one more frame; the
            // music fades before both screens do (02005114, 0203fb08).
            ++state_.retries;
            state_.retry = true;
            adventure1::fadeOut(kit, Screens::Lower);
            HintView view(kit, touch, catalogue_, state_, id_);
            auto drawView = [&] {
                view.drawControls();
                view.drawBack();
            };
            kit.hold(1); // measured: setting the object up (0205fb10)
            view.open();
            frame(drawView);
            adventure1::fadeIn(kit, Screens::Lower); // 0200fb90(1)
            do {
                frame(drawView);
            } while (view.step());
            adventure1::fadeOut(kit, Screens::Lower);
            view.close();
            kit.clearText(SceneKit::Lower);
            kit.lowerBackground.clear();
            frame([] {});
            if (sound_) {
                sound_->fadeOutMusic();
            }
            break;
        }
    }
    for (int b = -1; b >= -16; --b) { // 0200fce8(1, 1)
        kit.setBrightness(b);
        kit.present();
    }
    // Trying again fades the music out after the screens (02005114).
    if (fadeMusic && sound_) {
        sound_->fadeOutMusic();
    }
    kit.lowerBackground.clear();
    kit.upperBackground.clear();
}

} // namespace picarat::adventure1
