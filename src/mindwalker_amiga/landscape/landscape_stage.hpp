#pragma once
#include "mindwalker_amiga/data/assets.hpp"
#include "mindwalker_amiga/data/game_files.hpp"
#include "mindwalker_amiga/landscape/creatures.hpp"
#include "mindwalker_amiga/landscape/interlude.hpp"
#include "mindwalker_amiga/landscape/map_view.hpp"
#include "mindwalker_amiga/landscape/sound.hpp"
#include "mindwalker_amiga/landscape/landscape_bar.hpp"
#include "mindwalker_amiga/landscape/view_cells.hpp"
#include "mindwalker_amiga/landscape/view_scene.hpp"
#include "mindwalker_amiga/landscape/zap_bolt.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include "mindwalker_amiga/world/random.hpp"
#include "mindwalker_amiga/world/world.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// The landscape stage (program A) as far as it is rebuilt: the view, the
// walker falling onto the start cell and walking (speed, frames, height
// following, view shifts), turning with the compass, the map of visited cells
// and its points. Rebuilt from A's main loop ($200262), the step $209D8A /
// $20A45C and the routines they call.
// The flying creatures (their own module, creatures.hpp) fly, attach and
// catch the walker; a catch costs damage and, at 1600, a life. The opening
// reveal and the catch picture play as sequences (interlude.hpp).
// The walker's sprite is masked by the scenery in front of it (h11).
// Holding fire aims a lightning bolt at a cursor and zaps creatures. The
// emblem opens the map (map_view.hpp).
// Not rebuilt yet: the view's own creatures, finds,
// pyramids ($1000 cells), the pause box.
namespace mindwalker::landscape {

struct LandscapeInput {
    bool up{}, down{}, left{}, right{}, fire{};
    bool button{};      // the left mouse button
    int pointer_x{}, pointer_y{};
};

// Where the original reads its inputs, in order: the left button once per
// frame ($203F58), the stick ($208558: JOY1DAT, then the fire button) when the
// walker takes a step. A replay counts these reads as the recorder does.
enum class InputRead { button, stick, fire };
using InputReader = std::function<LandscapeInput(InputRead)>;

// The walker's record ($20B2F0 on).
struct Walker {
    int form{};                 // $206B64: 0-3, chosen by the surface it starts on
    int height{};               // $20B2F0: the sprite's rows (form table $206B1C)
    int x{}, z{};               // $20B2F4/$20B2FC: position in the view, 0-319 across its 4 x 80 units
    int level{};                // $20B2F8: the walker's level (0 = top of the picture)
    std::uint16_t cell_x{}, cell_y{}; // $20B300/$20B304: the world cell it stands on
    int screen_x{}, screen_y{}; // $20B308/$20B30C: where the sprite's feet are drawn
    std::uint16_t terrain{};    // $20B320: the terrain word of its cell
    std::uint16_t objects{};    // $20B34C
    int column{}, row{};        // $20B330/$20B334: its cell in the view
    int last_column{-1}, last_row{}; // $20B340/$20B344: where cell events last ran
    std::uint16_t next_terrain{}, next_objects{}, next_mix{}; // $20B32C/$20B350/$20B354
    int next_x{}, next_z{}, next_column{}, next_row{};        // $20B324/$20B328/$20B338/$20B33C
    std::uint16_t next_cell_x{}, next_cell_y{};                // $20B348/$20B34A
    int sprite_rows{};          // rows of the frame shown (0: hidden), $209B54
    bool in_water{};            // $20B358
    int frame{};                // $20B366: index into the form's frame list
    int frame_base{4};          // $20B362
    int frame_counter{};        // $20B36A
    int speed{2}, speed_counter{2}; // $20B372/$20B36E
    int last_dx{}, last_dz{};   // $20B35A/$20B35E
    int view_row{}, view_column{}; // $20B318/$20B31C: its place facing the view ($2057D0, each placing)
    int depth{};                // $20B310: its depth into the view + 24
    std::vector<std::uint16_t> visible; // the sprite's rows as h11 masks them (bit 15 = left)
    int hidden_frames{};        // $20B386: frames wholly hidden (from the 9th the marker shows)
    int edge_push{};            // $20B382: frames pushed at a held view's edge
    bool aiming{};              // $20B376: fire held (the target cursor shown, colours 19-31 lit)
    int cursor_x{}, cursor_y{}; // $20B37A/$20B37E: the target (drawing rows)
};

class LandscapeStage {
public:
    // What the landscape keeps when it is left ($20B056, shared +$18-$60)
    // and takes up again when it resumes (mode not 0: h35, $2096B2,
    // $20AEE4): the same world from the same seed, the walker where it was.
    struct Resume {
        std::uint16_t home_x{}, home_y{}, marked_u{0xFFFF}, marked_v{}; // +$18-$24
        int direction{};                                                // +$28
        std::uint16_t cell_x{}, cell_y{};                               // +$2C/$30
        int x{}, level{}, z{};                                          // +$34/$38/$3C
        int slow{};                                                     // +$40
        std::array<bool, 2> found{};                                    // +$44/$46
        int form{};                                                     // +$48
        std::uint16_t view_x{}, view_y{};                               // +$50/$54
        int damage{};                                                   // +$58
        bool ramp_closed{true};                                         // +$60
    };
    struct Start {
        std::uint32_t seed{};
        int stage{};
        int lives{3};
        std::uint32_t score{};
        std::optional<Resume> resume; // (none: a new game's landscape)
    };
    // $20B056: the state to resume from, as the stage stands.
    Resume resume_state() const;

    LandscapeStage(const data::GameFiles&, const data::Assets&, const Start&);

    // One frame of A's main loop (or of a picture sequence, which reads no
    // input: the opening reveal, a catch; or of the map); `gadget` is a gadget clicked this frame
    // (compass N E S W = ids 0-3 turn the view, $204278; the emblem, id 4,
    // opens and closes the map; any gadget ends the wait a click started,
    // $203FD0), or -1. The pointer comes with the button's read.
    void frame(const InputReader&, int gadget = -1);
    // Which of the bar's gadgets is under a pointer position, or -1.
    int gadget_at(int x, int y) const { return bar_.gadget_at(x, y); }
    platform::Image picture() const;

    const Walker& walker() const { return walker_; }
    int direction() const { return direction_; }
    std::uint16_t view_x() const { return view_x_; }
    std::uint16_t view_y() const { return view_y_; }
    std::uint32_t score() const { return score_; }
    int frozen() const { return frozen_; }
    // The walker stepped onto the marked block's ramp: the stage ends ($209F34).
    bool finished() const { return finished_; }
    // The next world's seed, once finished (shared +$4C).
    std::uint32_t next_seed() const { return next_seed_; }
    // The finds held ($2099BC/$2099BE) and the creature places left in view.
    const std::array<bool, 2>& found() const { return inputs_.found; }
    int creature_places() const { return cells_.creature_places; }
    bool ramp_closed() const { return ramp_closed_; }
    // The last life was lost to a catch ($204D6C).
    bool out_of_lives() const { return out_of_lives_; }
    // The stage is over and its last picture sequence has played.
    bool in_sequence() const { return interlude_.active(); }
    bool paused() const { return paused_; }
    bool over() const { return (finished_ || out_of_lives_) && !interlude_.active(); }
    int lives() const { return lives_; }
    int damage() const { return damage_; }
    // For replays of recordings that poke the damage ($20052E).
    void set_damage(int damage) { damage_ = damage; }
    // For replays: the stage clock from the original's own frames (its
    // seconds pass at its pace, slowdown included).
    void set_clock(int frames) { clock_frames_ = frames; }
    int clock() const { return clock_frames_; }
    const Creatures& creatures() const { return creatures_; }
    const ZapBolt& bolt() const { return bolt_; }
    // The sound, for the host's audio.
    void render_audio(std::span<float> stereo, int rate) { sound_.render(stereo, rate); }
    const LandscapeSound& sound() const { return sound_; }
    LandscapeSound& sound() { return sound_; }
    // The walker's sprite rows as shown (plane A << 16 | plane B), masked.
    std::vector<std::uint32_t> walker_sprite() const;

private:
    void rebuild_view();
    void repaint(bool full = true);
    void repaint_shown(Interlude::Repaint, int repeats = 1);
    void repaint_from(const Display& before, Interlude::Repaint, int repeats = 1);
    void pause();       // $204106
    void resume();      // $204196
    void after_pause();
    void follow_height(const InputReader&); // $209D8A
    void caught();          // $209D8A's branch after a catch
    void creatures_tick();  // $2003C0
    void toggle_map();      // $2042A0
    void sound_tick();
    Creatures::Scene creature_scene() const;
    Display current_display() const;
    void draw_walker(platform::Image&, const std::vector<screen::Rgb4>& colours) const;
    const data::Bitmap& walker_bitmap() const;
    void draw_creatures(platform::Image&, const std::vector<screen::Rgb4>& colours) const;
    void step(const InputReader&); // $20A45C
    void aim(int sx, int sz);      // $20A45C with fire held
    void take_cell();       // $20AB4E
    void cell_events();     // $209F34
    void take_find();       // $20A1B0
    void set_form(int form); // $206A2E
    void rebuild_view_keeping(); // $20A204 2
    void place_sprite();    // $209B54 / $2057D0
    void face(int direction);
    void anchor(int direction); // $20B18A
    // The map of visited cells ($205A36, $205B10, $205B90): a 64 x 64 window.
    void centre_map(std::uint16_t x, std::uint16_t y);
    bool visited(std::uint16_t x, std::uint16_t y) const;
    bool in_map(std::uint16_t x, std::uint16_t y) const;
    bool may_mark() const;  // $20596A

    const amiga::LoadedImage& a_;
    const data::Assets& assets_;
    world::World world_;
    int stage_{};
    int lives_{};
    std::uint32_t score_{};
    std::uint16_t view_x_{}, view_y_{};   // $209980/$209982
    std::uint16_t home_x_{}, home_y_{};   // $209984/$209986
    int direction_{};                     // $203070
    ViewInputs inputs_;
    ViewCells cells_;
    std::array<std::uint16_t, 32> colours_{};
    Walker walker_;
    world::ShiftRegister fidget_;         // h51 ($2004..; A's own shift register)
    ZapBolt bolt_;
    std::array<std::uint8_t, 512> map_{}; // 64 x 64 bits
    std::uint16_t map_centre_x_{}, map_centre_y_{}, map_x0_{}, map_x1_{}, map_y0_{}, map_y1_{};
    bool map_wrap_x_{}, map_wrap_y_{};
    int frozen_{};                        // $204DFC: frames the stage waits after a click
    bool button_was_down_{};              // $204DFA
    bool finished_{};                     // $204D72
    bool out_of_lives_{};                 // $204D6C
    bool caught_{};                       // $20B356
    int damage_{};                        // $20052E
    int flash_{};                         // $200518: colour 13's counter
    bool ramp_closed_{true};              // $2099B6: the way on closed (until a beacon marks its block)
    std::uint32_t next_seed_{};           // shared +$4C: the next world's seed, set on the way on
    int slow_creatures_{};                // $20052A: both finds held
    int slow_count_{};                    // $20051C
    int pyramid_steps_{};                 // $206B86
    int clock_frames_{}, clock_mark_{};   // the stage clock (shared +$6A as frames) and the last beacon
    std::uint32_t printed_score_{};       // the score as last printed ($20655A)
    bool aim_colours_{};                  // colours 19-31 lit by the aim's start
    screen::Canvas view_canvas_;
    LandscapeBar bar_;
    ViewPainter painter_;
    std::array<ViewPainter::DepthMask, 3> masks_;
    Creatures creatures_;
    Interlude interlude_;
    LandscapeSound sound_;
    bool stick_held_{}, catch_lost_{}, was_catching_{};
    MapView map_view_{a_};
    int map_frames_left_{};
    int queued_gadget_{-1};               // a gadget message not yet read
    int map_closing_{};                   // frames until the closed map's view shows
    bool paused_{};                       // $204D70
    int resume_wait_{};
    std::array<std::uint16_t, 32> map_palette_{}; // $2004AC as the map's view building leaves it
};

} // namespace mindwalker::landscape
