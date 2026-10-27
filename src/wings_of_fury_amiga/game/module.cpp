#include "wings_of_fury_amiga/game/module.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/data/preview.hpp"
#include "wings_of_fury_amiga/game/flight.hpp"
#include "wings_of_fury_amiga/game/player_view.hpp"
#include "wings_of_fury_amiga/game/scenery.hpp"
#include <algorithm>

namespace wings::game {
namespace {

constexpr int vblanks_per_tick = 4;   // $101754 queues a command every fourth vertical blank
constexpr int long_press = 10;        // $10C9E0: a press this many blanks or longer is held fire
constexpr int carrier_column_index = 0x21; // `rcar`: the map scan places the carrier by it ($103032)
constexpr int crash_pause_ticks = 40; // (not the original: the crash is not rebuilt yet)

class WingsOfFury final : public app::GameModule {
public:
    explicit WingsOfFury(const std::filesystem::path& content)
        : assets_(data::load_assets(content)), tables_(read_flight_tables(assets_.program)),
          map_(&assets_.maps.front()), day_(data::find_palette(assets_, "wingspalette")),
          sea_(data::find_palette(assets_, "ocean.palette")) {
        for (const auto& picture : assets_.pictures) {
            if (picture.name == "iff-dash") dashboard_ = &picture.picture;
        }
        set_up_carrier();
        new_plane();
    }

    double step_seconds() const override { return 1.0 / 50.0; } // PAL vertical blank
    double pixel_aspect() const override { return 0.5; }         // lowres view doubled to the hires dashboard

    void tick(const app::GameInput& input) override {
        sample_fire(input.fire);
        if (++vblank_ % vblanks_per_tick != 0) return;
        int command = 0;
        if (input.up) command |= command::up;
        if (input.down) command |= command::down;
        if (input.left) command |= command::left;
        if (input.right) command |= command::right;
        if (tapped_) command |= command::fire_tapped;
        if (held_) command |= command::fire_held;
        tapped_ = held_ = false;
        if (plane_.mode == 4 || plane_.mode == 6 || plane_.mode == 8) {
            // The crash ($10AFBA) and the next plane are not rebuilt yet:
            // after a pause a new plane stands on the deck.
            if (++crashed_ticks_ > crash_pause_ticks) new_plane();
            return;
        }
        flight_tick(plane_, world_, tables_, command);
        // The gear part follows its target a pixel a draw ($100580); the view
        // is drawn once per tick here.
        if (gear_offset_ != plane_.gear) gear_offset_ += gear_offset_ > plane_.gear ? -1 : 1;
    }

    platform::Image picture() const override {
        BackdropState backdrop;
        backdrop.camera = plane_.x;
        backdrop.eighth_scale = plane_.eighth_scale;
        backdrop.ground_line = plane_.eighth_scale ? 0x4B8 : ground_line_for(plane_.altitude);
        backdrop.vertical_scroll = plane_.sea;
        backdrop.wave_frame = wave_frame();
        backdrop.ships.push_back({world_.ships[4].first, world_.ships[4].last, world_.ships[4].bob});
        auto canvas = draw_sky_and_scenery(assets_, *map_, backdrop);

        CarrierDeckPicture deck;
        deck.camera = backdrop.camera;
        deck.ground_line = backdrop.eighth_scale ? 0x4B8 : backdrop.ground_line;
        deck.vertical_scroll = backdrop.vertical_scroll;
        deck.carrier_x = world_.carrier_x;
        deck.afloat = world_.ships[4].afloat;
        deck.deck = world_.ships[4].deck;
        deck.bob = world_.ships[4].bob;
        deck.spare_planes = spare_planes_;
        deck.elevator_state = world_.elevator;
        deck.lift = world_.lift;
        deck.eighth_scale = backdrop.eighth_scale;
        draw_carrier_deck(canvas, assets_, deck);

        PlayerPicture player;
        player.altitude = plane_.altitude;
        player.ground_line = deck.ground_line;
        player.vertical_scroll = backdrop.vertical_scroll;
        player.carrier_deck = deck.deck;
        player.carrier_bob = deck.bob;
        player.eighth_scale = backdrop.eighth_scale;
        if (backdrop.eighth_scale) {
            // -$4086 is the turn step, -$4082 the vertical speed ($101274).
            player.plane = eighth_scale_plane(assets_, plane_.turn, plane_.speed_y, plane_.facing < 0);
        } else {
            // Shapes from the pitch tables are mirrored in place for facing
            // left ($10ABDE); the turn shapes have one set per direction.
            const bool turning = plane_.turn != 0 && (plane_.mode == 0 || plane_.mode == 4);
            player.plane = hellcat(plane_.shape);
            player.plane_mirrored = !turning && plane_.facing < 0;
            player.gear = plane_.wheels.empty() ? nullptr : hellcat(plane_.wheels);
            player.gear_shown = player.gear != nullptr && (plane_.mode == 0 || plane_.mode == 7 || plane_.lift);
            player.gear_offset = gear_offset_;
            player.upset = plane_.turn != 0;
        }
        draw_player(canvas, player);
        draw_sea_and_sand(canvas, assets_, *map_, backdrop);
        return compose(canvas);
    }

private:
    const data::Shape* hellcat(const std::string& name) const {
        for (const auto& library : assets_.libraries) {
            if (library.name == "hellcat.shp") return library.library.find(name);
        }
        return nullptr;
    }

    // $10C9E0: fire pressed and released within 10 blanks is a tap; held
    // longer, every command while it is held carries the long press.
    void sample_fire(bool fire) {
        if (fire) {
            if (!fire_down_) press_start_ = vblank_;
            if (vblank_ - press_start_ >= long_press) held_ = true;
        } else if (fire_down_ && vblank_ - press_start_ < long_press) {
            tapped_ = true;
        }
        fire_down_ = fire;
    }

    int wave_frame() const {
        // -$5C70 steps 11 -> 0 every second draw ($10105E); one draw per tick here.
        return 11 - int((vblank_ / vblanks_per_tick / 2) % 12);
    }

    // The map scan ($102DE8 -> $103032): the US carrier spans the byte
    // offsets (rcar column x 2) - $A0 .. + $20, afloat, deck height $21.
    void set_up_carrier() {
        world_.map = map_;
        world_.carrier_x = map_->carrier_x();
        auto& carrier = world_.ships[4];
        for (std::size_t c = 0; c < map_->words.size(); ++c) {
            const auto column = map_->column(c);
            if (column.draw && column.index == carrier_column_index) {
                carrier.first = int(c) * 2 - 0xA0;
                carrier.last = carrier.first + 0xC0;
                break;
            }
        }
        carrier.afloat = true;
        carrier.ready = 4;
        carrier.deck = 0x21;
    }

    // A new plane on the deck ($10B7EC / $103684, without the elevator ride).
    void new_plane() {
        PlayerFlight p;
        p.x = world_.carrier_x;
        p.mode = 1;
        p.facing = -1;
        p.fuel = 0xC0;
        p.oil = 0x80;
        p.attitude = 4;
        p.gear = 5;
        p.fresh_on_deck = true;
        p.deck_low = world_.ships[4].first * 4 + 0x10;
        p.deck_high = world_.ships[4].last * 4 - 0x10;
        p.fuel_period = p.fuel_counter = 0x1C;
        p.sea_period = assets_.program.s16(0x11533C);
        p.sea_step = assets_.program.s16(0x1146BE);
        p.sea_counter = -1;
        p.sea = plane_.sea;
        p.turn_delay = 2;
        p.shape = tables_.deck_names[0];
        plane_ = p;
        // Stand it on the deck: one idle tick settles the height and shapes.
        flight_tick(plane_, world_, tables_, 0);
        gear_offset_ = plane_.gear;
        crashed_ticks_ = 0;
    }

    // The view (lowres, doubled) above the dashboard (hires).
    platform::Image compose(const IndexedCanvas& canvas) const {
        const int dash_height = dashboard_ ? dashboard_->height : 0;
        platform::Image image(640, canvas.height + dash_height);
        for (int y = 0; y < canvas.height; ++y) {
            const auto& palette = y < 151 ? day_ : sea_;
            for (int x = 0; x < canvas.width; ++x) {
                const auto index = canvas.at(x, y);
                const auto colour = data::to_color(index < palette.size() ? palette[index] : 0);
                image.put(2 * x, y, colour);
                image.put(2 * x + 1, y, colour);
            }
        }
        if (dashboard_) {
            for (int y = 0; y < dashboard_->height; ++y) {
                for (int x = 0; x < dashboard_->width && x < 640; ++x) {
                    const auto index = dashboard_->pixels[std::size_t(y * dashboard_->width + x)];
                    image.put(x, canvas.height + y, data::to_color(dashboard_->palette[index]));
                }
            }
        }
        return image;
    }

    data::Assets assets_;
    FlightTables tables_;
    const data::MissionMap* map_;
    data::Palette day_, sea_;
    const amiga::IlbmPicture* dashboard_{};
    FlightWorld world_;
    PlayerFlight plane_;
    int spare_planes_ = 3;
    long vblank_ = 0;
    long press_start_ = 0;
    bool fire_down_ = false, tapped_ = false, held_ = false;
    int gear_offset_ = 5;
    int crashed_ticks_ = 0;
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<WingsOfFury>(content);
}

} // namespace wings::game
