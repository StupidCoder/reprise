#pragma once
// The first adventure's list scroll bar (ARM9 02059920 .. 02059f40), used by
// the puzzle index and the WiFi puzzles' list.
#include "sound/a1_sound.hpp"
#include "story/a1_story.hpp"
#include <algorithm>
#include <cmath>

namespace picarat::adventure1 {

// 0200c1ac: 8-bit components to the 3D engine's 15-bit colour.
inline std::uint16_t rgb(int r, int g, int b) {
    return std::uint16_t((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
}

// The scroll bar (02059920 .. 02059f40): a thumb in a track, dragged with
// the stylus; its value is the first row shown.
struct ScrollBar {
    int x = 0, y = 0;                     // +0 / +4: the thumb's place
    int top = 0, bottom = 0;              // +8 / +0x10: the track
    int lowest = 0;                       // +0xc: the thumb's lowest place
    int range = 0;                        // +0x18: values 0..range
    int value = 0;                        // +0x1c
    int height = 0;                       // +0x20
    static constexpr int kVisible = 5;    // +0x24
    bool dragging = false, moved = false; // +0x2c / +0x2d
    int grabY = 0;                        // +0x30
    SceneKit::SpriteObject sprite;        // jiten_scroll.spr

    // 020599e4: the thumb's height for `range` (at least the sprite's).
    void setRange(SceneKit& kit, int r) {
        range = r;
        height = int(float(kVisible) / float(range + kVisible) * float(bottom - top));
        kit.select(sprite, 1u);
        height = std::max(height, kit.frameHeight(sprite));
        lowest = bottom - height;
    }

    // 02059f40.
    void setValue(SceneKit& kit, int v) {
        setRange(kit, range);
        value = v;
        y = top + int(float(lowest - top) * (float(v) / float(range)));
    }

    // 02059ca8: dragging (the looped scrape 0xbe while it moves), then 02059a7c.
    void update(Story& story, int& soundY) {
        SceneKit& kit = story.kit;
        const TouchState& t = story.touch;
        moved = false;
        if (story.sound) {
            if (!dragging || y == soundY || std::abs(y - soundY) < 4) {
                story.sound->stopLoopedEffect();
            } else {
                soundY = y;
                story.sound->startLoopedEffect(0xbe);
            }
        }
        if (t.pressed && x <= t.x && t.x <= x + 0x12 && y <= t.y && t.y <= y + height) {
            dragging = true;
            grabY = t.y;
            soundY = y;
        }
        if (t.released && dragging) {
            if (story.sound) {
                story.sound->stopLoopedEffect();
            }
            dragging = false;
        }
        if (t.down && dragging) {
            y += t.y - grabY;
            grabY = t.y;
            y = std::clamp(y, top, lowest);
            value = int(float(range) * (float(y - top) / float(lowest - top)));
            moved = true;
        }
        draw(kit);
    }

    void draw(SceneKit& kit) {
        const int w = kit.frameWidth(sprite);
        kit.rectangle(x + 2, y + 3, w - 4, height - 6,
                      dragging ? rgb(0xc2, 0x54, 0x12) : rgb(0x9c, 0x93, 0x79), 0x1f);
        kit.rectangle(x, y + 3, 2, height - 6, rgb(0x31, 0x1c, 0x10), 0x1f);
        kit.rectangle(x + w - 2, y + 3, 2, height - 6, rgb(0x31, 0x1c, 0x10), 0x1f);
        kit.rectangle(x + 2, y + 3, 1, height - 6, rgb(0xfe, 0xfb, 0xe3), 0x1f);
        kit.rectangle(x + w - 3, y + 3, 1, height - 6, rgb(0xfe, 0xfb, 0xe3), 0x1f);
        kit.select(sprite, "top");
        sprite.x = x;
        sprite.y = y;
        kit.draw(sprite);
        kit.select(sprite, "bottom");
        sprite.y = y + height - 3;
        kit.draw(sprite);
        kit.select(sprite, 1u);
    }
};

} // namespace picarat::adventure1
