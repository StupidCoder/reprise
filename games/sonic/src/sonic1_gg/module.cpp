// Sonic 1 (Game Gear) as a BlueStreak game module: loads the asset pack, runs the
// Session one frame at a time, mixes its sound, renders frames and provides
// quick saves. The optional enhancements live here too, beside the game.

#include "sonic1_gg/module.hpp"
#include "platform/host.hpp"
#include "sonic1_gg/audio/voice.hpp"
#include "sonic1_gg/catalog.hpp"
#include "sonic1_gg/enhancements/basin_water.hpp"
#include "sonic1_gg/enhancements/ring_effects.hpp"
#include "sonic1_gg/enhancements/water_effects.hpp"
#include "sonic1_gg/enhancements/widescreen.hpp"
#include "sonic1_gg/frontend/session.hpp"
#include "sonic1_gg/render/scene.hpp"
#include "sonic1_gg/save/snapshot.hpp"
#include <algorithm>
#include <map>
static_assert(platform::Host::audio_sample_rate == int(gg::Psg::sample_rate),
              "The host must play PSG output at its native sample rate");

namespace sonic1_gg {

namespace {

// One Game Gear video frame: 262 scanlines of 228 CPU clocks at 3.579545 MHz
// (about 59.92 Hz). The game updates once per frame.
constexpr unsigned frame_clocks = 262 * 228;

class SonicModule final : public shell::GameModule {
    // Declaration order matters: the session and sound driver keep references
    // to the asset members declared before them.
    SoundData sound_data_;
    FlowAssets flow_;
    Level first_, second_, third_, jungle_;
    std::vector<Level> special_;
    std::map<unsigned, Level> additional_;
    std::unique_ptr<Session> session_;
    SoundDriver sound_;
    gg::Psg psg_;
    LogoVoice voice_;
    std::uint64_t ticks_{};
    WaterEffects water_effects_;
    RingEffects ring_effects_;
    ArenaBarrier barrier_;
    ObjectFades fades_;
    bool expanded_{};
    std::array<int, 2> view_{160, 144};
    std::unique_ptr<BasinWater> basins_;
    unsigned basin_act_ = ~0u;

public:
    // `state`: a quick save to resume from. `level`: a level selector start
    // point (act number), or -1 to start at the SEGA logo.
    explicit SonicModule(const std::filesystem::path& p, std::span<const std::uint8_t> state,
                         int level = -1)
        : sound_data_(load_sound(p)), flow_(load_flow(p)), first_(load_level(p)),
          second_(load_level(p / "act2")), third_(load_level(p / "act3")),
          jungle_(load_level(p / "jungle")), sound_(sound_data_), voice_(flow_.voice) {
        for (unsigned i = 1; i <= 8; ++i) {
            special_.push_back(load_level(p / ("special" + std::to_string(i))));
        }
        for (unsigned a = 3; a < 6; ++a) {
            additional_.emplace(a, load_level(p / ("bridge" + std::to_string(a - 2))));
        }
        for (unsigned a = 6; a < 9; ++a) {
            additional_.emplace(
                a, load_level(p / (a == 6 ? "jungle" : "jungle" + std::to_string(a - 5))));
        }
        for (unsigned a = 9; a < 12; ++a) {
            additional_.emplace(a, load_level(p / ("labyrinth" + std::to_string(a - 8))));
        }
        for (unsigned a : {12, 13, 14, 15, 16, 17, 18, 20, 21, 22, 23, 24, 25, 26}) {
            additional_.emplace(a, load_level(p / ("stage" + std::to_string(a))));
        }
        session_ = std::make_unique<Session>(flow_, first_, jungle_, &second_, &third_, &special_,
                                             &additional_);
        for (auto e : session_->sound_events) {
            sound_.event(e);
        }
        if (level >= 0) {
            sound_.stop();
            if (level >= int(ending_start)) {
                session_->start_ending(level == int(good_ending_start));
            } else if (level >= 28) {
                session_->start_special(unsigned(level), 0);
            } else {
                session_->start_level(unsigned(level));
            }
            for (auto e : session_->sound_events) {
                sound_.event(e);
            }
        }
        if (!state.empty()) {
            load_snapshot(state, *session_, sound_, psg_, voice_, ticks_);
        }
        if (session_->game) {
            basins_ = std::make_unique<BasinWater>(session_->game->level);
            basin_act_ = session_->game->level.act;
        }
    }

    double step_seconds() const override { return frame_clocks / 3579545.0; }

    // One frame: the sound driver runs from the video interrupt (it is paused
    // while the sampled voice plays, as on the cartridge), then the game
    // updates, then the cosmetic effects follow the new game state.
    std::vector<float> tick(shell::GameInput in, const shell::Enhancements& enhancements) override {
        ++ticks_;
        if (!voice_.playing()) {
            sound_.tick();
        }
        for (auto v : sound_.take_writes()) {
            psg_.write(v);
        }
        // Gameplay Start belongs to the shell's pause overlay. Frontend Start
        // still launches the original title/maps and interrupts attract demos.
        // Attract demos replay original controller bytes, so they keep the
        // LCD rules; widescreen rules apply to played acts only.
        if (session_->game) {
            session_->game->view_width = gameplay() ? view_[0] : 160;
            session_->game->view_height = gameplay() ? view_[1] : 144;
        }
        session_->tick({in.left, in.right, in.jump || in.action, in.down, in.up},
                       in.start && !gameplay());
        if (session_->gameplay() && session_->game &&
            (!basins_ || basin_act_ != session_->game->level.act)) {
            basins_ = std::make_unique<BasinWater>(session_->game->level);
            basin_act_ = session_->game->level.act;
        }
        // Enhancements only observe the game; they never feed back into it.
        ring_effects_.tick(session_->gameplay() ? session_->game.get() : nullptr,
                           enhancements.rings);
        barrier_.tick(session_->gameplay() ? session_->game.get() : nullptr, expanded_);
        fades_.tick(session_->gameplay() ? session_->game.get() : nullptr, expanded_);
        water_effects_.tick(session_->gameplay() ? session_->game.get() : nullptr,
                            enhancements.water, basins_.get());
        for (auto e : session_->sound_events) {
            sound_.event(e);
        }
        if (session_->voice_requested) {
            voice_.start();
        }
        for (auto v : sound_.take_writes()) {
            psg_.write(v);
        }
        return voice_.playing() ? voice_.render(psg_, frame_clocks) : psg_.render(frame_clocks);
    }

    platform::Image render(const shell::Enhancements& enhancements, int canvas_width,
                           int canvas_height) const override {
        auto& s = *session_;
        const bool expanded = enhancements.widescreen;
        if (s.ending) {
            return s.ending->screen.render();
        }
        if (s.gameplay() || s.fading_game) {
            return render_game_view(*s.game, expanded, enhancements.shield_transparency,
                                    enhancements.water, &water_effects_, canvas_width,
                                    canvas_height, enhancements.rings ? &ring_effects_ : nullptr,
                                    basins_.get(), expanded ? &barrier_ : nullptr,
                                    expanded ? &fades_ : nullptr);
        }
        if (s.results && s.front.scene == FrontEnd::Scene::Results) {
            return s.results->screen.render();
        }
        if (s.game_over) {
            return s.game_over->screen.render();
        }
        return s.front.screen.render();
    }

    void set_view(bool widescreen, int canvas_width, int canvas_height) override {
        expanded_ = widescreen;
        view_ = widescreen ? expanded_view_size(canvas_width, canvas_height) : std::array{160, 144};
    }

    bool gameplay() const override { return session_->gameplay() && !session_->front.demo; }

    std::string status() const override {
        auto& s = *session_;
        if (s.ending) {
            return "Ending and credits";
        }
        if (s.game_over) {
            return "Game over";
        }
        if (!s.gameplay()) {
            return "Sonic 1 / Game Gear";
        }
        unsigned a = s.game->level.act;
        static const char* names[] = {"Green Hill", "Bridge",      "Jungle",
                                      "Labyrinth",  "Scrap Brain", "Sky Base"};
        if (a < 18) {
            return std::string(names[a / 3]) + " / Act " + std::to_string(a % 3 + 1);
        }
        if (a >= 28) {
            return "Special stage " + std::to_string(a - 27);
        }
        return "Ending";
    }

    void restart() override {
        water_effects_.reset();
        ring_effects_.reset();
        barrier_.reset();
        fades_.reset();
        session_->restart_act();
        sound_.stop();
        for (auto e : session_->sound_events) {
            sound_.event(e);
        }
        voice_ = LogoVoice(flow_.voice);
        psg_ = gg::Psg{};
    }

    std::vector<std::uint8_t> save_state() override {
        return save_snapshot(*session_, sound_, psg_, voice_, ticks_);
    }

    std::uint64_t ticks() const override { return ticks_; }
};
} // namespace

std::unique_ptr<shell::GameModule> make_module(const std::filesystem::path& p,
                                               std::span<const std::uint8_t> state) {
    return std::make_unique<SonicModule>(p, state);
}

std::unique_ptr<shell::GameModule> make_level_module(const std::filesystem::path& p,
                                                     unsigned level) {
    if (std::none_of(levels.begin(), levels.end(), [&](auto entry) { return entry.id == level; })) {
        throw std::runtime_error("This level is not a standalone start point");
    }
    return std::make_unique<SonicModule>(p, std::span<const std::uint8_t>{}, int(level));
}
} // namespace sonic1_gg
