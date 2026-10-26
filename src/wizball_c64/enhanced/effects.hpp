#pragma once
#include "platform/image.hpp"
#include "wizball_c64/game/display.hpp"
#include "wizball_c64/game/front_end.hpp"
#include "wizball_c64/game/play.hpp"
#include <array>
#include <cstdint>
#include <vector>

// Enhancements drawn over the finished play picture, each an upgrade of
// something the original shows (all optional, off by default):
// - explosions: an alien's explosion frames ($A891, sprites $5F-$5C) throw
//   debris in the explosion sprite's own colours, on the C64's double-wide
//   multicolour pixels, fading down the C64's brightness ramp;
//   and a short ring of light in the explosion's colour; the wiz's own
//   burst ($7DB8) gets a larger ring;
// - the catellite: a hit it takes ($8998) throws sparks of its colours,
//   and when the ninth loses it ($8652) it bursts like an alien;
// - tunnels ($81C9-$8369): sparkles swirl up round the wiz as it sinks
//   into a tunnel's mouth, and round the exit as it rises, the wiz slides
//   out and it sinks again in the next place;
// - the blacked-out landscape (a black droplet caught, $88B5), which the
//   original shows only in the frames after the wiz bumps it ($8A02), fades
//   out over three frames instead of going at once;
// - the level-complete flight ($AE11): its text glows, dithered, in its
//   cycling colour;
// - the Wizlab: the weapon taken sparkles on its token in the bar, and the
//   paint used for the place ($AD98) rises out of the colour cauldron;
// - the cauldrons: paint poured in ($9E0D raises the level row by row)
//   sends bubbles up through it that pop at the surface;
// - the smart bomb (token 6): besides the original's background strobe, a
//   ring of light sweeps out from the wiz over the whole field;
// - the wiz's rings opening and closing as it goes at a place's end or
//   comes at a life's start ($646F, $6537) shed sparkles of its colours;
// - the shield (token 7, $7EA5; the catellite's own, $89B6), shown by the
//   original as blinking, gets a shimmering dithered field round it; the
//   wizspray's rings ($8592), which flicker, a soft glow along them that
//   stays through the frames they are off;
// - paint drops: a falling droplet ($A8EC) leaves a short drip trail of its
//   own pixels, and bursting on the ground ($A925, sprites $53-$5B) it also
//   throws drops of its paint that land again;
// - pickups: a pearl, extra life or token the wiz takes ($A95B and on), or
//   a droplet the catellite catches ($88B5), sparkles where it goes;
// - shots: the wiz's and the catellite's shot characters leave a short
//   fading streak of their own pixels and spark where they stop; the
//   blazers (slots 3-5, $850A) also glow, dithered, in a darker shade; the
//   aliens' shots (and the wiz's pieces when it bursts, $7DB8, which use
//   the same slots) trail their dot along their flight;
// - star drift: the five drifting sky stars ($747E) move in single pixels
//   instead of the multicolour pixel pairs (their sub-character position
//   has the precision).
// - the menu's and the player screen's flying stars ($90BD) leave short
//   fading trails.
// The game's state is only read; the effects keep their own random numbers.
namespace wizball::enhanced {

struct Settings {
    bool explosions{}, splashes{}, shots{}, stars{}, pickups{}, wiz{}, catellite{}, cauldrons{}, tunnels{}, blackout{};
    bool flight{}, lab{}, front{};
    bool any() const {
        return explosions || splashes || shots || stars || pickups || wiz || catellite || cauldrons || tunnels || blackout ||
               flight || lab || front;
    }
};

class Effects {
public:
    // Once per frame, after the game's tick (play: the current game or null).
    void update(const game::Play* play, const Settings& settings);
    // The front end's screens (the flying stars' trails).
    void update_front(const game::FrontEnd& front, const Settings& settings);
    void draw_front(platform::Image& image, const game::Display& display, const game::FrontEnd& front) const;
    // For the play picture: the blacked-out landscape's fade (0: none).
    int blackout_fade() const { return blackout_fade_; }
    // Over the rendered picture of `display` (side columns: its extension).
    void draw(platform::Image& image, const game::Display& display, const game::Play& play,
              const Settings& settings) const;

private:
    struct Particle {
        float x{}, y{}, dx{}, dy{}; // VIC coordinates (x) and raster lines (y)
        int colour{}, life{}, age{};
        float floor{1e9f};          // lands (ends) on this line
    };
    // Pieces flying out from a point; `upwards` keeps them to the top half
    // and lets them land again `floor` lines below where they started.
    void draw_flight(platform::Image& image, const game::Display& d, const game::Play& play) const;
    void swirl(float x, float y, float width, const std::vector<int>& colours); // a sparkle rising, curling
    void burst(float x, float y, const std::vector<int>& colours, int count, float speed, int life,
               bool upwards = false, float floor = 1e9f);
    std::uint32_t next();

    std::vector<Particle> particles_;
    std::vector<Particle> free_particles_; // outside the playfield (the token bar, the panel)
    int last_weapon_{};
    std::array<std::uint8_t, 9> last_painted_{};
    void step(std::vector<Particle>& particles, bool clip);
    std::array<std::uint8_t, 6> object_state_{};
    struct LastSprite {
        bool active{};
        int x{}, y{}, colour{};
        std::uint8_t state{};
    };
    std::array<LastSprite, 6> last_sprites_{};
    struct Light {
        int x{}, y{}, colour{}, age{};
        float size{1.0f}; // the wiz's burst is larger
    };
    struct Wave {
        int x{}, y{}, age{};
    };
    std::vector<Wave> waves_;
    std::uint8_t last_flash_{}, last_dying_{};
    std::uint8_t last_catellite_hits_{};
    std::uint8_t spray_tint_{1}; // the rings' last shown tint
    std::array<std::uint8_t, 4> last_paint_{};
    struct Bubble {
        int cauldron{}, x{}, row{}; // x: pixel within the sprite, row: 0-20 from the top
        int clock{};
        bool popped{};
    };
    std::vector<Bubble> bubbles_;
    long frame_{};
    std::array<std::vector<std::array<int, 3>>, 3> star_history_{}; // the last frames' star sprites
    int blackout_fade_{};
    bool last_catellite_on_{};
    game::Catellite::Sprite last_catellite_{};
    std::vector<Light> lights_;
    std::array<bool, 6> falling_{}; // droplets falling now (for their trails)
    struct ShotTrace {
        bool active{};
        int x{}, y{}, dx{}, dy{}; // the cell's top left (VIC x, raster), direction
        int colour{};             // as shown (a multicolour cell's low three bits)
        bool blazer{};            // slots 3-5 ($850A)
    };
    std::array<ShotTrace, 6> shots_{};
    struct EnemyTrace {
        bool active{};
        int x{}, y{};     // the shot's cell, top left (VIC x, raster)
        int dx{}, dy{};   // its step (1/32 cells a move)
        int colour{};
    };
    std::array<EnemyTrace, 16> enemy_shots_{};
    std::array<std::vector<std::array<int, 2>>, 6> trails_{}; // recent cell positions per shot
    std::uint32_t seed_{0x2468ACE1};
    const game::Play* last_play_{};
};

} // namespace wizball::enhanced
