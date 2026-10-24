#pragma once
#include "c64/text_frame.hpp"
#include "fort_apocalypse_c64/data/assets.hpp"
#include "fort_apocalypse_c64/data/tables.hpp"
#include "fort_apocalypse_c64/enhanced/canvas.hpp"
#include "fort_apocalypse_c64/enhanced/widescreen.hpp"
#include "fort_apocalypse_c64/game/game.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// The particle effects and smooth motion: modern motion drawn the old way.
// A presentation layer over the game, which plays exactly as the original
// either way (the VIC's collisions still come from the original picture).
// Each frame it draws the game's picture again without what it draws on
// its own, and adds on top, on the C64's pixel grid and in its colours.
// Smooth motion:
// - the craft banking smoothly: its level sprite turned by the bank angle
//   (the original's steeper shapes are the level one turned some 5 and 7
//   degrees), with the front view in between;
// - tanks gliding from cell to cell, rocking as they go, each step a jolt
//   over a bump, settling on the slope of the rock under their tracks;
// - mines gliding a pixel at a time instead of their two-pixel steps, and
//   tanks' shells gliding from cell to cell.
// With smooth motion the frame is a tilemap of the terrain with tanks, mines
// and shells taken out, and those drawn on it as objects where the game has
// them now. (The rules keep the original's cells: objects stamp their
// characters into the map, and collisions read them there.)
// Particle effects:
// - debris thrown from shot impacts and from destroyed tanks, whose turret
//   is tossed on a ballistic arc, spinning; debris and turrets bounce off
//   the landscape (the characters' own pixels);
// - shot-down mines that drop in an arc, turning, and blow up where they
//   hit the ground; tanks' shells and the enemy's shots throw debris too;
// - a shot-down craft that keeps its flashing but bursts apart in a spray
//   of debris and fire, its wreck falling in an arc, turning.
namespace fort_apocalypse::enhanced {

class EnhancedView {
public:
    EnhancedView(const data::Assets&, const data::GameTables&);

    struct Settings {
        bool particles{}; // debris, fire, tossed turrets, wrecks
        bool smooth{};    // smooth banking, tanks and mines gliding, tanks rocking
        int wide_columns{}; // more on each side, for a widescreen view
    };
    // After each of the game's frames: advance the effects, draw the picture.
    void update(const game::Game&, const Settings&);
    const c64::Picture& picture() const { return picture_; }
    // Forget the effects (a restart or a loaded game).
    void reset();

private:
    struct Particle {
        double x{}, y{}, vx{}, vy{}; // world pixels, per frame
        u8 colour{};
        int life{}, age{};
        bool fire{};      // fades through the fire colours
        bool free{};      // has left solid ground (it starts inside a hit)
    };
    // A piece thrown off as a whole and spinning: a tank's turret (it
    // bounces, rolls and settles) or a shot-down mine (it blows up where it
    // hits the ground).
    struct Wreck {
        Bitmap bitmap;
        double x{}, y{}, vx{}, vy{}, angle{}, spin{};
        double radius{3}; // its probes against the ground
        int age{}, rest{};
        bool free{};
        bool explodes{};
        int craft_shape{-1}; // the player's craft: drawn in its sprite's colour
    };
    struct TankLook {
        int column{-1}, row{};
        double x{};                       // drawn at, in world pixels
        double pitch{}, spin{}, target{}; // degrees, degrees per frame
    };

    // World pixel solid: the map character there has a set pixel pair.
    bool solid(const game::Game&, double x, double y) const;
    // The ground's slope under a tank's tracks (left end and bottom of its
    // body in world pixels), in degrees; positive turns it clockwise.
    double floor_slope(const game::Game&, double left, double bottom) const;
    void spawn_impact(const game::Game&, const game::Game::ShotImpact&, const c64::TextFrame&);
    void spawn_tank_debris(const game::Game&, const game::Game::TankDestroyed&,
                           const c64::TextFrame&);
    void move_particles(const game::Game&);
    void spawn_shell_burst(const game::Game&, const game::Game::ShellBurst&,
                           const c64::TextFrame&);
    // The craft shot down: its wreck and a big explosion.
    void spawn_craft_wreck(const c64::TextFrame&, const Viewport&, const game::Game&);
    // Debris of a character, thrown back from a hit.
    void spawn_hit_debris(const game::Game&, game::Game::MapCell, u8 code, double x, double y,
                          double ux, double uy, const c64::TextFrame&);
    void spawn_mine_wreck(const game::Game&, const game::Game::MineDestroyed&,
                          const c64::TextFrame&);
    void move_wrecks(const game::Game&);
    // Pieces of a picture flung out from (x, y), and fire.
    void spawn_pieces(const Bitmap&, double left, double top, double share, double x, double y,
                      double speed);
    void spawn_fire(double x, double y, int count, double speed);
    void follow_window(const game::Game::MapCell&);
    double random(double low, double high);

    void draw_craft(const c64::TextFrame&, const c64::Layers&, const game::Game&);
    bool craft_on_screen(const c64::TextFrame&, const game::Game&) const;
    void track_craft(const c64::TextFrame&, const game::Game&, const Viewport&);
    // Takes out of the frame what is drawn on its own.
    void hide_objects(c64::TextFrame&, const game::Game&, const ScreenCodes&) const;
    void draw_tanks(const c64::TextFrame&, const game::Game&, const Viewport&, const Clip&);
    void draw_mines(const c64::TextFrame&, const game::Game&, const Viewport&, const Clip&);
    void draw_shells(const c64::TextFrame&, const game::Game&, const Viewport&, const Clip&);

    const data::Assets& assets_;
    const data::GameTables& tables_;
    c64::Picture picture_{};
    std::vector<Particle> particles_;
    std::vector<Wreck> wrecks_;
    std::array<TankLook, 6> tanks_{};
    std::array<std::optional<std::pair<double, int>>, 39> mines_{}; // drawn at x, row
    std::array<std::optional<std::array<double, 2>>, 6> shells_{};  // drawn at x, y
    Settings settings_{};
    std::optional<game::Game::MapCell> window_;
    double bank_{-1}; // smoothed bank step 0-8 (-1: not yet seen)
    int craft_shape_{-1};  // the shape last drawn for the craft
    double craft_angle_{};
    bool was_crashing_{};
    // The craft's place in the world, to give its wreck the craft's way.
    std::optional<std::array<double, 2>> craft_at_;
    std::array<double, 2> craft_velocity_{};
    std::uint32_t rng_{0x9E3779B9};
};

} // namespace fort_apocalypse::enhanced
