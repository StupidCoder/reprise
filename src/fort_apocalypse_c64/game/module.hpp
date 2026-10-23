#pragma once
#include "app/game_module.hpp"
#include "fort_apocalypse_c64/data/assets.hpp"
#include "fort_apocalypse_c64/data/tables.hpp"
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
    // A PAL C64 pixel is slightly narrower than tall.
    double pixel_aspect() const override { return 0.9365; }
    void audio(std::span<float> stereo, int sample_rate) override;

    bool save() override;
    bool load() override;
    bool has_save() const override { return saved_ != nullptr; }

    std::vector<app::GameOption> options() const override;
    void set_option(std::size_t index, int value) override;

    // The current frame's display window, for comparisons with the original.
    const c64::Picture& display() const { return game_->picture(); }
    Game& game() { return *game_; }

private:
    std::unique_ptr<const data::Assets> assets_;
    std::unique_ptr<const data::GameTables> tables_;
    std::unique_ptr<Game> game_, saved_;
    Options options_;
    std::vector<float> mono_;
};

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace fort_apocalypse::game
