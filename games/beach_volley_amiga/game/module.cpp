#include "beach_volley_amiga/game/module.hpp"
#include "amiga/paula_channels.hpp"
#include "beach_volley_amiga/data/assets.hpp"
#include "beach_volley_amiga/front/intro.hpp"
#include "beach_volley_amiga/match/match.hpp"
#include "beach_volley_amiga/sound/soundtracker.hpp"
#include "state/archive.hpp"
#include "ui/vhs.hpp"
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace beach_volley::game {
namespace {

constexpr std::uint32_t save_magic = 0x42564C59, save_version = 2; // "BVLY"

class Module final : public app::GameModule {
public:
    explicit Module(const std::filesystem::path& content)
        : content_(content), files_(data::load_intro_files(content)), art_(front::decode_intro(files_)),
          song_(sound::Song::parse(files_.song)),
          player_(sound::read_player_tables(files_.code, front::intro_sample_table, front::intro_period_table,
                                            files_.instruments.size()),
                  front::intro_note_channels, front::intro_effect_channels),
          intro_(art_, std::uint32_t(files_.instruments.size())) {
        // One sample memory for Paula: the bank, then the intro's block
        // with the voices.
        memory_.reserve(files_.instruments.size() + files_.block.size());
        for (const auto b : files_.instruments) memory_.push_back(std::int8_t(b));
        for (const auto b : files_.block) memory_.push_back(std::int8_t(b));
        paula_.set_memory(memory_);
        song_.halve_volumes(); // $230E4
        player_.start(song_, paula_); // $24114, before the titles' first step
    }

    double step_seconds() const override { return 1.0 / 50; } // a PAL vertical blank
    double pixel_aspect() const override { return 1.0; }

    void tick(const app::GameInput& input) override {
        if (match_) {
            match_->tick(match_input(input));
            return;
        }
        player_.tick(paula_); // the vertical blank's interrupt ($23D94)
        intro_.tick(players(input), paula_);
        if (intro_.phase() == front::Intro::Phase::chosen && player_.playing()) {
            player_.stop(paula_);
            // volley.code: the first venue's match (the venue cards before
            // it, and side B under a second player, are not rebuilt yet).
            if (intro_.players() == 1) {
                match_data_ = std::make_unique<match::MatchData>(match::load_match_data(content_, 1));
                match_ = std::make_unique<match::Match>(*match_data_, 1);
            }
        }
    }

    platform::Image picture() const override { return match_ ? match_->picture() : intro_.picture(); }

    void audio(std::span<float> stereo, int rate) override {
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        paula_.render(stereo, rate);
    }

    std::string stopped() const override {
        if (match_ && match_->over())
            return "The match is over. What follows it (the score screen, the next venue) is not rebuilt yet.";
        if (intro_.phase() != front::Intro::Phase::chosen || intro_.players() == 1) return {};
        return "The match for two players is not rebuilt yet: Beach Volley plays its one-player match so far.";
    }

    std::vector<app::GameOption> options() const override { return {app::screen_filter_option(vhs_)}; }
    void set_option(std::size_t index, int value) override {
        if (index == 0) vhs_ = std::clamp(value, 0, ui::vhs_levels - 1);
    }
    int vhs_level() const override { return vhs_; }

    // A quick save holds the intro as it stands (both screens, the copper's
    // palettes, its counters), the music player and Paula's channels.
    std::vector<std::uint8_t> write_state() override {
        state::Writer ar;
        std::uint32_t magic = save_magic, version = save_version;
        ar(magic, version);
        persist(ar);
        return std::move(ar.bytes);
    }
    void read_state(std::span<const std::uint8_t> in) override {
        state::Reader ar(in);
        std::uint32_t magic{}, version{};
        ar(magic, version);
        if (magic != save_magic || version != save_version)
            throw std::runtime_error("Beach Volley: not a save of this version.");
        persist(ar);
        ar.finish();
    }

private:
    template <class A> void persist(A& ar) {
        bool playing = player_.playing();
        ar(playing, player_, paula_, intro_);
        if constexpr (A::reading) {
            player_.bind(playing ? &song_ : nullptr);
            paula_.set_memory(memory_);
        }
        bool in_match = match_ != nullptr;
        int players = in_match ? intro_.players() : 0;
        ar(in_match, players);
        if constexpr (A::reading) {
            match_.reset();
            if (in_match) {
                if (!match_data_) match_data_ = std::make_unique<match::MatchData>(match::load_match_data(content_, 1));
                match_ = std::make_unique<match::Match>(*match_data_, players);
            }
        }
        if (match_) ar(*match_);
    }

    // The match's joystick: port 1 (side A) from the arrows or the
    // gamepad and its button.
    static match::MatchInput match_input(const app::GameInput& input) {
        match::MatchInput m;
        m.port1 = {input.up, input.down, input.left, input.right, input.fire};
        m.port0 = {input.up2, input.down2, input.left2, input.right2, input.fire2};
        return m;
    }

    // The menu's choice: F1 (or 1, or the gamepad's first button) one
    // player, F2 (2, or the second button) two; on a touch screen or with
    // the mouse, the choice pressed once the menu shows. (The host has no
    // F2 key yet: plan.md.)
    int players(const app::GameInput& input) const {
        if (input.f1 || input.fire) return 1;
        if (input.c64_keys[59] || input.button2) return 2;
        if (intro_.phase() == front::Intro::Phase::menu && input.pointer && input.mouse_button) {
            for (int n = 0; n < 2; ++n) {
                const auto& a = front::Intro::choice_areas[std::size_t(n)];
                if (input.pointer_x >= a[0] && input.pointer_x < a[0] + a[2] && input.pointer_y >= a[1] - 4 &&
                    input.pointer_y < a[1] + a[3] + 4)
                    return n + 1;
            }
        }
        return 0;
    }

    std::filesystem::path content_;
    data::IntroFiles files_;
    front::IntroArt art_;
    sound::Song song_;
    sound::Player player_;
    std::vector<std::int8_t> memory_;
    amiga::PaulaChannels paula_;
    front::Intro intro_;
    std::unique_ptr<match::MatchData> match_data_;
    std::unique_ptr<match::Match> match_;
    int vhs_{};
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(content);
}

} // namespace beach_volley::game
