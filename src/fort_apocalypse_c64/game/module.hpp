#pragma once
#include "app/game_module.hpp"
#include "c64/text_frame.hpp"
#include "fort_apocalypse_c64/data/assets.hpp"
#include "fort_apocalypse_c64/game/front_end.hpp"
#include <filesystem>
#include <memory>

namespace fort_apocalypse::game {

// Fort Apocalypse as a Joyport game module. It starts where the original
// starts after loading, at its title screen. So far the front end (title
// and options) is rebuilt; starting a game stops the module with a message.
class Module final : public app::GameModule {
public:
    explicit Module(data::Assets assets);

    // One PAL frame: 312 lines of 63 cycles at 985,248 Hz.
    double step_seconds() const override { return 312.0 * 63.0 / 985248.0; }
    void tick(const app::GameInput&) override;
    platform::Image picture() const override;
    // A PAL C64 pixel is slightly narrower than tall.
    double pixel_aspect() const override { return 0.9365; }
    std::string stopped() const override { return stopped_; }

    // The current frame's display window, for comparisons with the original.
    c64::Picture display() const;

private:
    // Owned here so the front end's reference to it stays valid.
    std::unique_ptr<const data::Assets> assets_;
    FrontEnd front_end_;
    std::string stopped_;
};

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace fort_apocalypse::game
