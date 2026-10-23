#pragma once
#include "c64/text_frame.hpp"
#include "fort_apocalypse_c64/data/assets.hpp"
#include "fort_apocalypse_c64/data/tables.hpp"
#include "fort_apocalypse_c64/enhanced/canvas.hpp"
#include "fort_apocalypse_c64/game/game.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// The enhanced mode: modern motion drawn the old way. A presentation layer
// over the game, which plays exactly as the original either way (the VIC's
// collisions still come from the original picture). Each frame it draws
// the game's picture again without the player's sprite and the tanks'
// characters, and adds on top, on the C64's pixel grid and in its colours:
// - the craft banking smoothly: its level sprite turned by the bank angle
//   (the original's steeper shapes are the level one turned some 5 and 7
//   degrees), with the front view in between;
// - tanks that rock as they drive, each step a jolt over a bump, settling
//   on the slope of the rock under their tracks;
// - debris thrown from shot impacts and from destroyed tanks, whose turret
//   is tossed on a ballistic arc, spinning; debris and turrets bounce off
//   the landscape (the characters' own pixels);
// - shot-down mines that drop in an arc, turning, and blow up where they
//   hit the ground.
namespace fort_apocalypse::enhanced {

class EnhancedView {
public:
    EnhancedView(const data::Assets&, const data::GameTables&);

    // After each of the game's frames: advance the effects, draw the picture.
    void update(const game::Game&);
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
    };
    struct TankLook {
        int column{-1}, row{};
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
    // Tank bodies found on the screen (their cells hidden from the frame).
    struct ShownTank {
        int screen_column{}, screen_row{};
        std::array<u8, 3> body{};
        std::optional<u8> turret;
        int index{-1}; // the game's tank, when it can be told
    };
    std::vector<ShownTank> take_tanks(c64::TextFrame&, const game::Game&);
    void draw_tanks(const std::vector<ShownTank>&, const c64::TextFrame&, const game::Game&,
                    const Viewport&, const Clip&);

    const data::Assets& assets_;
    const data::GameTables& tables_;
    c64::Picture picture_{};
    std::vector<Particle> particles_;
    std::vector<Wreck> wrecks_;
    std::array<TankLook, 6> tanks_{};
    std::optional<game::Game::MapCell> window_;
    double bank_{-1}; // smoothed bank step 0-8 (-1: not yet seen)
    std::uint32_t rng_{0x9E3779B9};
};

} // namespace fort_apocalypse::enhanced
