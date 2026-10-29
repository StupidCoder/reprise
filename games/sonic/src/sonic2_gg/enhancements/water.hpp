#pragma once
#include "gamegear/lens_rain.hpp"
#include "gamegear/water_splash.hpp"
#include "sonic2_gg/game/display.hpp"
#include <array>
#include <cstdint>
#include <vector>

// Water enhancement (Aqua Lake and Sky High 2's rain). None of this is
// cartridge behaviour, and none of it feeds back into the game.
//
// The original draws Aqua Lake's water in two ways. In acts 1 and 3 it is
// part of the level art: tiles whose colour 4 covers every other pixel (a
// checkerboard dither) with the priority bit set, so sprites show through
// the other half; a row of highlight colours above the dither is the
// surface. In act 2 the rising water is a colour change from the line
// interrupt at the surface, with object 0C stepping a small wave sprite
// along it. Object 0B is the splash when Sonic goes under. Sky High 2's
// rain is eight grey streak sprites (rain.cpp).
//
// Enhanced, the renderer (render/wide_view.cpp) draws that water as real
// translucency instead of the dither, leaves out the splash, wave and rain
// sprites, and hands what it found to draw_water(): the picture under the
// water bends at the game's own resolution (the parameters of Sonic 1's
// Labyrinth), act 2's wave runs translucent along the whole rising surface
// (acts 1 and 3 have no wave sprite and get none), splash droplets
// (gamegear/water_splash) burst where Sonic enters or leaves the water.
// The rain sprites (all eight moving in step, up and to the right) are
// replaced entirely: while the game says it rains, drops land on the camera
// lens and roll down it, leaving trails (gamegear/lens_rain).
namespace sonic2_gg {

class Game;

// Which tile pattern rows hold the water dither (colour 4 on every other
// pixel of the row), read from the act's patterns in VRAM.
class WaterArt {
public:
    explicit WaterArt(const Display&);

    bool water_row(unsigned tile, int row) const {
        return rows_[(tile & 0x1FF) * 8 + unsigned(row)];
    }

private:
    std::array<bool, 512 * 8> rows_{};
};

// True when the water enhancement changes the current picture: an Aqua
// Lake act or Sky High 2 (the rain), during play.
bool enhanced_water_act(const Game&);

// Object types whose sprites the enhancement replaces: the splash (0B) and
// the wave on the rising water (0C).
bool replaced_by_water(const Game&, std::uint8_t owner);

// Sky High 2, where it may rain, and whether a sprite table entry
// (DB00 + entry) is a rain drop.
bool rain_act(const Game&);
bool rain_entry(const Game&, unsigned entry);

// Droplet bursts where Sonic crosses a water surface, and the rain on the
// lens; once per update. `view_width`/`view_height`: the presented view.
class WaterEffects {
public:
    void reset();
    void tick(const Game&, bool enabled, int view_width, int view_height);

    // Drawing time: counts updates, so the waves pause with the game.
    std::uint64_t time() const { return time_; }

    const gg::WaterSplash& splash() const { return splash_; }

    const gg::LensRain& lens() const { return lens_; }

    // Calls f on each part of the state (quick saves).
    template <class F>
    void for_each_state(F&& f) {
        splash_.for_each_state(f);
        lens_.for_each_state(f);
        f(time_);
        f(primed_);
        f(wet_);
        f(zone_);
        f(act_);
        f(x_);
        f(y_);
        f(previous_dy_);
    }

private:
    void reset_splash();
    void tick_splash(const Game&, bool enabled);

    gg::WaterSplash splash_;
    gg::LensRain lens_;
    std::uint64_t time_{};
    bool primed_{}, wet_{};
    int zone_{-1}, act_{-1};
    int x_{}, y_{}, previous_dy_{};
};

// What the renderer found while drawing the picture.
struct WaterLayers {
    int width{}, height{};
    int view_x{}, view_y{}; // map position of the image's top-left pixel

    // Per pixel: water_pixel inside the dithered water.
    enum : std::uint8_t { water_pixel = 1 };

    std::vector<std::uint8_t> flags;
    // Per row: the colours in effect (raster changes included).
    std::vector<gg::Palette> palettes;
    // Act 2: the first row under the rising water (may lie outside the
    // image), or `none`.
    static constexpr int none = -1 << 20;
    int surface_row{none};
};

// The enhancement's layer over the picture (before the force field and HUD).
void draw_water(gg::Screen&, const Game&, const WaterLayers&, const WaterEffects&);

// The surface Y (map coordinates) of the dithered water in the map column
// at `x`, searched from `top` down to `bottom`, or WaterLayers::none.
int water_art_surface(const Game&, const WaterArt&, int x, int top, int bottom);

} // namespace sonic2_gg
