// The first adventure's challenges. See a1_challenge.hpp.
#include "menus/a1_challenge.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"
#include "system/a1_ui.hpp"
#include <cstdio>

namespace picarat::adventure1 {

namespace {

// 020bc1cc: the challenges' puzzles, three each.
constexpr int kPuzzles[5][3] = {
    {122, 58, 121}, {133, 137, 92}, {135, 91, 161}, {20, 29, 142}, {99, 81, 136}};

// 02035b70: the rows' text lines.
constexpr int kRowY[3] = {0x2a, 0x46, 0x62};

struct Challenges {
    Story& story;
    SceneKit& kit;
    Button challenges[5];        // +0..+0x10
    SpriteButton back;           // +0x14
    Button rows[3];              // +0x18..+0x20
    SceneKit::SpriteObject icon; // +0x24: challenge_icon.spr
    std::string rowText[3];      // +0x28..+0x30

    explicit Challenges(Story& s) : story(s), kit(s.kit) {
        // 02034c60.
        story.state.afterPuzzle = 0;
        kit.lowerPriority.reset();
        kit.clearText(SceneKit::Lower);
        kit.clearText(SceneKit::Upper);
        story.present();
        kit.lowerBackground = "challenge_bg";
        kit.upperBackground = kit.localized("?/challenge_sub");
        story.present();
        back.load(kit, "?/load_btn.spr", 4);
        const int xs[] = {10, 0x5c, 0xad, 0x33, 0x84}, ys[] = {6, 6, 6, 0x59, 0x59};
        for (int i = 0; i < 5; ++i) {
            Button& b = challenges[i];
            b.setSprite(kit, xs[i], ys[i], "?/challenge_btn.spr");
            kit.select(b.sprite, unsigned(i + 1));
            b.visibleIdle = true;
            b.pressDx = b.pressDy = 1;
            b.sound = 3;
        }
        for (int k = 0; k < 3; ++k) {
            rows[k].setRect(0, 0x24 + 0x1c * k, 0x100, 0x17);
            rows[k].sound = 3;
        }
        icon = kit.load("challenge_icon.spr", 0, 0, 1);
        if (story.sound) {
            story.sound->setupScene(3, [this] { story.stall(1); });
        }
        // Every story puzzle solved opens the fifth challenge (flag 511).
        if (story.state.solvedCount() == 0x84) {
            story.state.setFlag(511, true);
        }
    }

    bool available(int i) const {
        // 02035184.
        static constexpr int kFlags[4] = {507, 508, 509, 512};
        return i < 4 ? story.state.flag(kFlags[i]) : story.state.solvedCount() >= 0x84;
    }

    // 02035184.
    void drawMenu() {
        back.update(kit, story.touch);
        for (int i = 0; i < 5; ++i) {
            if (available(i)) {
                challenges[i].update(kit, story.touch);
            }
        }
    }

    // The solved marks of challenge i's puzzles at (0x11, 0x28 + 0x1c k).
    void drawMarks(int i) {
        for (int k = 0; k < 3; ++k) {
            if (story.state.solvedAt(kPuzzles[i][k])) {
                kit.select(icon, unsigned(i + 1));
                icon.x = 0x11;
                icon.y = 0x28 + 0x1c * k;
                kit.draw(icon);
            }
        }
    }

    // 02035244: the rows' texts (sys_400.txt's format with the number
    // 121 + 3 i + k and the puzzle's title from qtext q000 / q050 / q100),
    // the challenge's picture (challenge_<i + 1>).
    void setUp(int i) {
        const std::string format = story.text("stext", "stext", "sys_400.txt");
        for (int k = 0; k < 3; ++k) {
            const int puzzle = kPuzzles[i][k];
            const std::string archive = puzzle < 50 ? "q000" : puzzle < 100 ? "q050" : "q100";
            std::string title =
                story.text("qtext", archive, "t_" + std::to_string(puzzle) + ".txt");
            if (title.empty()) {
                title = "NO TITLE";
            }
            rowText[k] = formatText(format, {i * 3 + k + 0x79, title});
        }
        kit.clearText(SceneKit::Lower);
        for (int k = 0; k < 3; ++k) {
            drawTextBox(kit, SceneKit::Lower, 0x24, kRowY[k], rowText[k]);
        }
        kit.lowerBackground = "challenge_" + std::to_string(i + 1);
    }

    // 02035680: challenge i's page; true when a puzzle's event was chosen.
    bool page(int i) {
        GameState& state = story.state;
        SceneKit::SpriteObject title =
            kit.load("?/challenge_mes" + std::to_string(i + 1) + ".spr", 0x14, 8, 1);
        kit.variable(title, "pos", title.x, title.y);
        auto drawPage = [&] {
            drawMarks(i);
            back.update(kit, story.touch);
            kit.draw(title);
        };
        const bool fromList = state.challengePage == -1;
        if (fromList) {
            fadeOut(kit, Screens::Lower); // 0200ff00(1)
        }
        story.poll();
        setUp(i);
        drawPage();
        story.present();
        fadeIn(kit, fromList ? Screens::Lower : Screens::Both);
        for (;;) {
            story.poll();
            drawMarks(i);
            for (int k = 0; k < 3; ++k) {
                rows[k].update(kit, story.touch);
                // A row held shows red (0200df2c with colour 0xfc).
                drawTextBox(kit, SceneKit::Lower, 0x24, kRowY[k],
                            (rows[k].held(story.touch) ? "#R" : "") + rowText[k]);
                if (rows[k].clicked(kit, story.touch)) {
                    state.scene = scene::Event;
                    const int event = 0x15e + i * 3 + k;
                    state.event = std::uint32_t(event < 0 || event > 0x1df ? 0 : event);
                    state.returnScene = scene::Challenge;
                    state.challengePage = i;
                    back.update(kit, story.touch);
                    kit.draw(title);
                    story.present();
                    fadeOut(kit, Screens::Both);
                    return true;
                }
            }
            back.update(kit, story.touch);
            kit.draw(title);
            story.present();
            if (back.clicked()) {
                state.challengePage = -1;
                fadeOut(kit, Screens::Lower);
                kit.clearText(SceneKit::Lower);
                kit.lowerBackground = "challenge_bg";
                story.frame([this] { drawMenu(); });
                fadeIn(kit, Screens::Both);
                return false;
            }
        }
    }

    void run() {
        // 02035b8c.
        GameState& state = story.state;
        if (state.challengePage == -1) {
            story.frame([this] { drawMenu(); });
            fadeIn(kit, Screens::Both);
        } else if (page(state.challengePage)) {
            return;
        }
        for (;;) {
            story.frame([this] { drawMenu(); });
            for (int i = 0; i < 5; ++i) {
                if (available(i) && challenges[i].clicked(kit, story.touch) && page(i)) {
                    return;
                }
            }
            if (back.clicked()) {
                state.scene = scene::Continue;
                state.challengePage = -1;
                return;
            }
        }
    }

    // 0203509c.
    void finish() {
        fadeOut(kit, Screens::Both);
        kit.clearText(SceneKit::Lower);
        kit.clearText(SceneKit::Upper);
        kit.lowerBackground.clear();
        kit.upperBackground.clear();
        story.present();
    }
};

} // namespace

void challengeMenu(Story& story) {
    Challenges page(story);
    page.run();
    page.finish();
}

} // namespace picarat::adventure1
