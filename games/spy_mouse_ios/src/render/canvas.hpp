// Drawing shared by the level and the world map: pictures from the game files (cached), sprite
// cells placed in map space (y up), the mouse, its drawn path and the iris.
#pragma once

#include "data/game_files.hpp"
#include "game/drawn_path.hpp"
#include "game/mouse.hpp"
#include "platform/host.hpp"

#include <map>
#include <optional>
#include <string>

namespace doc::render {

struct Tint {
    float r = 1, g = 1, b = 1, a = 1;
};

class Canvas {
public:
    void bind(platform::Host& host, const data::GameFiles& files) {
        host_ = &host;
        files_ = &files;
    }
    platform::Host& host() const { return *host_; }
    const data::GameFiles& files() const { return *files_; }
    // A picture under res/ as a texture (null if missing); decoded once.
    const platform::Texture* texture(const std::string& relative);
    // The same picture's pixels.
    const platform::Image* image(const std::string& relative);
    // One cell of a sheet centred on `centre` (map space), scaled, mirrored and turned by
    // `degrees` counter-clockwise.
    void drawSprite(const platform::Texture& sheet, int frame, int row, float frameW, float frameH, game::Vec2 centre,
                    float scale, bool flip, float degrees, const Tint& tint,
                    platform::DrawStyle::Blend blend = platform::DrawStyle::Alpha);
    // Like drawSprite with separate x and y scales.
    void drawSprite2(const platform::Texture& sheet, int frame, int row, float frameW, float frameH, game::Vec2 centre,
                     float scaleX, float scaleY, float degrees, const Tint& tint);

    // The view's scroll in map space (infinite levels): map x minus this is screen x. Zero for
    // screen-space drawing (HUD, popups).
    float viewX = 0.0f;

private:
    platform::Host* host_ = nullptr;
    const data::GameFiles* files_ = nullptr;
    std::map<std::string, platform::Texture> textures_;
    std::map<std::string, std::optional<platform::Image>> images_;
};

// The mouse: a sequence cell when one plays (Sprite::renderSequenceSprite @0x1329c4: cell
// (cx, cy + row), moved by (ox, oy)), else the walk frame from the direction row of its run
// sheet.
void drawMouseFigure(Canvas& canvas, const game::Mouse& mouse, game::Vec2 at, const Tint& tint = {});

// Player::renderLow @0x123608 with Path::renderTexture @0x11e05c (mode 0): the dashed line from
// the end of the path back to the point the mouse heads for.
void drawMousePath(Canvas& canvas, const game::DrawnPath& path, const game::Mouse& mouse);

// PinholeManager's iris: a scrolling pattern over the screen with a hole in it.
// `kind` 0 the menu pattern, 5 the bonus pattern, 2 the death pattern with a cat-shaped hole.
// `radius` 0 with `shut` covers everything. `phase` scrolls the pattern (0..255).
class IrisPainter {
public:
    void draw(Canvas& canvas, int kind, game::Vec2 centre, float radius, bool shut, int phase);

private:
    platform::Texture frame_;
};

}  // namespace doc::render
