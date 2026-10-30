// The tutorial popup over a level (ScenePopupTutorial, layout guis/popup_tutorial.gui) drawn
// from its layout file (GuiItem::render @0x1d85ec: the item's picture stretched over its box,
// then its children). The texts and pictures the layout
// leaves open come from the popup's description (ScenePopupTutorial::load @0x225208).
#include "render/level_view.hpp"

#include <algorithm>
#include <cmath>

namespace doc::render {
namespace {

constexpr float kScreenH = 320.0f;

// Screen rectangle (y down) of a box given in screen points with y up.
struct Box {
    float x, y, w, h;  // y up: bottom edge
    float top() const { return kScreenH - (y + h); }
};

}  // namespace

// What ScenePopupTutorial::load fills in, and how the GUI items draw themselves.
struct LevelView::PopupContent {
    const game::NoticePopup* notice = nullptr;
    Affine xf;  // the bounce of the card the item sits in
};

void LevelView::drawGuiNode(const data::GuiNode& node, float parentX, float parentY, const PopupContent& outer) {
    const game::LevelNotice& notice = outer.notice->notice();
    // The Wi-Fi line shows only when the description asks for it (ScenePopupTutorial::load:
    // item LBL_BLUETOOTH_WIFI_HINT hidden unless the fifth element is there).
    if (node.name == "LBL_BLUETOOTH_WIFI_HINT" && !notice.wifiHint) return;
    Box box{node.position.x, node.position.y, node.size.x, node.size.y};
    if (node.relative) {
        box.x += parentX;
        box.y += parentY;
    }
    PopupContent content = outer;
    Affine& xf = content.xf;
    if (node.kind == 101) {
        // GuiBounceFrameCM (App::loadUserItem kind 101): the card and all it holds scale about
        // its centre while it bounces in (getScales @0x1dd920).
        const game::BounceFrame& card = content.notice->card();
        if (!card.visible()) return;
        float sx = 1.0f, sy = 1.0f;
        card.scales(sx, sy);
        const float cx = box.x + box.w * 0.5f, cy = box.top() + box.h * 0.5f;
        xf = outer.xf.after(Affine{sx, cx * (1.0f - sx), sy, cy * (1.0f - sy)});
    }
    auto drawPicture = [&](const std::string& path, const Box& b) {
        const platform::Texture* t = texture(path);
        if (!t) return;
        platform::DrawStyle style;
        style.r = node.colour.r;
        style.g = node.colour.g;
        style.b = node.colour.b;
        style.a = node.colour.a;
        host_->draw(*t, 0, 0, float(t->width()), float(t->height()), xf.x(b.x), xf.y(b.top()), b.w * xf.ax,
                    b.h * xf.ay, style);
    };
    if (node.kind == int(data::GuiKind::Rect)) {
        // GuiRect::render @0x20e2a4: a plain box in the item's colour (here the level's dimming).
        const auto byte = [](float v) { return std::uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
        host_->fillRect(xf.x(box.x), xf.y(box.top()), box.w * xf.ax, box.h * xf.ay,
                        (byte(node.colour.r) << 24) | (byte(node.colour.g) << 16) | (byte(node.colour.b) << 8) |
                            byte(node.colour.a));
    }

    std::string image = node.image;
    if (node.name == "TUTORIAL_IMG" && !notice.image.empty()) image = notice.image;  // GuiItem::setImage
    if (!image.empty()) drawPicture(image, box);

    if (node.label) {
        const data::GuiLabelFields& l = *node.label;
        std::string text;
        if (strings_) {
            if (node.name == "LBL_TITLE") text = strings_->text(notice.titleText);
            else if (node.name == "LBL_BODY") text = strings_->text(notice.bodyText);
            else if (strings_->id(l.key) >= 0) text = strings_->text(l.key);
            else text = l.text;
        }
        float scale = l.scale;
        // updateScaleTextToLabel @0x19d634: shrunk to the label's width.
        if (node.name == "LBL_TITLE" || node.name == "LBL_BLUETOOTH_WIFI_HINT") scale = fitScale(node, text);
        drawGuiLabel(node, box.x, box.y, box.w, box.h, text, scale, 1.0f, xf);
    }

    if (node.kind == int(data::GuiKind::ScrollFrame)) {
        // GuiScrollFrame: the children's region (recalcScrollArea @0x1db188, addToScrollRegion
        // @0x1dc500) and an offset that starts at 0 and, once per update, moves a fifth of the
        // way into its limits (updateScrollAxis @0x1dae28, x then y): above the region's
        // negated low edge (less 1.5 when that is negative) and below the frame's size minus the
        // region's high edge (getMinScrollOffset @0x1dada0; this frame has no margin and no
        // reversed axis). The body label is as tall as its wrapped text
        // (ScenePopupTutorial::load: getWrapHeight).
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        std::vector<data::GuiNode> children = node.children;
        for (data::GuiNode& child : children) {
            // (The region is taken as the layout loads, before the body gets its text's height.)
            minX = std::min(minX, child.position.x);
            minY = std::min(minY, child.position.y);
            maxX = std::max(maxX, child.position.x + child.size.x);
            maxY = std::max(maxY, child.position.y + child.size.y);
            if (child.name == "LBL_BODY" && child.label && strings_ && font_)
                child.size.y = font_->wrapHeight(*host_, strings_->text(notice.bodyText), child.size.x, 1.0f);
        }
        float dx = 0.0f, dy = 0.0f;
        if (minX <= maxX) {
            auto ease = [](float offset, float lower, float upper) {
                if (lower < 0.0f) lower = lower + -1.5f;
                if (offset < lower) offset = offset + (lower - offset) * 0.200000003f;
                if (offset > upper) offset = offset + (offset - upper) * -0.200000003f;
                return offset;
            };
            for (int i = 0; i < content.notice->updates(); ++i) {
                dx = ease(dx, -minX, box.w - maxX);
                dy = ease(dy, -minY, box.h - maxY);
            }
        }
        for (const data::GuiNode& child : children) drawGuiNode(child, box.x + dx, box.y + dy, content);
        return;
    }
    for (const data::GuiNode& child : node.children) drawGuiNode(child, box.x, box.y, content);
}
// updateScaleTextToLabel @0x19d634: the label's own scale, or less so that the text fits its
// width.
float LevelView::fitScale(const data::GuiNode& node, const std::string& text) {
    if (!node.label) return 1.0f;
    const data::GuiLabelFields& l = *node.label;
    Font* font = l.font == 2 ? headingFont_.get() : l.font == 1 ? mediumFont_.get() : font_.get();
    if (!font) return l.scale;
    const float w = font->width(text, 1.0f);
    return w > 0.0f ? std::min(1.0f, node.size.x / w) : l.scale;
}

// GuiLabel::render @0x1da064: the anchor moves into the box (x, y up, w, h) by the label's
// alignment, then the text is drawn with its fill, outline and shadow in the label's font.
void LevelView::drawGuiLabel(const data::GuiNode& node, float x, float y, float w, float h, const std::string& text,
                             float scale, float alpha, const Affine& xf) {
    if (!node.label || text.empty()) return;
    const data::GuiLabelFields& l = *node.label;
    Font* font = l.font == 2 ? headingFont_.get() : l.font == 1 ? mediumFont_.get() : font_.get();
    if (!font) return;
    float ax = x, ay = y;
    if (l.align & 0x20) ay += h * 0.5f;
    else if (l.align & 0x10) ay += h;
    if (l.align & 0x02) ax += w * 0.5f;
    else if (l.align & 0x04) ax += w;
    TextStyle style;
    style.fillColourSet = true;
    style.fillColour = {node.colour.r, node.colour.g, node.colour.b, node.colour.a * alpha};
    style.outline = Outline(l.outline);
    style.outlineWidth = l.outlineWidth;
    style.outlineColour = {l.outlineColour.r, l.outlineColour.g, l.outlineColour.b, l.outlineColour.a * alpha};
    style.shadow = l.shadow;
    style.shadowX = l.shadowOffset.x;
    style.shadowY = -l.shadowOffset.y;
    style.shadowColour = {l.shadowColour.r, l.shadowColour.g, l.shadowColour.b, l.shadowColour.a * alpha};
    // renderStr's flags; the vertical ones mirror for the y-down screen.
    const int flags = l.align & 0x77;
    font->drawFlags(*host_, text, ax, kScreenH - ay, flags, scale, l.wrap ? w : 0.0f, style, {1, 1, 1, alpha}, xf);
}

void LevelView::drawNotice(const game::Session& session) {
    const game::NoticePopup& notice = session.notice();
    if (!notice.active() || !popupLayout_) return;
    // ScenePopupTutorial::render @0x225858: the layout (its first item dims the level), then the
    // iris.
    PopupContent content;
    content.notice = &notice;
    for (const data::GuiNode& node : popupLayout_->root.children) drawGuiNode(node, 0.0f, 0.0f, content);
}

}  // namespace doc::render
