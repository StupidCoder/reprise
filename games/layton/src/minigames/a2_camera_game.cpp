// Camera repair (overlay 13). See a2_camera_game.hpp for how it plays.
#include "minigames/a2_camera_game.hpp"
#include <cmath>

namespace picarat {
namespace {
// ov13 020f2268: part records (0x1c bytes): animation name, rotation centre,
// target cell, size in cells, initial rotation, hit rectangles (counts set by
// the static initializer 020f21a8).
struct PartRecord {
    const char* name;
    int pivotX, pivotY, targetX, targetY, cellsW, cellsH, rotation;
    std::vector<std::array<int, 4>> rects;
};

const std::array<PartRecord, 10> kParts{{
    {"1", 20, 24, 3, 1, 3, 3, 0, {{0, 0, 48, 16}, {0, 16, 32, 32}}},
    {"2", 28, 16, 6, 0, 4, 2, 3, {{0, 0, 48, 16}, {0, 16, 64, 16}}},
    {"3", 24, 28, 10, 0, 3, 4, 3, {{0, 16, 48, 16}, {16, 0, 16, 64}, {16, 16, 32, 32}}},
    {"4", 32, 32, 5, 2, 5, 4, 2, {{0, 0, 64, 64}, {64, 16, 16, 32}}},
    {"5", 20, 24, 9, 2, 2, 4, 3, {{0, 0, 32, 16}, {16, 16, 16, 48}}},
    {"6", 16, 32, 11, 3, 2, 3, 0, {{0, 16, 16, 32}, {16, 0, 16, 48}}},
    {"7", 16, 32, 3, 4, 3, 4, 1, {{0, 0, 32, 64}, {32, 32, 16, 16}}},
    {"8", 36, 16, 5, 6, 4, 2, 1, {{16, 0, 48, 16}, {0, 16, 64, 16}}},
    {"9", 8, 16, 9, 5, 1, 2, 0, {{0, 0, 16, 32}}},
    {"10", 36, 16, 9, 6, 4, 2, 0, {{16, 0, 48, 16}, {0, 16, 64, 16}}},
}};

// ov13 020f2178: tray arrows (no / on / off animations, rectangle).
struct Arrow {
    const char* none;
    const char* on;
    const char* off;
    int x, y, w, h;
};

const std::array<Arrow, 2> kArrows{{{"left_no", "left_on", "left_off", 6, 5, 32, 28},
                                    {"right_no", "right_on", "right_off", 219, 5, 32, 28}}};

// ov2 0206ac58: angle of a quarter-turn index (02086ef8).
float rotationAngle(int r) {
    static const float table[4]{0.0f, 90.0f, 180.0f, -90.0f};
    return table[r & 3];
}

// ov2 0206abe4: nearest quarter turn of an angle.
int quarter(float angle) {
    float a = 45.0f + angle;
    if (a > 360.0f) {
        a -= 360.0f;
    }
    if (a < 0.0f) {
        a += 360.0f;
    }
    return int(a / 90.0f) & 3;
}

float normalized(float a) {
    if (a > 180.0f) {
        a -= 360.0f;
    }
    if (a < -180.0f) {
        a += 360.0f;
    }
    return a;
}

int cellOf(int v, int bias) {
    const int t = v + bias;
    return t / 16 - 16;
} // 020f01e4 (bias 0x100) / 020f01fc (0xd0): division toward zero

int roundHalf(float v) {
    return int(0.5f + v);
} // 0203a290(0.5, v) then 0203aed0
} // namespace

// Where a part's top-left corner lies relative to its rotation centre, for
// r quarter turns.
int CameraGame::offsetX(const Part& p, int r) {
    const int t[4]{p.pivotX, p.height - p.pivotY, p.width - p.pivotX, p.pivotY};
    return t[r & 3];
}

int CameraGame::offsetY(const Part& p, int r) {
    const int t[4]{p.pivotY, p.pivotX, p.height - p.pivotY, p.width - p.pivotX};
    return t[r & 3];
}

CameraGame::CameraGame(GraphicsAssets& assets, PresentFrame present, AudioService* audio,
                       GameState& state)
    : kit_(assets, std::move(present), audio, SceneKind::Camera), state_(state) {
    // 020efda0: room sound 9, objects, backgrounds, the part layout, cm_txt.plz.
    // The turned parts go through the DS pipeline with its edge
    // anti-aliasing (ov2 0206ecfc, camera 0206d8a4); partly covered edge
    // pixels mix with the picture (measured on a part caught turning).
    kit_.ds3D = SceneKit::Camera3D{dspipe::spriteCamera(), true, false};
    kit_.clearText(SceneKit::Lower);
    kit_.clearText(SceneKit::Upper);
    if (auto s = kit_.sound()) {
        s->roomSound(9, true);
    }
    exit_ = kit_.button("subgame/camera/%s/modoru_btn.spr", 0, 0, 4);
    cursor_ = kit_.load("subgame/camera/%s/mov_rot.spr", 0, 0);
    parts_ = kit_.load("subgame/camera/camera_parts.spr", 0, 0);
    icons_ = kit_.load("subgame/camera/camera_p_icon.spr", 0, 0);
    arrows_ = kit_.load("subgame/camera/camera_icon_arrow.spr", 0, 0);
    congratulation_ = kit_.load("subgame/camera/?/congratulation.spr", 0, 0);
    photoWindow_ = kit_.load("subgame/photo/photo_window.spr", 0, 0);
    kit_.lowerBackground = "subgame/camera/camera_as_bg";
    kit_.upperBackground = "subgame/camera/camera_as_sub";
    reset();
    if (auto s = kit_.sound()) {
        while (!s->bgmSettled()) {
            kit_.wait(1);
        }
    }
}

// Puts every collected part back into the tray (or where the save says).
void CameraGame::reset() {
    // 020f0214.
    state_100 = 100;
    timer_ = 0;
    touchX_ = touchY_ = -1;
    dragging_ = -1;
    scroll_ = 0;
    owned_.clear();
    for (int i = 0; i < 10; ++i) {
        if (state_.progress.bit(0x24c, 10, std::uint32_t(i))) {
            owned_.push_back(i);
        }
    }
    order_ = owned_;
    for (int i = 0; i < 10; ++i) {
        auto& p = parts[std::size_t(i)];
        const auto& r = kParts[std::size_t(i)];
        p = Part{};
        p.pivotX = r.pivotX;
        p.pivotY = r.pivotY;
        p.width = r.cellsW << 4;
        p.height = r.cellsH << 4;
        p.rotation = r.rotation & 3;
        p.target = p.angle = rotationAngle(p.rotation);
    }
    for (const int i : owned_) {
        auto& p = parts[std::size_t(i)];
        p.present = true;
        const unsigned packed = state_.progress.u16(0x24eu + std::size_t(i) * 2); // 020829bc
        if (!((packed >> 14) & 1)) {
            continue;
        }
        p.rotation = int(packed >> 12) & 3;
        p.cellX = int(packed & 0x3f);
        p.cellY = int(packed >> 6) & 0x3f;
        p.px = p.cellX * 16;
        p.py = p.cellY * 16 + 0x30; // 020f01d0 / 020f01d8
        p.target = rotationAngle(p.rotation);
        p.drawX = p.px + offsetX(p, p.rotation);
        p.drawY = p.py + offsetY(p, p.rotation);
        p.fx = float(p.drawX);
        p.fy = float(p.drawY);
        p.angle = p.target;
    }
    rebuildTray(-1);
}

// Stores each part's cell and rotation in the save.
void CameraGame::save() {
    // 020f003c: each present part as placed flag | rotation << 12 | y << 6 | x.
    for (int i = 0; i < 10; ++i) {
        const auto& p = parts[std::size_t(i)];
        if (!p.present) {
            continue;
        }
        std::uint16_t v = 0;
        if (p.cellX >= 0 && p.cellY >= 0) {
            v = std::uint16_t((p.cellX & 0x3f) | ((p.rotation & 3) << 12) |
                              ((p.cellY & 0x3f) << 6) | 0x4000);
        }
        state_.progress.put16(0x24eu + std::size_t(i) * 2, v); // 020829e0
    }
}

// Every part on its target cell, the right way round.
bool CameraGame::complete() const {
    // 020f0508: every part present, at its target cell and unrotated.
    for (int i = 0; i < 10; ++i) {
        const auto& p = parts[std::size_t(i)];
        const auto& r = kParts[std::size_t(i)];
        if (!p.present || p.cellX != r.targetX || p.cellY != r.targetY || p.rotation != 0) {
            return false;
        }
    }
    return true;
}

// Refills the five tray slots with the parts not on the grid.
void CameraGame::rebuildTray(int keep) {
    // 020f0d54: unplaced owned parts (except the one in hand), scroll clamp.
    tray_.clear();
    for (const int i : owned_) {
        const auto& p = parts[std::size_t(i)];
        if (i != dragging_ && (p.cellX < 0 || p.cellY < 0)) {
            tray_.push_back(i);
        }
    }
    const int count = int(tray_.size());
    if (count <= scroll_ + 5) {
        scroll_ = count - 6;
    }
    if (scroll_ < 1) {
        scroll_ = 0;
    }
    if (keep < 0) {
        return;
    }
    for (int k = 0; k < int(owned_.size()); ++k) {
        if (owned_[std::size_t(k)] != keep) {
            continue;
        }
        if (count <= k + 5) {
            scroll_ = count - 6;
        }
        if (k <= scroll_) {
            scroll_ = k;
        }
        return;
    }
}

// Picks up the part under the stylus (from the tray or the grid).
bool CameraGame::pick(int x, int y) {
    // 020f0584: from the tray (y < 0x30) or the topmost placed part under the point.
    if (dragging_ >= 0) {
        return false;
    }
    if (y < 0x30) {
        const int t = x + 0x1d0;
        const unsigned slot = unsigned(std::uint16_t(t / 32 - 16)); // 020f01b8
        if (slot < 5) {
            const std::size_t at = std::size_t(int(slot) + scroll_);
            const unsigned part = at < tray_.size() ? unsigned(tray_[at]) : 0xffffu;
            if (part < 10 && parts[part].present) {
                auto& p = parts[part];
                if (p.cellX < 0 || p.cellY < 0) {
                    dragging_ = int(part);
                    p.cellX = 0;
                    p.cellY = 0;
                    p.drawX = x;
                    p.drawY = y;
                    p.px = x - offsetX(p, p.rotation);
                    p.py = y - offsetY(p, p.rotation);
                    p.fx = float(p.px);
                    p.fy = float(p.py);
                }
            }
        }
    } else {
        for (int k = int(order_.size()) - 1; k >= 0 && dragging_ < 0; --k) {
            const int i = order_[std::size_t(k)];
            auto& p = parts[std::size_t(i)];
            if (p.cellX < 0 || p.cellY < 0) {
                continue;
            }
            const int r = p.rotation & 3;
            const int ox[4]{p.pivotX, -p.pivotY, -p.pivotX, p.pivotY},
                oy[4]{p.pivotY, p.pivotX, -p.pivotY, -p.pivotX};
            const int baseX = p.drawX - ox[r], baseY = p.drawY - oy[r];
            for (const auto& q : kParts[std::size_t(i)].rects) {
                const int x0 = q[0], y0 = q[1], x1 = q[0] + q[2], y1 = q[1] + q[3];
                const int L[4]{x0, -y1, -x1, y0}, R[4]{x1, -y0, -x0, y1}, T[4]{y0, x0, -y1, -x1},
                    B[4]{y1, x1, -y0, -x0};
                if (baseX + L[r] <= x && x <= baseX + R[r] - 1 && baseY + T[r] <= y &&
                    y <= baseY + B[r] - 1) {
                    dragging_ = i;
                    break;
                }
            }
        }
        if (dragging_ >= 0) {
            parts[std::size_t(dragging_)].savedRotation = parts[std::size_t(dragging_)].rotation;
        }
    }
    if (dragging_ < 0) {
        return false;
    }
    // The part goes to the top of the drawing order.
    for (std::size_t k = 0; k < order_.size(); ++k) {
        if (order_[k] == dragging_) {
            order_.erase(order_.begin() + std::ptrdiff_t(k));
            order_.push_back(dragging_);
            break;
        }
    }
    kit_.effect(0x191);
    rebuildTray(-1);
    return true;
}

// Drops the held part on the grid if it fits there, else back to the tray.
bool CameraGame::drop(int x, int y) {
    // 020f09c8: back to the tray above y 0x30, otherwise snapped to the grid
    // and clamped; at its target, unrotated, it sounds 0x193 (else 0x192).
    (void)x;
    if (dragging_ < 0) {
        return false;
    }
    auto& p = parts[std::size_t(dragging_)];
    bool plain = true;
    int keep = dragging_;
    if (y < 0x30) {
        p.cellX = p.cellY = -1;
        p.fx = p.fy = 0;
        p.drawX = p.drawY = 0;
        p.px = p.py = 0;
    } else {
        const int r = p.rotation & 3;
        const int extraX = rotatedWidth(p, r) / 16 - 1, extraY = rotatedHeight(p, r) / 16 - 1;
        keep = -1;
        p.cellX = cellOf(p.drawX - offsetX(p, r), 0x100);
        p.cellY = cellOf(p.drawY - offsetY(p, r), 0xd0);
        if (p.cellX < 0) {
            p.cellX = 0;
        }
        if (p.cellY < 0) {
            p.cellY = 0;
        }
        if (p.cellX + extraX > 15) {
            p.cellX = 15 - extraX;
        }
        if (p.cellY + extraY > 7) {
            p.cellY = 7 - extraY;
        }
        p.px = p.cellX * 16;
        p.py = p.cellY * 16 + 0x30;
        p.drawX = p.px + offsetX(p, r);
        p.drawY = p.py + offsetY(p, r);
        const auto& t = kParts[std::size_t(dragging_)];
        if (p.rotation == 0 && p.cellX == t.targetX && p.cellY == t.targetY) {
            kit_.effect(0x193);
            plain = false;
        }
    }
    dragging_ = -1;
    if (plain) {
        kit_.effect(0x192);
    }
    rebuildTray(keep);
    return true;
}

// One frame of input: drag parts, turn a part by dragging around it (snapped
// to quarter turns), scroll the tray, quit; returns true when the scene ends.
bool CameraGame::logic() {
    // 020f0f00.
    if (researchTrace) {
        researchTrace(kit_.polls - 1, "state " + std::to_string(state_100) + " drag " +
                                          std::to_string(dragging_) + " scroll " +
                                          std::to_string(scroll_) + " tray " +
                                          std::to_string(tray_.size()));
    }
    if (state_100 < 400) {
        kit_.updateButton(exit_);
        if (kit_.clicked(exit_)) {
            state_.scene = scene::Menu;
            if (auto s = kit_.sound()) {
                s->fadeOutBgm(s->duration(0x3e9));
            }
            kit_.fadeOut();
            return true;
        }
    }
    if (state_100 == 500) {
        state_.eventId = 0x47fe;
        state_.scene = scene::DramaEvent;
        if (auto s = kit_.sound()) {
            s->stopBgm(0);
        }
        kit_.fadeOut();
        return true;
    }
    auto finishMove = [&] {
        dragging_ = -1;
        state_100 = 100;
        touchX_ = touchY_ = -1;
        if (complete()) {
            if (auto s = kit_.sound()) {
                s->bgmVolume(0, s->duration(0x4b0));
            }
            state_100 = 400;
            timer_ = 0;
        }
    };
    switch (state_100) {
    case 100: {
        if (!kit_.pressed()) {
            break;
        }
        int x = -1, y = -1;
        kit_.pressPosition(x, y);
        if (pick(x, y)) {
            const auto& p = parts[std::size_t(dragging_)];
            const int r = p.rotation & 3;
            const int sx = std::max(8, rotatedWidth(p, r) >> 2),
                      sy = std::max(8, rotatedHeight(p, r) >> 2);
            const float dx = float(x - p.drawX) / float(sx), dy = float(y - p.drawY) / float(sy);
            state_100 = dx * dx + dy * dy <= 1.0f ? 200 : 300;
            touchX_ = x;
            touchY_ = y;
            break;
        }
        for (int i = 0; i < 2; ++i) {
            const bool enabled = i == 0 ? scroll_ > 0 : scroll_ + 5 < int(tray_.size());
            if (!enabled) {
                continue;
            }
            const auto& a = kArrows[std::size_t(i)];
            if (x >= a.x && y >= a.y && x < a.x + a.w && y < a.y + a.h) {
                kit_.effect(400);
                state_100 = 600 + i;
                touchX_ = x;
                touchY_ = y;
                break;
            }
        }
        break;
    }
    case 200: { // moving a part
        if (!kit_.released()) {
            kit_.position(touchX_, touchY_);
            auto& p = parts[std::size_t(dragging_)];
            const int r = p.rotation & 3;
            p.px = cellOf(touchX_ - offsetX(p, r) + 8, 0x100) * 16;
            p.py = cellOf(touchY_ - offsetY(p, r) + 8, 0xd0) * 16 + 0x30;
            p.drawX = p.px + offsetX(p, r);
            p.drawY = p.py + offsetY(p, r);
            break;
        }
        if (dragging_ >= 0) {
            const auto& p = parts[std::size_t(dragging_)];
            drop(touchX_, touchY_);
            (void)p;
        }
        finishMove();
        break;
    }
    case 300: { // turning a part about its centre
        if (!kit_.released()) {
            if (dragging_ < 0) {
                break;
            }
            int nx = touchX_, ny = touchY_;
            kit_.position(nx, ny);
            auto& p = parts[std::size_t(dragging_)];
            const float a0 = float(
                std::atan2(double(float(touchY_ - p.drawY)), double(float(touchX_ - p.drawX))));
            const float a1 =
                float(std::atan2(double(float(ny - p.drawY)), double(float(nx - p.drawX))));
            float delta = 180.0f * (a1 - a0) / 3.14159274f;
            delta = normalized(delta);
            p.rotation = quarter(delta + rotationAngle(p.savedRotation));
            p.target = normalized(rotationAngle(p.rotation));
            break;
        }
        if (dragging_ >= 0) {
            auto& p = parts[std::size_t(dragging_)];
            p.rotation = quarter(p.target);
            p.target = rotationAngle(p.rotation);
            drop(touchX_, touchY_);
        }
        finishMove();
        break;
    }
    case 400:
        if (++timer_ >= 0x3c) {
            if (auto s = kit_.sound()) {
                s->playStream(0, 5, -1, 1.0f, 0, false); // 0207fb84
            }
            state_.progress.setBit(0x24c, 16, 10, true); // CameraSolved (0208294c)
            ++state_100;
            timer_ = 0;
        }
        break;
    case 401:
        if (++timer_ >= 0x78) {
            ++state_100;
            timer_ = 0;
        }
        break;
    case 402:
        if (!kit_.pressed()) {
            break;
        }
        kit_.clearText(SceneKit::Lower);
        kit_.clearText(SceneKit::Upper);
        kit_.textCentered(0x5c, kit_.text.expand(kit_.member("cm", "msg_cam.txt")), 5, 0x80,
                          SceneKit::Lower);
        kit_.effect(0x78);
        ++state_100;
        timer_ = 0;
        break;
    case 403:
        if (!kit_.pressed()) {
            break;
        }
        kit_.clearText(SceneKit::Lower);
        kit_.clearText(SceneKit::Upper);
        kit_.effect(0x78);
        state_100 = 500;
        timer_ = 0;
        break;
    case 600:
    case 601: {
        if (!kit_.released()) {
            kit_.position(touchX_, touchY_);
            break;
        }
        const auto& a = kArrows[std::size_t(state_100 - 600)];
        if (touchX_ >= a.x && touchY_ >= a.y && touchX_ < a.x + a.w && touchY_ < a.y + a.h) {
            scroll_ = (scroll_ + (state_100 - 600) * 2 + 9) % 10;
        }
        state_100 = 100;
        break;
    }
    default:
        break;
    }
    // Easing of the parts in the drawing order (0.5 per frame, snapping
    // under half a pixel or one degree).
    for (const int i : order_) {
        auto& p = parts[std::size_t(i)];
        auto ease = [](float& f, int target) {
            const float t = float(target);
            if (std::fabs(t - f) >= 0.5f) {
                f = f + 0.5f * (t - f);
            } else {
                f = t;
            }
        };
        ease(p.fx, p.drawX);
        ease(p.fy, p.drawY);
        const float d = normalized(p.target - p.angle);
        if (std::fabs(d) >= 1.0f) {
            p.angle = normalized(p.angle + 0.5f * d);
        } else {
            p.angle = p.target;
        }
    }
    if (scroll_ < 0) {
        scroll_ = 0;
    }
    if (scroll_ > 5) {
        scroll_ = 5;
    }
    return false;
}

// Draws the grid, the parts and the tray.
void CameraGame::draw() {
    // 020f1bac.
    if (!owned_.empty()) {
        for (int at = scroll_, k = 0; at < int(tray_.size()) && k < 5; ++at, ++k) {
            const unsigned part = unsigned(tray_[std::size_t(at)]);
            if (part >= 10) {
                continue;
            }
            kit_.select(icons_, kParts[part].name);
            icons_.x = (at - scroll_) * 0x20 + 0x30 + 0x10;
            icons_.y = 0x12;
            kit_.drawRotated(icons_, 0);
        }
        for (int i = 0; i < 2; ++i) {
            const bool enabled = i == 0 ? scroll_ > 0 : scroll_ + 5 < int(tray_.size());
            const auto& a = kArrows[std::size_t(i)];
            kit_.select(arrows_, !enabled ? a.none : state_100 == 600 + i ? a.on : a.off);
            arrows_.x = a.x;
            arrows_.y = a.y;
            kit_.draw(arrows_);
        }
        for (const int i : order_) {
            auto& p = parts[std::size_t(i)];
            if (p.cellX < 0 || p.cellY < 0) {
                continue;
            }
            const int r = p.rotation & 3;
            float a = p.angle;
            if (a < 0.0f) {
                a += 360.0f;
            }
            const unsigned turns = unsigned(65536.0f * a / 360.0f); // 0203af04
            kit_.select(parts_, kParts[std::size_t(i)].name);
            parts_.x = roundHalf(p.fx) + (rotatedWidth(p, r) >> 1) - offsetX(p, r);
            parts_.y = roundHalf(p.fy) + (rotatedHeight(p, r) >> 1) - offsetY(p, r);
            kit_.drawRotated(parts_, turns);
        }
    }
    if ((state_100 == 200 || state_100 == 300) && dragging_ >= 0) {
        const auto& p = parts[std::size_t(dragging_)];
        kit_.select(cursor_, state_100 == 200 ? "gfx" : "rot");
        cursor_.x = roundHalf(p.fx);
        cursor_.y = roundHalf(p.fy);
        kit_.drawRotated(cursor_, 0);
    }
    if (state_100 == 401 || state_100 == 402) {
        congratulation_.x = 0x2f;
        congratulation_.y = 0x67;
        kit_.draw(congratulation_);
    }
    if (state_100 == 403) {
        photoWindow_.x = 4;
        photoWindow_.y = 0x34;
        kit_.draw(photoWindow_);
    }
    if (state_100 >= 400 && state_100 < 600) {
        return;
    }
    kit_.drawButton(exit_);
}

// Runs the repair until the player quits or the camera is complete.
void CameraGame::run() {
    // 020f0e90 (draw, present, then logic each frame), then 020f003c saves.
    kit_.poll();
    draw();
    kit_.present();
    kit_.fadeIn();
    for (;;) {
        kit_.poll();
        draw();
        kit_.present();
        if (logic()) {
            break;
        }
    }
    save();
    // 02069f4c: returning to an event restores the bag's saved BGM.
    if (state_.scene == scene::DramaEvent) {
        if (auto s = kit_.sound()) {
            s->switchBgm(state_.savedBgm);
        }
    }
}
} // namespace picarat
