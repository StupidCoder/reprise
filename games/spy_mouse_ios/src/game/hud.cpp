#include "game/hud.hpp"

#include <algorithm>

namespace doc::game {

void Hud::init(float bonusPanelWidth) {
    items[kTrail].out = {0.0f, 54.0f};
    items[kScore].out = {0.0f, 54.0f};
    items[kBonus].out = {bonusPanelWidth, 0.0f};
    for (HudItem& item : items) {
        item.in = {};
        item.position = item.out;
        item.target = item.out;  // hidden until the intro ends (SceneGame::hideHudItems after initHUD)
        item.timerMs = 0;
    }
    handBounce = -1.0f;
    handWidth = 0.0f;
}

void Hud::updateItems(int dtMs) {
    const float ease = 4.0f / float(dtMs);
    for (HudItem& item : items) {
        if (item.timerMs <= -1) {
            item.timerMs += dtMs;
            if (item.timerMs >= 0) {
                item.timerMs = 0;
                item.target = item.out;
            }
        } else if (item.timerMs >= 1) {
            item.timerMs -= dtMs;
            if (item.timerMs <= 0) {
                item.timerMs = 0;
                item.target = item.in;
                fadeMs = 3300;
            }
        }
        const Vec2 d = item.target - item.position;
        if (d.lengthSquared() <= 0.2f) item.position = item.target;
        else item.position += d * ease;
    }
    if (pauseVisible) {
        if (pauseAlpha < 1.0f) pauseAlpha = std::min(1.0f, pauseAlpha + float(dtMs) * 0.00499999989f);
    } else if (pauseAlpha > 0.0f) {
        pauseAlpha = std::max(0.0f, pauseAlpha + float(dtMs) * -0.00499999989f);
    }
}

void Hud::updateCounters(int dtMs) {
    if (scoreBounceMs >= 1) scoreBounceMs -= dtMs;
    if (glowMs >= 1) glowMs -= dtMs;
    if (fadeMs >= 1) fadeMs -= dtMs;
}

void Hud::updateHand(int dtMs, float handBonus) {
    const float dt = float(dtMs);
    if (handBonus > 1.0f) handWidth = std::min(20.0f, handWidth + (20.0f - handWidth) * dt * 0.00999999978f);
    else handWidth = std::max(0.0f, handWidth + handWidth * dt * -0.00999999978f);
    if (handBounce >= 0.0f) {
        handBounce = handBounce + dt * 0.00200000009f;
        if (handBounce > 6.0f) handBounce = -1.0f;
    }
}

void Hud::countScore(int dtMs, int score, bool popupShowing) {
    if (popupShowing) return;
    if (shownScore < float(score)) shownScore = std::min(float(score), shownScore + float(dtMs) * 0.5f);
}

void Hud::protractAll(int delayMs) {
    for (HudItem& item : items) {
        if (delayMs < 1) item.target = item.in;
        else item.timerMs = delayMs;
    }
    fadeMs = 3300;
}

void Hud::retractAll(int delayMs) {
    for (HudItem& item : items) {
        if (delayMs > 0) item.timerMs = -delayMs;
        else item.target = item.out;
    }
}

}  // namespace doc::game
