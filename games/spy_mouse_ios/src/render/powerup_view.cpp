// (1.1.1) The power-up bar (game/powerup_bar.hpp), drawn as SceneGame's GUI items: the slide
// frame's picture (hud/popup_powerup.png) with its title, the scroll frame's rows (backing,
// icon, count) clipped to the list, and the store button. Numbers from
// SceneGame::initGuiElements @0xf9172 and updateHUD @0xfe3ac; checked against runs x_pu2/x_pu3.
#include "render/level_view.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace doc::render {
namespace {

constexpr float kScreenH = 320.0f;
// The card's labels, from its top (y down): the title band and the description's box
// (initGuiElements: 220 wide; placements fitted to runs x_pu2/x_pu4).
constexpr float kCardTitleTop = 5.0f, kCardTitleHeight = 36.0f;
constexpr float kCardTextLeft = 31.0f, kCardTextTop = 41.0f, kCardTextWidth = 220.0f, kCardTextHeight = 108.0f;

const char* powerupIcon(game::Powerup t) {
    switch (t) {
    case game::Powerup::Banana: return "hud/bannana";
    case game::Powerup::FlourBomb: return "hud/bomb";
    case game::Powerup::RollerSkate: return "hud/rollerskate";
    case game::Powerup::Invinciball: return "hud/hamsterball";
    case game::Powerup::Kiska: return "hud/kiska";
    case game::Powerup::XRay: return "hud/xray_specs";
    }
    return "";
}

}  // namespace

// The "New Power-up!" card (SceneGame +0x57c04, a child of the slide frame): its picture,
// title (string 0x711, FNT_HEADING, pale yellow with a dark shadow), the power-up's
// description (0x74c + type, FNT_TXT wrapped, orange, shrunk in steps of 0.05 until it fits:
// updateWordWrapScaleTextToLabel @0x19d530) and the arrow button (item 4), all scaled about
// the card's centre while it bounces.
void LevelView::drawPowerupCard(const game::Session& session, float dy) {
    const game::PowerupBar& bar = session.powerupBar();
    if (!bar.card().visible() || bar.cardType() < 0) return;
    const platform::Texture* card = texture("ui/popup_powerup_info.png");
    if (!card) return;
    const float x = game::PowerupBar::kCardLeft, y = game::PowerupBar::kCardTop + dy;
    const float w = game::PowerupBar::kCardWidth, h = game::PowerupBar::kCardHeight;
    float sx = 1.0f, sy = 1.0f;
    bar.card().scales(sx, sy);
    const float cx = x + w * 0.5f, cy = y + h * 0.5f;
    const Affine xf{sx, cx * (1.0f - sx), sy, cy * (1.0f - sy)};
    platform::DrawStyle plain;
    host_->draw(*card, 0, 0, float(card->width()), float(card->height()), xf.x(x), xf.y(y), w * xf.ax, h * xf.ay, plain);
    if (strings_) {
        data::GuiNode title;
        title.colour = {1.0f, 0xfb / 255.0f, 0x90 / 255.0f, 1.0f};
        data::GuiLabelFields tl;
        tl.font = 2;
        tl.align = 0x22;
        tl.shadow = true;
        tl.shadowColour = {0x72 / 255.0f, 0x23 / 255.0f, 0x0a / 255.0f, 0xcc / 255.0f};
        tl.shadowOffset = {1.0f, -1.0f};
        title.label = tl;
        // (Its box: from 0.2 of the card's width, 0.65 of it wide; the text shrinks to fit.)
        title.size = {w * 0.649999976f, kCardTitleHeight};
        const std::string heading = strings_->text(0x711);
        drawGuiLabel(title, x + w * 0.200000003f, kScreenH - y - kCardTitleTop - kCardTitleHeight, title.size.x,
                     kCardTitleHeight, heading, fitScale(title, heading), 1.0f, xf);
        data::GuiNode text;
        text.colour = {1.0f, 0x73 / 255.0f, 0x33 / 255.0f, 1.0f};
        data::GuiLabelFields l;
        l.font = 0;
        l.align = 0x22;
        l.wrap = true;
        text.label = l;
        const std::string body = strings_->text(0x74c + bar.cardType());
        float scale = 1.0f;
        while (scale > 0.5f && font_ && font_->wrapHeight(*host_, body, kCardTextWidth, scale) > kCardTextHeight)
            scale += -0.0500000007f;
        drawGuiLabel(text, x + kCardTextLeft, kScreenH - y - kCardTextTop - kCardTextHeight, kCardTextWidth,
                     kCardTextHeight, body, scale, 1.0f, xf);
    }
    if (const platform::Texture* arrow = texture("ui/btn_arrow.png")) {
        const float ax = game::PowerupBar::kArrowLeft, ay = game::PowerupBar::kArrowTop + dy;
        host_->draw(*arrow, 0, 0, float(arrow->width()), float(arrow->height()), xf.x(ax), xf.y(ay),
                    float(arrow->width()) * xf.ax, float(arrow->height()) * xf.ay, plain);
        // (Highlighted: its pressed picture pulsing over it, GuiScaleButton::render @0x28a01c.)
        if (const platform::Texture* over = texture("ui/btn_arrow_over.png"); over && bar.tutorial() == 6) {
            platform::DrawStyle pulse;
            pulse.a = std::clamp((std::sin(float(bar.arrowHighlightMs()) * 0.00999999978f) + 1.0f) * 0.5f, 0.0f, 1.0f);
            host_->draw(*over, 0, 0, float(over->width()), float(over->height()), xf.x(ax), xf.y(ay),
                        float(arrow->width()) * xf.ax, float(arrow->height()) * xf.ay, pulse);
        }
    }
}

// A highlighted GuiScaleButton (render @0x28a01c): its pressed picture pulses over it at
// (sin(t x 0.01) + 1) / 2; with a direction an arrow (sprites/arrow.png, pointing down) bobs
// towards it: (1 - pulse) x half its height plus 20 away. Direction 2 (turned 270 degrees, as
// for the power-up and store buttons) puts it right of the button pointing left, centred on
// it, its tip 19 short of the button's right plus that (run l12_digger: 26..45 for the
// power-up button). The other directions are placed by symmetry (HYPOTHESIS); the pulse's
// phase differs from the original's (open item).
void LevelView::drawHighlight(const char* overImage, float x, float y, float w, float h, int highlightMs, int arrowDir) {
    if (highlightMs < 0) return;
    const float pulse = std::clamp((std::sin(float(highlightMs) * 0.00999999978f) + 1.0f) * 0.5f, 0.0f, 1.0f);
    if (const platform::Texture* over = texture(overImage)) {
        platform::DrawStyle style;
        style.a = pulse;
        host_->draw(*over, 0, 0, float(over->width()), float(over->height()), x, y, w, h, style);
    }
    if (arrowDir == 0) return;
    const platform::Texture* arrow = texture("sprites/arrow.png");
    if (!arrow) return;
    const float aw = float(arrow->width()), ah = float(arrow->height());
    const float push = (1.0f - pulse) * ah * 0.5f + 20.0f;
    // (Turned a quarter, the picture's box is ah wide and aw high about the same centre.)
    platform::DrawStyle style;
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    switch (arrowDir) {
    case 2: cx = x + w - 0.5f + push, style.degrees = 90.0; break;   // right, pointing left
    case 1: cx = x + 0.5f - push, style.degrees = 270.0; break;      // left, pointing right
    case 3: cy = y + 0.5f - push; break;                              // above, pointing down
    default: cy = y + h - 0.5f + push, style.degrees = 180.0; break; // below, pointing up
    }
    host_->draw(*arrow, 0, 0, aw, ah, cx - aw * 0.5f, cy - ah * 0.5f, aw, ah, style);
}

// Digger's tutorial over the level (layout guis/powerups_intro_tutorial.gui).
void LevelView::drawDiggerNode(const game::Session& session, const data::GuiNode& node, float parentX, float parentY,
                               const Affine& outer) {
    const game::PowerupBar::Tutorial& d = session.powerupBar().digger();
    float x = node.position.x, y = node.position.y;
    if (node.relative) {
        x += parentX;
        y += parentY;
    }
    const float w = node.size.x, h = node.size.y;
    Affine xf = outer;
    if (node.name == "FRAME_CHARACTER") {
        // GuiSlideFrame: in from the right and out to it, by (1 - progress) x its width.
        if (d.digger.progress() <= 0.0f) return;
        x += (1.0f - d.digger.progress()) * w;
    } else if (node.name == "TUTORIAL_FRAME") {
        if (!d.bubble.visible()) return;
        float sx = 1.0f, sy = 1.0f;
        d.bubble.scales(sx, sy);
        const float cx = x + w * 0.5f, cy = kScreenH - (y + h * 0.5f);
        xf = outer.after(Affine{sx, cx * (1.0f - sx), sy, cy * (1.0f - sy)});
    } else if (node.name == "BTN_CONTINUE" && !d.continueShown) {
        return;
    }
    std::string image = node.image;
    if (node.name == "IMG_DIGGER_POSED") {
        // His pose goes with what he says (SceneGame::setPowerupTutorialText @0xf70d0).
        image = d.text == 0x6a8 ? "ui/store/digger_welcome.png"
                : d.text == 0x6aa ? "ui/store/digger_welcome2.png" : "ui/store/digger_explain.png";
    }
    const float top = kScreenH - (y + h);
    if (!image.empty())
        if (const platform::Texture* t = texture(image)) {
            platform::DrawStyle style;
            style.a = node.colour.a;
            host_->draw(*t, 0, 0, float(t->width()), float(t->height()), xf.x(x), xf.y(top), w * xf.ax, h * xf.ay, style);
        }
    if (node.name == "BTN_CONTINUE")
        drawHighlight(node.pressedImage.c_str(), xf.x(x), xf.y(top), w * xf.ax, h * xf.ay, d.continueHighlightMs, 0);
    if (node.label) {
        std::string text;
        float scale = node.label->scale;
        if (node.name == "LBL_TUTORIAL_TEXT") {
            if (strings_ && d.text >= 0) text = strings_->text(d.text);
            // updateWordWrapScaleTextToLabel @0x19d530: shrunk in steps of 0.05 until it fits.
            while (scale > 0.5f && font_ && font_->wrapHeight(*host_, text, w, scale) > h) scale += -0.0500000007f;
        } else {
            text = node.label->text;
            if (strings_ && strings_->id(node.label->key) >= 0) text = strings_->text(node.label->key);
        }
        drawGuiLabel(node, x, y, w, h, text, scale, 1.0f, xf);
    }
    for (const data::GuiNode& child : node.children) drawDiggerNode(session, child, x, y, xf);
}

void LevelView::drawDigger(const game::Session& session) {
    if (session.powerupBar().tutorial() < 1 || session.powerupBar().tutorial() > 5 || !diggerLayout_) return;
    for (const data::GuiNode& node : diggerLayout_->root.children) drawDiggerNode(session, node, 0.0f, 0.0f, {});
}

void LevelView::drawPowerupBar(const game::Session& session) {
    const game::PowerupBar& bar = session.powerupBar();
    if (bar.slide().progress() <= 0.0f) return;
    const platform::Texture* back = texture("hud/popup_powerup.png");
    if (!back) return;
    // GuiSlideFrame::render: moved up by (1 - progress) x the frame's height (the bar's top
    // and its picture: 319).
    const float dy = -(1.0f - bar.slide().progress()) * (game::PowerupBar::kTop + float(back->height()));
    platform::DrawStyle plain;
    const float barX = game::PowerupBar::kLeft, barY = game::PowerupBar::kTop + dy;
    host_->draw(*back, 0, 0, float(back->width()), float(back->height()), barX, barY, float(back->width()),
                float(back->height()), plain);

    // The title, "Power-ups" (string 0x710; GuiLabelSM in FNT_TXT, pale yellow with a dark
    // brown shadow), 15% in, its line plus 15 below the picture's top.
    if (strings_) {
        data::GuiNode title;
        title.colour = {1.0f, 0xfb / 255.0f, 0x90 / 255.0f, 1.0f};
        data::GuiLabelFields l;
        l.font = 0;
        l.align = 0x22;
        l.shadow = true;
        l.shadowColour = {0x72 / 255.0f, 0x23 / 255.0f, 0x0a / 255.0f, 0xcc / 255.0f};
        l.shadowOffset = {1.0f, -1.0f};  // (HYPOTHESIS: the offset is set from registers)
        title.label = l;
        const float lineH = font_ ? font_->lineHeight() : 14.0f;
        const float w = float(back->width()) * 0.7f, h = lineH + 15.0f;
        const float x = barX + float(back->width()) * 0.15f;
        // (GL y of the label's bottom: the picture's top less its line and 15.)
        const float yGl = kScreenH - barY - h;
        drawGuiLabel(title, x, yGl, w, h, strings_->text(0x710), 1.0f, 1.0f, {});
    }

    // The rows, clipped to the list.
    const float listX = game::PowerupBar::kListLeft, listY = game::PowerupBar::kListTop + dy;
    const float listW = game::PowerupBar::kListWidth, rowH = game::PowerupBar::kRowHeight;
    host_->setClip(listX, listY, listW, game::PowerupBar::kListHeight);
    const platform::Texture* backing = texture("hud/popup_backing.png");
    for (int k = 0; k < game::kPowerupCount; ++k) {
        const game::Powerup t = game::Powerup(game::Profile::kBarOrder[std::size_t(k)]);
        const game::PowerupRecord& r = session.powerups()[std::size_t(t)];
        const float rowY = listY + float(k) * rowH - bar.scroll();
        if (backing && k % 2 == 1)
            host_->draw(*backing, 0, 0, float(backing->width()), float(backing->height()), listX, rowY, listW, rowH, plain);
        // The icon (its "_empty" picture when refused or none are left), 27.5 in, centred.
        const bool enabled = session.powerupEnabled(t), available = session.powerupAvailable(t);
        const std::string icon = std::string(powerupIcon(t)) + (enabled && available ? ".png" : "_empty.png");
        if (const platform::Texture* tex = texture(icon))
            host_->draw(*tex, 0, 0, float(tex->width()), float(tex->height()), listX + 27.5f - float(tex->width()) * 0.5f,
                        rowY + rowH * 0.5f - float(tex->height()) * 0.5f, float(tex->width()), float(tex->height()), plain);
        // The count (FNT_MEDIUM, orange, centred in a 35-wide box 40 from the row's right), or
        // the "active" / "unlimited" picture.
        const char* picture = nullptr;
        if (r.kind == 1 && r.active) picture = "hud/powerup_active.png";
        else if (r.kind == 0 && r.count == -1) picture = "hud/powerup_infinite.png";
        if (picture) {
            if (const platform::Texture* tex = texture(picture))
                host_->draw(*tex, 0, 0, float(tex->width()), float(tex->height()),
                            listX + listW - 40.0f + 17.5f - float(tex->width()) * 0.5f,
                            rowY + rowH * 0.5f - float(tex->height()) * 0.5f, float(tex->width()), float(tex->height()),
                            plain);
        } else {
            data::GuiNode count;
            count.colour = {0xfe / 255.0f, 0x6b / 255.0f, 0x1b / 255.0f, 1.0f};
            data::GuiLabelFields l;
            l.font = 1;
            l.align = 0x22;
            count.label = l;
            drawGuiLabel(count, listX + listW - 40.0f, kScreenH - rowY - rowH, 35.0f, rowH, std::to_string(r.count),
                         1.0f, 1.0f, {});
        }
    }
    host_->clearClip();

    // GuiScrollFrame::renderFade @0x1dbc64 (with GuiScrollFrameSM's frame grown by 1 at both
    // ends): the bar's own picture over the list's top and bottom 15 points, opaque at the
    // edge and clear inward (two triangle strips with vertex alphas 1 and 0).
    {
        const float fade = 15.0f, tw = float(back->width()), th = float(back->height());
        const float top = listY - 1.0f, bottom = listY + game::PowerupBar::kListHeight + 1.0f;
        auto strip = [&](float edge, float inner) {
            auto v = [&](float x, float y, float a) {
                platform::Vertex p;
                p.x = x, p.y = y, p.u = (x - barX) / tw, p.v = (y - barY) / th, p.a = a;
                return p;
            };
            const std::vector<platform::Vertex> vs{v(listX, edge, 1.0f), v(listX + listW, edge, 1.0f),
                                                   v(listX, inner, 0.0f), v(listX + listW, inner, 0.0f)};
            host_->drawTriangles(back, vs, {0, 1, 2, 1, 3, 2}, {});
        };
        strip(top, top + fade);
        strip(bottom, bottom - fade);
    }

    // GuiScrollFrameSM::render @0x296ff8: an arrow under the list while rows follow below, and
    // one turned over above it once it is scrolled down (2 from the list: twice LevelState
    // +0x60, 1 here; HYPOTHESIS: a scale; run x_pu3).
    if (const platform::Texture* arrow = texture("ui/scroll_arrow.png")) {
        // (At x 51.75 GL covers the pixels from 52 on.)
        const float ax = std::ceil(listX + listW * 0.5f - float(arrow->width()) * 0.5f);
        const float maxScroll = float(game::kPowerupCount) * rowH - game::PowerupBar::kListHeight;
        if (bar.scroll() < maxScroll)
            host_->draw(*arrow, 0, 0, float(arrow->width()), float(arrow->height()), ax,
                        listY + game::PowerupBar::kListHeight + 2.0f, float(arrow->width()), float(arrow->height()), plain);
        if (bar.scroll() > 0.0f) {
            platform::DrawStyle flipped;
            flipped.flipY = true;
            host_->draw(*arrow, 0, 0, float(arrow->width()), float(arrow->height()), ax, listY - 2.0f - float(arrow->height()),
                        float(arrow->width()), float(arrow->height()), flipped);
        }
    }

    // The store button (item 5, hud/hud_store.png) 10 in and 14 above the picture's bottom,
    // with "Get More" (string 0x712) one point up from its middle (two here: measured on
    // x_pu3, the font's placement differs by one).
    if (const platform::Texture* store = texture("hud/hud_store.png")) {
        const float sx = barX + 10.0f, sy = barY + float(back->height()) - 14.0f - float(store->height());
        host_->draw(*store, 0, 0, float(store->width()), float(store->height()), sx, sy, float(store->width()),
                    float(store->height()), plain);
        drawHighlight("hud/hud_store_over.png", sx, sy, float(store->width()), float(store->height()),
                      session.storeHighlightMs(), 2);
        if (strings_) {
            data::GuiNode label;
            data::GuiLabelFields l;
            l.font = 0;
            l.align = 0x22;
            label.label = l;
            drawGuiLabel(label, sx, kScreenH - sy - float(store->height()) + 2.0f, float(store->width()),
                         float(store->height()), strings_->text(0x712), 1.0f, 1.0f, {});
        }
    }
    drawPowerupCard(session, dy);
}

}  // namespace doc::render
