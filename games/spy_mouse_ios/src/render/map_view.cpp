#include "render/map_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace doc::render {
namespace {

constexpr float kScreenH = 320.0f;
// The landscape meshes are centred on the screen (renderSceneGraph: glTranslatef(240, 160)).
const game::Vec2 kLandscapeOrigin{240.0f, 160.0f};
constexpr float kMeshScale = 1.2f;  // loadSceneGraph loads every map mesh at 1.2 times its size
constexpr float kNodeFrame = 37.0f;  // ui/map_nodes.png: 8 frames of 37 x 23

// bounceScale @0x19c938: a pop-in from nothing that overshoots and settles (0..1).
void bounceScale(float t, float& sx, float& sy) {
    if (t <= 0.4f) {
        sx = sy = t * 2.5f;
    } else if (t <= 0.6f) {
        const float d = t + -0.4f;
        sx = d * -0.99999988f + 1.0f;
        sy = d * 1.00000012f + 1.0f;
    } else if (t <= 0.8f) {
        const float d = t + -0.6f;
        sx = d * 2.00000024f + 0.8f;
        sy = d * -2.00000024f + 1.2f;
    } else {
        const float d = t + -0.8f;
        sx = d * -1.00000036f + 1.2f;
        sy = d + 0.8f;
    }
}

// bounceScale2 @0x19c860: a squash and stretch around full size (0..1).
void bounceScale2(float t, float& sx, float& sy) {
    if (t <= 0.33f) {
        sx = t * -0.606060565f + 1.0f;
        sy = t * 0.606060743f + 1.0f;
    } else if (t <= 0.66f) {
        const float d = t + -0.33f;
        sx = d * 1.21212125f + 0.8f;
        sy = d * -1.21212125f + 1.2f;
    } else {
        const float d = t + -0.66f;
        sx = d * -0.588235497f + 1.2f;
        sy = d * 0.588235319f + 0.8f;
    }
}

}  // namespace

bool MapView::load(platform::Host& host, const data::GameFiles& files, const game::WorldMap& map, std::string& error) {
    files_ = &files;
    world_ = map.world();
    canvas_.bind(host, files);
    if (!mesh(piece("background_main", -1))) {
        error = "world map pictures missing";
        return false;
    }
    if (auto ttf = files.readResource("fonts/SPYmouse_efigs.ttf")) {
        std::string fontError;
        font_ = Font::load(*ttf, 14, fontError);
        mediumFont_ = Font::load(std::move(*ttf), 20, fontError);
    }
    std::string ignored;
    strings_ = data::StringTable::load(files, "EN", ignored);
    layout_ = data::loadGuiLayout(files, "menu_map", ignored);
    return true;
}

std::string MapView::piece(const char* kind, int number) const {
    char name[96];
    if (number < 0) std::snprintf(name, sizeof name, "world/world%02d/world%02d_%s.obj", world_ + 1, world_ + 1, kind);
    else std::snprintf(name, sizeof name, "world/world%02d/world%02d_%s%02d.obj", world_ + 1, world_ + 1, kind, number);
    return name;
}

const data::ObjMesh* MapView::mesh(const std::string& path) {
    auto it = meshes_.find(path);
    if (it == meshes_.end()) it = meshes_.emplace(path, data::loadObjMesh(*files_, path, kMeshScale)).first;
    return it->second ? &*it->second : nullptr;
}

std::string MapView::text(const std::string& key) const {
    if (!strings_ || key.empty()) return {};
    if (key[0] == '#') return strings_->text(std::atoi(key.c_str() + 1));
    return strings_->text(key);
}

// Mesh::render @0x19164c: each part with its texture; the map's view drops the height, so a
// vertex lands at (x, z) from the origin. Texture rows count from the bottom.
void MapView::drawMesh(const data::ObjMesh& m, game::Vec2 origin, float scaleX, float scaleY, float alpha) {
    for (const data::ObjMesh::Part& part : m.parts) {
        const platform::Texture* t = canvas_.texture(part.texture);
        std::vector<platform::Vertex> v;
        std::vector<int> idx;
        v.reserve(part.triangles.size());
        for (const data::ObjMesh::Corner& c : part.triangles) {
            idx.push_back(int(v.size()));
            v.push_back({origin.x + c.x * scaleX, kScreenH - (origin.y + c.z * scaleY), c.u, 1.0f - c.v, 1, 1, 1, alpha});
        }
        canvas_.host().drawTriangles(t, v, idx);
    }
}

// RenderNodeRoad::render @0x1e5f00 draws the road where its alpha mask (alpha_gradient.png
// on the mask mesh, its u shifted by `shift`) is opaque. The port subdivides the road's
// triangles and gives each small corner the mask's alpha at that point.
void MapView::drawRevealingRoad(const data::ObjMesh& road, const data::ObjMesh& mask, float shift) {
    const platform::Image* gradient = mask.parts.empty() ? nullptr : canvas_.image(mask.parts.front().texture);
    auto maskAlpha = [&](float x, float z) {
        // The mask's (u, v) at (x, z), from the mask triangle that holds it.
        for (const data::ObjMesh::Part& part : mask.parts)
            for (std::size_t i = 0; i + 2 < part.triangles.size(); i += 3) {
                const auto& a = part.triangles[i];
                const auto& b = part.triangles[i + 1];
                const auto& c = part.triangles[i + 2];
                const float det = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
                if (std::fabs(det) < 1e-6f) continue;
                const float l1 = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / det;
                const float l2 = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / det;
                const float l3 = 1.0f - l1 - l2;
                if (l1 < -0.02f || l2 < -0.02f || l3 < -0.02f) continue;
                const float u = l1 * a.u + l2 * b.u + l3 * c.u - shift;
                const float v = l1 * a.v + l2 * b.v + l3 * c.v;
                if (!gradient || gradient->width <= 0) return u <= 0.5f ? 1.0f : 0.0f;
                const int px = std::clamp(int(u * float(gradient->width)), 0, gradient->width - 1);
                const int py = std::clamp(int((1.0f - v) * float(gradient->height)), 0, gradient->height - 1);
                return float(gradient->rgba[(std::size_t(py) * std::size_t(gradient->width) + std::size_t(px)) * 4 + 3]) /
                       255.0f;
            }
        return 0.0f;
    };
    constexpr int kSplit = 8;
    for (const data::ObjMesh::Part& part : road.parts) {
        const platform::Texture* t = canvas_.texture(part.texture);
        std::vector<platform::Vertex> v;
        std::vector<int> idx;
        for (std::size_t i = 0; i + 2 < part.triangles.size(); i += 3) {
            const auto& a = part.triangles[i];
            const auto& b = part.triangles[i + 1];
            const auto& c = part.triangles[i + 2];
            auto corner = [&](int p, int q) {
                // Barycentric grid point (p, q) of kSplit.
                const float s = float(p) / kSplit, r = float(q) / kSplit, w = 1.0f - s - r;
                const float x = a.x * w + b.x * s + c.x * r, z = a.z * w + b.z * s + c.z * r;
                const float u = a.u * w + b.u * s + c.u * r, tv = a.v * w + b.v * s + c.v * r;
                v.push_back({kLandscapeOrigin.x + x, kScreenH - (kLandscapeOrigin.y + z), u, 1.0f - tv, 1, 1, 1,
                             maskAlpha(x, z)});
                return int(v.size()) - 1;
            };
            for (int p = 0; p < kSplit; ++p)
                for (int q = 0; q < kSplit - p; ++q) {
                    const int k0 = corner(p, q), k1 = corner(p + 1, q), k2 = corner(p, q + 1);
                    idx.insert(idx.end(), {k0, k1, k2});
                    if (q < kSplit - p - 1) {
                        const int k3 = corner(p + 1, q + 1);
                        idx.insert(idx.end(), {k1, k3, k2});
                    }
                }
        }
        canvas_.host().drawTriangles(t, v, idx);
    }
}

void MapView::drawClouds(const game::WorldMap& map, bool shadows) {
    for (int i = 0; i < 5; ++i) {
        const game::WorldMap::Cloud& c = map.clouds()[std::size_t(i)];
        // renderClouds @0x1606a8: a slow sway along the drift, then the shadow 100 below.
        const float sway = game::libSin(float(map.timeMs()) / (c.speed * 600.0f)) * 1.5f;
        game::Vec2 offset{-(sway * 0.5f), sway * 0.866025388f};
        offset = offset * c.speed;
        char name[64];
        std::snprintf(name, sizeof name, shadows ? "world/aliveness/cloud_%02d_shadow.png" : "world/aliveness/cloud_%02d.png",
                      i + 1);
        const platform::Texture* t = canvas_.texture(name);
        if (!t) continue;
        game::Vec2 at = c.position + offset;
        if (shadows) at.y += -100.0f;
        canvas_.drawSprite(*t, 0, 0, float(t->width()), float(t->height()), at, 2.0f, false, 30.0f,
                           {1, 1, 1, shadows ? 1.0f : 0.7f});
    }
}

void MapView::drawHouses(const game::WorldMap& map, bool front) {
    const auto& shown = front ? map.scenery().housesFront : map.scenery().housesBack;
    const game::WorldMap::Reveal reveal = map.reveal();
    for (const auto& [number, position] : map.scenery().housePositions) {
        if (std::find(shown.begin(), shown.end(), number) == shown.end()) continue;
        const data::ObjMesh* m = mesh(piece("building", number));
        if (!m) continue;
        float sx = 1.0f, sy = 1.0f;
        if (reveal.phase == 2 && reveal.level + 1 == number) bounceScale(reveal.housePop, sx, sy);
        if (map.walkBounce() >= 0.0f && map.focusedNode() >= 0) {
            const game::WorldMap::Node& n = map.nodes()[std::size_t(map.focusedNode())];
            if (n.level && number - 1 < int(map.levels().size()) && map.levels()[std::size_t(number - 1)].info == n.level)
                bounceScale2(map.walkBounce(), sx, sy);
        }
        drawMesh(*m, position, sx, sy);
    }
}

void MapView::drawNodes(const game::WorldMap& map) {
    const platform::Texture* icons = canvas_.texture("ui/map_nodes.png");
    const platform::Texture* select = canvas_.texture("ui/node_select.png");
    const platform::Texture* glow = canvas_.texture("ui/node_highlight.png");
    // The chosen pad's ring breathes (sin of the time at 0.005 per ms).
    const float s = (game::libSin(float(map.timeMs()) * 0.005f) + 1.0f) * 0.5f;
    float bx = 1.0f, by = 1.0f;
    if (map.walkBounce() >= 0.0f) bounceScale(map.walkBounce(), bx, by);
    const float ringX = (s * 0.1f + 1.0f) * bx, ringY = (s * 0.05f + 1.0f) * by;
    for (std::size_t i = 0; i < map.nodes().size(); ++i) {
        const game::WorldMap::Node& n = map.nodes()[i];
        const int frame = map.padFrame(int(i));
        if (frame < 0) continue;
        if (select && int(i) == map.focusedNode())
            canvas_.drawSprite2(*select, 0, 0, float(select->width()), float(select->height()), n.position, ringX, ringY,
                                0.0f, {});
        if (glow && n.highlight > 0.0f)
            canvas_.drawSprite(*glow, 0, 0, float(glow->width()), float(glow->height()),
                               {n.position.x, n.position.y + float(glow->height()) * 0.2f}, n.highlight, false, 0.0f, {});
        if (icons) canvas_.drawSprite(*icons, frame, 0, kNodeFrame, float(icons->height()), n.position, 1.0f, false, 0.0f, {});
    }
}

// GuiMenuMapPlayItem::render @0x217ac4: the bubble (a bounce frame popping in) with its label.
void MapView::drawPlayBubble(const game::WorldMap& map) {
    const game::WorldMap::PlayBubble& play = map.playBubble();
    if (!play.shown) return;
    const platform::Texture* button = canvas_.texture("ui/btn_play_worldmap.png");
    if (!button) return;
    float sx = 1.0f, sy = 1.0f;
    const float t = std::min(1.0f, float(play.ageMs) / 300.0f);  // hypothesis: the pop-in's length
    if (t < 1.0f) bounceScale(t, sx, sy);
    canvas_.drawSprite2(*button, 0, 0, float(button->width()), float(button->height()), play.centre, sx, sy, 0.0f, {});
    if (!mediumFont_) return;
    std::string label = play.worldNumber < 0 ? text("#590") : text("#591");  // "Play!", "World" (1.1.1 ids)
    if (play.worldNumber >= 0) {
        char n[16];
        std::snprintf(n, sizeof n, "%d", play.worldNumber);
        label += n;
    }
    // Fitted to 0.8 of the button's width at most (string widths against DAT_001f9e9c).
    float scale = 1.0f;
    const float width = font_ ? font_->width(label, 1.0f) : 0.0f;
    if (width > 0.0f) scale = std::min(1.0f, float(button->width()) * 0.8f / width);
    TextStyle style;
    style.fillColourSet = true;
    style.fillColour = {1, 1, 1, 1};
    if (font_)
        font_->drawFlags(canvas_.host(), label, play.centre.x, kScreenH - play.centre.y, 0x22, scale * std::min(sx, sy), 0.0f,
                         style, {1, 1, 1, 1});
}

void MapView::drawGui(const game::WorldMap& map, const data::GuiNode& node, float parentX, float parentY) {
    float x = node.position.x, y = node.position.y;
    // The bars slide in from the screen's edges as the map opens (GuiSlideFrame; hypothesis:
    // from 0.48 s for 0.26 s, read off the reference shots).
    if (node.kind == int(data::GuiKind::SlideFrame)) {
        const float t = std::clamp((float(map.timeMs()) - 476.0f) / 255.0f, 0.0f, 1.0f);
        const float away = node.position.y > 160.0f ? node.size.y + 20.0f : -(node.size.y + 20.0f);
        y += (1.0f - t) * (1.0f - t) * away;
    }
    if (node.relative) {
        x += parentX;
        y += parentY;
    }
    const float w = node.size.x, h = node.size.y;
    if (node.kind == int(data::GuiKind::Rect)) {
        const auto c = node.colour;
        auto byte = [](float v) { return std::uint32_t(std::clamp(int(std::lround(v * 255.0f)), 0, 255)); };
        canvas_.host().fillRect(x, kScreenH - (y + h), w, h,
                                byte(c.r) << 24 | byte(c.g) << 16 | byte(c.b) << 8 | byte(c.a));
    }
    if (node.name == "LBL_BACKPACK_NEW") return;  // only while the backpack holds something new
    if (!node.image.empty())
        if (const platform::Texture* t = canvas_.texture(node.image)) {
            platform::DrawStyle style;
            style.a = node.colour.a;
            if (node.name == "BACKPACK_GLOW") style.degrees = -double(map.timeMs()) * 0.0702634f;  // hypothesis: f6c's spin
            canvas_.host().draw(*t, 0, 0, float(t->width()), float(t->height()), x, kScreenH - (y + h), w, h, style);
        }
    if (node.billboard && font_) {
        // The level's number and name (setLevelTitle @0x163d64), fading in when it changes.
        std::string title = text(map.titleKey());
        if (!map.titleNumber().empty() && !title.empty()) title = map.titleNumber() + " " + title;
        const float alpha = std::clamp(float(map.titleAgeMs()) / 300.0f, 0.0f, 1.0f);  // hypothesis
        TextStyle style;
        style.fillColourSet = true;
        style.fillColour = {0.996f, 0.96f, 0.808f, alpha};  // showScene: the billboard's colour
        font_->drawFlags(canvas_.host(), title, x + w * 0.5f, kScreenH - (y + h * 0.5f), 0x22, node.billboard->scale, 0.0f,
                         style, {1, 1, 1, alpha});
    }
    for (const data::GuiNode& child : node.children) drawGui(map, child, x, y);
}

void MapView::draw(platform::Host& host, const game::WorldMap& map) {
    canvas_.bind(host, *files_);
    const game::WorldMap::Reveal reveal = map.reveal();
    // Layer 0: background (opaque), roads, grass, pads.
    if (const data::ObjMesh* m = mesh(piece("background_main", -1))) drawMesh(*m, kLandscapeOrigin);
    std::vector<int> roads = map.scenery().roads;
    std::sort(roads.begin(), roads.end());
    const int revealRoad = reveal.level >= 0 && reveal.phase == 0 ? reveal.level + (map.world() != 0 ? 1 : 0) : -1;
    for (int road : roads) {
        char name[96];
        std::snprintf(name, sizeof name, "road_%02d", road);
        const data::ObjMesh* m = mesh(piece(name, -1));
        if (!m) continue;
        std::snprintf(name, sizeof name, "road_%02d_alpha", road);
        const data::ObjMesh* mask = mesh(piece(name, -1));
        if (road == revealRoad && mask) drawRevealingRoad(*m, *mask, reveal.shift);
        else drawMesh(*m, kLandscapeOrigin);
    }
    for (int g : map.scenery().grass)
        if (const data::ObjMesh* m = mesh(piece("grass", g))) drawMesh(*m, kLandscapeOrigin);
    for (int p : map.scenery().roadPads)
        if (const data::ObjMesh* m = mesh(piece("node", p))) drawMesh(*m, kLandscapeOrigin);
    drawClouds(map, true);
    // Layer 1: the landscape's details; layer 3: houses behind.
    if (const data::ObjMesh* m = mesh(piece("background_details", -1))) drawMesh(*m, kLandscapeOrigin);
    drawHouses(map, false);
    // Particles, pads, the mouse and its path, then the houses in front.
    for (const game::Particle& p : map.particles().slots()) {
        if (!p.active || p.delayMs >= 1 || p.type >= game::kSpriteParticleTypes) continue;
        const game::ParticleKind& kind = map.particles().kind(p.type);
        const bool add = p.type == 0x2A || (p.type >= 0x18 && p.type <= 0x1B);
        if (const platform::Texture* t = canvas_.texture(kind.sheet))
            canvas_.drawSprite(*t, p.sprite.frame(), 0, p.sprite.frameWidth(), p.sprite.frameHeight(), p.position, p.alpha,
                               false, p.rotation, {p.red, p.green, p.blue, std::min(1.0f, p.scale)},
                               add ? platform::DrawStyle::Add : platform::DrawStyle::Alpha);
    }
    drawNodes(map);
    drawMousePath(canvas_, map.path(), map.mouse());
    // (SceneMenuMap::render @0x15e5d8: while it teleports, a blur stands in for the mouse; the
    // first is mirrored when it leaves towards the left, the second always is.)
    const game::WorldMap::Teleport& tp = map.teleport();
    if (tp.state == 0) {
        drawMouseFigure(canvas_, map.mouse(), map.mouse().position);
    } else if (const platform::Texture* blur = canvas_.texture(tp.state == 1 ? "sprites/player_blur1.png"
                                                                            : "sprites/player_blur2.png")) {
        const game::SpriteAnim& anim = tp.state == 1 ? tp.out : tp.in;
        const game::Vec2 at = tp.state == 1 ? tp.at : tp.at + game::Vec2{0.0f, 7.0f};
        const bool mirror = tp.state == 1 ? tp.from.x < 0.0f : true;
        canvas_.drawSprite(*blur, anim.frame(), 0, anim.frameWidth(), anim.frameHeight(), at, 1.0f, mirror, 0.0f, {});
    }
    drawHouses(map, true);
    drawClouds(map, false);
    // The iris, then the menus on top.
    if (map.irisOpening() < 1.0f) {
        const game::Vec2 c = map.irisCentre();
        const float full = 480.0f * 0.666666687f + std::max(std::fabs(c.x - 240.0f), std::fabs(c.y - 160.0f));
        float r = map.irisOpening() * full;
        if (map.state() == game::WorldMap::State::Opening || map.state() == game::WorldMap::State::Arriving)
            r = std::max(25.0f, r);
        iris_.draw(canvas_, map.irisKind(), c, r, map.irisOpening() <= 0.0f, map.timeMs() / 17);
    }
    if (layout_)
        for (const data::GuiNode& node : layout_->root.children) drawGui(map, node, 0.0f, 0.0f);
    drawPlayBubble(map);
}

std::string MapView::buttonAt(const game::WorldMap& map, game::Vec2 screen) const {
    const game::WorldMap::PlayBubble& play = map.playBubble();
    if (play.shown) {
        const game::Vec2 p{screen.x, kScreenH - screen.y};
        if ((p - play.centre).length() <= 26.0f) return "PLAY";
    }
    if (layout_) {
        // The quit button in the top bar.
        if (const data::GuiNode* quit = layout_->root.find("BTN_QUIT")) {
            (void)quit;
            if (screen.x < 50.0f && screen.y < 40.0f) return "BTN_QUIT";
        }
    }
    return {};
}

}  // namespace doc::render
