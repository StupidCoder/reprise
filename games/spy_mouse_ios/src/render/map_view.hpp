// Draws the world map (SceneMenuMap::render @0x15e5d8) in the original's order: the landscape
// (background, roads, grass, pads), cloud shadows, the details layer and the houses behind
// it, node icons, the mouse and its path, the houses in front, clouds, the iris and the menu
// bars (guis/menu_map.gui) with the Play bubble.
#pragma once

#include "data/gui_layout.hpp"
#include "data/obj_mesh.hpp"
#include "data/strings.hpp"
#include "game/world_map.hpp"
#include "render/canvas.hpp"
#include "render/text.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace doc::render {

class MapView {
public:
    bool load(platform::Host& host, const data::GameFiles& files, const game::WorldMap& map, std::string& error);
    void draw(platform::Host& host, const game::WorldMap& map);
    // The button under a touch (screen points, y down): "PLAY", "BTN_QUIT", or empty.
    std::string buttonAt(const game::WorldMap& map, game::Vec2 screen) const;
    // Only what buttonAt needs, without a host (see LevelView::loadButtons).
    void loadButtons(const data::GameFiles& files) {
        std::string ignored;
        layout_ = data::loadGuiLayout(files, "menu_map", ignored);
    }

private:
    const data::ObjMesh* mesh(const std::string& path);
    // A mesh placed with its origin at `origin` (map space), scaled about it.
    void drawMesh(const data::ObjMesh& mesh, game::Vec2 origin, float scaleX = 1.0f, float scaleY = 1.0f,
                  float alpha = 1.0f);
    // A road appearing behind its alpha mask scrolled by `shift` (RenderNodeRoad::render).
    void drawRevealingRoad(const data::ObjMesh& road, const data::ObjMesh& mask, float shift);
    std::string piece(const char* kind, int number) const;
    void drawClouds(const game::WorldMap& map, bool shadows);
    void drawNodes(const game::WorldMap& map);
    void drawHouses(const game::WorldMap& map, bool front);
    void drawPlayBubble(const game::WorldMap& map);
    void drawGui(const game::WorldMap& map, const data::GuiNode& node, float parentX, float parentY);
    std::string text(const std::string& key) const;

    Canvas canvas_;
    IrisPainter iris_;
    const data::GameFiles* files_ = nullptr;
    int world_ = 0;
    std::map<std::string, std::optional<data::ObjMesh>> meshes_;
    std::optional<data::GuiLayout> layout_;  // guis/menu_map.gui
    std::optional<data::StringTable> strings_;
    std::unique_ptr<Font> font_, mediumFont_;
};

}  // namespace doc::render
