#pragma once
#include "bruce_lee_c64/game/game.hpp"
#include "c64/text_frame.hpp"
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

// The optional visual upgrades: a presentation layer that draws the play
// screen again from the game's state. Smooth motion lets the actors and
// room 18's fireballs glide over the three frames between the game's
// steps and scrolls the lift a row at a time; particle effects add sparks
// where blows land, a burst of light when a lantern is taken, the pieces of
// a crumbling floor and embers behind the fireballs; light effects give the
// hanging lanterns a glow that follows their flickering flames (with an
// in-between frame when a flame changes) and flash an actor a blow lands
// on; cracks crawl across a crumbling floor before it opens. Everything
// stays on the C64's pixel grid and in its 16 colours, and only shows what
// the game itself does (but the gore option, which the original doesn't
// have: defeated enemies burst into bloody pieces): the rules, the
// collisions and the sounds are the original's (the game keeps its own
// picture for its collision checks).
namespace bruce_lee::enhanced {

struct Settings {
    bool smooth{};    // actors, fireballs and the lift's scrolling
    bool particles{};
    bool lights{};    // the lanterns' glow and flames, the flash of a blow that lands
    bool gibs{};      // defeated enemies burst into bloody pieces (not in the original)

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(smooth, particles, lights, gibs); }
};

class Effects {
public:
    // Once per frame, after the game's tick.
    void update(const game::Game&, Settings);
    const c64::Picture& picture() const { return picture_; }

private:
    struct Glide {
        bool valid{};
        double x{}, y{};          // shown place (pixel pairs, lines)
        double vx{}, vy{};
        int frames_left{};
        std::uint8_t target_x{}, target_y{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(valid, x, y, vx, vy, frames_left, target_x, target_y); }
    };
    struct Particle {
        double x{}, y{};          // picture pixels
        double vx{}, vy{}, gravity{};
        int age{}, life{};
        const std::uint8_t* colours{}; // fading colours, over the particle's life
        int colour_count{};
        bool bounces{};           // lands on the room's graphics and stays a while
        bool resting{};
        int size{1};              // in double-wide pixels (square)

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        int palette_code() const;
        bool use_palette(int);
        template <class A> void persist(A &ar) {
            int palette = A::reading ? 0 : palette_code();
            if (palette < 0)
                A::fail();
            ar(x, y, vx, vy, gravity, age, life, colour_count, bounces, resting, size, palette);
            if constexpr (A::reading)
                if (!use_palette(palette))
                    A::fail();
        }
    };
    void glide(Glide&, bool present, std::uint8_t x, std::uint8_t y, bool snap);
    bool smooth_lift(const game::World&, data::Charset&);
    bool flame_inbetween(const game::World&, data::Charset&);
    void glow(const game::World&, const c64::Layers&);
    void cracks(const game::World&, const c64::Layers&);
    void burst_gibs(double x, double y);
    void events(const game::World&, Settings);
    void spark(double x, double y, int count);
    void burst(double x, double y);
    void fragments(double x, double y, std::uint8_t colour);
    void ember(double x, double y);
    void step_particles(const game::World&, const c64::Layers&);
    int pixel_pair(const game::World&, const c64::Layers&, int x, int y) const;
    void watch_defeats(const game::World&);
    void carnage(const game::World&);
    void draw_popups(const game::World&);
    void watch_hazards(const game::World&);
    static std::array<double, 2> body_middle(const game::World&, const game::Actor&);
    void draw_particles();
    double random(double low, double high);

    std::array<Glide, 3> actors_{};
    std::array<Glide, 2> fireballs_{};
    std::vector<Particle> particles_;
    std::mt19937 rng_{1984};

    // What the last frame showed, for spotting the game's events.
    bool known_{};
    int room_{-1};
    std::vector<bool> lanterns_;
    std::array<std::uint8_t, 3> moves_{};
    std::array<std::uint8_t, 3> crumble_timers_{};
    std::uint8_t fragment_colour_{};
    // One-colour tables for pieces of any colour (smashed decoration).
    static constexpr std::array<std::uint8_t, 16> debris_colours_{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    // Light effects.
    std::array<int, 3> flash_{};               // frames of white left per actor
    std::array<std::uint8_t, 3> flash_moves_{};
    long frame_{};
    // The smooth lift: when its glyphs last changed, and the charset drawn.
    std::uint8_t lift_phase_{0xFF};
    long lift_changed_{};
    data::Charset charset_{};                  // the room charset as shown (lift, flames)
    // The flames ($40A9): each flame glyph's last pattern, the one before
    // and the frames of its in-between left; each lantern's glow level
    // easing to its flame.
    std::array<std::uint8_t, 5> flame_rows_{};
    std::array<std::uint8_t, 5> flame_old_{};
    std::array<int, 5> flame_inbetween_{};
    std::vector<double> glow_level_;
    // Enemies, for spotting a defeat (energy gone, then removed).
    std::array<bool, 3> was_present_{};
    std::array<bool, 3> was_defeated_{};
    std::array<std::array<double, 2>, 3> last_place_{};
    std::array<std::uint32_t, 3> hazard_deaths_{};
    // Carnage mode: the last of its events shown, and the combo counts
    // floating up from where they were scored.
    std::uint32_t carnage_seen_{};
    struct Popup {
        double x{}, y{};
        std::string text;
        int age{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(x, y, text, age); }
    };
    std::vector<Popup> popups_;
    // Falling decoration as it looked (0xFF: nothing), where it was last.
    struct ChunkPicture {
        std::uint32_t id{};
        int width{}, height{};
        std::vector<std::uint8_t> pixels;
        double x{}, y{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(id, width, height, pixels, x, y); }
    };
    std::vector<ChunkPicture> chunks_;
    void draw_chunks(const game::World&);
    bool bruce_burst_{};                       // Bruce in pieces until his dying move ends

    c64::Picture picture_;

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(actors_, fireballs_, particles_, rng_, known_, room_, lanterns_, moves_, crumble_timers_,
           fragment_colour_, flash_, flash_moves_, frame_, lift_phase_, lift_changed_, charset_, flame_rows_,
           flame_old_, flame_inbetween_, glow_level_, was_present_, was_defeated_, last_place_,
           hazard_deaths_, carnage_seen_, popups_, chunks_, bruce_burst_, picture_);
    }
};

} // namespace bruce_lee::enhanced
