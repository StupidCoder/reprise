// Sonic Triple Trouble (Game Gear) as a BlueStreak game module: loads the
// asset pack, runs the Game one frame at a time, plays its sound through the
// PSG and renders the picture.

#include "triple_trouble_gg/module.hpp"
#include "gamegear/psg.hpp"
#include "gamegear/wide_view.hpp"
#include "platform/host.hpp"
#include "triple_trouble_gg/audio/voice.hpp"
#include "triple_trouble_gg/catalog.hpp"
#include "triple_trouble_gg/enhancements/widescreen.hpp"
#include "triple_trouble_gg/game/game.hpp"
#include "triple_trouble_gg/game/save.hpp"
#include "triple_trouble_gg/render/wide_view.hpp"
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
static_assert(platform::Host::audio_sample_rate == int(gg::Psg::sample_rate),
              "The host must play PSG output at its native sample rate");

namespace triple_trouble_gg {

namespace {

// One Game Gear video frame: 262 scanlines of 228 CPU clocks (59.92 Hz).
constexpr unsigned frame_clocks = 262 * 228;

class TripleTroubleModule final : public shell::GameModule {
public:
    TripleTroubleModule(const std::filesystem::path& pack, std::span<const std::uint8_t> state,
                        std::optional<Game::LevelStart> level = {})
        : assets_(load_assets(pack)), voice_(assets_), level_(level) {
        game_ = make_game();
        if (!state.empty()) {
            load(state);
        }
    }

    double step_seconds() const override { return frame_clocks / 3579545.0; }

    std::vector<float> tick(shell::GameInput in, const shell::Enhancements&) override {
        if (!stopped_.empty()) {
            return silence();
        }
        ++ticks_;
        std::uint8_t buttons = 0;
        buttons |= in.up ? button::up : 0;
        buttons |= in.down ? button::down : 0;
        buttons |= in.left ? button::left : 0;
        buttons |= in.right ? button::right : 0;
        buttons |= (in.jump || in.action) ? button::one : 0;
        buttons |= (in.start && !gameplay()) ? button::start : 0;
        // Widescreen rules apply to played acts only; attract demos replay
        // original controller bytes and keep the LCD rules.
        const bool wide = game_->wide_play();
        set_game_view(*game_, wide ? view_[0] : 160, wide ? view_[1] : 144);
        try {
            game_->frame(buttons);
        } catch (const std::exception& e) {
            // The reconstruction is incomplete: the game stops at the first
            // part not reconstructed yet, keeping its last picture.
            stopped_ = e.what();
            return silence();
        }
        // Enhancements only observe the game; they never feed back into it.
        barrier_.tick(arena_view(*game_, expanded_));
        for (const auto& w : game_->take_sound_writes()) {
            if (w.port == 0x06) {
                psg_.stereo(w.value);
            } else {
                psg_.write(w.value);
            }
        }
        if (game_->voice_requested) {
            voice_.start();
        }
        return voice_.playing() ? voice_.render(psg_, frame_clocks) : psg_.render(frame_clocks);
    }

    platform::Image render(const shell::Enhancements& enhancements, int canvas_width,
                           int canvas_height) const override {
        if (!enhancements.widescreen) {
            return game_->picture();
        }
        const auto size = gg::expanded_view_size(canvas_width, canvas_height);
        return render_frame(*game_, size[0], size[1], &barrier_);
    }

    void set_view(bool widescreen, int canvas_width, int canvas_height) override {
        expanded_ = widescreen;
        view_ =
            widescreen ? gg::expanded_view_size(canvas_width, canvas_height) : std::array{160, 144};
    }

    bool gameplay() const override { return stopped_.empty() && game_->in_play(); }

    std::string status() const override {
        return stopped_.empty() ? "Triple Trouble / Game Gear" : "Stopped: " + stopped_;
    }

    std::string stopped() const override { return stopped_; }

    void restart() override {
        stopped_.clear();
        game_ = make_game();
        barrier_.reset();
        psg_ = gg::Psg{};
        voice_ = LogoVoice(assets_);
    }

    std::vector<std::uint8_t> save_state() override {
        if (!stopped_.empty() || !game_->can_save()) {
            throw std::runtime_error("Quick saves are possible during an act");
        }
        save::Writer w;
        game_->save(w);
        barrier_.for_each_state([&](const auto& part) { save::write_part(w, part); });
        psg_.serialize(w);
        w.value(ticks_);
        return std::move(w.out);
    }

    void load(std::span<const std::uint8_t> state) {
        save::Reader r{state};
        game_->load(r);
        barrier_.for_each_state([&](auto& part) { save::read_part(r, part); });
        psg_.serialize(r);
        r.value(ticks_);
    }

    std::uint64_t ticks() const override { return ticks_; }

    // A frame of silence (interleaved stereo).
    static std::vector<float> silence() {
        return std::vector<float>(
            2 * std::size_t(double(frame_clocks) * gg::Psg::sample_rate / gg::Psg::clock_rate),
            0.0f);
    }

private:
    std::unique_ptr<Game> make_game() const {
        return level_ ? std::make_unique<Game>(assets_, *level_) : std::make_unique<Game>(assets_);
    }

    Assets assets_;
    std::unique_ptr<Game> game_;
    gg::Psg psg_;
    LogoVoice voice_;
    std::uint64_t ticks_{};
    std::string stopped_;
    std::optional<Game::LevelStart> level_;
    // Widescreen enhancement: the presented view and the arena force field.
    bool expanded_{};
    std::array<int, 2> view_{160, 144};
    ArenaBarrier barrier_;
};

} // namespace

std::unique_ptr<shell::GameModule> make_module(const std::filesystem::path& p,
                                               std::span<const std::uint8_t> state) {
    return std::make_unique<TripleTroubleModule>(p, state);
}

std::unique_ptr<shell::GameModule> make_level_module(const std::filesystem::path& p,
                                                     unsigned level) {
    using Kind = Game::LevelStart::Kind;
    if (level >= special_stage_start && level < special_stage_start + special_stage_count) {
        return std::make_unique<TripleTroubleModule>(
            p, std::span<const std::uint8_t>{},
            Game::LevelStart{0, 0, 0x01, true, Kind::special_stage,
                             u8(level - special_stage_start)});
    }
    if (level == ending_start || level == good_ending_start) {
        return std::make_unique<TripleTroubleModule>(
            p, std::span<const std::uint8_t>{},
            Game::LevelStart{0, 0, 0x01, true, Kind::ending,
                             u8(level == good_ending_start ? 6 : 1)});
    }
    if (level >= levels.size()) {
        throw std::runtime_error("This level is not a start point");
    }
    const auto& l = levels[level];
    return std::make_unique<TripleTroubleModule>(
        p, std::span<const std::uint8_t>{}, Game::LevelStart{u8(l.zone), u8(l.act), 0x01, true});
}

} // namespace triple_trouble_gg
