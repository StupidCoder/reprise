// The first adventure's page of mysteries. See a1_mysteries.hpp.
#include "menus/a1_mysteries.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"
#include "system/a1_ui.hpp"

namespace picarat::adventure1 {

namespace {
struct Mysteries {
    Story& story;
    SceneKit& kit;
    Button portraits[10];                  // +0..+0x24
    SpriteButton back;                     // +0x28
    SceneKit::SpriteObject newMark, intro; // +0x34, +0x38
    bool showIntro = true;                 // +0x3c

    bool raised(int i) const { return story.state.flag(300 + i); }

    bool solved(int i) const { return story.state.flag(0x140 + i); }

    bool unseen(int i) const { return i >= 0 && i < 16 && (story.state.mysteries >> i & 1); }

    explicit Mysteries(Story& s) : story(s), kit(s.kit) {
        // 0202e358: the pictures; the 3D layer (the portraits) blends 15:15
        // over them (0209adac(1, 0xf, 0xf, 0xf)).
        kit.lowerPriority.reset();
        story.present();
        kit.lowerBackground = "info_mode_bg";
        kit.upperBackground = "info_mode_sub";
        story.present();
        for (int i = 0; i < 10; ++i) {
            Button& b = portraits[i];
            b.setSprite(kit, 4 + 0x32 * (i % 5), 0x1e + 0x4a * (i / 5), "info_mode_chr.spr");
            b.visibleIdle = true;
            b.pressDx = b.pressDy = 1;
            kit.select(b.sprite, unsigned(solved(i) ? i + 0xb : i + 1));
            b.sound = 3;
        }
        back.load(kit, "?/load_btn.spr", 4);
        newMark = kit.load("?/new_button.spr", 0, 0, 1);
        kit.select(newMark, 1u);
        intro = kit.load("?/info_mes.sbj", 0x32, 0x40, 1);
        intro.x = 0x32;
        intro.y = 0x40;
        if (story.sound) {
            story.sound->setupScene(0x11, [this] { story.stall(1); });
        }
    }

    // 0202e894.
    void draw() {
        for (int i = 0; i < 10; ++i) {
            if (!raised(i) && !solved(i)) {
                continue;
            }
            portraits[i].update(kit, story.touch);
            if (unseen(i)) {
                newMark.x = portraits[i].x + 9;
                newMark.y = portraits[i].y + 0xe;
                kit.draw(newMark);
            }
        }
        back.update(kit, story.touch);
        if (showIntro) {
            kit.draw(intro);
        }
    }

    // 0202e6ac: mystery n (1..10) on the upper screen: its title (storytext
    // it_<n>.txt, centred at row 0xb), its text (id_<n>.txt, or id_<n+10>.txt
    // once solved) from (0x11, 0x2d).
    void show(int n) {
        std::string title =
            story.text("storytext", "storytext", "it_" + std::to_string(n) + ".txt");
        if (title.empty()) {
            title = "NO TITLE : " + std::to_string(n);
        }
        const int page = story.state.flag(n + 0x13f) ? n + 10 : n;
        std::string text =
            story.text("storytext", "storytext", "id_" + std::to_string(page) + ".txt");
        if (text.empty()) {
            text = "NO DATA : " + std::to_string(page);
        }
        kit.clearText(SceneKit::Upper);
        drawCentredLine(kit, SceneKit::Upper, 0xb, title, 0x80);
        drawTextBox(kit, SceneKit::Upper, 0x11, 0x2d, text);
        kit.upperBackground = "info_mode_sub2";
        showIntro = false;
    }

    void run() {
        // 0202e948.
        story.poll();
        draw();
        story.present();
        fadeIn(kit, Screens::Both);
        for (;;) {
            story.poll();
            draw();
            story.present();
            if (back.clicked()) {
                break;
            }
            for (int i = 0; i < 10; ++i) {
                if (raised(i) && portraits[i].clicked(kit, story.touch)) {
                    story.state.mysteries = std::uint16_t(story.state.mysteries & ~(1u << i));
                    show(i + 1);
                }
            }
        }
    }

    // 0202e598: the trunk's mark goes once every mystery has been looked at.
    void finish() {
        fadeOut(kit, Screens::Both);
        if (story.state.mysteries == 0) {
            story.state.newMystery = false;
        }
        kit.clearText(SceneKit::Upper);
        kit.clearText(SceneKit::Lower);
        kit.lowerBackground.clear();
        kit.upperBackground.clear();
        story.present();
    }
};
} // namespace

void mysteriesPage(Story& story) {
    Mysteries page(story);
    page.run();
    page.finish();
}

} // namespace picarat::adventure1
