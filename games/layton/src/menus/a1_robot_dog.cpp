// The first adventure's robot dog. See a1_robot_dog.hpp.
#include "menus/a1_robot_dog.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"
#include "system/a1_script.hpp"
#include "system/a1_ui.hpp"
#include <cmath>

namespace picarat::adventure1 {

namespace {
struct Part {                     // the dog script's 0xc2 (V268 02025580)
    int x = 0, y = 0;             // +0x188 / +0x208: where it shows on the dog (dogp.spr)
    int layer = 0;                // +0x288: drawing order (0..30)
    int targetX = 0, targetY = 0; // +0x88 / +0x108: where it flies to
    int alpha = 0x1f;             // +0x308
};

struct RobotDog {
    Story& story;
    SceneKit& kit;
    SpriteButton back;                    // +4
    Button arrowButton;                   // +8
    Button slots[5];                      // +0x14
    SceneKit::SpriteObject message;       // +0xc: ?/robodog_mes.spr
    SceneKit::SpriteObject icons[20];     // +0x28: dogi_<n>.spr
    SceneKit::SpriteObject arrow, parts;  // +0x78 dog_arrow, +0x7c dogp
    SceneKit::SpriteObject digits, whole; // +0x80 artmode_num, +0x10 dog_gfx
    Part layout[32];
    bool complete = false;         // +0x388: complete when the page opened
    static constexpr int page = 0; // +0x84: never changes (the arrow only tells)

    GameState& state() { return story.state; }

    bool placed(int part) const {
        return part >= 0 && part < 0x20 &&
               (story.state.dogParts[std::size_t(part >> 3)] >> (part & 7) & 1);
    }

    int waiting(int slot) const { // 0201b218
        return slot >= 0 && slot < 0x20 ? story.state.gizmos[std::size_t(slot)] : 0;
    }

    explicit RobotDog(Story& s) : story(s), kit(s.kit) {
        // 02024de4.
        kit.lowerPriority.reset();
        story.present();
        complete = state().dogComplete();
        kit.lowerBackground = complete ? "robodog_bg2" : "robodog_bg";
        kit.upperBackground = "robodog_top_bg";
        story.present();
        back.load(kit, "?/dog_btn.spr", 4, 0xc1, 0x9b);
        message = kit.load("?/robodog_mes.spr", 0, 0, 1); // at its "pos"
        for (int i = 0; i < 20; ++i) {
            icons[i] = kit.load("dogi_" + std::to_string(i + 1) + ".spr", 0, 0, 1);
        }
        arrow = kit.load("dog_arrow.spr", 0xf, 0x9f, 1);
        arrow.x = 0xf;
        arrow.y = 0x9f;
        arrowButton.setRect(0xf, 0x9f, 0x13, 0x15);
        parts = kit.load("dogp.spr", 0, 0, 1);
        for (int k = 0; k < 5; ++k) {
            slots[k].setRect(0x25 + 0x20 * k, 0x9d, 0x18, 0x18);
        }
        ScriptContext c = story.commonCommands();
        c.rebind(0xc2, [this](CommandArguments& a) {
            const int n = a.integer() - 1;
            Part p;
            p.x = a.integer();
            p.y = a.integer();
            p.layer = a.integer();
            p.targetX = a.integer();
            p.targetY = a.integer();
            if (n >= 0 && n < 32) {
                layout[n] = p;
            }
        });
        story.runScript("script/dog/dogscript.gds", c);
        whole = kit.load("dog_gfx.spr", 0x32, 3, 1);
        whole.x = 0x32;
        whole.y = 3;
        digits = kit.load("artmode_num.spr", 0, 0, 1);
        if (story.sound) {
            story.sound->setupScene(0xe, [this] { story.stall(1); });
            story.sound->loadMusic(8, [this] { story.stall(1); });
            story.sound->startLoadedMusic(true);
        }
        state().newGizmo = false;
        if (state().dogComplete()) {
            // Its name (+0x626) on the lower text layer.
            std::string name(state().dogName.begin(), state().dogName.end());
            name = name.c_str();
            drawCentredText(kit, SceneKit::Lower, 0x9b, name, false, 0,
                            -(100 - (int(name.size() >> 1) * 0xd >> 1)));
        }
    }

    // 02025618: what the dog has sniffed out (three digits) and its caption.
    void drawFinds() {
        const int finds = state().dogFinds;
        const int d[3] = {finds % 1000 / 100, finds % 100 / 10, finds % 10};
        for (int k = 0; k < 3; ++k) {
            kit.select(digits, unsigned(d[k] + 1));
            digits.x = 0x7b + 0xc * k;
            digits.y = 0xb0;
            kit.draw(digits);
        }
        kit.draw(message);
    }

    // 02025744: the dog; incomplete: its parts in place by layer.
    void drawDog() {
        if (complete) {
            drawFinds();
        }
        if (state().dogComplete()) {
            kit.draw(whole, 0x1f);
            return;
        }
        for (int layer = 0; layer < 0x1f; ++layer) {
            for (int i = 0; i < 0x20; ++i) {
                if (layout[i].layer != layer || (!placed(i) && layout[i].alpha == 0x1f)) {
                    continue;
                }
                kit.select(parts, unsigned(i + 1));
                parts.x = layout[i].x;
                parts.y = layout[i].y;
                kit.draw(parts, layout[i].alpha);
            }
        }
    }

    // 02025810.
    void draw(bool buttons) {
        // 020255ac: the waiting parts of the page.
        for (int k = 0; k < 5; ++k) {
            const int id = waiting(k + page * 5);
            if (id > 0 && id <= 20) {
                icons[id - 1].x = 0x25 + 0x1f * k;
                icons[id - 1].y = 0x9d;
                kit.draw(icons[id - 1]);
            }
        }
        drawDog();
        // 02025b30: more parts after this page.
        bool more = false;
        for (int k = 0; k < 0x20; ++k) {
            more = more || waiting((page + 1) * 5 + k) != 0;
        }
        if (more) {
            kit.draw(arrow);
        }
        if (buttons) {
            back.update(kit, story.touch);
            arrowButton.update(kit, story.touch);
        }
        for (auto& slot : slots) {
            slot.update(kit, story.touch);
        }
    }

    void frame() {
        story.poll();
        draw(true);
        story.present();
    }

    // 02025880: part `part` flies from (x, y) to its place, 6 pixels of
    // height a frame, then changes into its piece on the dog.
    void place(int part, int x, int y) {
        Part& p = layout[part];
        if (story.sound) {
            story.sound->effect(0x26c);
        }
        const float dx = float(p.targetX - x), dy = float(p.targetY - y);
        const float stepX = std::fabs(dx) / (std::fabs(dy) / 6.0f);
        float fx = float(x);
        int iy = y;
        auto show = [&](int alpha) {
            story.poll();
            draw(true);
            icons[part].x = int(fx);
            icons[part].y = iy;
            kit.draw(icons[part], alpha);
            story.present();
        };
        while (iy != p.targetY) {
            iy = std::max(iy - 6, p.targetY);
            if (fx < float(p.targetX)) {
                fx = std::min(fx + stepX, float(p.targetX));
            } else if (fx > float(p.targetX)) {
                fx = std::max(fx - stepX, float(p.targetX));
            }
            show(0x1f);
        }
        for (int alpha = 0x1f; alpha > 1; alpha -= 2) {
            show(alpha);
        }
        if (story.sound) {
            story.sound->effect(0x26d);
        }
        for (int alpha = 2; alpha < 0x20; alpha += 2) {
            p.alpha = alpha;
            frame();
        }
        state().dogParts[std::size_t(part >> 3)] |= std::uint8_t(1 << (part & 7)); // 0201b158
        p.alpha = 0x1f;
        // 0201b0c4: the row closes up (effect 0x26f once).
        bool sounded = false;
        for (bool moved = true; moved;) {
            moved = false;
            for (int i = 0; i < 0x1f; ++i) {
                auto& g = state().gizmos;
                if (g[std::size_t(i)] == 0 && g[std::size_t(i + 1)] != 0) {
                    if (!sounded && story.sound) {
                        story.sound->effect(0x26f);
                    }
                    sounded = true;
                    g[std::size_t(i)] = g[std::size_t(i + 1)];
                    g[std::size_t(i + 1)] = 0;
                    moved = true;
                    break;
                }
            }
        }
        if (state().dogComplete() && story.sound) {
            story.sound->effect(0x26e);
        }
    }

    void run() {
        // 02025b80.
        frame();
        frame();
        fadeIn(kit, Screens::Both);
        if (state().dogComplete() && story.sound) {
            story.sound->effect(0x26e);
        }
        for (;;) {
            if (state().dogComplete() && !(state().events[0x100] & 1)) {
                // Complete for the first time: event 0x100 after 100 frames.
                for (int k = 0; k < 100; ++k) {
                    frame();
                }
                state().scene = scene::Event;
                state().event = 0x100;
                return;
            }
            frame();
            if (back.clicked()) {
                return;
            }
            for (int k = 0; k < 5; ++k) {
                const int id = waiting(k + page * 5);
                if (id != 0 && slots[k].clicked(kit, story.touch)) {
                    state().gizmos[std::size_t(k + page * 5)] = 0; // 0201b268
                    place(id - 1, 0x25 + 0x1f * k, 0x9d);
                }
            }
        }
    }

    // 02025220: music slowly out, the room's sounds again.
    void finish() {
        fadeOut(kit, Screens::Both);
        if (story.sound) {
            story.sound->fadeOutMusicSlowly();
        }
        story.stall(10);
        if (story.sound) {
            story.sound->forgetLoadedMusic();
            story.sound->setupRoom(state().room, [this] { story.stall(1); });
        }
        kit.clearText(SceneKit::Lower);
        kit.clearText(SceneKit::Upper);
        kit.lowerBackground.clear();
        kit.upperBackground.clear();
        story.present();
    }
};
} // namespace

void robotDogPage(Story& story) {
    RobotDog page(story);
    page.run();
    page.finish();
}

} // namespace picarat::adventure1
