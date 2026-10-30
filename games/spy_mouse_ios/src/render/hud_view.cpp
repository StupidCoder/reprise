// The in-level HUD (SceneGame::renderHUD @0xfd234), drawn from game::Hud and the session.
// The original works in GL coordinates (y up from the bottom of the screen); the comments keep
// its numbers, the code converts to screen pixels (y down) at the end.
#include "render/level_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace doc::render {
namespace {

constexpr float kScreenW = 480.0f, kScreenH = 320.0f;

// bounceScale @0x19c938: grow from nothing, overshoot and settle (t in 0..1).
void bounceScale(float& sx, float& sy, float t) {
    if (t <= 0.4f) {
        sx = sy = t * 2.5f;
    } else if (t <= 0.6f) {
        const float u = t + -0.4f;
        sx = u * -0.999999881f + 1.0f;
        sy = u * 1.00000012f + 1.0f;
    } else if (t <= 0.8f) {
        const float u = t + -0.6f;
        sx = u * 2.00000024f + 0.8f;
        sy = u * -2.00000024f + 1.2f;
    } else {
        const float u = t + -0.8f;
        sx = u * -1.00000036f + 1.2f;
        sy = u + 0.8f;
    }
}

// bounceScale2 @0x19c860: a squash and stretch (t in 0..1).
void bounceScale2(float& sx, float& sy, float t) {
    if (t <= 0.33f) {
        sx = t * -0.606060565f + 1.0f;
        sy = t * 0.606060743f + 1.0f;
    } else if (t <= 0.66f) {
        const float u = t + -0.33f;
        sx = u * 1.21212125f + 0.8f;
        sy = u * -1.21212125f + 1.2f;
    } else {
        const float u = t + -0.66f;
        sx = u * -0.588235497f + 1.2f;
        sy = u * 0.588235319f + 0.8f;
    }
}

// getTrailScoreName @0x2216bc: the hand's name.
int trailScoreNameId(game::PokerHand hand) {
    switch (hand) {
    case game::PokerHand::Pair: return 0x160;
    case game::PokerHand::Threes: return 0x162;
    case game::PokerHand::TwoPair: return 0x161;
    case game::PokerHand::Fours: return 0x164;
    case game::PokerHand::FullHouse: return 0x163;
    case game::PokerHand::Straight: return 0x165;
    case game::PokerHand::Flush: return 0x166;
    default: return 0xC5;
    }
}

// The HUD's frame per carried kind on hud/hud_cheese.png (renderHUD @0xfdfb0): cheese 10..6 ->
// 1..5, 0x13 -> 6, 0xb -> 7, 0xc -> 8; 0 is an empty slot.
int hudCheeseFrame(int type) {
    if (type >= 6 && type <= 10) return std::abs(10 - type) + 1;
    if (type == 0xB) return 7;
    if (type == 0xC) return 8;
    if (type == 0x13) return 6;
    return -1;
}

}  // namespace

// A HUD picture at a GL position (x, y bottom-left, y up), tinted.
void LevelView::hudRect(const platform::Texture& t, float x, float y, float w, float h, float alpha) {
    platform::DrawStyle style;
    style.a = alpha;
    host_->draw(t, 0, 0, float(t.width()), float(t.height()), x, kScreenH - y - h, w, h, style);
}

// SceneGame::renderHUDElement @0xfcee0: a pill of left cap, stretched middle, right piece,
// optionally a stretched right middle, and the right cap; returns its width.
float LevelView::hudPill(float x, float y, float midWidth, float extraWidth, float alpha) {
    const platform::Texture* left = texture("hud/hud_elem_left.png");
    const platform::Texture* mid = texture("hud/hud_elem_mid.png");
    const platform::Texture* right = texture("hud/hud_elem_right.png");
    const platform::Texture* rightMid = texture("hud/hud_elem_right_mid.png");
    const platform::Texture* cap = texture("hud/hud_elem_right_cap.png");
    if (!left || !mid || !right || !rightMid || !cap) return 0.0f;
    float at = x;
    hudRect(*left, at, y, float(left->width()), float(left->height()), alpha);
    at += float(left->width());
    hudRect(*mid, at, y, midWidth, float(mid->height()), alpha);
    at += midWidth;
    hudRect(*right, at, y, float(right->width()), float(right->height()), alpha);
    at += float(right->width());
    if (extraWidth > 0.0f) {
        hudRect(*rightMid, at, y, extraWidth, float(rightMid->height()), alpha);
        at += extraWidth;
    }
    hudRect(*cap, at, y, float(cap->width()), float(cap->height()), alpha);
    return at + float(cap->width()) - x;
}

void LevelView::hudText(const std::string& text, float x, float yGl, float sx, float sy, const Colour& colour) {
    if (!font_) return;
    // renderStr flags 0x22 under bounceScale2's scale about the point.
    const float y = kScreenH - yGl;
    font_->drawFlags(*host_, text, x, y, 0x22, 1.0f, 0.0f, TextStyle{}, colour,
                     Affine{sx, x * (1.0f - sx), sy, y * (1.0f - sy)});
}

// SceneGame::renderHUDPopupText @0xf6d04: a text in two layers (a brown copy 2 below, then
// cream), left-aligned at x, bouncing in while t < 1 and fading over t 5..6.
void LevelView::hudPopupText(const std::string& text, float x, float yGl, float t) {
    if (!font_ || text.empty()) return;
    float sx = 1.0f, sy = 1.0f;
    if (t > 0.0f && t < 1.0f) bounceScale(sx, sy, t);
    float alpha = 1.0f;
    if (t >= 5.0f && t < 6.0f) alpha = std::min(1.0f, 6.0f - t);
    const float w = font_->width(text);
    hudText(text, x + w * 0.5f, yGl + -2.0f, sx, sy, Colour::bytes(0x72, 0x23, 0x0A, int(alpha * 255.0f + 0.5f)));
    hudText(text, x + w * 0.5f, yGl, sx, sy, Colour::bytes(0xFF, 0xF7, 0xBE, int(alpha * 255.0f + 0.5f)));
}

void LevelView::drawHud(const game::Session& session) {
    const game::Hud& hud = session.hud();
    const game::LevelProgress& g = session.progress();
    // On the 480 x 320 screen the HUD rests at 0.6 and brightens after events (fade timer).
    float alpha = 0.6f;
    if (hud.fadeMs >= 1) alpha = std::sin(float(hud.fadeMs) * 0.000303030305f) + 0.6f;
    alpha = std::min(alpha, 1.0f);

    // (The boss chase and the boss fight, game modes 1 and 2, have no pills: renderHUD @0xfd8d0
    // goes on past them; only the pause button shows.)
    const std::string& mode = session.level().info().mode;
    const bool pills = mode != "infinite" && mode != "boss";
    const platform::Texture* left = texture("hud/hud_elem_left.png");
    if (!left || !pills) {
        drawHudButtons(session);
        return;
    }
    const float leftW = float(left->width()), leftH = float(left->height());

    // Glow behind a pill after a score pulse: up over 150 ms, down over the next 150 ms.
    const game::Vec2 p1 = hud.items[game::Hud::kScore].position;
    if (hud.glowMs >= 1)
        if (const platform::Texture* glow = texture("hud/hud_glow.png")) {
            float a = hud.glowMs < 0x97 ? std::max(0.0f, (float(hud.glowMs) + -150.0f) * 0.00666666683f + 1.0f)
                                        : std::max(0.0f, (float(hud.glowMs) + -300.0f) * -0.00666666683f);
            a = std::min(a, 1.0f);
            const float gx = hud.glowLeft ? p1.x : kScreenW - float(glow->width()) + p1.x;
            hudRect(*glow, gx, p1.y + kScreenH - float(glow->height()), float(glow->width()), float(glow->height()), a);
        }

    // The score pill: cheese icon, the score counting up (bouncing after a pulse).
    hudPill(p1.x, p1.y + kScreenH - leftH, 50.0f, 0.0f, alpha);
    if (const platform::Texture* icon = texture("hud/hud_cheese_icon.png"))
        hudRect(*icon, p1.x + leftW + -5.0f, p1.y + kScreenH - leftH, float(icon->width()), float(icon->height()), alpha);
    {
        char text[32];
        std::snprintf(text, sizeof text, "%.0f", double(hud.shownScore));
        float sx = 1.0f, sy = 1.0f;
        bounceScale2(sx, sy, float(hud.scoreBounceMs) * -0.00200000009f + 1.0f);
        const float w = font_ ? font_->width(text) : 0.0f;
        hudText(text, p1.x + 2.0f * leftW + 50.0f - leftW + w * -0.5f + 5.0f, p1.y + kScreenH + leftH * -0.6f, sx, sy,
                {1, 1, 1, alpha});
    }

    // The trail pill: one 19-unit slot per cheese the mouse can carry, and the hand multiplier
    // opening on its right.
    const game::Vec2 p0 = hud.items[game::Hud::kTrail].position;
    const platform::Texture* cheese = texture("hud/hud_cheese.png");
    const float slot = cheese ? float(cheese->height()) : 19.0f;  // frames are square (Sprite +0x94)
    const float midW = float(session.strength()) * slot + -14.0f;
    // (It follows the score pill: its x is the score item's plus the score pill's width,
    // renderHUD @0xfd234; 90 with 1.0.5's offsets, 116 with 1.1.1's.)
    const game::Vec2 base{p1.x + 50.0f + 2.0f * leftW, kScreenH - leftH + p0.y};
    hudPill(base.x, base.y, midW, hud.handWidth, alpha);
    // The slots start half a slot into the pill, 7 back (renderHUD @0xfdeec); the multiplier
    // and texts use the pill's own measures.
    const game::Vec2 slotsStart{base.x + leftW + slot * 0.5f, base.y + leftH * 0.5f};
    const float firstSlotX = slotsStart.x + -7.0f;
    const float textAlpha = std::min(1.0f, std::max(0.0f, (hud.handWidth + -16.0f) * 0.25f));
    if (textAlpha > 0.0f && hud.hand != game::PokerHand::Bluff) {
        float sx = 1.0f, sy = 1.0f;
        if (hud.handBounce > 0.0f && hud.handBounce < 0.75f) bounceScale2(sx, sy, hud.handBounce * 1.33000004f);
        char text[32];
        std::snprintf(text, sizeof text, "x%.0f", double(session.handBonus(hud.hand)));
        hudText(text, base.x + midW + 2.0f * leftW + 2.0f, slotsStart.y, sx, sy, {1, 1, 1, alpha * textAlpha});
    }
    if (hud.handBounce > 0.0f && !hud.exitBonus && strings_)
        hudPopupText(strings_->text(trailScoreNameId(hud.hand)), base.x + midW + 2.0f * leftW + hud.handWidth + 3.0f,  // (1.0.5: 5)
                     base.y + leftH * 0.5f, hud.handBounce);

    if (g.lastScoreShowMs >= 1) {
        // The trail just scored: its points in the pill ("Exit Bonus" beside it for home holes).
        float sx = 1.0f, sy = 1.0f;
        if (g.lastScoreShowMs <= 500) bounceScale(sx, sy, float(g.lastScoreShowMs) * 0.00200000009f);
        if (hud.exitBonus && g.lastScore >= 1 && strings_)
            hudPopupText(strings_->text(0x24b), base.x + midW + 2.0f * leftW + hud.handWidth + 10.0f,
                         base.y + leftH * 0.5f, hud.handBounce);
        char text[32];
        std::snprintf(text, sizeof text, "%d", g.lastScore);
        hudText(text, base.x + (2.0f * leftW + midW) * 0.5f, base.y + leftH * 0.4f, sx, sy, {1, 1, 1, 1});
    } else if (cheese) {
        // The carried cheese, then empty slots up to the capacity; the hand's cheese bounce.
        float x = firstSlotX;
        const float y = slotsStart.y + -1.5f;
        int shown = 0;
        auto drawSlot = [&](int frame, float sx, float sy) {
            platform::DrawStyle style;
            const float w = slot * sx, h = slot * sy;
            host_->draw(*cheese, float(frame) * slot, 0, slot, slot, x - w * 0.5f, kScreenH - y - h * 0.5f, w, h, style);
        };
        for (int id : session.carried()) {
            const game::LevelObject& o = session.objects()[std::size_t(id)];
            const int frame = hudCheeseFrame(o.type);
            if (frame < 0) continue;
            float sx = 1.0f, sy = 1.0f;
            const int kind = o.type - 6;
            const bool inHand = kind == hud.topType || kind == hud.secondType || hud.hand == game::PokerHand::Straight;
            if (hud.handBounce > 0.0f && hud.handBounce < 1.0f && inHand) bounceScale2(sx, sy, hud.handBounce);
            drawSlot(frame, sx, sy);
            ++shown;
            x += slot;
        }
        for (; shown < session.strength(); ++shown) {
            drawSlot(0, 1.0f, 1.0f);
            x += slot;
        }
    }

    drawHudButtons(session);
}

// The level's GUI buttons (SceneGame::initGuiElements @0xf9172): the pause button at the top
// right, 1 in from the corner (item 0), and the power-up button at the top left, 1 in and one
// below the pause button's top (item 1). Both are drawn after the dimming; the power-up button
// stays when the pause button fades in the home hole (runs x_pu1, l4h1_win, l11_pause).
void LevelView::drawHudButtons(const game::Session& session) {
    const game::Hud& hud = session.hud();
    const platform::Texture* paws = texture("hud/hud_paws.png");
    if (paws && hud.pauseAlpha > 0.0f && !hud.pauseHidden)
        hudRect(*paws, kScreenW - float(paws->width()) - 1.0f, kScreenH - float(paws->height()) - 1.0f,
                float(paws->width()), float(paws->height()), hud.pauseAlpha);
}

// The power-up button (item 1), drawn over the power-up bar (run x_pu3); hidden while Digger
// talks unless it opened the bar, highlighted with an arrow when he asks for it.
void LevelView::drawPowerupButton(const game::Session& session) {
    if (session.powerupButtonHidden()) return;
    const platform::Texture* paws = texture("hud/hud_paws.png");
    if (const platform::Texture* powerup = texture("hud/hud_powerup.png"); powerup && paws) {
        hudRect(*powerup, 1.0f, kScreenH - float(paws->height()) - 2.0f, float(powerup->width()),
                float(powerup->height()), 1.0f);
        drawHighlight("hud/hud_powerup_over.png", 1.0f, 2.0f, float(powerup->width()), float(powerup->height()),
                      session.powerupButtonHighlightMs(), 2);
    }
}

}  // namespace doc::render
