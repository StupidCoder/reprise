#pragma once
#include "amiga/hunk.hpp"
#include "mindwalker_amiga/data/assets.hpp"
#include "mindwalker_amiga/landscape/view_scene.hpp"
#include "mindwalker_amiga/world/random.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The landscape's flying creatures (A's module at $206BA8): twelve sprite
// creatures in two columns of up to six bands. A view chooses how many bands
// it has and which creatures start in it (or brings back the kinds it had,
// from a history of eight views); free creatures fly about their band,
// bounce, and now and then attach a tether to the walker when it is near and
// they are not hidden behind the scenery; an attached creature's tether end
// creeps towards the walker's head and catches it on arrival.
namespace mindwalker::landscape {

struct Creature {
    bool present{};       // $20837A
    bool enabled{};       // $208392: its band exists in this view
    int x{}, y{};         // $20822A / $2081FA: its flight position, top left (drawing rows)
    int top{}, bottom{};  // $208166 / $20819A: its band
    int dx{}, speed_x{}, dy{}, speed_y{}; // $20825A / $20828A / $2082BA / $2082EA
    int kind{-1};         // $208136: 0-4 (images from S's +$2D0 set)
    int depth{};          // $2081CA: the view row it is behind (0 = in front of all)
    int tether_x{-1}, tether_y{}; // $20834A / $20831A: the tether's end (-1: free)
    int shown_x{-80}, shown_y{-20}; // where its sprite was last moved ($206DD4; drawing rows)
    // Which of its sprite's pixels the scenery in front leaves visible, row
    // by row (bit 15 = left), as last built ($206DD4 / $203384).
    std::array<std::uint16_t, 14> visible{};
    std::uint16_t visible_row{}; // its sprite word (row 11, plane A) as last built
};

class Creatures {
public:
    explicit Creatures(const amiga::LoadedImage& landscape, const data::Assets&);

    // What the creatures need to know of the stage each update.
    struct Scene {
        int walker_x{}, walker_y{};  // the walker sprite's left edge and feet ($20B308/$20B30C)
        int head{};                  // the walker's head offset by form ($206B7E)
        int stage{};                 // +$5C
        const std::array<ViewPainter::DepthMask, 3>* masks{};
    };

    // $206D4A: every creature gone, none attached.
    void reset();
    // $2070A8 for a new view: the outgoing view's kinds into the history,
    // then the new view's bands and creatures ($2071AA). `view_x/y` the
    // view's origin; `creature_places` its count ($2099AC); `platform` and
    // `distance` A's platform function and block distance at the view's
    // middle; `marked` whether the marked block is in view ($2099B4).
    void enter_view(std::uint16_t view_x, std::uint16_t view_y, int creature_places, int platform, int distance,
                    bool marked, const Scene&);
    // One frame's share ($207722): every 5th frame (slower with each one
    // attached) the creatures move. Returns true when one caught the walker.
    bool update(const Scene&);
    // $206D9A: the view history cleared.
    void forget_views();
    // $2070BE: the current view's kinds into the history.
    void record_view();
    // $207E1A: every sprite moved off screen (in its band).
    void hide_all(const Scene&);
    // $207E5A: the present creatures' sprites back in place.
    void show_present(const Scene&);
    // $207EAC: nobody present, all hidden.
    void clear_presence(const Scene&);
    bool quiet() const { return quiet_; }

    int attached() const { return attached_; }          // $2083AE
    int score_bonus() const { return score_bonus_; }    // $208132: creature places x 25
    const std::array<Creature, 12>& all() const { return creatures_; }
    // The tether layer (display plane 4 over the view, drawing rows).
    const std::vector<std::uint8_t>& tethers() const { return tethers_; }
    // The timed clearing and redrawing of tethers ($2003C0).
    void tick_tethers();
    // $200E62: the tether layer cleared (a catch's picture; drawn again on
    // the next tick).
    void clear_tethers();

    // A creature's two sprite images (attached pair: plane A, plane B).
    const data::Bitmap& image_a(int kind) const;
    const data::Bitmap& image_b(int kind) const;

private:
    void place(int i, int x, int y, const Scene&); // $206DD4
    void choose_course(int i);             // $207D02
    void choose_depth(int i);              // $207DA6
    int choose_kind(int stage);            // $2076C0
    void set_kind(int i, int kind);        // $206ED8
    void move_free(int i, const Scene&);   // $2079BE
    bool follow(int i, const Scene&, int head_y); // $207810
    void draw_tether(int i);               // $20794E / $2061BA

    const amiga::LoadedImage& a_;
    const data::Assets& assets_;
    std::array<Creature, 12> creatures_;
    world::ShiftRegister random_a_, random_b_, random_c_; // $207F80, $207F84, $207F88
    int attached_{};
    int timer_{};               // $2083AA
    int appear_chance_{};       // $208128 (out of 4096)
    std::uint16_t kind_mask_{}; // $20812C
    int score_bonus_{};
    bool quiet_{};              // $208130
    struct ViewMemory {
        std::uint16_t x{0xFFFF}, y{};
        std::array<int, 5> kinds{};
    };
    std::array<ViewMemory, 8> history_{}; // $208034, 24 bytes each
    int history_next_{};                   // $2080F4
    std::uint16_t current_x_{0xFFFF}, current_y_{}; // $2080F8/$2080FA
    std::vector<std::uint8_t> tethers_;
    int tether_clock_{}, tether_round_{};  // $200520 / $200524
    bool tethers_drawn_{};                 // $200528
};

} // namespace mindwalker::landscape
