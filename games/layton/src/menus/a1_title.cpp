// The first adventure's title screen. See a1_title.hpp.
#include "menus/a1_title.hpp"
#include "menus/a1_save.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"

namespace picarat::adventure1 {

namespace {
struct Title {
    Story& story;
    SceneKit& kit;
    bool saves; // 02024338 == 0: a slot holds a game
    Button start, resume, secret;
    SceneKit::SpriteObject car, logo;
    SceneKind previousScene = SceneKind::PuzzleCard;

    explicit Title(Story& s) : story(s), kit(s.kit), saves(s.hasSavedGame()) {
        // 0202ce14: a frame presented first (0200432c, 02004254, 02003ef8).
        story.marker("title-ctor");
        story.present();
        // Buttons: sprite "gfx", pressed "gfx2", sound 2, centred on x 0x80
        // (02051358). "New game" sits lower when it is alone.
        auto button = [&](Button& b, int y, const char* sprite) {
            b.setSprite(kit, 0, y, sprite, "gfx");
            b.visibleIdle = true;
            b.pressDx = b.pressDy = 0;
            b.setPressedAnimation(kit, "gfx2");
            b.sound = 2;
            b.x = 0x80 - (kit.frameWidth(b.sprite) >> 1);
            b.sprite.x = b.x;
        };
        button(start, saves ? 0x1e : 0x41, "?/startbutton.spr");
        button(resume, 0x41, "?/continuebutton.spr");
        button(secret, 99, "?/secretbutton.spr");
        car = kit.load("start_car.spr", 0x50, 0x80, 1);
        car.x = 0x50;
        car.y = 0x80;
        kit.lowerBackground = "start_select";
        kit.lowerOverlay = "start_select2";
        // Measured on the ROM's screen: the fence (BG3) over the landscape
        // (BG2), the car above both.
        kit.lowerPriority = std::array<int, 4>{0, 0, 2, 1};
        kit.upperBackground = "select_title";
        // 0200928c: BG2 scrolls by -1, BG3 by -2 every second VBlank.
        kit.lowerScrollX = kit.lowerOverlayScrollX = 0;
        kit.lowerOverlayAutoScroll = {true, -1, 0};
        kit.lowerAutoScroll = {true, -2, 0};
        // Measured (a new game: presents at ROM frames 538, 549, then the
        // screen's loop from 577): the loads take 10 frames, the sounds
        // (kind 7) 27.
        kit.hold(10);
        story.present();
        // The sound loader waits with VBlank waits (02003d64).
        int soundFrames = 0;
        if (story.sound) {
            story.sound->setupScene(7, [&] {
                story.stall(1);
                ++soundFrames;
            });
        }
        kit.hold(std::max(0, 27 - soundFrames));
        // The logo (2D, upper screen), centred.
        logo = kit.load("?/sub_title_logo.sbj", 0, 0, 1);
        kit.select(logo, "logo");
        logo.x = (0x100 - kit.frameWidth(logo)) >> 1;
        logo.y = (0xc0 - kit.frameHeight(logo)) >> 1;
    }

    // 0202d288 (the destructor; called explicitly: its sound loading waits
    // frames): sounds of kind 2, scrolling off, both screens cleared.
    void finish() {
        kit.setScene(previousScene);
        kit.upperSpriteBlend.reset();
        if (story.sound) {
            story.sound->setupScene(2, [this] { story.stall(1); });
        }
        kit.lowerAutoScroll = {};
        kit.lowerOverlayAutoScroll = {};
        kit.lowerScrollX = kit.lowerOverlayScrollX = 0;
        kit.lowerPriority.reset();
        kit.lowerOverlay.clear();
        kit.lowerBackground.clear();
        kit.upperBackground.clear();
        kit.clearText(SceneKit::Upper);
        kit.clearText(SceneKit::Lower);
    }

    // 0202d41c: buttons (continue and secret only with a saved game), car.
    void draw() {
        start.update(kit, story.touch);
        if (saves) {
            resume.update(kit, story.touch);
            secret.update(kit, story.touch);
        }
        kit.draw(car, 31);
    }

    // After a closed load screen: the title as it was (0200928c, the
    // pictures).
    void backToTitle() {
        story.poll();
        kit.draw(logo);
        draw();
        kit.lowerPriority = std::array<int, 4>{0, 0, 2, 1};
        kit.lowerOverlayAutoScroll = {true, -1, 0};
        kit.lowerAutoScroll = {true, -2, 0};
        kit.lowerBackground = "start_select";
        kit.lowerOverlay = "start_select2";
        story.present();
        fadeIn(kit, Screens::Both);
        if (story.sound) {
            story.sound->setupScene(7, [this] { story.stall(1); });
        }
    }

    TitleChoice run() {
        // 0202d480. The logo blends over the upper background (OBJ over BG3,
        // 0209adac(0x10, 8, eva, 15 - eva)): invisible at first, one step more
        // every fifth frame after the first second, up to 15 (logo * 15/16).
        int blend = 0;
        story.marker("title-run");
        previousScene = kit.scene();
        kit.setScene(SceneKind::Title); // reported to the host
        story.frame([&] { draw(); });
        if (story.sound) {
            story.sound->playMusic(1);
        }
        fadeIn(kit, Screens::Both);
        for (unsigned frame = 1;; ++frame) {
            story.poll();
            if (frame > 0x3c && blend < 15 && frame % 5 == 1) {
                ++blend;
                kit.upperSpriteBlend = SceneKit::Blend{blend, 15 - blend};
            }
            if (blend > 0) {
                kit.draw(logo);
            }
            draw();
            story.present();
            if (start.clicked(kit, story.touch)) {
                break;
            }
            if (saves && secret.clicked(kit, story.touch)) {
                // 0202e050: a game to open the bonus section with.
                LoadScreen load(story, true);
                load.drawOwner = [&] {
                    kit.draw(logo);
                };
                if (load.run()) {
                    if (story.sound) {
                        story.sound->stopMusicSoftly(); // 02005354
                    }
                    kit.upperSpriteBlend = SceneKit::Blend{15, 0};
                    story.state.scene = scene::Continue;
                    fadeOut(kit, Screens::Both); // 0200fce8(1, 1)
                    return TitleChoice::Secret;
                }
                backToTitle();
            }
            if (saves && resume.clicked(kit, story.touch)) {
                // The logo shows fully from now on, the landscape stops.
                blend = 15;
                kit.upperSpriteBlend = SceneKit::Blend{15, 0};
                kit.lowerAutoScroll = {};
                kit.lowerOverlayAutoScroll = {};
                LoadScreen load(story);
                load.drawOwner = [&] {
                    kit.draw(logo);
                };
                if (load.run()) {
                    // 02004990: the loaded room's sounds.
                    if (story.sound) {
                        story.sound->setupRoom(story.state.room, [this] { story.stall(1); });
                    }
                    return TitleChoice::Continue;
                }
                backToTitle();
            }
        }
        story.state.scene = scene::Reset;
        fadeOut(kit, Screens::Both); // 0200fce8
        return TitleChoice::NewGame;
    }
};
} // namespace

TitleChoice titleScreen(Story& story) {
    Title title(story);
    const TitleChoice choice = title.run();
    title.finish();
    return choice;
}

} // namespace picarat::adventure1
