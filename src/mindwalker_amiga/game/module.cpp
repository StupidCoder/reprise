#include "mindwalker_amiga/game/module.hpp"
#include "mindwalker_amiga/data/game_files.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include "mindwalker_amiga/title/title_stage.hpp"
#include "mindwalker_amiga/world/world.hpp"
#include "mindwalker_amiga/landscape/landscape_stage.hpp"
#include "mindwalker_amiga/tunnel/tunnel_stage.hpp"
#include "mindwalker_amiga/arena/arena_stage.hpp"
#include "mindwalker_amiga/data/assets.hpp"
#include <optional>
#include "platform/files.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>

namespace mindwalker::game {
namespace {

// Intuition's mouse pointer, from Workbench's preferences on the game disk
// (struct Preferences: PointerMatrix at +28, two control words then 16 rows of
// two plane words; XOffset/YOffset at +100/+101; colours 17-19 at +102).
struct MousePointer {
    std::array<std::array<std::uint8_t, 16>, 16> pixels{};
    int x_offset{}, y_offset{};
    std::array<screen::Rgb4, 3> colours{};

    explicit MousePointer(const std::vector<std::uint8_t>& prefs) {
        const auto word = [&](std::size_t at) { return std::uint16_t(prefs.at(at) << 8 | prefs.at(at + 1)); };
        for (int y = 0; y < 16; ++y) {
            const auto a = word(28 + 4 + std::size_t(y) * 4), b = word(28 + 6 + std::size_t(y) * 4);
            for (int x = 0; x < 16; ++x) {
                pixels[std::size_t(y)][std::size_t(x)] = std::uint8_t((a >> (15 - x) & 1) | (b >> (15 - x) & 1) << 1);
            }
        }
        x_offset = std::int8_t(prefs.at(100));
        y_offset = std::int8_t(prefs.at(101));
        for (std::size_t c = 0; c < 3; ++c) colours[c] = word(102 + c * 2) & 0xFFF;
    }

    void draw(platform::Image& image, int x0, int y0) const {
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const auto v = pixels[std::size_t(y)][std::size_t(x)];
                if (v) image.put(x0 + x_offset + x, y0 + y_offset + y, screen::to_color(colours[v - 1u]));
            }
        }
    }
};

// What T mixes into its seed from CurrentTime ($600142): seconds since 1
// January 1978 shifted left by 3, XOR the microseconds.
std::uint32_t clock_value() {
    using namespace std::chrono;
    constexpr seconds amiga_epoch{252460800}; // 1978-01-01 in Unix time
    const auto now = duration_cast<microseconds>(system_clock::now().time_since_epoch()) - amiga_epoch;
    const auto secs = std::uint32_t(duration_cast<seconds>(now).count());
    const auto micros = std::uint32_t(now.count() % 1000000);
    return secs << 3 ^ micros;
}

class MindWalker final : public app::GameModule {
    enum class Phase { title, loading, landscape, tunnel, arena, stopped };

public:
    explicit MindWalker(const std::filesystem::path& content)
        : files_(content), picture_(platform::read_file(content / "MindB")),
          pointer_(platform::read_file(content / "system-configuration")), assets_(data::load_assets(content)),
          title_(files_, picture_, clock_value()) {}

    // The stages wait for each vertical blank (PAL, 50 Hz, like the reference
    // machine; hypothesis until the release's video standard is settled).
    double step_seconds() const override { return 1.0 / 50.0; }
    double pixel_aspect() const override { return 1.0; }

    void tick(const app::GameInput& input) override {
        if (input.pointer) {
            mouse_x_ = input.pointer_x;
            mouse_y_ = input.pointer_y;
        }
        const bool press = input.mouse_button && !button_was_down_;
        button_was_down_ = input.mouse_button;
        switch (phase_) {
        case Phase::title:
            title_.frame({mouse_x_, mouse_y_, input.mouse_button});
            if (title_.phase() == title::TitleStage::Phase::loading) {
                const auto& game = *title_.new_game();
                state_ = {game.seed, game.stage, game.lives, game.score, std::nullopt, 0};
                load_next(Phase::landscape);
            }
            break;
        case Phase::loading:
            // The loading picture on S's screen, held a fixed 2 seconds
            // (owner's decision) while the loader brings the next program.
            if (++loading_frames_ >= 100) open(next_);
            break;
        case Phase::landscape: {
            int gadget = press ? landscape_->gadget_at(mouse_x_, mouse_y_) : -1;
            if (gadget == -2) { // Intuition's close gadget: CLOSEWINDOW quits the stage
                stop("Left the landscape (score %u).", landscape_->score());
                return;
            }
            landscape::LandscapeInput in{input.up, input.down, input.left, input.right, input.fire,
                                         input.mouse_button, mouse_x_, mouse_y_};
            landscape_->frame([&](landscape::InputRead) { return in; }, gadget);
            if (!landscape_->over()) break;
            state_.score = landscape_->score();
            state_.lives = landscape_->lives();
            if (landscape_->out_of_lives()) {
                stop("The creatures took every life (score %u). What follows is not rebuilt yet.", state_.score);
            } else {
                // The way on ($20B056 keeps the landscape, mode 9): the tunnel.
                state_.resume = landscape_->resume_state();
                state_.tunnel_seed = landscape_->next_seed();
                load_next(Phase::tunnel);
            }
            break;
        }
        case Phase::tunnel: {
            int gadget = press ? tunnel_->gadget_at(mouse_x_, mouse_y_) : -1;
            tunnel::TunnelInput in{input.up, input.down, input.left, input.right, input.fire, input.mouse_button};
            tunnel_->frame([&](tunnel::TunnelRead) { return in; }, gadget);
            if (!tunnel_->over()) break;
            state_.score = tunnel_->score();
            switch (tunnel_->outcome()) {
            case tunnel::TunnelStage::Outcome::ran_out: load_next(Phase::landscape); break; // mode 1: the landscape again
            case tunnel::TunnelStage::Outcome::caught: load_next(Phase::arena); break; // mode 2
            default: stop("Left the tunnel (score %u).", state_.score); break;
            }
            break;
        }
        case Phase::arena: {
            const int gadget = press ? arena_->gadget_at(mouse_x_, mouse_y_) : -1;
            arena::ArenaInput in{input.up, input.down, input.left, input.right, input.fire, input.mouse_button};
            arena_->frame([&](arena::ArenaRead) { return in; }, gadget);
            if (!arena_->over()) break;
            state_.score = arena_->score();
            state_.lives = arena_->lives();
            switch (arena_->outcome()) {
            case arena::ArenaStage::Outcome::won:
                // C's exit: progress +7 (at most 42), +$60 set (the way on
                // closed again), mode 3: the psyche screen.
                state_.stage = std::min(state_.stage + 7, 42);
                if (state_.resume) state_.resume->ramp_closed = true;
                stop("The arena is won (score %u). The psyche screen is not rebuilt yet.", state_.score);
                break;
            case arena::ArenaStage::Outcome::left: load_next(Phase::landscape); break; // mode 1
            case arena::ArenaStage::Outcome::died:
                if (state_.lives < 1) stop("The arena took the last life (score %u). What follows is not rebuilt yet.", state_.score);
                else load_next(Phase::landscape); // mode 1, a life less
                break;
            default: stop("Left the arena (score %u).", state_.score); break;
            }
            break;
        }
        case Phase::stopped: break;
        }
    }

    void load_next(Phase next) {
        next_ = next;
        loading_frames_ = 0;
        phase_ = Phase::loading;
        landscape_.reset();
        tunnel_.reset();
        arena_.reset();
    }

    void open(Phase next) {
        phase_ = next;
        if (next == Phase::landscape) {
            landscape_.emplace(files_, assets_,
                               landscape::LandscapeStage::Start{state_.seed, state_.stage, state_.lives, state_.score,
                                                                state_.resume});
        } else if (next == Phase::tunnel) {
            tunnel_.emplace(files_, assets_,
                            tunnel::TunnelStage::Start{state_.tunnel_seed, state_.stage, state_.lives, state_.score});
        } else {
            arena_.emplace(files_, assets_,
                           arena::ArenaStage::Start{state_.tunnel_seed, state_.stage, state_.lives, state_.score,
                                                    state_.resume ? state_.resume->form : 0, std::nullopt});
        }
    }

    void stop(const char* format, std::uint32_t score) {
        char text[200];
        std::snprintf(text, sizeof text, format, score);
        message_ = text;
        phase_ = Phase::stopped;
    }

    void audio(std::span<float> stereo, int sample_rate) override {
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        if (phase_ == Phase::landscape) landscape_->render_audio(stereo, sample_rate);
        if (phase_ == Phase::tunnel) tunnel_->render_audio(stereo, sample_rate);
        if (phase_ == Phase::arena) arena_->render_audio(stereo, sample_rate);
    }

    platform::Image picture() const override {
        platform::Image image = phase_ == Phase::landscape ? landscape_->picture()
                                : phase_ == Phase::tunnel  ? tunnel_->picture()
                                : phase_ == Phase::arena   ? arena_->picture()
                                : phase_ == Phase::loading ? title_.loading_canvas().picture({0x000, 0x0F0})
                                                           : title_.picture();
        pointer_.draw(image, mouse_x_, mouse_y_);
        return image;
    }

    std::string stopped() const override {
        if (title_.phase() == title::TitleStage::Phase::quit) return "Mind Walker: QUIT.";
        return phase_ == Phase::stopped ? message_ : std::string();
    }

private:
    data::GameFiles files_;
    std::vector<std::uint8_t> picture_;
    MousePointer pointer_;
    data::Assets assets_;
    title::TitleStage title_;
    int mouse_x_{160}, mouse_y_{100};
    int loading_frames_{};
    bool button_was_down_{};
    Phase phase_{Phase::title}, next_{Phase::landscape};
    // What crosses between the programs (the loader's shared block).
    struct GameState {
        std::uint32_t seed{};          // the world's
        int stage{}, lives{3};
        std::uint32_t score{};
        std::optional<landscape::LandscapeStage::Resume> resume; // +$18-$60
        std::uint32_t tunnel_seed{};   // +$4C
    } state_;
    std::string message_;
    std::optional<landscape::LandscapeStage> landscape_;
    std::optional<tunnel::TunnelStage> tunnel_;
    std::optional<arena::ArenaStage> arena_;
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<MindWalker>(content);
}

} // namespace mindwalker::game
