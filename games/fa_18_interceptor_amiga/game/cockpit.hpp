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
    // The head-up display ($127EB8, with $13AA91 set once the flight runs).
    bool hud = true;
    int heading80{};       // heading, 80 per degree ($68)
    int g_tenths{10};      // load factor in tenths ($126960), negative: pushed over
    // The panel's latitude and longitude in tenths of a degree ($126DCE, $126E5C).
    int north_tenths{}, west_tenths{};
};

CockpitReadings read_cockpit(const Aircraft& aircraft);

class Cockpit {
public:
    explicit Cockpit(const data::Assets& assets);

    // Draws the cockpit over a 320-column frame placed at column `left`.
    void draw(IndexedImage& image, int left, const CockpitReadings& readings) const;

    static constexpr int view_bottom = 144; // the outside view's last row above the panel

    // The outside view's numbers on the black below it ($126AB2, $126B48,
    // $126C16): "HDG 357   0 KTS   195 FT".
    void draw_outside(IndexedImage& image, int left, const CockpitReadings& readings) const;

private:
    void text(IndexedImage& image, int x, int y, const std::string& t, int colour) const;
    void hud(IndexedImage& image, int left, const CockpitReadings& r) const;

    const data::Font& font_;
    const std::vector<std::string>& messages_;
    const amiga::IlbmPicture* panel_{};
    const amiga::IlbmPicture* shield_{};
};

} // namespace interceptor::game
