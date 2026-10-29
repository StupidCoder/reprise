// The first adventure's trunk. See a1_trunk.hpp.
#include "menus/a1_trunk.hpp"
#include "menus/a1_save.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"
#include <algorithm>

namespace picarat::adventure1 {

namespace {
// 020d31b8: the first entry of the story memo's index page; kept while the
// game runs (-1: start on the page of the current chapter).
int memoFirst = -1;

constexpr std::uint64_t kWalkerStops = 0x57e40; // 100 hours in seconds (0x57e3f < t)

// Flags that open compartments (0201aea4): the robot dog, the painting,
// the hotel rooms.
constexpr int kDogFlag = 500, kPaintingFlag = 501, kHotelFlag = 502;

// The story memo: entry n (1..64) unread (0201b97c).
bool unread(const GameState& state, int n) {
    return n >= 0 && n < 64 && state.memoUnread[std::size_t(n)];
}

// The trunk's "new" mark of the memo goes once no entry is unread.
void forgetReadMemo(GameState& state) {
    if (state.memoUnread.none()) {
        state.newMemo = false;
    }
}

bool hasItem(const GameState& state, int item) { // 02019bb0
    return std::find(state.items.begin(), state.items.end(), std::uint8_t(item)) !=
           state.items.end();
}
} // namespace

Trunk::Trunk(Story& s) : story(s), kit(s.kit) {
    GameState& state = story.state;
    if (memoFirst < 0) {
        memoFirst = (std::max(1, int(state.chapter)) - 1) / 6 * 6;
    }
    // The upper screen: menutop2 with the status.
    kit.upperBackground = kit.localized("?/menutop2");
    coinsDigits_ = kit.load("status_number.sbj", 0, 0, 1);
    picaratDigits_ = kit.load("status_number2.sbj", 0, 0, 1);
    // The walking figure; its sprite name is the program's (02021b5c).
    walker_ = kit.load(story.programString(0x02021b5c), 0, 0, 1);
    kit.select(walker_, state.playTime() >= kWalkerStops ? 2u : 1u);
    walker_.x = 0x14;
    walker_.y = 0x99;
    // The medal for the hint coins: more than 30, 50, 80, 100.
    const int coins = state.puzzle.coins;
    const int medal = coins > 100 ? 5 : coins > 0x50 ? 4 : coins > 0x32 ? 3 : coins > 0x1e ? 2 : 1;
    medal_ = kit.load("medal_" + std::to_string(medal) + ".sbj", 0x6d, 0x4e, 1);
    medal_.x = 0x6d;
    medal_.y = 0x4e;

    auto button = [&](Button& b, int x, int y, const std::string& sprite, int sound,
                      const char* idle = "gfx", const char* pressed = "pressed") {
        b.setSprite(kit, x, y, sprite, idle);
        b.visibleIdle = true;
        b.setPressedAnimation(kit, pressed);
        b.sound = sound;
    };
    button(memo_, 10, 0x11, "?/mm_btn_memo.spr", 3);
    button(save_, 0xba, 0xd, "?/mm_btn_save.spr", 3);
    button(index_, 0x82, 0x1c, "?/mm_btn_jiten.spr", 3);
    button(close_, 0xb9, 0x9f, "?/mm_btn_tojiru.spr", 0x16);
    button(mysteries_, 0x49, 0xd, "?/mm_btn_chart.spr", 3);
    // A compartment whose collection is complete shows its "fix" picture:
    // the hotel (0201b664), the painting (0201b578), the robot dog
    // (0201b1e0).
    const bool hotelDone = state.hotelComplete();
    const bool paintingDone = state.paintingComplete();
    const bool dogDone = state.dogComplete();
    button(hotel_, 100, 0x56, hotelDone ? "?/mm_btn_room_fix.spr" : "?/mm_btn_room.spr", 3);
    button(painting_, 0xa6, 0x5a,
           paintingDone ? "?/mm_btn_picture_fix.spr" : "?/mm_btn_picture.spr", 3);
    button(dog_, 0x27, 0x54, dogDone ? "?/mm_btn_parts_fix.spr" : "?/mm_btn_parts.spr", 3);
    for (int i = 0; i < 3; ++i) {
        items_[i] = kit.load("item_" + std::to_string(i + 1) + ".spr", 0x89, 0x9d, 1);
        items_[i].x = 0x89;
        items_[i].y = 0x9d;
    }
    memoIcon_ = kit.load("?/story_gfx.spr", 0, 0, 1);
    kit.select(memoIcon_, "hat");
    bagTitle_ = kit.load("?/bag_mes.spr", 9, 0x9b, 1);
    bagTitle_.x = 9;
    bagTitle_.y = 0x9b;
    window_ = kit.load("prize_window.spr", 0x18, 0x38, 1);
    kit.select(window_, "window");
    window_.x = 0x18;
    window_.y = 0x38;
    button(memoClose_, 0xbb, 0xa4, "?/story_close_buttons.spr", 0x15, "close1", "close2");
    button(memoNext_, 0x66, 0xa8, "?/story_page_buttons.spr", 0x14, "next1", "next2");
    button(memoBack_, 8, 0xa8, "?/story_page_buttons.spr", 0x14, "back1", "back2");
    button(memoReturn_, 0xbb, 0xa4, "?/story_close_buttons.spr", 4, "return1", "return2");
    newMark_ = kit.load("?/new_button.spr", 0, 0, 1);
    pageDigits_ = kit.load("artmode_num.spr", 0, 0, 1);
    for (int k = 0; k < 6; ++k) {
        entries_[k].setRect(8, 0x1d + 0x16 * k, 0xf0, 0x14);
    }
    kit.lowerPriority = std::array<int, 4>{1, 2, 0, 3};
    setBackground();
    if (story.sound) {
        story.sound->setupScene(3, [this] { story.stall(1); });
    }
}

void Trunk::finish() {
    // 02021c14: the room plays no entry sound, and the return scene is 4.
    story.state.enterSound = -1;
    story.state.returnScene = 4;
    kit.lowerPriority = std::array<int, 4>{2, 0, 1, 3};
    kit.upperBackground.clear();
    kit.lowerBackground.clear();
    kit.lowerBackdrop.reset();
    kit.clearText(SceneKit::Lower);
    kit.clearText(SceneKit::Upper);
}

void Trunk::setBackground() {
    kit.lowerBackground = mode_ == 0 ? "new_bag" : "story_memo_index";
    // 0200bd78(0xfc, 0xf7, 0xd9) writes the backdrop, but the picture's
    // palette, loaded at the next VBlank, overwrites colour 0 (measured: the
    // trunk shows new_bag's colour 0 where BG3, behind the text, is
    // transparent).
    if (const auto* tile = kit.assets.backgroundTile(kit.lowerBackground)) {
        kit.lowerBackdrop = std::uint16_t(tile->pixels[0] | 0x8000);
    }
}

void Trunk::drawCount(SceneKit::SpriteObject& digits, int x, int y, int value, int step,
                      int width) {
    // 020227f4 / 02022914 / 02022490: digit k from the right is drawn when
    // the number reaches 10^k (the last always).
    int power = 1;
    for (int k = 1; k < width; ++k) {
        power *= 10;
    }
    for (int k = width - 1, at = x; k >= 0; --k, at += step, power /= 10) {
        if (k > 0 && value < power) {
            continue;
        }
        kit.select(digits, unsigned(value / power % 10 + 1));
        digits.x = at;
        digits.y = y;
        kit.draw(digits);
    }
}

void Trunk::drawStatus() {
    GameState& state = story.state;
    // 0202267c: the play time, hours (modulo 100) and minutes.
    const auto seconds = state.playTime();
    const int hours = int(seconds / 3600), minutes = int(seconds / 60);
    auto digit = [&](int value, int x) {
        kit.select(coinsDigits_, unsigned(value + 1));
        coinsDigits_.x = x;
        coinsDigits_.y = 0x82;
        kit.draw(coinsDigits_);
    };
    digit(hours % 100 / 10, 0x9d);
    digit(hours % 10, 0xa5);
    digit(minutes % 60 / 10, 0xc6);
    digit(minutes % 10, 0xce);
    // 02022a40: puzzles found and solved, hint coins, picarats, the medal,
    // the figure, the place's name (0200dd50: the upper text layer, black).
    drawCount(picaratDigits_, 0x1d, 0x73, state.seenCount(), 0x12, 3);
    drawCount(picaratDigits_, 0x1d, 0x2d, state.solvedCount(), 0x12, 3);
    drawCount(coinsDigits_, 0xc1, 0x56, state.puzzle.coins, 10, 3);
    drawCount(picaratDigits_, 0x78, 0x20, int(state.puzzle.score), 0x12, 5);
    kit.draw(medal_);
    if (seconds >= kWalkerStops) {
        kit.select(walker_, 2u);
    }
    kit.draw(walker_);
    drawCentredLine(kit, SceneKit::Upper, 0xa1, state.place, 0xa6);
}

void Trunk::draw() {
    // 02022c64.
    GameState& state = story.state;
    if (mode_ == 0) {
        save_.update(kit, story.touch);
        mysteries_.update(kit, story.touch);
        auto mark = [&](bool on, int x, int y) {
            if (on) {
                newMark_.x = x;
                newMark_.y = y;
                kit.draw(newMark_);
            }
        };
        mark(state.newMystery, 0x54, 0x29);
        close_.update(kit, story.touch);
        memo_.update(kit, story.touch);
        index_.update(kit, story.touch);
        if (state.flag(kHotelFlag)) {
            hotel_.update(kit, story.touch);
        }
        if (state.flag(kPaintingFlag)) {
            painting_.update(kit, story.touch);
        }
        if (state.flag(kDogFlag)) {
            dog_.update(kit, story.touch);
        }
        mark(state.newGizmo, 0x34, 0x70);
        mark(state.newMemo, 0x18, 0x37);
        mark(state.newFurniture, 0x74, 0x77);
        mark(state.newPainting, 0xb5, 0x6f);
        for (int i = 0; i < 3; ++i) {
            if (hasItem(state, i + 1)) {
                kit.draw(items_[i]);
            }
        }
        kit.draw(bagTitle_);
    }
    drawStatus();
}

void Trunk::run() {
    // 02023818.
    story.marker("trunk-run");
    forgetReadMemo(story.state);
    story.poll();
    // 0200bd78(0xfc, 0xf7, 0xd9): the lower backdrop colour (not kept by
    // the kit; the pictures cover the screen).
    draw();
    story.present();
    fadeIn(kit, Screens::Both); // 0200f7b4
    for (;;) {
        bool done = false;
        if (mode_ == 0) {
            done = mainPage();
        } else {
            memoIndex();
        }
        forgetReadMemo(story.state);
        if (done) {
            break;
        }
    }
    fadeOut(kit, Screens::Both); // 0200fce8
    story.state.reward = false;
}

bool Trunk::mainPage() {
    // 02023950: priorities 3D 1, text 3, BG2 0, BG3 2.
    GameState& state = story.state;
    kit.lowerPriority = std::array<int, 4>{1, 3, 0, 2};
    for (;;) {
        story.poll();
        draw();
        story.present();
        if (close_.clicked(kit, story.touch)) {
            state.scene = scene::Room;
            return true;
        }
        if (state.flag(kHotelFlag) && hotel_.clicked(kit, story.touch)) {
            state.scene = scene::HotelRoom;
            state.returnScene = scene::TrunkMenu;
            return true;
        }
        if (mysteries_.clicked(kit, story.touch)) {
            state.scene = scene::Mysteries;
            state.returnScene = scene::TrunkMenu;
            return true;
        }
        if (state.flag(kPaintingFlag) && painting_.clicked(kit, story.touch)) {
            state.scene = scene::Painting;
            state.returnScene = scene::TrunkMenu;
            return true;
        }
        if (state.flag(kDogFlag) && dog_.clicked(kit, story.touch)) {
            state.scene = scene::RobotDog;
            state.returnScene = scene::TrunkMenu;
            return true;
        }
        kit.clearText(SceneKit::Lower); // 0200d8bc
        if (index_.clicked(kit, story.touch)) {
            state.scene = scene::PuzzleIndex;
            return true;
        }
        if (memo_.clicked(kit, story.touch)) {
            mode_ = 1;
            fadeOut(kit, Screens::Lower, 1);
            setBackground();
            return false;
        }
        if (save_.clicked(kit, story.touch)) {
            SaveScreen screen(story, SaveScreen::From::Trunk, window_);
            screen.drawOwner = [this] {
                drawStatus();
            };
            screen.run();
            setBackground();
            kit.lowerPriority = std::array<int, 4>{1, 3, 0, 2};
        }
    }
}

void Trunk::drawEntries(int first, bool texts) {
    // 02023bfc: the page number (1-based, one digit).
    pageDigits_.x = 0xe3;
    pageDigits_.y = 0xf;
    kit.select(pageDigits_, unsigned((first / 6 + 1) % 10 + 1));
    kit.draw(pageDigits_);
    // 02023c68: up to six entries of the chapters reached, unread ones
    // marked, with their titles (storytext t_<n>.txt) at x 0x26.
    if (texts) {
        kit.clearText(SceneKit::Lower);
    }
    int y = 0x1f;
    for (int k = 0; k < 6; ++k) {
        const int n = first + k + 1;
        if (n > story.state.chapter) {
            continue;
        }
        kit.select(memoIcon_, unread(story.state, n) ? 2u : 1u);
        memoIcon_.x = 10;
        memoIcon_.y = y;
        kit.draw(memoIcon_);
        if (texts) {
            std::string title =
                story.text("storytext", "storytext", "t_" + std::to_string(n) + ".txt");
            if (title.empty()) {
                title = "NO TITLE : " + std::to_string(n);
            }
            drawCentredText(kit, SceneKit::Lower, y + 3, title, false, 0, -0x26);
        }
        y += 0x16;
    }
}

void Trunk::memoIndex() {
    // 02023e00: priorities 3D 1, text 0, BG2 2, BG3 3.
    forgetReadMemo(story.state);
    int first = memoFirst;
    kit.lowerPriority = std::array<int, 4>{1, 0, 2, 3};
    story.poll();
    memoClose_.update(kit, story.touch);
    memoBack_.update(kit, story.touch);
    memoNext_.update(kit, story.touch);
    draw();
    drawEntries(first, true);
    story.present();
    if (story.sound) {
        story.sound->setupScene(0xf, [this] { story.stall(1); });
    }
    fadeIn(kit, Screens::Lower, 1);
    bool texts = false;
    for (;;) {
        story.poll();
        memoClose_.update(kit, story.touch);
        memoBack_.update(kit, story.touch);
        memoNext_.update(kit, story.touch);
        for (auto& entry : entries_) {
            entry.update(kit, story.touch);
        }
        draw();
        drawEntries(first, texts);
        story.present();
        texts = false;
        if (memoClose_.clicked(kit, story.touch)) {
            memoFirst = first;
            mode_ = 0;
            kit.clearText(SceneKit::Lower);
            setBackground();
            fadeOut(kit, Screens::Lower, 1);
            if (story.sound) {
                story.sound->setupScene(3, [this] { story.stall(1); });
            }
            kit.lowerPriority = std::array<int, 4>{1, 3, 0, 2};
            story.poll();
            draw();
            story.present();
            fadeIn(kit, Screens::Lower, 1);
            return;
        }
        if (memoNext_.clicked(kit, story.touch)) {
            first += 6;
            if (first > 0x2f) {
                first = 0;
            }
            texts = true;
        }
        if (memoBack_.clicked(kit, story.touch)) {
            first -= 6;
            if (first < 0) {
                first = 0x2a;
            }
            texts = true;
        }
        for (int k = 0; k < 6; ++k) {
            const int n = first + k + 1;
            if (n <= story.state.chapter && entries_[k].clicked(kit, story.touch)) {
                if (story.sound) {
                    story.sound->effect(3);
                }
                memoPage(n);
                setBackground();
                texts = true;
                break;
            }
        }
    }
}

void Trunk::memoPage(int entry) {
    // 02024178: the entry is read now; its text (storytext d_<n>.txt) on
    // story_memo_bg until "return".
    story.state.memoUnread[std::size_t(entry)] = false; // 0201b9c0(state, n, 0)
    forgetReadMemo(story.state);
    kit.lowerBackground = "story_memo_bg";
    std::string text = story.text("storytext", "storytext", "d_" + std::to_string(entry) + ".txt");
    if (text.empty()) {
        text = "NO DATA : " + std::to_string(entry);
    }
    kit.clearText(SceneKit::Lower);
    drawTextBox(kit, SceneKit::Lower, 0x12, 0x1a, text);
    memoReturn_.cancel();
    for (;;) {
        story.poll();
        memoReturn_.update(kit, story.touch);
        draw();
        story.present();
        if (memoReturn_.clicked(kit, story.touch)) {
            return;
        }
    }
}

} // namespace picarat::adventure1
