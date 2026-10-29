#pragma once
#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include "fa_18_interceptor_amiga/game/scene.hpp"
#include <array>
#include <optional>
#include <vector>

// The cockpit view's frame around the outside view: the head-up display's
// glass outline, the glare shield (`pix/frnt5`) and the instrument panel
// (`pix/inst5`) with the numbers the game writes into it (the right
// display's height, speed, thrust and zoom, the fuel, the radar range).
// Positions measured on the original's screen (`work/ref/ff_runway.png`);
// the readouts' rules from the code (game.md "Cockpit").
namespace interceptor::game {

struct CockpitReadings {
    int feet{}, knots{}, thrust_percent{}, zoom{1}, fuel_pounds{}, radar_miles{40};
    struct Contact {
        int x{}, y{}, colour{};
        bool wide{};
    };
    std::vector<Contact> contacts; // the radar's blips ($126230)
    int message{}, message_colour{1}; // the message line's record and colour
    // The head-up display ($127EB8, with $13AA91 set once the flight runs).
    bool hud = true;
    int heading80{};       // heading, 80 per degree ($68)
    // The load factor's number ($126960): (pitch rate + 1/8) / 8 - 10 (+ 10
    // inverted); shown as its size in tenths, with a minus sign from 0 up.
    int g_display{-10};
    bool inverted{};       // $13ABBA bit 1
    bool f16{};            // the F-16's head-up display (tapes)
    // The panel's latitude and longitude in tenths of a degree ($126DCE, $126E5C).
    int north_tenths{}, west_tenths{};
    int gear{};            // the gear byte's high bits ($7C: 0 down .. $60 up)
    // The weapons: the selected one (0 none, 1 the gun, 2 Sidewinders, 3
    // AMRAAMs; +$63's high nibble / $10), its count (rounds or missiles),
    // the missiles on their stations, and the gun's aiming circle (column,
    // row of its centre; none without the gun or zoomed).
    int weapon{}, weapon_count{};
    int sidewinders{}, amraams{};
    std::optional<std::array<int, 2>> pipper;
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
    void draw_outside(IndexedImage& image, int left, const CockpitReadings& readings, int top = 0xA8,
                      bool numbers = true) const;

private:
    void text(IndexedImage& image, int x, int y, const std::string& t, int colour) const;
    void hud(IndexedImage& image, int left, const CockpitReadings& r) const;
    void stores(IndexedImage& image, int left, const CockpitReadings& r) const;

    const data::Font& font_;
    const std::vector<std::string>& messages_;
    std::optional<std::array<std::uint8_t, 7 * 12>> strip_; // the compass strip ($132D9C)
    std::array<std::array<std::uint8_t, 7 * 4>, 4> gear_images_{}; // $132D0C, $132CF0, $132CD4, $132CB8
    // The original's character position tables (x within the field).
    struct Positions {
        std::vector<int> feet, feet_label, knots, knots_label, g_letter, g_fa18, g_f16, height_tape, speed_tape, heading;
    } positions_;
    const amiga::IlbmPicture* panel_{};
    const amiga::IlbmPicture* shield_{};
};

} // namespace interceptor::game
