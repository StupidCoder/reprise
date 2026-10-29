// The results screen drawn from its layout (guis/game_results.gui) and the screen's state
// (game/results.hpp). SceneGameResults::render @0x1ebbdc draws the iris first (the bonus
// pattern closing over the level), then the layout's items; animated frames scale what they
// hold about their centre; in the last step a ring fills round the continue button.
#include "render/level_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace doc::render {
namespace {

constexpr float kScreenH = 320.0f;

const game::BounceFrame* bounceFrameFor(const game::ResultsScreen& r, const std::string& name) {
    if (name == "FRAME_RESULT") return &r.card();
    if (name == "FRAME_RETRY") return &r.retryFrame();
    if (name == "FRAME_CONT") return &r.continueFrame();
    if (name == "GOAL_FRAME1") return &r.goalFrame(0);
    if (name == "GOAL_FRAME2") return &r.goalFrame(1);
    if (name == "GOAL_FRAME3") return &r.goalFrame(2);
    return nullptr;
}

}  // namespace

void LevelView::drawResultsNode(const game::Session& session, const data::GuiNode& node, float parentX,
                                float parentY, const Affine& outer) {
    const game::ResultsScreen& results = session.results();
    // Items the screen hides outside challenges and without a new best (showScene).
    // (The online rank badge is not ported.)
    if (node.name == "ITM_CHALLENGE" || node.name == "LBL_CHALLENGE_REQUIREMENT" || node.name == "IMPROVED_RANK" ||
        (node.name == "IMG_IMPROVED_SCORE" && !results.badgeShown()))
        return;
    // Version 1.1.1's additions, not reconstructed yet: the card for a newly opened world and
    // the store's cheese wedges (counter and stamp). HYPOTHESIS: hidden unless they apply, as
    // the 1.1.1 reference run of 1-1 shows no new-world card (v111_l11_obj).
    if (node.name == "NEW_WORLD_UNLOCKED_BOUNCE" || node.name == "FRAME_CHEESE_COUNT" || node.name == "IMG_CHEESE_FOUND")
        return;
    if (node.name == "POPUP_GOAL_INFO") {
        drawGoalPopup(results.popup(), {results.goal(0), results.goal(1), results.goal(2)}, node);
        return;
    }
    float x = node.position.x, y = node.position.y;
    if (node.relative) {
        x += parentX;
        y += parentY;
    }
    float w = node.size.x, h = node.size.y;
    if (node.name == "IMG_IMPROVED_SCORE") {
        // Sized to its picture (showScene), then scaled from 2 down to 1 (update: +0x140).
        if (const platform::Texture* t = texture(node.image)) {
            w = float(t->width());
            h = float(t->height());
        }
        w *= results.badgeScale();
        h *= results.badgeScale();
    }
    Affine xf = outer;
    if (node.name == "PLUS_FRAME_CONT") {
        // GuiPlusFrame::render @0x20fb10: its content scaled about its centre while pulsing.
        const float s = results.pulseScale();
        const float cx = x + w * 0.5f, cy = kScreenH - (y + h * 0.5f);
        xf = outer.after(Affine{s, cx * (1.0f - s), s, cy * (1.0f - s)});
    }
    if (const game::BounceFrame* frame = bounceFrameFor(results, node.name)) {
        if (!frame->visible()) return;
        float sx = 1.0f, sy = 1.0f;
        frame->scales(sx, sy);
        // About the frame's centre (screen points, y down).
        const float cx = x + w * 0.5f, cy = kScreenH - (y + h * 0.5f);
        xf = outer.after(Affine{sx, cx * (1.0f - sx), sy, cy * (1.0f - sy)});
    }
    auto picture = [&](const std::string& path) {
        const platform::Texture* t = texture(path);
        if (!t) return;
        platform::DrawStyle style;
        style.r = node.colour.r;
        style.g = node.colour.g;
        style.b = node.colour.b;
        style.a = node.colour.a;
        const float top = kScreenH - (y + h);
        host_->draw(*t, 0, 0, float(t->width()), float(t->height()), xf.x(x), xf.y(top), w * xf.ax, h * xf.ay, style);
    };

    if (node.kind == int(data::GuiKind::Rect)) {
        // GuiRect::render @0x20e2a4: a plain box in the item's colour.
        const auto byte = [](float v) { return std::uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
        host_->fillRect(xf.x(x), xf.y(kScreenH - (y + h)), w * xf.ax, h * xf.ay,
                        (byte(node.colour.r) << 24) | (byte(node.colour.g) << 16) | (byte(node.colour.b) << 8) |
                            byte(node.colour.a));
    }

    std::string image = node.image;
    // The medal buttons show their goal's picture, plain or won (GuiLevelGoalButton
    // ::setGoalType @0x22728c, LevelGoal::getStarImage @0x1fec0c).
    for (int i = 0; i < 3; ++i)
        if (node.name == "LEVEL_GOAL_BTN_" + std::to_string(i + 1)) {
            const int kind = results.goal(i).kind;
            // (A level without goals, like the boss chase, gets no picture at all.)
            if (kind >= 0 && kind < int(goalStars_.size()))
                image = results.goalLit(i) ? goalStars_[std::size_t(kind)].second : goalStars_[std::size_t(kind)].first;
            else
                image.clear();
        }
    if (!image.empty()) picture(image);

    if (node.label) {
        const data::GuiLabelFields& l = *node.label;
        std::string text;
        float scale = l.scale;
        char buffer[64];
        const game::Level& level = session.level();
        if (node.name == "LEVEL_TITLE") {
            if (strings_) text = strings_->text(level.info().nameKey);
            scale = fitScale(node, text);
        } else if (node.name == "LEVEL_NUMBER") {
            text = level.info().number;
        } else if (node.name == "LBL_CHEESE_SCORE_VALUE") {
            std::snprintf(buffer, sizeof buffer, "%d", int(results.points()));
            text = buffer;
        } else if (node.name == "LBL_BONUS_SCORE_VALUE") {
            std::snprintf(buffer, sizeof buffer, "%d", int(results.bonusShown()));
            text = buffer;
        } else if (node.name == "LBL_TOTAL_SCORE_VALUE") {
            std::snprintf(buffer, sizeof buffer, "%.0f", double(results.total()));
            text = buffer;
        } else if (node.name == "LBL_TIME") {
            // SceneGameResults::update @0x1ed3de: minutes and seconds played.
            const int ms = results.playTimeMs();
            std::snprintf(buffer, sizeof buffer, "(%dm %ds)", ms / 60000, (ms / 1000) % 60);
            text = buffer;
        } else if (strings_ && strings_->id(l.key) >= 0) {
            text = strings_->text(l.key);
            if (node.name == "LBL_TOTAL_SCORE" || node.name == "LBL_CHEESE_SCORE" || node.name == "LBL_BONUS_SCORE")
                scale = fitScale(node, text);
        } else {
            text = l.text;
        }
        drawGuiLabel(node, x, y, w, h, text, scale, 1.0f, xf);
    }

    for (const data::GuiNode& child : node.children) drawResultsNode(session, child, x, y, xf);
}

// GuiPopupGoalInfo::update @0x2268bc lays the popup out for its line: the medal's picture at
// three quarters of its size half out to the left and a fifth below, the text (FNT_TXT, at
// most 220 wide, squeezed beyond) right of it, the stretched middle under both and the right
// end after them, all centred on the screen.
void LevelView::drawGoalPopup(const game::GoalPopup& popup, const std::array<game::ResultsGoal, 3>& goals,
                              const data::GuiNode& node) {
    const int i = popup.goal();
    if (i < 0 || !font_) return;
    const game::ResultsGoal& goal = goals[std::size_t(i)];
    const int kind = goal.kind;
    if (kind < 0 || kind >= int(goalStars_.size())) return;
    const platform::Texture* star = texture(goal.won ? goalStars_[std::size_t(kind)].second : goalStars_[std::size_t(kind)].first);
    const platform::Texture* mid = texture(node.image);
    const platform::Texture* cap = texture("ui/rosetta_popup_right.png");
    if (!star || !mid || !cap) return;
    const float starW = float(star->width()) * 0.75f, starH = float(star->height()) * 0.75f;
    float textW = font_->width(goal.infoText);
    float textScale = 1.0f;
    if (textW > 220.0f) {
        textScale = 220.0f / textW;
        textW = 220.0f;
    }
    const float capW = float(cap->width()), h = float(mid->height());
    const float width = starW * 0.5f + textW - capW;  // the middle piece
    const float x = game::kScreenWidth * 0.5f + (starW * 0.5f + textW) * -0.5f, y = popup.y();
    auto draw = [&](const platform::Texture& t, float px, float py, float pw, float ph) {
        host_->draw(t, 0, 0, float(t.width()), float(t.height()), px, kScreenH - (py + ph), pw, ph, {});
    };
    draw(*mid, x, y, width, h);
    draw(*cap, x + width, y, capW, h);
    draw(*star, x + starW * -0.5f, y + starH * -0.2f, starW, starH);
    data::GuiNode label;
    label.colour = {1.0f, 1.0f, 1.0f, 1.0f};
    label.label = data::GuiLabelFields{};
    label.label->font = 0;
    label.label->align = 0x22;
    drawGuiLabel(label, x + starW * 0.5f - 12.0f, y, width, h, goal.infoText, textScale, 1.0f, {});
}

void LevelView::drawResults(const game::Session& session) {
    const game::ResultsScreen& results = session.results();
    if (!results.active() || !resultsLayout_) return;
    for (const data::GuiNode& node : resultsLayout_->root.children) drawResultsNode(session, node, 0.0f, 0.0f, {});

    // The ring round the continue button (drawTexturedCircle @0x19d2b0): a fan from the top,
    // clockwise in 10 degree steps over the filled part, textured with the button's outline.
    if (results.state() != 6 || results.outline() <= 0.0f) return;
    const data::GuiNode* button = resultsLayout_->root.find("GAME_RESULTS_CONTINUE");
    const data::GuiNode* frame = resultsLayout_->root.find("FRAME_CONT");
    const data::GuiNode* plus = resultsLayout_->root.find("PLUS_FRAME_CONT");
    const data::GuiNode* card = resultsLayout_->root.find("FRAME_RESULT");
    const platform::Texture* ring = texture("ui/btn_results_cont_outline.png");
    if (!button || !frame || !plus || !card || !ring) return;
    const float bx = card->position.x + frame->position.x + plus->position.x + button->position.x;
    const float by = card->position.y + frame->position.y + plus->position.y + button->position.y;
    const float cx = bx + button->size.x * 0.5f, cy = kScreenH - (by + button->size.y * 0.5f + 4.0f);
    const float radius = 30.0f * results.pulseScale();
    const float sweep = 360.0f * results.outline();
    std::vector<platform::Vertex> vertices;
    std::vector<int> indices;
    vertices.push_back({cx, cy, 0.5f, 0.5f});
    auto rim = [&](float degrees) {
        const float a = degrees * 0.0174532924f;
        const float s = std::sin(a), c = std::cos(a);
        // Texture rows count from the bottom in the original.
        vertices.push_back({cx + s * radius, cy - c * radius, (s + 1.0f) * 0.5f, 1.0f - (c + 1.0f) * 0.5f});
    };
    for (float a = 0.0f; a < sweep; a += 10.0f) rim(a);
    rim(sweep);
    for (int i = 1; i + 1 < int(vertices.size()); ++i) {
        indices.push_back(0);
        indices.push_back(i);
        indices.push_back(i + 1);
    }
    host_->drawTriangles(ring, vertices, indices);
}

// SceneWorldCompleteResults (layout guis/world_complete.gui): the card and the continue button
// bounce; updateDynamicLabels @0x218d14 fills "World %d Completed" (string 0x261), the goals
// ("%d/%d") and the closing line (string 0x256 with every goal won, else 0x25b). The goal
// info popup (STAR_INFO) stays below the screen.
void LevelView::drawWorldNode(const game::Session& session, const data::GuiNode& node, float parentX,
                              float parentY, const Affine& outer) {
    const game::WorldCompleteScreen& screen = session.worldComplete();
    if (node.name == "STAR_INFO") return;
    float x = node.position.x, y = node.position.y;
    if (node.relative) {
        x += parentX;
        y += parentY;
    }
    float w = node.size.x, h = node.size.y;
    Affine xf = outer;
    const game::BounceFrame* frame = node.name == "FRAME_RESULT" ? &screen.card()
                                     : node.name == "FRAME_CONT" ? &screen.continueFrame()
                                                                 : nullptr;
    if (frame) {
        if (!frame->visible()) return;
        float sx = 1.0f, sy = 1.0f;
        frame->scales(sx, sy);
        const float cx = x + w * 0.5f, cy = kScreenH - (y + h * 0.5f);
        xf = outer.after(Affine{sx, cx * (1.0f - sx), sy, cy * (1.0f - sy)});
    }
    if (node.name == "PLUS_FRAME_CONT") {
        // GuiPlusFrame::render @0x20fb10: its content scaled about its centre.
        const float kx = screen.pulseScaleX(), ky = screen.pulseScaleY();
        const float cx = x + w * 0.5f, cy = kScreenH - (y + h * 0.5f);
        xf = outer.after(Affine{kx, cx * (1.0f - kx), ky, cy * (1.0f - ky)});
    }
    if (!node.image.empty())
        if (const platform::Texture* t = texture(node.image)) {
            // (Items without a size of their own take their picture's.)
            if (w <= 0.0f || h <= 0.0f) {
                w = float(t->width());
                h = float(t->height());
            }
            platform::DrawStyle style;
            style.r = node.colour.r;
            style.g = node.colour.g;
            style.b = node.colour.b;
            style.a = node.colour.a;
            host_->draw(*t, 0, 0, float(t->width()), float(t->height()), xf.x(x), xf.y(kScreenH - (y + h)), w * xf.ax,
                        h * xf.ay, style);
        }
    if (node.label) {
        const data::GuiLabelFields& l = *node.label;
        std::string text;
        char buffer[96];
        if (node.name == "LBL_WORLD_COMPLETED" && strings_) {
            std::snprintf(buffer, sizeof buffer, strings_->text(0x269).c_str(), screen.setup().world + 1);
            text = buffer;
        } else if (node.name == "TEXT_GOALS_COMPLETED") {
            std::snprintf(buffer, sizeof buffer, "%d/%d", screen.setup().goalsWon, screen.setup().goalsTotal);
            text = buffer;
        } else if (node.name == "TEXT_PLAY_AGAIN" && strings_) {
            text = strings_->text(screen.allGoals() ? 0x25e : 0x263);
        } else if (strings_ && strings_->id(l.key) >= 0) {
            text = strings_->text(l.key);
        } else {
            text = l.text;
        }
        drawGuiLabel(node, x, y, w, h, text, l.scale, 1.0f, xf);
    }
    for (const data::GuiNode& child : node.children) drawWorldNode(session, child, x, y, xf);
}

void LevelView::drawWorldComplete(const game::Session& session) {
    if (!worldLayout_) return;
    for (const data::GuiNode& node : worldLayout_->root.children) drawWorldNode(session, node, 0.0f, 0.0f, {});
}

namespace {

// Screen box (y down) of the named item: positions add up from the root (GuiItem
// ::updateScreenPos @0x1d8c10).
bool findBox(const data::GuiNode& node, const std::string& name, float px, float py, float box[4]) {
    float x = node.position.x, y = node.position.y;
    if (node.relative) {
        x += px;
        y += py;
    }
    if (node.name == name) {
        box[0] = x;
        box[1] = kScreenH - (y + node.size.y);
        box[2] = node.size.x;
        box[3] = node.size.y;
        return true;
    }
    for (const data::GuiNode& child : node.children)
        if (findBox(child, name, x, y, box)) return true;
    return false;
}

}  // namespace

std::string LevelView::buttonAt(const game::Session& session, game::Vec2 screen) const {
    auto inside = [&](const std::optional<data::GuiLayout>& layout, const std::string& name) {
        float box[4];
        if (!layout) return false;
        for (const data::GuiNode& top : layout->root.children)
            if (findBox(top, name, 0.0f, 0.0f, box))
                return screen.x >= box[0] && screen.x < box[0] + box[2] && screen.y >= box[1] &&
                       screen.y < box[1] + box[3];
        return false;
    };
    if (session.kiska().active()) return kiskaLayout_ ? game::KiskaPopup::buttonAt(kiskaLayout_->root, screen) : "";
    if (session.results().active()) {
        // The buttons answer once their frames have come in.
        if (session.results().continueFrame().progress() >= 1.0f && inside(resultsLayout_, "GAME_RESULTS_CONTINUE"))
            return "GAME_RESULTS_CONTINUE";
        if (session.results().retryFrame().progress() >= 1.0f && inside(resultsLayout_, "GAME_RESULTS_RETRY"))
            return "GAME_RESULTS_RETRY";
        for (int i = 0; i < 3; ++i) {
            const std::string name = "LEVEL_GOAL_BTN_" + std::to_string(i + 1);
            if (session.results().goalFrame(i).visible() && inside(resultsLayout_, name)) return name;
        }
        return {};
    }
    if (session.worldComplete().active()) {
        if (session.worldComplete().continueFrame().progress() >= 1.0f && inside(worldLayout_, "GAME_RESULTS_CONTINUE"))
            return "GAME_RESULTS_CONTINUE";
        return {};
    }
    if (session.pause().active()) {
        // The menu's buttons (in the card and the corner while those are in) and the play
        // button in place of the pause button.
        const game::PauseMenu& pause = session.pause();
        for (const char* name : {"BTN_CONTINUE", "BTN_RESTART", "BTN_QUIT", "LEVEL_GOAL_BTN_1", "LEVEL_GOAL_BTN_2",
                                 "LEVEL_GOAL_BTN_3"})
            if (pause.card().visible() && inside(pauseLayout_, name)) return name;
        for (const char* name : {"BTN_OPTIONS", "BTN_HELP"})
            if (pause.corner().visible() && inside(pauseLayout_, name)) return name;
        if (inside(pauseLayout_, "BTN_CONTINUE_2")) return "BTN_CONTINUE_2";
        return {};
    }
    if (session.notice().active() && inside(popupLayout_, "BTN_CONTINUE")) return "BTN_CONTINUE";
    // The pause button at the top right (hud/hud_paws.png; a GUI button of the level).
    if (const game::Hud& hud = session.hud(); hud.pauseVisible && !hud.pauseHidden && pausePaws_.x > 0.0f &&
        screen.x >= game::kScreenWidth - pausePaws_.x && screen.y < pausePaws_.y)
        return "PAUSE";
    return {};
}

}  // namespace doc::render
