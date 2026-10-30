// The tutorial popup over a level (ScenePopupTutorial, layout guis/popup_tutorial.gui) drawn
// from its layout file: ScenePopupTutorial::render @0x225858 dims the level with black at half
// the popup's alpha, then draws the layout's items (GuiItem::render @0x1d85ec: the item's
// picture stretched over its box, then its children). The texts and pictures the layout
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
    float alpha = 1.0f;
};

void LevelView::drawGuiNode(const data::GuiNode& node, float parentX, float parentY, const PopupContent& content) {
    const game::LevelNotice& notice = content.notice->notice();
    // Items the popup hides (ScenePopupTutorial::load): the picture when an extra text is
    // shown, the extra text otherwise, the wifi line unless asked for.
    const bool extra = !notice.bodyExtraText.empty();
    if ((node.name == "TUTORIAL_IMG" && extra) || (node.name == "LBL_BODY_EXTRA" && !extra) ||
        (node.name == "LBL_BLUETOOTH_WIFI_HINT" && !notice.wifiHint))
        return;
    Box box{node.position.x, node.position.y, node.size.x, node.size.y};
    if (node.relative) {
        box.x += parentX;
        box.y += parentY;
    }
    const float alpha = node.colour.a * content.alpha;
    auto drawPicture = [&](const std::string& path, const Box& b, bool flip) {
        const platform::Texture* t = texture(path);
        if (!t) return;
        platform::DrawStyle style;
        style.r = node.colour.r;
        style.g = node.colour.g;
        style.b = node.colour.b;
        style.a = alpha;
        style.flipX = flip;
        host_->draw(*t, 0, 0, float(t->width()), float(t->height()), b.x, b.top(), b.w, b.h, style);
    };
    auto drawChildren = [&](float x, float y) {
        for (const data::GuiNode& child : node.children) drawGuiNode(child, x, y, content);
    };

    if (node.name == "POSED_MOUSE") {
        // GuiPosedMouse (setPose @0x224608, render @0x224708): the pose picture at its own
        // size (world0<pose>/mm_mouse.png, squeek_mode for pose 0), then the eyes at the
        // pose's fraction of the picture (g_vMouseEyeOffset), shut while blinking.
        drawChildren(box.x, box.y);
        const game::PosedMouse& mouse = content.notice->mouse();
        const int pose = mouse.pose();
        const std::string dir = pose == 0 ? std::string("ui/mouse_pose/squeek_mode/")
                                          : "ui/mouse_pose/world0" + std::to_string(pose) + "/";
        const platform::Texture* body = texture(dir + "mm_mouse.png");
        if (!body) return;
        Box b{box.x, box.y, float(body->width()), float(body->height())};
        drawPicture(dir + "mm_mouse.png", b, mouse.flipped());
        // Eye pictures per pose: open, left, (none), right, shut; poses 4, 5 and 7 have no
        // open eyes (GuiPosedMouse::loadEyeTextures @0x2244b4).
        static const char* const kEyes[5] = {"open", "left", nullptr, "right", "closed"};
        const int slot = mouse.eyesShut() ? 4 : std::clamp(mouse.eyes(), 0, 3);
        if (!kEyes[slot] || (slot == 0 && ((0xb0 >> pose) & 1))) return;
        const std::string eyesPath = dir + "mm_mouse_" + kEyes[slot] + ".png";
        const platform::Texture* eyes = texture(eyesPath);
        if (!eyes || pose < 0 || pose >= int(eyeOffsets_.size())) return;
        float fx = eyeOffsets_[std::size_t(pose)].x, fy = eyeOffsets_[std::size_t(pose)].y;
        Box e{b.x + fx * b.w, b.y + fy * b.h, float(eyes->width()), float(eyes->height())};
        if (mouse.flipped()) e.x = b.x + (1.0f - fx) * b.w - e.w;
        drawPicture(eyesPath, e, mouse.flipped());
        return;
    }

    std::string image = node.image;
    if (node.name == "TUTORIAL_IMG" && !notice.image.empty()) image = notice.image;
    if (!image.empty()) drawPicture(image, box, false);

    if (node.label) {
        const data::GuiLabelFields& l = *node.label;
        std::string text;
        if (strings_) {
            if (node.name == "LBL_TITLE") text = strings_->text(notice.titleText);
            else if (node.name == "LBL_BODY") text = strings_->text(notice.bodyText);
            else if (node.name == "LBL_BODY_EXTRA") text = strings_->text(notice.bodyExtraText);
            else if (strings_->id(l.key) >= 0) text = strings_->text(l.key);
            else text = l.text;
        }
        float scale = l.scale;
        // updateScaleTextToLabel @0x19d634: shrunk to the label's width.
        if (node.name == "LBL_TITLE" || node.name == "LBL_BLUETOOTH_WIFI_HINT") scale = fitScale(node, text);
        drawGuiLabel(node, box.x, box.y, box.w, box.h, text, scale, content.alpha, {});
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
    drawChildren(box.x, box.y);
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
    const float alpha = std::clamp(notice.alpha(), 0.0f, 1.0f);
    host_->fillRect(0, 0, 480.0f, kScreenH, std::uint32_t(std::lround(alpha * 0.5f * 255.0f)));
    PopupContent content;
    content.notice = &notice;
    content.alpha = alpha;
    for (const data::GuiNode& node : popupLayout_->root.children) drawGuiNode(node, 0.0f, 0.0f, content);
}

}  // namespace doc::render
