#pragma once
#include "marble_madness_amiga/data/game_files.hpp"
#include "marble_madness_amiga/game/terrain.hpp"
#include <array>
#include <cstdint>
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
};

class Regions {
public:
    static constexpr int slots = 25;

    Regions(const data::GameFiles&, data::Course);

    // $10FC02: the view moved from band `from` to band `to` (16 lines each,
    // counted from the course's view offset): start the listed regions whose
    // band range is entered, stop running ones whose range is left.
    void bands_changed(int from, int to);
    // $1100A2: one world update of every region (pictured frames patch the
    // terrain's animated heights).
    void update(Terrain&);

    std::array<Region, slots>& all() { return regions_; }
    const std::array<Region, slots>& all() const { return regions_; }

    // What the scripts may ask about the marbles, set by the race before
    // each update.
    struct Racer {
        bool racing{};            // record +$18 == 1
        std::int32_t x{}, y{};
        std::int8_t feature{};    // +$1B
    };
    std::array<Racer, 2> racers{};
    int racer_count{1};           // $1005F0

    // Pictures the regions put into the playfield since the last call
    // ($110A46 / $11090C): the draw item (Track address), its x and its line
    // on the screen at the time (the playfield's ring line minus the view's).
    struct Blit {
        std::uint32_t item{};
        std::int16_t x{}, y{};
    };
    std::vector<Blit> take_blits() { return std::move(blits_); }
    // (view - the descriptor's first view), set by the race whenever the
    // view moves; pictured regions are placed relative to it.
    std::int16_t view_scroll{};
    std::int32_t view{};          // $1009D2, for the regions' screen positions

    // Continues a region's script where it stopped ($110198).
    void resume(Region& r, Terrain& terrain) {
        terrain_ = &terrain;
        interpret(r);
    }
    // Track region-block entry n (header +$14), a script or frame list address.
    std::uint32_t block_entry(int n) const;

private:
    void interpret(Region&);         // $110198
    void show_frame(Region&);        // $110830
    bool next_frame(Region&);        // $10FB90
    void step(Region&);              // $10FECA
    void stop(Region&);              // $10FD82 with 1
    void start(Region&, std::uint32_t script); // $10FD82 with 0
    bool running(std::uint32_t list_entry) const; // $10FB36
    Region* free_slot();

    const amiga::LoadedImage* track_{};
    std::uint32_t block_{};       // Track header +$14
    data::Course course_{};
    Terrain* terrain_{}; // set by each call that may patch it
    std::array<Region, slots> regions_{};
    std::vector<Blit> blits_;
};

} // namespace marble_madness::game
