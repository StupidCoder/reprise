#include "game/kiska_popup.hpp"

namespace doc::game {

void KiskaPopup::open() {
    active_ = true;
    leaving_ = false;
    // (The frames are made going in; showScene sends them in again: goIn, vtable +0x50.)
    card_ = BounceFrame{};
    kiska_ = BounceFrame{};
    card_.goIn();
    kiska_.goIn();
}

bool KiskaPopup::press(const std::string& button) {
    if (!active_) return false;
    if (button == "BTN_LATER" || button == "BTN_NO") {
        // Both frames go out (goOut, vtable +0x54); the next update pops the scene.
        card_.goOut();
        kiska_.goOut();
        leaving_ = true;
        return true;
    }
    if (button == "BTN_BUY") {
        // Engine::popScene at once; the store purchase that follows is not ported.
        active_ = false;
        return true;
    }
    return false;
}

bool KiskaPopup::update(int dtMs) {
    if (!active_) return false;
    card_.update(dtMs);
    kiska_.update(dtMs);
    if (!leaving_) return false;
    active_ = false;
    return true;
}

namespace {

std::string findButton(const data::GuiNode& node, float parentX, float parentY, Vec2 screen) {
    if (node.name == "FRAME_SKIP") return {};
    float x = node.position.x, y = node.position.y;
    if (node.relative) {
        x += parentX;
        y += parentY;
    }
    if (node.kind == int(data::GuiKind::Button) && !node.name.empty()) {
        const float top = 320.0f - (y + node.size.y);
        if (screen.x >= x && screen.x < x + node.size.x && screen.y >= top && screen.y < top + node.size.y)
            return node.name;
    }
    for (const data::GuiNode& child : node.children) {
        std::string found = findButton(child, x, y, screen);
        if (!found.empty()) return found;
    }
    return {};
}

}  // namespace

std::string KiskaPopup::buttonAt(const data::GuiNode& root, Vec2 screen) {
    return findButton(root, 0.0f, 0.0f, screen);
}

}  // namespace doc::game
