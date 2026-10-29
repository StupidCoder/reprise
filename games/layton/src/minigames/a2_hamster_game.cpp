// The hamster (overlay 15): the item screen and the scene loop. The walk
// itself is in a2_hamster_walk.cpp. See a2_hamster_game.hpp for how it plays.
#include "minigames/a2_hamster_game.hpp"
#include "story/a2_story_game.hpp"
#include <cstdio>

namespace picarat {

// Steps one walk must reach to pass each level.
int HamsterGame::goalSteps(int level) {
    static constexpr int table[6] = {0, 8, 14, 18, 24, 30};
    return table[std::min(level, 5)];
}

// The name the player gave the hamster (save +0x2f7).
std::string HamsterGame::hamsterName() const {
    return std::string(reinterpret_cast<const char*>(&state_.progress.bytes[0x2f7]));
}

// A text of the hamster minigame (rc/ge/hm_exp.plz).
std::string HamsterGame::member(const std::string& name) {
    return kit_.text.expand(kit_.member("hm", name));
}

// Alternates between the item screen and the walk until the player leaves
// or level 6 is reached.
void HamsterGame::run() {
    // 020f86c8: 02082ac8 makes sure the hamster has at least level 1.
    if (!state_.progress.bytes[0x283]) {
        state_.progress.bytes[0x283] = 1;
    }
    mode_ = 0;
    level_ = -1;
    itemsCreate();
    // 020f8864: one frame, fade in, then frames until the scene ends.
    frame();
    kit_.fadeIn();
    for (;;) {
        if (!frame()) {
            break;
        }
        if (mode_ == 3) {
            state_.eventId = 0x484e;
            state_.setEventViewed(0x484e, true);
            state_.scene = scene::DramaEvent;
            return;
        }
        if (mode_ == 2) {
            break;
        }
    }
    state_.scene = scene::Menu;
}

// One frame of the current screen.
bool HamsterGame::frame() {
    kit_.poll();
    if (mode_ == 0) {
        itemsLogic();
        itemsDraw();
        if (items_->state == 3) {
            kit_.present();
            kit_.fadeOut();
            itemsDestroy();
            walkCreate();
            mode_ = 1;
            level_ = level();
            frame();
            kit_.fadeIn();
            return true;
        }
        if (items_->state == 4) {
            kit_.present();
            if (auto s = kit_.sound()) {
                s->fadeOutBgm(s->duration(0x3e9));
            }
            kit_.fadeOut();
            mode_ = 2;
            return true;
        }
    } else if (mode_ == 1) {
        walkLogic();
        walkDraw();
        if (walkState() == 600) {
            kit_.present();
            kit_.fadeOut();
            walkDestroy();
            if (level_ != level() && level() == 6) {
                mode_ = 3;
                return true;
            }
            itemsCreate();
            mode_ = 0;
            frame();
            kit_.fadeIn();
            return true;
        }
        if (walkState() == 700) {
            kit_.present();
            if (auto s = kit_.sound()) {
                s->fadeOutBgm(s->duration(0x3e9));
            }
            kit_.fadeOut();
            mode_ = 2;
            return true;
        }
    }
    kit_.present();
    return true;
}

// ---------------------------------------------------------------------------
// Item screen (020f32d0 / 020f38d8 / 020f3dc8 / 020f379c)

void HamsterGame::itemsCreate() {
    if (auto s = kit_.sound()) {
        s->roomSound(0xc, true);
    }
    kit_.clearText(SceneKit::Lower);
    kit_.clearText(SceneKit::Upper);
    kit_.resetDisplay(); // 0206dea4
    items_ = std::make_unique<Items>();
    auto& m = *items_;
    kit_.lowerBackground = "subgame/ham/" + kit_.language + "/itemset_bg";
    kit_.upperBackground = "subgame/ham/" + kit_.language + "/itemset_sub";
    m.back = kit_.button("subgame/ham/%s/modoru_btn.spr", 0, 0, 4);
    m.start = kit_.button("subgame/ham/%s/start_btn.spr", 0, 0, -1);
    m.clear = kit_.button("subgame/ham/%s/clear_btn.spr", 0, 0, 4);
    m.icon = kit_.load("subgame/ham/seticon.spr", 0, 0);
    m.upperIcon = kit_.load("subgame/ham/seticon.sbj", 0, 0);
    m.count = kit_.load("subgame/ham/iten_num.spr", 0, 0);
    m.digits = kit_.load("subgame/ham/yerrow_num.spr", 0, 0);
    m.levelNumber = kit_.load("subgame/ham/level_num.sbj", 0, 0);
    m.levelText = kit_.load("subgame/ham/?/level_mes.sbj", 0, 0);
    m.face = kit_.load("subgame/ham/ham_face" + std::to_string(std::clamp(level(), 1, 5)) + ".spr",
                       10, 0x29);
    itemsLoad(m);
    // Hamster name (02072e34, upper title layer) and the default description.
    kit_.textTitle(0xd, kit_.text.expand(hamsterName()), 0, 0x57, SceneKit::Upper);
    kit_.textTyped(0x24, 0x2d, member("obj_n00.txt"), -1);
    kit_.textTyped(0x11, 0x46, member("obj_e00.txt"), -1);
    if (auto s = kit_.sound()) {
        while (!s->bgmSettled()) {
            kit_.wait(1);
        }
    }
}

void HamsterGame::itemsDestroy() {
    // 020f4360: the field back into save +0x28e (low nibble first, item+1).
    auto& m = *items_;
    std::array<std::uint8_t, 24> packed{};
    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 8; ++col) {
            const int item = m.grid[std::size_t(row)][std::size_t(col)];
            if (item < 0) {
                continue;
            }
            const int i = row * 8 + col;
            packed[std::size_t(i >> 1)] |= std::uint8_t((i & 1) ? ((item + 1) << 4) : (item + 1));
        }
    }
    std::copy(packed.begin(), packed.end(), state_.progress.bytes.begin() + 0x28e);
    items_.reset();
}

// Item screen: clears the field.
void HamsterGame::itemsReset(Items& m) {
    for (int i = 0; i < 10; ++i) {
        m.owned[std::size_t(i)] = owned(i);
        m.placed[std::size_t(i)] = 0;
    }
    for (auto& row : m.grid) {
        row.fill(-1);
    }
}

// Item screen: puts the items saved on the field back.
void HamsterGame::itemsLoad(Items& m) {
    itemsReset(m);
    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 8; ++col) {
            const int i = row * 8 + col;
            int v = state_.progress.bytes[0x28e + std::size_t(i >> 1)];
            if (i & 1) {
                v >>= 4;
            }
            if (v & 15) {
                itemsPlace(m, col, row, (v & 15) - 1);
            }
        }
    }
}

// Item screen: takes an item off the field.
bool HamsterGame::itemsRemove(Items& m, int col, int row) {
    if (col < 0 || col > 7 || row < 0 || row > 5) {
        return false;
    }
    const int item = m.grid[std::size_t(row)][std::size_t(col)];
    if (item < 0 || item > 9) {
        return false;
    }
    --m.placed[std::size_t(item)];
    m.grid[std::size_t(row)][std::size_t(col)] = -1;
    return true;
}

// Item screen: places an item on a cell, replacing what was there (only as
// many of each item as the player owns).
bool HamsterGame::itemsPlace(Items& m, int col, int row, int item) {
    if (col < 0 || col > 7 || row < 0 || row > 5 || item < 0 || item > 9) {
        return false;
    }
    itemsRemove(m, col, row);
    if (m.placed[std::size_t(item)] < m.owned[std::size_t(item)]) {
        ++m.placed[std::size_t(item)];
        m.grid[std::size_t(row)][std::size_t(col)] = item;
        return true;
    }
    return false;
}

// Item screen: the description of an item.
void HamsterGame::itemsShowText(Items& m, int item) {
    m.selected = item;
    kit_.clearText(SceneKit::Upper);
    kit_.textTitle(0xd, kit_.text.expand(hamsterName()), 0, 0x57, SceneKit::Upper);
    char name[32];
    std::snprintf(name, sizeof name, "obj_n%02d.txt", item + 1);
    kit_.textTyped(0x24, 0x2d, member(name), -1);
    std::snprintf(name, sizeof name, "obj_e%02d.txt", item + 1);
    kit_.textTyped(0x11, 0x46, member(name), -1);
}

// Item screen: one frame of input (drag items, start the walk, quit).
void HamsterGame::itemsLogic() {
    auto& m = *items_;
    kit_.updateButton(m.start);
    kit_.updateButton(m.clear);
    kit_.updateButton(m.back);
    if (kit_.clicked(m.clear)) {
        itemsReset(m);
    }
    if (kit_.clicked(m.start)) {
        m.state = 3;
        kit_.effect(0x6f);
        return;
    }
    if (kit_.clicked(m.back)) {
        m.state = 4;
        return;
    }
    // Item bar: 24-pixel slots from x 11; field: 20-pixel cells from (89, 37).
    auto slot = [](int x) {
        return (x + 0x175) / 24 - 16;
    };
    auto column = [](int x) {
        return (x + 0xe7) / 20 - 16;
    };
    auto rowOf = [](int y) {
        return (y + 0x11b) / 20 - 16;
    };
    if (m.state == 0) {
        if (!kit_.pressed()) {
            return;
        }
        int x = -1, y = -1;
        kit_.position(x, y);
        m.item = -1;
        if (y < 0x25) {
            const int i = slot(x);
            if (unsigned(i) < 10 && m.placed[std::size_t(i)] < m.owned[std::size_t(i)]) {
                m.state = 1;
                m.dragX = x;
                m.dragY = y;
                m.fromCol = -1;
                m.fromRow = -1;
                m.item = i;
                ++m.placed[std::size_t(i)];
            }
        } else {
            const int col = column(x), row = rowOf(y);
            if (unsigned(col) < 8 && unsigned(row) < 6 &&
                m.grid[std::size_t(row)][std::size_t(col)] >= 0) {
                m.state = 1;
                m.dragX = x;
                m.dragY = y;
                m.fromCol = col;
                m.fromRow = row;
                m.item = m.grid[std::size_t(row)][std::size_t(col)];
                itemsRemove(m, col, row);
                ++m.placed[std::size_t(m.item)];
            }
        }
        if (m.item >= 0) {
            kit_.effect(0x1ae);
            itemsShowText(m, m.item);
        }
        return;
    }
    if (m.state != 1) {
        if (m.state == 2) {
            m.state = 0;
        }
        return;
    }
    if (!kit_.released()) {
        int x = m.dragX, y = m.dragY;
        kit_.position(x, y);
        m.dragX = x;
        m.dragY = y;
        return;
    }
    auto finish = [&] {
        m.state = 0;
        m.dragX = m.dragY = m.fromCol = m.fromRow = m.item = -1;
        kit_.effect(0x1af);
    };
    if (m.dragY > 0x24) {
        const int col = column(m.dragX), row = rowOf(m.dragY);
        if (unsigned(col) < 8 && unsigned(row) < 6 && (col != 0 || row != 0)) {
            --m.placed[std::size_t(m.item)];
            itemsPlace(m, col, row, m.item);
            finish();
            return;
        }
        if (m.fromCol >= 0 && m.fromRow >= 0) {
            --m.placed[std::size_t(m.item)];
            itemsPlace(m, m.fromCol, m.fromRow, m.item);
            finish();
            return;
        }
    }
    --m.placed[std::size_t(m.item)];
    finish();
}

// Item screen: draws the field and items.
void HamsterGame::itemsDraw() {
    auto& m = *items_;
    kit_.drawButton(m.clear);
    kit_.drawButton(m.start);
    kit_.drawButton(m.back);
    for (int i = 0; i < 10; ++i) {
        const int left = m.owned[std::size_t(i)] - m.placed[std::size_t(i)];
        if (left <= 0) {
            continue;
        }
        const int x = i * 24 + 11;
        kit_.select(m.icon, std::to_string(i + 1));
        m.icon.x = x;
        m.icon.y = 8;
        kit_.draw(m.icon);
        if (left > 1) {
            kit_.select(m.count, std::to_string(left));
            m.count.x = x + 11;
            m.count.y = 0x10;
            kit_.draw(m.count);
        }
    }
    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 8; ++col) {
            const int item = m.grid[std::size_t(row)][std::size_t(col)];
            if (item < 0) {
                continue;
            }
            kit_.select(m.icon, std::to_string(item + 1));
            m.icon.x = col * 20 + 0x59;
            m.icon.y = row * 20 + 0x25;
            kit_.draw(m.icon);
        }
    }
    kit_.draw(m.face);
    // Goal steps (record from level 6 on) and record, three digits each.
    const int lv = level();
    unsigned goal = lv > 5 ? state_.progress.bytes[0x2be] : unsigned(goalSteps(lv));
    for (int k = 0, x = 0x46; k < 3; ++k, x -= 7) {
        kit_.select(m.digits, std::to_string(goal % 10));
        goal /= 10;
        m.digits.x = x;
        m.digits.y = 0x70;
        kit_.draw(m.digits);
    }
    unsigned best = state_.progress.bytes[0x2be];
    for (int k = 0, x = 0x46; k < 3; ++k, x -= 7) {
        kit_.select(m.digits, std::to_string(best % 10));
        best /= 10;
        m.digits.x = x;
        m.digits.y = 0x89;
        kit_.draw(m.digits);
    }
    if (m.item >= 0) {
        kit_.select(m.icon, std::to_string(m.item + 1));
        m.icon.x = m.dragX - 9;
        m.icon.y = m.dragY - 9;
        kit_.draw(m.icon);
    }
    if (m.selected >= 0) {
        kit_.select(m.upperIcon, std::to_string(m.selected + 1));
        m.upperIcon.x = 9;
        m.upperIcon.y = 0x2a;
        kit_.draw(m.upperIcon);
    }
    kit_.select(m.levelNumber, std::to_string(6 - lv));
    m.levelNumber.x = 0x9e;
    m.levelNumber.y = 0xc;
    kit_.draw(m.levelNumber);
    kit_.select(m.levelText, std::to_string(lv));
    m.levelText.x = 0xae;
    m.levelText.y = 0xd;
    kit_.draw(m.levelText);
}
} // namespace picarat
