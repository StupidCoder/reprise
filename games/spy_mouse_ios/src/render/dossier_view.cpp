// (1.1.1) The dossier page (SceneDossier::render @0x286ad8) and the message box over it
// (SceneMsgBox, guis/msgbox.gui).
#include "render/level_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace doc::render {

namespace {

constexpr float kScreenH = 320.0f;

// Item positions within a layout by name (relative positions summed).
bool findNode(const data::GuiNode& node, const std::string& name, float px, float py, const data::GuiNode*& found,
              float& x, float& y) {
    const float nx = node.relative ? px + node.position.x : node.position.x;
    const float ny = node.relative ? py + node.position.y : node.position.y;
    if (node.name == name) {
        found = &node;
        x = nx;
        y = ny;
        return true;
    }
    for (const data::GuiNode& child : node.children)
        if (findNode(child, name, nx, ny, found, x, y)) return true;
    return false;
}

}  // namespace

void LevelView::drawDossier(const game::Session& session) {
    const game::DossierScreen& page = session.dossierScreen();
    if (!page.active() || !dossierLayout_ || page.dossier() < 0 || page.dossier() >= int(dossiers_.size())) return;
    const data::DossierData& dossier = dossiers_[std::size_t(page.dossier())];
    platform::Host& host = *host_;
    // Pictures by their corner in screen points with y up (the original's).
    auto picture = [&](const std::string& path, game::Vec2 corner) {
        if (const platform::Texture* t = texture(path)) {
            const float w = float(t->width()), h = float(t->height());
            host.draw(*t, 0, 0, w, h, corner.x, kScreenH - (corner.y + h), w, h);
        }
    };
    // Turned `degrees` counter-clockwise (glRotatef, y up) about `pivot`, the picture's corner
    // `offset` from it.
    auto turned = [&](const std::string& path, game::Vec2 pivot, game::Vec2 offset, float degrees) {
        const platform::Texture* t = texture(path);
        if (!t) return;
        const float w = float(t->width()), h = float(t->height());
        const float a = degrees * 0.0174532924f;
        const float c = std::cos(a), s = std::sin(a);
        const game::Vec2 centre{offset.x + w * 0.5f, offset.y + h * 0.5f};
        const game::Vec2 at{pivot.x + centre.x * c - centre.y * s, pivot.y + centre.x * s + centre.y * c};
        host.draw(*t, 0, 0, w, h, at.x - w * 0.5f, kScreenH - at.y - h * 0.5f, w, h, double(-degrees));
    };
    auto dimScreen = [&](float alpha) {
        host.fillRect(0, 0, game::kScreenWidth, game::kScreenHeight,
                      std::uint32_t(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)));
    };

    // The level darkens to half (+0x130), unless the note is over the page (drawn after it).
    if (page.dim() >= 0.01f && !page.note().active) dimScreen(page.dim());
    picture("ui/dossiers/paper.png", page.paper());

    // Scene::render: the heading, the page's text in its scrolling frame, the back button.
    // updates @0x287316: the heading 30 right and 250 up from the paper's corner; the frame by
    // the paper's corner (HYPOTHESIS: the offset @0x6000c0+0xc8 keeps the frame at its layout
    // place relative to the paper at rest, (220, 22) from the folder's (22, 5)).
    const game::Vec2 paper = page.paper();
    for (const data::GuiNode& node : dossierLayout_->root.children) {
        if (node.name == "DOSSIER_HEADING" && node.label) {
            const std::string text = strings_ ? strings_->text(node.label->key) : node.label->text;
            drawGuiLabel(node, paper.x + 30.0f, paper.y + 250.0f, node.size.x, node.size.y, text, node.label->scale, 1.0f, {});
        } else if (node.name == "BTN_BACK") {
            picture(node.image, {node.position.x, node.position.y});
        }
    }
    const data::GuiNode* frame = nullptr;
    float frameX = 0, frameY = 0;
    for (const data::GuiNode& top : dossierLayout_->root.children)
        if (findNode(top, "SCROLL_FRAME", 0.0f, 0.0f, frame, frameX, frameY)) break;
    if (frame && strings_ && font_ && mediumFont_) {
        const game::Vec2 at{paper.x + (frameX - 242.0f), paper.y + (frameY - 27.0f)};
        const float w = frame->size.x, h = frame->size.y;
        host.setClip(at.x, kScreenH - (at.y + h), w, h);
        // setActiveDossier @0x2862f4: lines from the frame's top down (y up, negative).
        data::GuiNode label;
        label.label = data::GuiLabelFields{};
        auto line = [&](float x, float y, float lineW, const std::string& text, int font, int align, bool wrap,
                        const data::GuiColour& colour, float scale) {
            label.colour = colour;
            label.label->font = font;
            label.label->align = align;
            label.label->wrap = wrap;
            drawGuiLabel(label, at.x + x, at.y + y, lineW, h, text, scale, 1.0f, {});
        };
        const data::GuiColour heading{0x45 / 255.0f, 0x89 / 255.0f, 0xbb / 255.0f, 1.0f};
        const data::GuiColour body{0x89 / 255.0f, 0xad / 255.0f, 200 / 255.0f, 1.0f};
        // The title (getDossierType), FNT_MEDIUM centred, squeezed to the frame less 6.
        const std::string title = strings_->text(dossier.title);
        const float titleW = mediumFont_->width(title);
        line(0.0f, -30.0f, w, title, 1, 0x12, false, heading, titleW > 0.0f ? std::min(1.0f, (w - 6.0f) / titleW) : 1.0f);
        float y = -30.0f - mediumFont_->lineHeight();
        // addLineToScrollingArea @0x285f60: a blue heading line (if any), a dash for list
        // entries (the text then 15 further right), the grey-blue text wrapped to the width.
        auto entry = [&](int headingId, const std::string& text, bool dash) {
            float dy = 0.0f;
            float x = 0.0f, lineW = w;
            if (headingId != -1) {
                line(0.0f, y, w, strings_->text(headingId), 0, 0x11, false, heading, 1.0f);
                dy = font_->lineHeight();
            }
            const float ty = y - dy;
            if (dash) {
                line(0.0f, ty, w, "-", 0, 0x11, true, heading, 1.0f);
                x = 15.0f;
                lineW = w - 15.0f;
            }
            line(x, ty, lineW, text, 0, 0x11, true, body, 1.0f);
            return font_->wrapHeight(host, text, lineW, 1.0f) + dy;
        };
        // "First appearance:" with the place (World N, Level M: name).
        std::string place;
        if (dossier.lastSeen < 0) {
            place = strings_->text(0x707);
        } else if (levelTable_) {
            if (const data::LevelInfo* info = levelTable_->byUid(dossier.lastSeen)) {
                const std::size_t dash = info->number.find('-');
                const int number = dash == std::string::npos ? 1 : std::atoi(info->number.c_str() + dash + 1);
                char buffer[256];
                std::snprintf(buffer, sizeof buffer, "%s %i, %s %i: %s", strings_->text(0x24f).c_str(), info->world + 1,
                              strings_->text(info->mode == "boss" ? 0x694 : 0x602).c_str(), std::max(1, number),
                              strings_->text(info->nameKey).c_str());
                place = buffer;
            }
        }
        // (Each entry goes 30 below the one before, plus its own height.)
        y = y - 30.0f - entry(0x5e6, place, false);
        const int headings[5] = {0x5eb, -1, -1, 0x5ff, -1};
        for (int k = 0; k < 5; ++k) {
            if (dossier.tips[std::size_t(k)] == -1) continue;
            y = y - 30.0f - entry(headings[k], strings_->text(dossier.tips[std::size_t(k)]), true);
        }
        host.clearClip();
    }

    // The folder's top over the paper, then the screenshot turned about its middle and the
    // portrait turned about its corner.
    picture("ui/dossiers/folder_top.png", page.folder());
    if (const platform::Texture* shot = texture("ui/dossiers/screenshot_" + dossier.name + ".png")) {
        const float w = float(shot->width()), h = float(shot->height());
        turned("ui/dossiers/screenshot_" + dossier.name + ".png", page.screenshot() + game::Vec2{w * 0.5f, h * 0.5f},
               {-w * 0.5f, -h * 0.5f}, page.screenshotTurn());
    }
    turned("ui/dossiers/portrait_" + dossier.name + ".png", page.portrait(), {0.0f, 0.0f}, page.portraitTurn());
    if (page.note().active) {
        dimScreen(page.dim());
        drawMessageBox(page.note(), page.dim());
    }
}

// SceneMsgBox: its layout (the dimming box, the card with the title, the text and the buttons
// the type shows).
void LevelView::drawMessageBox(const game::MessageBox& box, float) {
    if (!box.active || !msgboxLayout_ || !strings_) return;
    std::function<void(const data::GuiNode&, float, float)> node = [&](const data::GuiNode& n, float px, float py) {
        if ((n.name == "BTN_YES" || n.name == "BTN_NO") && box.type == 0) return;
        if (n.name == "BTN_OK" && box.type != 0) return;
        const float x = n.relative ? px + n.position.x : n.position.x;
        const float y = n.relative ? py + n.position.y : n.position.y;
        if (n.kind == int(data::GuiKind::Rect)) {
            const auto byte = [](float v) { return std::uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
            host_->fillRect(x, kScreenH - (y + n.size.y), n.size.x, n.size.y,
                            (byte(n.colour.r) << 24) | (byte(n.colour.g) << 16) | (byte(n.colour.b) << 8) | byte(n.colour.a));
        }
        if (!n.image.empty())
            if (const platform::Texture* t = texture(n.image))
                host_->draw(*t, 0, 0, float(t->width()), float(t->height()), x, kScreenH - (y + n.size.y), n.size.x, n.size.y);
        if (n.label) {
            std::string text = strings_->text(n.label->key);
            if (n.name == "LBL_TITLE" && box.title >= 0) text = strings_->text(box.title);
            if (n.name == "LBL_TEXT" && box.text >= 0) text = strings_->text(box.text);
            if (n.name == "LBL_OK" && box.okText >= 0) text = strings_->text(box.okText);
            float scale = n.label->scale;
            if (n.name == "LBL_TITLE" || n.name == "LBL_OK") scale = fitScale(n, text);
            drawGuiLabel(n, x, y, n.size.x, n.size.y, text, scale, 1.0f, {});
        }
        for (const data::GuiNode& child : n.children) node(child, x, y);
    };
    for (const data::GuiNode& n : msgboxLayout_->root.children) node(n, 0.0f, 0.0f);
}

}  // namespace doc::render
