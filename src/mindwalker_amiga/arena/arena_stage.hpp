#pragma once
#include "amiga/paula_voices.hpp"
#include "mindwalker_amiga/arena/arena_map.hpp"
#include "mindwalker_amiga/data/assets.hpp"
#include "mindwalker_amiga/data/game_files.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include "mindwalker_amiga/screen/gadget_bar.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The arena (program C; main loop $400040): a hunt through the maze of
// 8 x 8 rooms (arena_map) for the target, then back to the exit in the home
// room. The player walks freely, across walls too, but a pulse of yellow
// runs through the walls' thirteen colours now and then, and standing on a
// wall while it runs is death. The temperature gauge and the drone's pitch
// tell how near the target is (afterwards: the exit). Creatures (six
// sprites) start in each room and come in at its edges; some chase the
// player, and touching one drains strength (a lightning bolt); at no
// strength the player dies. A beam from the player's aim (fire held: the
// stick turns it, and the beam reaches far) bent by a drifting wind kills
// them. Taking the target walls the exit in and gives a life; the exit
// with it wins (the psyche screen follows), without it leaves (back to the
// landscape); a death costs a life and leaves.
namespace mindwalker::arena {

struct ArenaInput {
    bool up{}, down{}, left{}, right{}, fire{};
    bool button{}; // the left mouse button
};

// Where C reads its inputs, in order: the left button each frame
// ($4005A8), then the stick and the fire button ($40251E) on moving frames.
enum class ArenaRead { button, stick, fire };
using ArenaReader = std::function<ArenaInput(ArenaRead)>;

class ArenaStage {
public:
    struct Start {
        std::uint32_t seed{};  // shared +$4C
        int stage{};           // +$5C
        int lives{3};          // +$6E
        std::uint32_t score{}; // +$66
        int form{};            // +$48: the walker's form (the player's figure)
    };
    enum class Outcome { playing, won, left, died, quit };

    ArenaStage(const data::GameFiles&, const data::Assets&, Start);

    // One frame (20 ms); `gadget` a gadget clicked this frame (-2 the close
    // gadget; the P box, id $10, pauses and resumes).
    void frame(const ArenaReader&, int gadget = -1);
    platform::Image picture(bool sprites = true) const; // (without sprites: as a screen dump shows it)
    void render_audio(std::span<float> stereo, int rate) { voices_.render(stereo, rate); }

    Outcome outcome() const { return outcome_; }
    bool over() const { return outcome_ != Outcome::playing && sequence_.empty(); }
    bool in_sequence() const { return !sequence_.empty(); } // frames without reads (a scroll, a death, the win)
    std::uint32_t score() const { return score_; }
    int lives() const { return lives_; }
    int gadget_at(int x, int y) const;

    // For replays.
    struct Player {
        int x{0xA0}, y{0x69};      // $40264E / $402652: in the room, 0-255 x 0-191
        int room_x{4}, room_y{4};  // h10 / $401CA4
        int aim{8};                // h12: 16 directions
        int strength{0x15E};       // $40264A
        int frame{};               // $402646: the figure's frame (index into its set)
        bool has_target{};         // $401084
    };
    const Player& player() const { return player_; }
    const ArenaMap& map() const { return map_; }
    int wind_direction() const { return wind_direction_[0]; }
    int wind_strength() const { return wind_strength_[0]; }
    int pulse() const { return pulse_; }
    int frozen() const { return frozen_; }
    int sound_period() const { return period_history_[3]; }
    void set_pulse(int counter) { pulse_ = counter; } // (a poke in a recording)
    // For replays: the state the recorder logs, by name (`report(name, value)`).
    void describe(const std::function<void(const std::string&, long)>& report) const;

private:
    struct Creature {
        bool active{};
        int x{}, y{}, vx{}, vy{};
        unsigned behaviour{};      // bit 0 chases across, bit 1 up and down
        int dying{};               // 0 alive, 1-15 dissolving
        unsigned anim{};
        bool shown{};              // its sprite placed on screen
        std::array<std::uint16_t, 34> sprite{}; // 17 rows, two words each (its current image)
    };
    using Step = std::function<void()>;

    void tick(const ArenaReader&);
    void enter_room(bool transition);                 // $40167C
    void spawn_room_creatures();                      // $403DAC
    void draw_room(bool transition);                  // $40168C
    void draw_panel();                                // $402720
    void show_lives();                                // $40052C
    void draw_strength();                             // h14
    void print_score();                               // $404908
    void move_player();                               // $401D24
    void pick_frame();                                // $402058
    void beam();                                      // $403382
    void creatures();                                 // $4036CE
    void creature_step(int slot);                     // $40370E
    void spawn_at_edge(int slot);                     // $403AAC
    void place_creature(int slot, int x, int y);      // $403EDE
    void set_creature_image(int slot);                // $404080
    void hit_player();                                // $40412C
    void lightning();                                 // $4041D2
    void bolt(int x0, int y0, int x1, int y1, int amplitude); // $404322
    bool pulse_step();                                // $402930
    void wind_step();                                 // $402C34
    void sound_tick();                                // $404E46
    void die();                                       // $401A90 (as a sequence)
    void wall_death();                                // $402A2C (as a sequence)
    void victory();                                   // $40653A (as a sequence)
    void pause();                                     // $40070C
    void resume();                                    // $40077E
    void scroll(int dx, int dy, int x0, int y0, int x1, int y1); // ScrollRaster on the screen
    void overlay_line(int x0, int y0, int x1, int y1, std::uint16_t pattern = 0xFFFF);
    int sine(int direction, int radius) const;        // $402DB2 (cosine at direction + 4)
    void load_colours() { shown_colours_ = colours_; }

    const amiga::LoadedImage& c_;
    const data::Assets& assets_;
    const data::ImageSet& figure_;
    // The game's generators (world/random.hpp), each seeded from C's data
    // XOR the world's seed (a zero result replaced by a constant).
    MazeRandom maze_random_;                   // the maze's; play draws creatures' cells and bolts from it
    world::ShiftRegister fidget_random_;       // $4025FC: the standing figure's fidgets
    world::ShiftRegister wind_seed_a_, wind_seed_b_; // $402EBC / $402EC0: the wind's generators' seeds
    world::ShiftRegister pulse_random_;        // $402ED4: the pulse's skipped rounds
    std::array<world::Mixer, 4> wind_turn_, wind_pull_; // $4030A4 / $4030C4
    world::ShiftRegister creature_random_;     // h21 $4045EC: behaviour, drift, edges, kills, colours
    world::ShiftRegister edge_random_;         // $4045F0: where on an edge; erosion
    world::ShiftRegister spawn_random_;        // $4045F4: coming in, animation phases, bolts
    ArenaMap map_;
    int stage_{}, lives_{}, form_{};
    std::uint32_t seed_{}, score_{};
    std::array<std::uint16_t, 32> colours_{}, shown_colours_{};
    screen::Canvas screen_;                    // the screen (pens 0-31)
    screen::Canvas back_;                      // f4c: a room drawn before it scrolls in
    std::vector<std::uint8_t> overlay_ = std::vector<std::uint8_t>(320 * 200); // f48: becomes display plane 4
    screen::GadgetBar bar_;
    Player player_;
    int figure_rows_{};                        // $402632
    int cell_x_{}, cell_y_{};                  // $402656 / $40265A: the player's map cell (as last looked at)
    bool sprite_shown_{};
    int stand_counter_{};                      // $40263E
    int aim_counter_{};                        // $403624
    bool stick_up_{}, stick_down_{}, stick_left_{}, stick_right_{}, fire_{};
    int direction_{};                          // $402600: the last room change (0 right, 1 left, 2 up, 3 down)
    bool stop_spawning_{};                     // $402604
    std::array<Creature, 6> creatures_{};
    std::array<std::array<std::uint16_t, 34>, 4> creature_frames_{}; // h27
    int bolt_frames_{}, bolt_x_{}, bolt_y_{}, bolt_turn_{}; // $404874-$404880
    std::array<int, 4> wind_direction_{}, wind_strength_{}; // $403080 / $403090 (only slot 0 is used)
    std::uint16_t line_pattern_{};                         // $403608
    int pulse_{}, pulse_wait_{}, pulse_runs_{}, pulse_colour_{}; // $402EC4, $402EC8, $402ECC, $402ED0
    int sound_wait_{};                                     // $4050B4
    std::array<int, 4> period_history_{900, 900, 900, 900}; // $4050C4
    int hit_noise_{}, death_noise_{}, death_pitch_{};      // $4050B8, $4050BC, $4050C0
    int frozen_{};                                         // $4010FA
    bool button_was_down_{}, paused_{};
    Outcome outcome_{Outcome::playing};
    std::vector<Step> sequence_;   // frames of a scripted sequence (room transition, death, victory, resume)
    amiga::PaulaVoices voices_;
    std::vector<std::int8_t> drone_, noise_, squeal_, victory_wave_ = std::vector<std::int8_t>(64);
    int victory_count_{};                                  // $406800
};

} // namespace mindwalker::arena
