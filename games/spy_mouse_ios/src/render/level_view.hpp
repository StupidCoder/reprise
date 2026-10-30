// Draws a play session from its state, in the original's order (SceneGame::render @0xff8d0):
// map and low decorations, low particles, objects and the mouse by depth in the low, normal and
// high passes (the drawn path after the low pass), high decorations and particles, then the HUD.
// Pictures are read from the user's game files when first needed.
#pragma once

#include "data/level_table.hpp"

#include "data/program_tables.hpp"

#include "data/gui_layout.hpp"
#include "data/strings.hpp"
#include "game/session.hpp"
#include "platform/host.hpp"
#include "render/canvas.hpp"
#include "render/text.hpp"

#include <memory>
#include <optional>

#include <map>
#include <vector>
#include <string>

namespace doc::render {

class LevelView {
public:
    bool load(platform::Host& host, const data::GameFiles& files, const game::Level& level, std::string& error);
    void draw(platform::Host& host, const game::Session& session);

    // Which button of the popup or the results screen a touch at `screen` (points, y down)
    // is on: "BTN_CONTINUE", "GAME_RESULTS_CONTINUE", "GAME_RESULTS_RETRY", or empty.
    std::string buttonAt(const game::Session& session, game::Vec2 screen) const;
    // Only what buttonAt needs (the GUI layouts and the pause button's size), without a host:
    // the touches of a run restored from a quick save are resolved like the live ones.
    void loadButtons(const data::GameFiles& files);

    // Sheet row for a heading (getSpriteDirFromAngle @0x19d724): `directions` sectors, the
    // first centred on east, numbered from a quarter turn on.
    static int spriteDirection(float angle, int directions);

private:
    const platform::Texture* texture(const std::string& relative);
    void drawSprite(const platform::Texture& sheet, int frame, int row, float frameW, float frameH, game::Vec2 centre,
                    float scale, bool flip, float degrees, const Tint& tint,
                    platform::DrawStyle::Blend blend = platform::DrawStyle::Alpha);
    void drawFluff(const game::Session& session, bool high);
    void drawInGameTutorial(const game::Session& session);
    void drawParticles(const game::Session& session, bool lowLayer);
    void drawObject(const game::Session& session, const game::LevelObject& o, int pass);
    void drawHomeHole(const game::Session& session, const game::LevelObject& o, int pass);
    void drawBouncyArrow(const game::LevelObject& o);
    void drawCheese(const game::Session& session, const game::LevelObject& o);
    void drawLure(const game::Session& session, const game::LevelObject& o);  // twine, catnip
    void drawSpitterRoom(const game::Session& session, const game::LevelObject& o, int pass);
    void drawVacuumRoom(const game::Session& session, const game::LevelObject& o, int pass);
    void drawSwitchOrDoor(const game::Session& session, const game::LevelObject& o, int pass);
    void drawProjectiles(const game::Session& session, int pass);
    void drawCat(const game::Session& session, const game::Cat& cat, const game::LevelObject& o, int pass);
    void drawCatRoute(const game::Cat& cat);
    void drawViewArc(const game::Session& session, const game::Cat& cat);
    void applyShake(const game::Session& session);
    void drawCaughtAnimation(const game::Session& session);
    void drawSheen(const game::Session& session, const game::LevelObject& o, float scale);
    const platform::Image* image(const std::string& relative);  // decoded pictures (CPU)
    void drawMouse(const game::Session& session);
    void drawPath(platform::Host& host, const game::Session& session);
    void drawScreenCorners();
    void drawIntro(const game::Session& session);
    void drawHandTutorial(const game::Session& session);
    void drawIris(const game::Session& session);
    // The tutorial popup (popup_view.cpp).
    struct PopupContent;
    void drawNotice(const game::Session& session);
    void drawGuiNode(const data::GuiNode& node, float parentX, float parentY, const PopupContent& content);
    float fitScale(const data::GuiNode& node, const std::string& text);
    void drawGuiLabel(const data::GuiNode& node, float x, float y, float w, float h, const std::string& text,
                      float scale, float alpha, const Affine& xf);
    // Kiska's offer (kiska_view.cpp).
    void drawKiska(const game::Session& session);
    void drawDossier(const game::Session& session);
    void drawMessageBox(const game::MessageBox& box, float dim);
    void drawKiskaNode(const game::KiskaPopup& popup, const data::GuiNode& node, float parentX, float parentY,
                       const Affine& outer);
    // The results screen (results_view.cpp).
    void drawCheeseStamp(const game::ResultsScreen& results);
    void drawResults(const game::Session& session);
    void drawWorldComplete(const game::Session& session);
    void drawHudButtons(const game::Session& session);
    void drawDroid(const game::Session& session, const game::LevelObject& o, int pass);
    void drawSpeech(const game::Session& session);
    void drawWorldNode(const game::Session& session, const data::GuiNode& node, float parentX, float parentY,
                       const Affine& outer);
    void drawGoalPopup(const game::GoalPopup& popup, const std::array<game::ResultsGoal, 3>& goals,
                       const data::GuiNode& node);
    // The pause menu (render/pause_view.cpp).
    void drawPause(const game::Session& session);
    void drawPauseNode(const game::Session& session, const data::GuiNode& node, float parentX, float parentY,
                       const Affine& outer);
    void drawResultsNode(const game::Session& session, const data::GuiNode& node, float parentX, float parentY,
                         const Affine& xf);
    // HUD (hud_view.cpp).
    void drawHud(const game::Session& session);
    void hudRect(const platform::Texture& t, float x, float y, float w, float h, float alpha);
    float hudPill(float x, float y, float midWidth, float extraWidth, float alpha);
    void hudText(const std::string& text, float x, float yGl, float sx, float sy, const Colour& colour);
    void hudPopupText(const std::string& text, float x, float yGl, float t);

    platform::Host* host_ = nullptr;
    const data::GameFiles* files_ = nullptr;
    std::string background_;
    Canvas canvas_;                                      // pictures by path under res/
    IrisPainter iris_;
    std::unique_ptr<Font> font_;                         // FNT_TXT (14 px): the HUD's font
    std::unique_ptr<Font> headingFont_;                  // FNT_HEADING (25 px)
    std::unique_ptr<Font> mediumFont_;                   // FNT_MEDIUM (20 px)
    std::optional<data::GuiLayout> popupLayout_;         // guis/popup_tutorial.gui
    std::optional<data::GuiLayout> resultsLayout_;       // guis/game_results.gui
    std::optional<data::GuiLayout> worldLayout_;         // guis/world_complete.gui
    std::optional<data::GuiLayout> pauseLayout_;         // guis/pause.gui
    std::optional<data::GuiLayout> kiskaLayout_;         // guis/level_skip.gui (1.0.5: levelSkip.gui)
    std::optional<data::GuiLayout> dossierLayout_;       // guis/dossier.gui (1.1.1)
    std::optional<data::GuiLayout> msgboxLayout_;        // guis/msgbox.gui
    std::vector<data::DossierData> dossiers_;
    std::optional<data::LevelTable> levelTable_;
    game::Vec2 pausePaws_;                               // hud/hud_paws.png size
    std::vector<std::pair<std::string, std::string>> goalStars_;  // medal pictures per goal kind
    std::vector<data::GuiVec> eyeOffsets_;               // posed mouse eyes per pose
    std::optional<data::StringTable> strings_;
    std::vector<platform::Texture> sheenFrames_;              // this frame's cheese sheens
    std::size_t sheenUsed_ = 0;
};

}  // namespace doc::render
