// The pause menu drawn from its layout (guis/pause.gui) and the menu's state (game/pause.hpp).
// ScenePause::showScene @0x14e51c hides what a normal level does not use (the best score, the
// boss face, the challenge, the Kiska timer), names the level and fits the three button labels
// to one common scale; ScenePause::render @0x14d428 draws the items over the level.
#include "render/level_view.hpp"

#include <algorithm>
#include <cmath>

namespace doc::render {
namespace {

constexpr float kScreenH = 320.0f;

}  // namespace

void LevelView::drawPause(const game::Session& session) {
    const game::PauseMenu& pause = session.pause();
    if (!pause.active() || !pauseLayout_) return;
    for (const data::GuiNode& node : pauseLayout_->root.children) drawPauseNode(session, node, 0.0f, 0.0f, {});
}

void LevelView::drawPauseNode(const game::Session& session, const data::GuiNode& node, float parentX, float parentY,
                              const Affine& outer) {
    const game::PauseMenu& pause = session.pause();
    if (node.name == "LBL_BEST_SCORE" || node.name == "ITM_BOSS_FACE" || node.name == "ITM_CHALLENGE" ||
        node.name == "KISKA_FRAME")
        return;
    if (node.name == "POPUP_GOAL_INFO") {
        drawGoalPopup(pause.popup(), {pause.goal(0), pause.goal(1), pause.goal(2)}, node);
        return;
    }
    float x = node.position.x, y = node.position.y;
    if (node.relative) {
        x += parentX;
        y += parentY;
    }
    const float w = node.size.x, h = node.size.y;
    Affine xf = outer;
    const game::BounceFrame* frame =
        node.name == "FRAME" ? &pause.card() : node.name == "FRAME_HELPOPTIONS" ? &pause.corner() : nullptr;
    if (frame) {
        if (!frame->visible()) return;
        float sx = 1.0f, sy = 1.0f;
        frame->scales(sx, sy);
        const float cx = x + w * 0.5f, cy = kScreenH - (y + h * 0.5f);
        xf = outer.after(Affine{sx, cx * (1.0f - sx), sy, cy * (1.0f - sy)});
    }

    std::string image = node.image;
    // The ribbons show the level's goals, gold when won before (ScenePause::updateGoalItems
    // @0x14d1c8, GuiLevelGoalButton::setGoalType).
    for (int i = 0; i < 3; ++i)
        if (node.name == "LEVEL_GOAL_BTN_" + std::to_string(i + 1)) {
            const int kind = pause.goal(i).kind;
            if (kind >= 0 && kind < int(goalStars_.size()))
                image = pause.goal(i).wonBefore ? goalStars_[std::size_t(kind)].second : goalStars_[std::size_t(kind)].first;
        }
    if (!image.empty())
        if (const platform::Texture* t = texture(image)) {
            platform::DrawStyle style;
            style.r = node.colour.r;
            style.g = node.colour.g;
            style.b = node.colour.b;
            style.a = node.colour.a;
            const float top = kScreenH - (y + h);
            host_->draw(*t, 0, 0, float(t->width()), float(t->height()), xf.x(x), xf.y(top), w * xf.ax, h * xf.ay, style);
        }

    if (node.label) {
        const data::GuiLabelFields& l = *node.label;
        const game::Level& level = session.level();
        std::string text;
        float scale = l.scale;
        data::GuiNode shown = node;
        if (node.name == "LBL_LEVEL_NAME") {
            if (strings_) text = strings_->text(level.info().nameKey);
            scale = fitScale(node, text);
        } else if (node.name == "LBL_LEVEL_NUMBER") {
            text = level.info().number;
        } else if (node.name == "LBL_RESUME" || node.name == "LBL_RETRY" || node.name == "LBL_EXIT_MAP") {
            // Centred (0x22) and all three at the smallest of their fitting scales.
            if (strings_) text = strings_->text(l.key);
            shown.label->align = 0x22;
            scale = 1.0f;
            for (const char* name : {"LBL_RESUME", "LBL_RETRY", "LBL_EXIT_MAP"})
                if (const data::GuiNode* other = pauseLayout_->root.find(name); other && other->label && strings_)
                    scale = std::min(scale, fitScale(*other, strings_->text(other->label->key)));
        } else if (strings_ && strings_->id(l.key) >= 0) {
            text = strings_->text(l.key);
            if (node.name == "LBL_TAP_RIBBONS") scale = fitScale(node, text);
        } else {
            text = l.text;
        }
        drawGuiLabel(shown, x, y, w, h, text, scale, 1.0f, xf);
    }

    for (const data::GuiNode& child : node.children) drawPauseNode(session, child, x, y, xf);
}

}  // namespace doc::render
