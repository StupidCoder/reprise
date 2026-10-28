#pragma once
#include "marble_madness_amiga/data/game_files.hpp"
#include "marble_madness_amiga/game/probe.hpp"
#include "marble_madness_amiga/game/random.hpp"
#include "marble_madness_amiga/game/terrain.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

// Course regions: the scripted parts of a course (moving ground, rails, the
// start gate, goal markers, traps). Each Track lists them with a script and
// the screen bands they belong to; up to 25 run at a time ($100CE2, $56 bytes
// each), switched on and off as the view scrolls ($10FC02). A region's
// script (bytecode in the Track, run by $110198) sets a reference point, a
// terrain code that tells the marble code how to react ($116EE0), waits,
// moves, and steps through frame lists whose entries name a picture and a
// patch of the animated ground heights ($110830, $10D4xx).
namespace marble_madness::game {

struct Region {
    bool active{};            // +$18
    std::uint8_t player{};    // +$19
    std::uint8_t state{};     // +$1A: 0 holding, 1 moving, 2 animating, 3 starting, 4 waiting for a feature
    std::uint8_t feature{};   // +$1B: the feature number a waiting region watches
    std::int16_t count{};     // +$1C: hold time, or frame-list rounds
    std::uint8_t kind{};      // +$1E: 0 no picture, 1 frames, 2 frames only while moving or animating
    std::int8_t code{};       // +$1F: terrain code (what touching it does)
    std::uint8_t move_tick{}, move_period{}; // +$20, +$21
    std::uint8_t anim_tick{}, anim_period{}; // +$22, +$23
    std::uint8_t loop{}, loop2{};            // +$24, +$25
    std::int16_t width{}, height{};          // +$26, +$28 (pictured regions)
    std::uint32_t loop_pc{}, loop2_pc{}, return_pc{}; // +$2A, +$2E, +$32
    std::uint32_t pc{};       // +$36 script position (Track address)
    std::uint32_t script{};   // +$3A
    std::uint32_t frame{};    // +$3E current frame entry
    std::uint32_t shown_frame{}; // +$42
    std::uint32_t anim_frames{}, move_frames{}; // +$46, +$4A
    std::int16_t screen_x{}, screen_y{};        // +$4E, +$50
    std::int16_t band_from{}, band_to{};        // +$52, +$54
    std::int32_t vx{}, vy{};  // +$0, +$4 (16.16 per step, or screen pixels for pictured regions)
    std::int32_t x{}, y{}, z{}; // +$C, +$10, +$14 reference point (16.16)

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(active, player, state, feature, count, kind, code, move_tick, move_period, anim_tick, anim_period,
           loop, loop2, width, height, loop_pc, loop2_pc, return_pc, pc, script, frame, shown_frame,
           anim_frames, move_frames, screen_x, screen_y, band_from, band_to, vx, vy, x, y, z);
    }
};

class Regions {
public:
    static constexpr int slots = 25;

    Regions(const data::GameFiles&, data::Course);

    // $10FC02: the view moved from band `from` to band `to` (16 lines each,
    // counted from the course's view offset): start the listed regions whose
    // band range is entered, stop running ones whose range is left.
    void bands_changed(int from, int to, Random& random);
    // $1100A2: one world update of every region (pictured frames patch the
    // terrain's animated heights).
    void update(Terrain&, Probe&);

    std::array<Region, slots>& all() { return regions_; }
    const std::array<Region, slots>& all() const { return regions_; }

    // What the scripts may ask about the marbles, set by the race before
    // each update.
    struct Racer {
        bool racing{};            // record +$18 == 1
        std::int32_t x{}, y{};
        std::int8_t feature{};    // +$1B

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(racing, x, y, feature); }
    };
    std::array<Racer, 2> racers{};
    int racer_count{1};           // $1005F0

    // Pictures the regions put into the playfield since the last call
    // ($110A46 / $11090C): the draw item (Track address), its x and its line
    // on the screen at the time (the playfield's ring line minus the view's).
    struct Blit {
        std::uint32_t item{};
        std::int16_t x{}, y{};
        // Intermediate's wave reaching the ramp's end: 0-15 for the special
        // drawing ($112464), else -1.
        std::int8_t wave_end{-1};
        // Aerial's vacuums and bumpers: the course cell their picture is
        // drawn over (outside its mask) instead of the playfield ($110A46
        // links it into the picture's cell record), else -1.
        std::int16_t background{-1};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(item, x, y, wave_end, background); }
    };
    std::vector<Blit> take_blits() { return std::move(blits_); }
    // (view - the descriptor's first view), set by the race whenever the
    // view moves; pictured regions are placed relative to it.
    std::int16_t view_scroll{};
    std::int32_t view{};          // $1009D2, for the regions' screen positions

    // Continues a region's script where it stopped ($110198).
    void resume(Region& r, Terrain& terrain, Probe& probe) {
        terrain_ = &terrain;
        probe_ = &probe;
        interpret(r);
    }
    // $10FD82 with 1, for course rules.
    void stop_region(Region& r) { stop(r); }
    // Sounds the scripts asked for since the last call (entry, side).
    struct Sound {
        int id{};
        bool right{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(id, right); }
    };
    std::vector<Sound> take_sounds() { return std::exchange(sounds_, {}); }
    // The regions registered for Aerial's course occlusions ($110338):
    // code 10 ($110174), codes 11 and 13 ($11015C, six), code 12 ($110178,
    // four); -1 for none.
    int code10() const { return code10_; }
    const std::array<int, 6>& codes11_13() const { return codes11_13_; }
    const std::array<int, 4>& codes12() const { return codes12_; }
    // Display objects the regions added or removed since the last call
    // (the race keeps the display list).
    struct DisplayEvent {
        bool add{};
        std::uint8_t type{}, index{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(add, type, index); }
    };
    std::vector<DisplayEvent> take_display_events() { return std::exchange(display_events_, {}); }

    // Ultimate's moving goal area (code $19, $11E5EA-$11E992): its region
    // lists four pictures of the course's last 30 map rows (stored as
    // further map rows: the frame's first row) with a patch of the heights
    // each. A task renders the next picture into a spare buffer; once it is
    // done the region's step patches the heights and picks the frame's set
    // of cover bit-maps ($1006A4), and the next display shows the picture
    // and asks for the following one, round and round. The rendering takes
    // 18-25 updates in the original, as busy as the program is (measured
    // on Ultimate's recordings); the port takes the usual 21, and replays
    // of the original the times it logged.
    static constexpr int picture_render_time = 21;
    std::deque<int> forced_render_times;
    // The display step ($1076B6 with $11E98D set, $11E588): show a finished
    // picture and ask for the next.
    void show_rendered_picture();
    // The map rows the moving area shows: the first row it replaces and
    // the first row shown there (-1 while it shows its own rows).
    int picture_home_row() const { return picture_.home_row; }
    int picture_shown_row() const { return picture_.shown_row; }
    // Which frame's cover bit-maps the marbles use ($1006A4 / $90).
    int cover_set() const { return picture_.cover_set; }
    // Research (route search): updates until the next picture is due.
    int picture_due_in() const { return picture_.rendering ? picture_.render_left : picture_render_time + 1; }

    // The drawbridge region (code 3, Beginner), or nullptr ($110158).
    const Region* drawbridge() const { return drawbridge_ < 0 ? nullptr : &regions_[std::size_t(drawbridge_)]; }

    // Track region-block entry n (header +$14), a script or frame list address.
    std::uint32_t block_entry(int n) const;

private:
    void interpret(Region&);         // $110198
    void show_frame(Region&);        // $110830
    void apply_patch(std::uint32_t patch);
    void poll_picture(Region&);      // $11E848
    bool next_frame(Region&);        // $10FB90
    void step(Region&);              // $10FECA
    void stop(Region&);              // $10FD82 with 1
    void start(Region&, std::uint32_t script); // $10FD82 with 0
    bool running(std::uint32_t list_entry) const; // $10FB36
    Region* free_slot();
    void reset_wave_heights();       // Intermediate, block entry 3 ($110C80)
    void draw_wave(int round);       // Intermediate, block entry 4 ($110C96)

    const amiga::LoadedImage* track_{};
    const amiga::LoadedImage* program_{};
    std::uint32_t block_{};       // Track header +$14
    data::Course course_{};
    Terrain* terrain_{}; // set by each call that may patch it
    Probe* probe_{};     // likewise: $106A00 leaves the regions' points in it
    std::array<Region, slots> regions_{};
    int drawbridge_{-1};
    int code10_{-1};
    std::array<int, 6> codes11_13_{-1, -1, -1, -1, -1, -1};
    std::array<int, 4> codes12_{-1, -1, -1, -1};
    std::vector<Sound> sounds_;
    void sound(int id, bool right) { sounds_.push_back({id, right}); }
    void sound(int id) { sound(id, probe_ != nullptr && probe_->x() < probe_->y()); } // $1224D8
    bool wave_ready_{true};       // $1007A5
    int waves_{};                 // $1007A4: code-6 regions running
    std::vector<DisplayEvent> display_events_;
    std::vector<Blit> blits_;
    struct Picture {
        bool wanted{};            // $11E98E
        bool rendering{};         // $11E98F
        bool swap_due{};          // $11E98D
        int render_left{};
        int rendering_row{-1};
        int shown_row{-1};
        int home_row{-1};
        int cover_set{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(wanted, rendering, swap_due, render_left, rendering_row, shown_row, home_row, cover_set);
        }
    } picture_;

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(racers, racer_count, view_scroll, view, forced_render_times, block_, course_, regions_,
           drawbridge_, code10_, codes11_13_, codes12_, sounds_, wave_ready_, waves_, display_events_, blits_,
           picture_);
    }
};

} // namespace marble_madness::game
