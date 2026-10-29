#pragma once
#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include "fa_18_interceptor_amiga/game/scene.hpp"

// The cockpit view's frame around the outside view: the head-up display's
// glass outline, the glare shield (`pix/frnt5`) and the instrument panel
// (`pix/inst5`) with the numbers the game writes into it (the right
// display's height, speed, thrust and zoom, the fuel, the radar range).
// Positions measured on the original's screen (`work/ref/ff_runway.png`);
// the readouts' rules from the code (game.md "Cockpit").
namespace interceptor::game {

struct CockpitReadings {
    int feet{}, knots{}, thrust_percent{}, zoom{1}, fuel_pounds{}, radar_miles{40};
    int message{}, message_colour{1}; // the message line's record and colour
};

CockpitReadings read_cockpit(const Aircraft& aircraft);

class Cockpit {
public:
    explicit Cockpit(const data::Assets& assets);

    // Draws the cockpit over a 320-column frame placed at column `left`.
    void draw(IndexedImage& image, int left, const CockpitReadings& readings) const;

    static constexpr int view_bottom = 144; // the outside view's last row above the panel

private:
    const data::Font& font_;
    const std::vector<std::string>& messages_;
    const amiga::IlbmPicture* panel_{};
    const amiga::IlbmPicture* shield_{};
};

} // namespace interceptor::game
