#include "wings_of_fury_amiga/game/player_view.hpp"
#include <algorithm>

namespace wings::game {
namespace {

constexpr int screen_centre = 0xA0;
constexpr int normal_bottom = 0xA1; // the plane's clip bottom ($100462)
constexpr int deck_clip_base = 0xA2;
constexpr int gear_lift = 5;
constexpr int propeller_first = 0x43; // fa*
constexpr int propeller_second = 0x57; // fb*
constexpr int facing_left_step = 10;

constexpr int elevator_entry = 0x25;
constexpr int parked_plane_entry = 0x26;
constexpr int most_parked = 9;
constexpr int first_parked_offset = 0xC8;
constexpr int parked_spacing = 0x1E;

int deck_clip_bottom(int deck, int bob, int vertical_scroll) {
    return deck_clip_base - deck + bob + vertical_scroll + 3;
}

// $105174 (see scenery.cpp): an entry at a world x, skipped when its screen x
// is outside -$80..$1C0; in the 1/8 view x and y are scaled down by 8.
void draw_at_world(IndexedCanvas& canvas, const data::Shape* shape, int world_x, int camera, bool eighth, int y) {
    const int origin = std::int16_t(camera + std::int16_t(0xFF60 << (eighth ? 3 : 0)));
    int x = std::int16_t(world_x - origin);
    if (eighth) {
        x >>= 3;
        y = std::int16_t(y) >> 3;
    }
    if (shape == nullptr || x < -0x80 || x > 0x1C0) return;
    canvas.draw(*shape, x - shape->offset_x, y - shape->offset_y);
}

} // namespace

void draw_carrier_deck(IndexedCanvas& canvas, const data::Assets& assets, const CarrierDeckPicture& deck) {
    const int ship_y = deck.ground_line + deck.vertical_scroll + deck.bob;
    if (deck.afloat) {
        const int shown = std::min(deck.spare_planes, most_parked) - 1;
        const auto* parked = deck.eighth_scale ? data::eighth_scale_shape(assets, parked_plane_entry)
                                               : data::scenery_shape(assets, parked_plane_entry);
        for (int i = 0; i < shown; ++i) {
            draw_at_world(canvas, parked, deck.carrier_x - first_parked_offset - i * parked_spacing, deck.camera,
                          deck.eighth_scale, ship_y);
        }
    }
    if (deck.elevator_state != 1 && !deck.eighth_scale) {
        const auto saved = canvas.clip;
        canvas.clip.bottom = deck.elevator_state != 0 ? deck_clip_bottom(deck.deck, deck.bob, deck.vertical_scroll)
                                                      : normal_bottom;
        draw_at_world(canvas, data::scenery_shape(assets, elevator_entry), deck.carrier_x, deck.camera, false,
                      deck.ground_line - deck.deck + deck.bob + deck.lift + deck.vertical_scroll);
        canvas.clip = saved;
    }
}

const data::Shape* propeller_shape(const data::Assets& assets, bool engine_running, bool upset, int counter, int attitude,
                                   bool facing_left) {
    if (!engine_running || upset) return nullptr;
    const int step = assets.propeller_steps[std::size_t(counter & 3)];
    if (step == 0) return nullptr;
    const int entry = attitude - gear_lift + (step == 1 ? propeller_first : propeller_second) +
                      (facing_left ? facing_left_step : 0);
    return data::hellcat_shape(assets, entry);
}

const data::Shape* eighth_scale_plane(const data::Assets& assets, int upset, int pitch, bool facing_left) {
    const bool right = !facing_left;
    int entry;
    if (upset == 0) {
        entry = std::max<int>(std::int16_t(-pitch) >> 2, -2) + (right ? 6 : 0) + 0x2A;
    } else if (upset >= 9 && upset < 0x12) {
        entry = upset + 0x2F + ((right != (upset > 0xD)) ? 9 : 0);
    } else {
        const int u = upset <= 8 ? upset : 0x1A - upset;
        entry = ((u - 1) >> 2) + 0x34 + (right ? 2 : 0);
    }
    return data::eighth_scale_shape(assets, entry);
}

void draw_player(IndexedCanvas& canvas, const PlayerPicture& player) {
    if (player.hidden || player.plane == nullptr) return;
    const int x = screen_centre;
    const int y = player.eighth_scale ? std::int16_t(player.ground_line - player.altitude) >> 3
                                      : player.ground_line - player.altitude;
    if (player.eighth_scale) {
        const auto saved = canvas.clip;
        canvas.clip.bottom = player.clip_at_deck
                                 ? deck_clip_bottom(player.carrier_deck, player.carrier_bob, player.vertical_scroll)
                                 : normal_bottom;
        canvas.draw(*player.plane, x - player.plane->offset_x, y - player.plane->offset_y);
        canvas.clip = saved;
        return;
    }
    const auto put = [&](const data::Shape* shape, bool mirrored, int at_y) {
        if (shape == nullptr) return;
        if (mirrored) {
            canvas.draw_mirrored(*shape, x - (shape->width() - 1 - shape->offset_x), at_y - shape->offset_y);
        } else {
            canvas.draw(*shape, x - shape->offset_x, at_y - shape->offset_y);
        }
    };

    // $10526E: the clip window for the plane.
    const auto saved = canvas.clip;
    canvas.clip.bottom = player.clip_at_deck
                             ? deck_clip_bottom(player.carrier_deck, player.carrier_bob, player.vertical_scroll)
                             : normal_bottom;
    if (player.deck_part_shown && !player.upset) put(player.deck_part, player.deck_part_mirrored, y);
    if (player.gear_shown && !player.upset) put(player.gear, player.gear_mirrored, y + player.gear_offset - gear_lift);
    put(player.plane, player.plane_mirrored, y);
    if (player.deck_part_shown && player.upset) put(player.deck_part, player.deck_part_mirrored, y);
    canvas.clip = saved; // $10524A: the full view again

    if (player.propeller != nullptr) {
        canvas.draw_xor(*player.propeller, x - player.propeller->offset_x, y - player.propeller->offset_y);
    }
}

} // namespace wings::game
