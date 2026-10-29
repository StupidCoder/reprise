// Sonic Advance (Game Boy Advance) as a BlueStreak game module: loads the
// asset pack, runs the Game one frame at a time, plays its sound through the
// GBA sound hardware model and renders the picture.
//
// Limits: no quick saves, and the cartridge's save is not kept between runs.
// Paths not reconstructed yet (other characters, multiplayer, the continue
// and time-over screens, the act select) stop the game with a message.

#include "sonic_advance_gba/module.hpp"
#include "platform/host.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include <stdexcept>
#include <string>

namespace sonic_advance_gba {

namespace {

// One GBA video frame: 228 lines of 1232 clocks at 16.78 MHz (59.73 Hz).
constexpr u32 frame_cycles = 228 * 1232;
constexpr double frame_seconds = frame_cycles / 16777216.0;

class SonicAdvanceModule final : public shell::GameModule {
public:
    explicit SonicAdvanceModule(const std::filesystem::path& pack) : assets_(Assets::load(pack)) {
        start();
    }

    double step_seconds() const override { return frame_seconds; }

    std::vector<float> tick(shell::GameInput in, const shell::Enhancements&) override {
        if (stopped_.empty()) {
            ++ticks_;
            u16 b = 0;
            b |= in.up ? button::up : 0;
            b |= in.down ? button::down : 0;
            b |= in.left ? button::left : 0;
            b |= in.right ? button::right : 0;
            b |= in.jump ? button::a : 0;
            b |= in.action ? button::b : 0;
            b |= in.start ? button::start : 0;
            try {
                game_->frame(b);
                Sound& snd = game_->sound();
                snd.take_requests();
                // The frame's mix goes to the direct sound FIFOs (A right,
                // B left), the tone generators run along.
                const auto& e = snd.engine();
                // (The timer gives exactly samples_per_frame per frame.)
                const double rate =
                    double(gba::Apu::clock_rate) * e.samples_per_frame() / frame_cycles;
                snd.hardware().queue_direct(e.mixed(0), e.mixed(1), rate);
                return snd.hardware().render(frame_cycles);
            } catch (const std::exception& e) {
                // The reconstruction is incomplete: the game stops at the
                // first part not reconstructed yet, keeping its last picture.
                stopped_ = e.what();
            }
        }
        return silence();
    }

    platform::Image render(const shell::Enhancements&, int, int) const override {
        return game_ ? game_->display().render()
                     : platform::Image(gba::screen_width, gba::screen_height);
    }

    void set_view(bool, int, int) override {}

    // Start always goes to the game, which has its own pause screen; the
    // shell's pause menu is on Escape / Guide.
    bool gameplay() const override { return false; }

    std::string status() const override {
        return stopped_.empty() ? "Sonic Advance / Game Boy Advance" : "Stopped: " + stopped_;
    }

    std::string stopped() const override { return stopped_; }

    void restart() override { start(); }

    std::vector<std::uint8_t> save_state() override {
        throw std::runtime_error("Quick saves are not available for Sonic Advance yet");
    }

    std::uint64_t ticks() const override { return ticks_; }

private:
    void start() {
        stopped_.clear();
        try {
            // The cartridge's save is not kept yet: every start is the first
            // after the language was chosen (English).
            game_ = std::make_unique<Game>(assets_, SaveData{});
        } catch (const std::exception& e) {
            stopped_ = e.what();
        }
    }

    static std::vector<float> silence() {
        return std::vector<float>(
            2 * std::size_t(frame_seconds * platform::Host::audio_sample_rate), 0.0f);
    }

    Assets assets_;
    std::unique_ptr<Game> game_;
    std::uint64_t ticks_{};
    std::string stopped_;
};

} // namespace

std::unique_ptr<shell::GameModule> make_module(const std::filesystem::path& pack,
                                               std::span<const std::uint8_t>) {
    return std::make_unique<SonicAdvanceModule>(pack);
}

} // namespace sonic_advance_gba
