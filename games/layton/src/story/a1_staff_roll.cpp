// The first adventure's staff roll. See a1_staff_roll.hpp.
#include "story/a1_staff_roll.hpp"
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"
#include "system/a1_ui.hpp"

namespace picarat::adventure1 {

namespace {
// 0203bd60's object.
struct StaffRoll {
    Story& story;
    SceneKit& kit;
    SceneKit::SpriteObject closing;  // +0x6c: ?/staff_gfx.spr
    SceneKit::SpriteObject backdrop; // +0x74: staff_bg.sbj, tiled on the upper screen
    bool glow = true;                // 0220f100: the edges' glow
    int musicDelay = 0x78;           // +0x64
    bool musicStarted = false;       // +0x72
    int topPicture = 1;              // +0x48: the upper picture being loaded / shown next
    int topTime = 100;               // +0x4c: steps until the next upper picture
    bool loading = true;             // +0x70 / +0x71: an upper picture is being loaded
    int blend = 0;                   // +0x60: the upper picture's weight (0..15)
    int rollPicture = 2;             // +0x58: the lower picture below the shown one
    int scroll = 0;                  // +0x5c: the lower pictures' scroll (0..0xbf)
    int glowSteps = 0;               // 020d5fa8: the glow's first alpha

    explicit StaffRoll(Story& s) : story(s), kit(s.kit) {
        // 0203bd60: four seconds of black first.
        story.marker("staff-roll");
        kit.lowerPriority.reset();
        kit.lowerTextAlpha = -1;
        story.present();
        story.stall(0xf0);
        closing = kit.load("?/staff_gfx.spr", 0, 0, 1);
        backdrop = kit.load("staff_bg.sbj", 0, 0, 1);
        glowSteps = int(story.programWord(0x020d5fa8));
        story.present();
        kit.clearText(SceneKit::Lower);
        kit.clearText(SceneKit::Upper);
        kit.upperBackground.clear();
        kit.upperOverlay.clear();
        kit.upperOverlayBlend = SceneKit::Blend{0, 15};
    }

    // 0203c05c: the ending theme starts once no stream plays and the delay
    // has run out.
    void music() {
        if (!musicStarted && story.sound && !story.sound->streamPlaying()) {
            if (musicDelay < 1) {
                story.sound->playStream("BGM_09.SAD", 0xf, 0xe, 0x7f);
                musicStarted = true;
            } else {
                --musicDelay;
            }
        }
    }

    // 0203c0ec: the upper backdrop (64x64 tiles), then the lower edges' glow:
    // two-pixel bands of (0xf8, 0xe0, 0x88) fading from alpha 15 inwards.
    void draw() {
        for (int x = 0; x < 0x100; x += 0x40) {
            for (int y = 0; y < 0xc0; y += 0x40) {
                backdrop.x = x;
                backdrop.y = y;
                kit.draw(backdrop);
            }
        }
        if (!glow) {
            return;
        }
        const std::uint16_t colour = 0x1f | (0xe0 >> 3) << 5 | (0x88 >> 3) << 10;
        for (int k = 0, alpha = glowSteps; k < 0x20 && alpha >= 2; ++k, --alpha) {
            kit.rectangle(0, 2 * k, 0x100, 2, colour, alpha);
        }
        for (int k = 0, alpha = glowSteps; k < 0x20 && alpha >= 2; ++k, --alpha) {
            kit.rectangle(0, 0xbf - 2 * k, 0x100, 2, colour, alpha);
        }
    }

    // A frame of the roll: the scroll shown, then two more VBlank waits.
    void step() {
        story.poll();
        draw();
        kit.lowerScrollY = scroll;
        story.present();
        story.stall(1);
        story.stall(1);
    }

    std::string rollName(int n) const { return "staff_roll" + std::to_string(n); }

    // 0203c254: the shown picture moves into the map's first half, the next
    // one into the second.
    void nextPictures() {
        story.poll();
        draw();
        kit.lowerBackground = rollName(rollPicture);
        kit.lowerScrollY = scroll;
        story.present();
        music();
        ++scroll;
        if (++rollPicture > 0x20) {
            rollPicture = 1;
        }
        story.poll();
        draw();
        kit.lowerBackgroundBelow = rollName(rollPicture);
        kit.lowerScrollY = scroll;
        story.present();
        music();
    }

    void run() {
        // 0203c348.
        kit.lowerBackground = rollName(1);
        kit.lowerBackgroundBelow = rollName(2);
        kit.lowerScrollY = 0;
        story.poll();
        draw();
        story.present();
        fadeIn(kit, Screens::Both, 1, 3);
        for (;;) {
            music();
            // The upper pictures load in the background (020032f0 /
            // 0200332c); here a load is done at the first look.
            if (loading) {
                loading = false;
            }
            if (topPicture < 0x10) {
                --topTime;
            }
            if (topTime < 1 && !loading) {
                if (blend == 0) {
                    if (topPicture < 0x11) {
                        kit.upperOverlay = "staff_top" + std::to_string(topPicture);
                    }
                    if (++topPicture < 0x11) {
                        topTime = 0x17d;
                        loading = true;
                    }
                } else {
                    --blend;
                }
            } else if (topTime > 0 && blend < 0xf) {
                ++blend;
            }
            kit.upperOverlayBlend = SceneKit::Blend{blend, 0xf - blend};
            step();
            ++scroll;
            if (rollPicture == 0x20) {
                break;
            }
            if (scroll == 0xc0) {
                scroll = 0;
                nextPictures();
            }
        }
        // The closing picture fades in over the last credit.
        glow = false;
        closing.x = 0xc;
        closing.y = 0x4a;
        kit.select(closing, 1u);
        for (int alpha = 2; alpha < 0x20; ++alpha) {
            story.poll();
            draw();
            kit.draw(closing, alpha);
            story.present();
            story.stall(1);
            story.stall(1);
            music();
        }
        for (int k = 0; k < 300; ++k) {
            story.stall(1);
            music();
        }
        theEnd();
    }

    // 0203c648: "the end" until a touch or a button.
    void theEnd() {
        fadeOut(kit, Screens::Both, 1, 3);
        music();
        kit.clearText(SceneKit::Upper);
        kit.clearText(SceneKit::Lower);
        kit.lowerBackground.clear();
        kit.lowerBackgroundBelow.clear();
        kit.upperOverlay.clear();
        kit.upperOverlayBlend.reset();
        kit.lowerScrollY = 0;
        story.present();
        music();
        auto end = kit.load("?/end_mes.spr", 0, 0, 1); // at its "pos"
        music();
        kit.lowerBackground = "the_end2";
        kit.upperBackground = "the_end_sub2";
        story.poll();
        kit.draw(end);
        story.present();
        music();
        if (story.sound) {
            story.sound->effect(0x3e7);
        }
        fadeIn(kit, Screens::Both, 1, 4);
        for (int hold = 0x78;; --hold) {
            music();
            story.poll();
            kit.draw(end);
            story.present();
            // 02003ae8: a button (the host's "skip"), or a touch.
            if (hold < 1 && (kit.hostInput().skip || story.touch.released)) {
                break;
            }
        }
        fadeOut(kit, Screens::Both, 1);
    }

    // 0203bf40.
    void finish() {
        fadeOut(kit, Screens::Both, 1);
        if (story.sound) {
            story.sound->closeStream();
        }
        kit.clearText(SceneKit::Upper);
        kit.clearText(SceneKit::Lower);
        kit.lowerBackground.clear();
        kit.upperBackground.clear();
        story.present();
        story.state.scene = scene::Reset;
    }
};
} // namespace

void staffRoll(Story& story) {
    StaffRoll roll(story);
    roll.run();
    roll.finish();
}

} // namespace picarat::adventure1
