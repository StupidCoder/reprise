// Kiska's offer over the level (SceneLevelSkip, layout guis/level_skip.gui (1.0.5: levelSkip.gui); see
// game/kiska_popup.hpp). SceneLevelSkip::render @0x1fa8d0 draws its items (Scene::render),
// then a black cover at 1 - its fade (+0xc4, 1 while the offer shows: nothing). Its two bounce
// frames, Kiska and the card, scale about their centres as they come in and go out.
// showScene @0x1fb994 hides the frame for a bought Kiska (FRAME_SKIP) and shrinks the titles
// and button captions to fit their labels (updateScaleTextToLabel).
#include "render/level_view.hpp"

namespace doc::render {

void LevelView::drawKiskaNode(const game::KiskaPopup& popup, const data::GuiNode& node, float parentX, float parentY,
                              const Affine& outer) {
    if (node.name == "FRAME_SKIP") return;
    float x = node.position.x, y = node.position.y;
    if (node.relative) {
        x += parentX;
        y += parentY;
    }
    const float w = node.size.x, h = node.size.y;
    Affine xf = outer;
    const game::BounceFrame* frame =
        node.name == "FRAME" ? &popup.card() : node.name == "FRAME_KISKA" ? &popup.kiska() : nullptr;
    if (frame) {
        if (!frame->visible()) return;
        float sx = 1.0f, sy = 1.0f;
        frame->scales(sx, sy);
        const float cx = x + w * 0.5f, cy = game::kScreenHeight - (y + h * 0.5f);
        xf = outer.after(Affine{sx, cx * (1.0f - sx), sy, cy * (1.0f - sy)});
    }
    if (!node.image.empty()) {
        if (const platform::Texture* t = texture(node.image)) {
            platform::DrawStyle style;
            style.r = node.colour.r;
            style.g = node.colour.g;
            style.b = node.colour.b;
            style.a = node.colour.a;
            const float top = game::kScreenHeight - (y + h);
            host_->draw(*t, 0, 0, float(t->width()), float(t->height()), xf.x(x), xf.y(top), w * xf.ax, h * xf.ay,
                        style);
        }
    }
    if (node.label) {
        const data::GuiLabelFields& l = *node.label;
        std::string text = l.text;
        if (strings_ && strings_->id(l.key) >= 0) text = strings_->text(l.key);
        float scale = l.scale;
        if (node.name == "LBL_TITLE" || node.name == "LBL_SUBTITLE" || node.name == "LBL_KISKA_TITLE" ||
            node.name == "LBL_LATER" || node.name == "LBL_BUY")
            scale = fitScale(node, text);
        drawGuiLabel(node, x, y, w, h, text, scale, 1.0f, xf);
    }
    for (const data::GuiNode& child : node.children) drawKiskaNode(popup, child, x, y, xf);
}

void LevelView::drawKiska(const game::Session& session) {
    const game::KiskaPopup& popup = session.kiska();
    if (!popup.active() || !kiskaLayout_) return;
    for (const data::GuiNode& node : kiskaLayout_->root.children) drawKiskaNode(popup, node, 0.0f, 0.0f, {});
}

}  // namespace doc::render
