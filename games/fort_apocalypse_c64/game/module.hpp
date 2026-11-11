#pragma once
#include "app/game_module.hpp"
#include "fort_apocalypse_c64/data/assets.hpp"
#include "fort_apocalypse_c64/data/tables.hpp"
#include "fort_apocalypse_c64/enhanced/enhanced.hpp"
#include "fort_apocalypse_c64/enhanced/widescreen.hpp"
#include "fort_apocalypse_c64/game/game.hpp"
#include <filesystem>
#include <memory>

namespace fort_apocalypse::game {

// Fort Apocalypse as a Joyport game module, from the title screen on.
class Module final : public app::GameModule {
public:
    Module(data::Assets assets, data::GameTables tables);

    // One PAL frame: 312 lines of 63 cycles at 985,248 Hz.
    double step_seconds() const override { return 312.0 * 63.0 / 985248.0; }
    void tick(const app::GameInput&) override;
    platform::Image picture() const override;
    void set_world_surface_supported(bool supported) override { world_supported_ = supported; }
    std::optional<app::WorldSurface> world_surface() const override;
    int vhs_level() const override { return vhs_; }
    // A PAL C64 pixel is slightly narrower than tall.
    double pixel_aspect() const override { return 0.9365; }
    void set_canvas(int width, int height) override;
    void audio(std::span<float> stereo, int sample_rate) override;

    std::vector<std::uint8_t> write_state() override;
    void read_state(std::span<const std::uint8_t>) override;
    bool save() override;
    bool load() override;
    bool has_save() const override { return saved_ != nullptr; }

    std::vector<app::GameOption> options() const override;
    void set_option(std::size_t index, int value) override;

    // The current frame's display window, for comparisons with the original.
    const c64::Picture& display() const { return game_->picture(); }
    // What the player sees (the enhanced mode's picture when it is on).
    const c64::Picture& shown() const { return shown_; }
    Game& game() { return *game_; }

private:
    std::unique_ptr<const data::Assets> assets_;
    std::unique_ptr<const data::GameTables> tables_;
    std::unique_ptr<Game> game_, saved_;
    Options options_;
    bool particles_{}, smooth_{}, widescreen_{};
    bool world_supported_{}, world_enabled_{}; // presentation only; not save state
    // The particle effects run when chosen, and in carnage mode (its debris).
    bool particles() const { return particles_ || options_.carnage; }
    // With particle effects, a crash's last frame holds until the wreck has
    // come to rest (or fire or the space bar is pressed): the game waits.
    // From the crash's first frame a copy of the game is shown instead,
    // playing on (tanks, mines, the enemy, the men) with the crash kept open
    // and its camera following the wreck; it is dropped when the real game
    // goes on: the next life starts with "GET READY", where the real game's
    // world takes over.
    bool holding_{}, held_this_crash_{}, hold_button_{}, after_hold_{}, skip_{};
    int hold_frames_{};
    std::unique_ptr<Game> world_;
    // The VHS tape look's level (0 = off) and the frames it animates by.
    int vhs_{};
    long frames_{};
    // Carnage mode: a big kill holds the game a few frames (hit-stop) and
    // shakes the playfield.
    int hit_stop_{};
    double shake_{};
    enhanced::EnhancedView enhanced_;
    enhanced::WideSize wide_{};
    c64::Picture shown_{};
    template <class A> void persist_runtime(A &a) {
        bool crash = bool(world_);
        a(crash);
        if constexpr (A::reading) {
            world_.reset();
            if (crash)
                world_ = std::make_unique<Game>(*assets_, *tables_);
        }
        a(*game_);
        if (crash)
            a(*world_);
        a(holding_, held_this_crash_, hold_button_, after_hold_, skip_, hold_frames_, frames_, hit_stop_,
          shake_, enhanced_, shown_);
    }
    void draw();
    void crash_tick(const app::GameInput&, bool pressed);
    std::vector<float> mono_;
};

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace fort_apocalypse::game
