#pragma once
#include "amiga/paula_voices.hpp"
#include "mindwalker_amiga/data/assets.hpp"
#include "mindwalker_amiga/data/game_files.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include "mindwalker_amiga/screen/gadget_bar.hpp"
#include "mindwalker_amiga/world/random.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

// The tunnel (program B; main loop $300040): an octagonal tunnel of 17
// rings, drawn once into a 480 x 300 picture and seen through the screen,
// which pans as the joystick steers and as the tunnel wanders on its own;
// four colours rotate to carry it forwards, faster as its pace counts down
// from 7. Translucent rectangles (display plane 4) come up out of the depth,
// growing, drifting outwards; one grown across the middle of the screen is
// caught (the arena follows, with points for the pace left). When the pace
// has run out the tunnel collapses and the landscape follows. The two hands
// stay in front.
namespace mindwalker::tunnel {

struct TunnelInput {
    bool up{}, down{}, left{}, right{}, fire{};
    bool button{}; // the left mouse button
};

// Where B reads its inputs, in order: the left button each frame
// ($30040C), then the stick and the fire button ($3026C4) on moving frames.
enum class TunnelRead { button, stick, fire };
using TunnelReader = std::function<TunnelInput(TunnelRead)>;

class TunnelStage {
public:
    struct Start {
        std::uint32_t seed{}; // shared +$4C (set by the landscape's way on)
        int stage{};          // +$5C
        int lives{3};         // +$6E
        std::uint32_t score{}; // +$66
    };
    enum class Outcome { playing, caught, ran_out, quit };

    TunnelStage(const data::GameFiles&, const data::Assets&, const Start&);

    // One frame of B's loop (or of its closing collapse); `gadget` a gadget
    // clicked this frame (the P box, id $10, pauses).
    void frame(const TunnelReader&, int gadget = -1);
    platform::Image picture() const;
    void render_audio(std::span<float> stereo, int rate) { voices_.render(stereo, rate); }

    Outcome outcome() const { return outcome_; }
    bool over() const { return outcome_ != Outcome::playing && collapse_ < 0; }
    std::uint32_t score() const { return score_; }
    int gadget_at(int x, int y) const;

    // For replays.
    int pan_x() const { return pan_x_; }
    int pan_y() const { return pan_y_; }
    int pace() const { return pace_; }
    int ticks() const { return ticks_; }
    int laps() const { return laps_; }
    const world::Mixer& wander() const { return wander_; }
    struct Rect {
        int size{}, x{}, y{}, shown_x{}, shown_y{};
    };
    const std::array<Rect, 4>& rects() const { return rects_; }
    const std::array<world::Mixer, 12>& rect_random() const { return rect_random_; }
    int frozen() const { return frozen_; }

private:
    void draw_tunnel();                        // h8 / $300EDE
    void ring(int radius, int first_pen, bool pens, int pen); // $300EDE
    void move(const TunnelReader&);            // $301086
    void steer();                              // $301186
    void rectangle(int slot);                  // $302842
    void rotate_colours(bool load);            // $301100
    void sound();                              // $303380
    void collapse_step();                      // $300CFE, one ring
    struct Box {
        int x0, x1, y0, y1;
    };
    Box box(int x, int y, int size) const;     // $302C84

    const amiga::LoadedImage& b_;
    const data::Assets& assets_;
    int stage_{}, lives_{};
    std::uint32_t score_{};
    std::vector<std::uint8_t> tunnel_ = std::vector<std::uint8_t>(480 * 300); // pens 0-3
    std::vector<std::uint8_t> rect_layer_ = std::vector<std::uint8_t>(320 * 200); // display plane 4
    std::array<std::uint16_t, 32> colours_{};  // h3 as it changes
    std::array<std::uint16_t, 32> shown_colours_{}; // as last loaded
    screen::GadgetBar bar_;
    world::ShiftRegister seed_;                // $301350
    world::Mixer wander_;                      // $301370
    std::array<world::Mixer, 12> rect_random_; // h22
    std::array<Rect, 4> rects_{};
    int pan_x_{0x50}, pan_y_{0x32};            // $301368 / $30136C
    int pace_{7}, ticks_{}, laps_{};           // $301360, h10, $301364
    int frame_count_{};                        // $302E24
    int growth_{};                             // $302E28: 22 - stage / 3
    int frozen_{};                             // $300C42: frames still after a click
    bool button_was_down_{};
    bool stick_left_{}, stick_right_{}, stick_up_{}, stick_down_{};
    bool paused_{};
    int resume_wait_{};
    Outcome outcome_{Outcome::playing};
    int collapse_{-1}, collapse_frame_{};      // the closing collapse: ring, frame in its step
    amiga::PaulaVoices voices_;
    std::array<int, 4> last_volume_{};         // $3034C4
};

} // namespace mindwalker::tunnel
