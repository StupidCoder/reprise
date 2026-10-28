#include "wings_of_fury_amiga/game/dashboard.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace wings::game {
namespace {

constexpr std::uint32_t a4 = 0x11AF52; // `wings`' small-data base ($100000 layout)

// Entries of the game's dash.shp pointer table (names: -$6E36).
constexpr int weapon_icons = 0;       // + weapon: rockets, bombs, torpedo
constexpr int warning_off = 3, warning_on = 4;
constexpr int sight = 5;              // $1041B4
constexpr int digit_strip = 6;        // the rolling digits
constexpr int score_digits = 7;
constexpr int deck_back = 0x38;       // + frame: the carrier deck seen from the cockpit
constexpr int deck_seen_left = 0x49, deck_seen_right = 0x41;
constexpr int bow_right = 0x51, bow_left = 0x59;
constexpr int needles = 0x5C;         // + position / 4
constexpr int gauge_face = 0x73;
constexpr int kill_bar = 0x74;

// The forward view's window ($10525C) and its fills ($104564).
constexpr int view_left = 0x102, view_right = 0x17C, view_top = 7, view_bottom = 0x20;
constexpr int view_centre = 0x13F;
constexpr int torpedo_run = 4;                    // an enemy plane's mode (+2)
constexpr int alarm_left = 8, alarm_right = 9;    // ltar, rtar
constexpr int sky = 2, sea = 1, island_line = 5;
constexpr int ship_surface = 1, island_surface = 2;

int word(int v) {
    return std::int16_t(v);
}

class Dash {
public:
    Dash(IndexedCanvas& canvas, const data::Assets& assets, const DashboardTables& t, const DashboardInput& in)
        : canvas(canvas), assets(assets), t(t), in(in) {
        for (const auto& library : assets.libraries) {
            if (library.name == (in.night ? "nightdash.shp" : "dash.shp")) parts = &library.library;
        }
    }

    const data::Shape* part(int entry) const {
        if (parts == nullptr || entry < 0 || std::size_t(entry) >= t.part_names.size()) return nullptr;
        return parts->find(t.part_names[std::size_t(entry)]);
    }
    // -$7CD4: masked, at the position minus the handle.
    void put(int entry, int x, int y, bool handle = true) { put(part(entry), x, y, handle); }
    void put(const data::Shape* s, int x, int y, bool handle = true) {
        if (s == nullptr) return;
        canvas.draw(*s, handle ? x - s->offset_x : x, handle ? y - s->offset_y : y);
    }
    // -$7CE0: the whole rectangle.
    void put_opaque(int entry, int x, int y, bool handle) {
        if (const auto* s = part(entry)) canvas.draw_opaque(*s, handle ? x - s->offset_x : x, handle ? y - s->offset_y : y);
    }
    void clip_rows(int top, int bottom) {
        canvas.clip.top = top;
        canvas.clip.bottom = bottom;
    }

    // ---- the forward view ($10417E) ----

    // The map as the game holds it: the file's size in bytes (the column
    // words after an 8-byte header, and 8 bytes past them read as 0).
    int map_bytes() const { return int(in.map->words.size()) * 2 + 8; }
    int column_word(int byte_offset) const {
        const auto c = std::size_t(byte_offset / 2);
        return c < in.map->words.size() ? in.map->words[c] : 0;
    }
    // $104A52: the ship covering a byte offset of the map; none found leaves
    // the last one tried (the carrier) for the caller that does not check.
    const DashboardInput::Ship* ship_at(int offset, bool& found) const {
        found = false;
        for (const auto& s : in.ships) {
            if (offset >= s.first && offset <= s.last) {
                found = true;
                return &s;
            }
        }
        return in.ships.empty() ? nullptr : &in.ships.back();
    }

    // $1045A6: the frame for scenery `index` in band `band` (-1: none); the
    // table it is in is the dash library, or for a ship with an overlay the
    // world scenery table.
    int frame_for(int index, int surface, int band, int offset, bool& from_world) {
        from_world = false;
        const auto scenery = [&](int base) { return base + t.band_frame[std::size_t(band)]; };
        const auto ship = [&](int base) { return base + t.ship_frame[std::size_t(band)]; };
        if (index >= 6 && index <= 8) return scenery(0xE); // trees
        if (index == 4 || index == 5) return scenery(index == 4 ? 0x1B : 0x22);
        if (index == 3) return scenery(0x14);
        if (surface == ship_surface) {
            int base = in.facing < 0 ? 0xA : 0;
            if (index == 0x22 || index == 0xF6) return ship(base + 0x4D); // towers
            bool found = false;
            const auto* s = ship_at(offset, found);
            if (!found || s == nullptr) return -1;
            if (s->overlay != 0) {
                overlay_seen = true;
                from_world = true;
                return ship(base + s->overlay);
            }
            overlay_seen = false;
            return ship(base + 0x3C);
        }
        if (index >= 0xF && index <= 0x1E) return scenery(index == 0xF ? 0x29 : 0x30);
        return -1;
    }

    void forward_view() {
        canvas.clip = {0x100, view_top, 0x190, view_bottom};
        const int horizon = (std::int16_t(in.altitude) >> 4) + 0x18; // $103802
        canvas.fill(view_left, view_top, view_right, view_bottom, sky);
        canvas.fill(view_left, horizon, view_right, view_bottom, sea);
        if (in.eighth_scale || in.map == nullptr) return;
        ahead(horizon);
        // $1041B4: the sight, lower the lower the plane flies.
        const int y = in.altitude >= 0x51 ? 0xD : (std::uint16_t(0x51 - in.altitude) >> 2) + 0xD;
        put(sight, view_centre, y);
    }

    // $104206: eleven bands ahead, the farthest first (one row lower each).
    void ahead(int horizon) {
        const int bytes = map_bytes(), last = bytes - 2;
        int excluded = 4; // a surface whose columns are not shown
        if (in.camera >= 0 && in.camera < bytes * 4) {
            const int offset = (in.camera >> 3) * 2;
            if ((column_word(offset) & 3) == ship_surface) {
                bool found = false;
                const auto* s = ship_at(offset, found);
                if (s != nullptr && (s->state == 0 || s->state == 0x1770)) excluded = ship_surface;
            }
        }
        int row = horizon;
        const int step = in.facing * 2;
        for (int i = 0; i < 11; ++i) {
            const int band = 10 - i;
            const int far = t.band_distance[std::size_t(i)], near = t.band_distance[std::size_t(i) + 1];
            int offset = word(word((in.facing < 0 ? -far : far) + (std::int16_t(in.camera) >> 3)) * 2);
            // $1042A0: the enemy planes in the band, centred, raised by
            // their height; facing right the pictures swap sides.
            {
                const int span = word((far - near) * 2);
                int lo = word(in.facing < 0 ? offset + span : offset - span), hi = offset;
                if (hi < lo) std::swap(lo, hi);
                for (const auto& e : in.enemies) {
                    if (e.column < lo || e.column > hi) continue;
                    int picture = e.picture;
                    if (in.facing >= 0) {
                        picture += 0x1C;
                        if (std::uint16_t(picture) >= 0x38) picture -= 0x38;
                    }
                    picture = (picture & ~0xFF) | ((picture + t.enemy_band[std::size_t(band)]) & 0xFF); // (add.b)
                    if (picture < 0 || std::size_t(picture) >= t.enemy_names.size() || parts == nullptr) continue;
                    put(parts->find(t.enemy_names[std::size_t(picture)]), view_centre,
                        row - ((std::int16_t(e.altitude) >> 2) + 4));
                }
            }
            int trees = -1, other = -1;
            bool island = false;
            for (int n = 0; n <= far - near; ++n, offset = word(offset + step)) {
                if (offset < 0 || offset > last) continue;
                const int w = column_word(offset), surface = w & 3;
                int index = (w & 0x7FC) >> 2;
                last_surface = surface;
                if (surface == excluded) index = 0;
                if (index != 0) {
                    if (index >= 6 && index <= 8) trees = index;
                    else if (other != 0x22 && other != 0xF6) other = index;
                }
                if (surface == island_surface) island = true;
            }
            if (island) canvas.fill(view_left, row, view_right, row, island_line);
            bool world = false;
            if (trees >= 0) {
                const int frame = frame_for(trees & 0x1FF, last_surface, band, offset, world);
                if (frame >= 0) put(frame, view_centre, row);
            }
            if (other >= 0) {
                const int frame = frame_for(other & 0x1FF, last_surface, band, offset, world);
                if (frame >= 0) put(world ? data::scenery_shape(assets, frame) : part(frame), view_centre, row);
            }
            ++row;
        }
        if (!overlay_seen) deck_view(horizon);
    }

    // $104430: on a ship's deck the deck ahead as seen from the cockpit.
    void deck_view(int horizon) {
        if (in.camera < 0 || in.camera >= map_bytes() * 4) return;
        const int base = (in.camera >> 3) * 2;
        if ((column_word(base) & 3) != ship_surface) return;
        const int step = in.facing < 0 ? 2 : -2;
        const int top_entry = in.facing < 0 ? deck_seen_left : deck_seen_right;
        int d = 0;
        while ((column_word(base + d) >> 2 & 0x1FF) != 0 && base + d >= 0 && base + d < map_bytes()) d += step;
        int columns = std::abs(d >> 1);
        columns = std::min<int>(columns, int(t.deck_band.size()) - 1);
        const int band = t.deck_band[std::size_t(columns)];
        const int y = horizon + t.deck_y[std::size_t(band)] + 7;
        const int saved = canvas.clip.top;
        canvas.clip.top = horizon + t.deck_clip[std::size_t(band)];
        put(deck_back + t.deck_frame[std::size_t(band)], view_centre, y + 1);
        put(top_entry, view_centre, y);
        canvas.clip.top = saved;
        put(in.facing >= 0 ? bow_right : bow_left, view_centre, y);
    }

    // ---- the gauges and counters ($10EE16) ----

    void gauge(int x_light, int x_needle, bool lit, int needle) {
        put(gauge_face, x_needle, 0x12);
        put(lit ? warning_on : warning_off, x_light, 0xA, false);
        put(needles + needle / 4, x_needle, 0x12);
    }

    // $10F2B0: a digit of the strip, opaque, shifted to show `digit`.
    void digit(int digit, int x, int y) { put_opaque(score_digits, x, y + t.digit_y[std::size_t(digit)], true); }

    void instruments(const DashboardGauges& g) {
        canvas.clip = {0, 0, 0x280, 0x25}; // $10F2DC
        // $10F21A: the alarm: the first torpedo bomber on its run, an arrow
        // to its side (`ltar` / `rtar`).
        for (const auto& e : in.enemies) {
            if ((e.mode & 7) != torpedo_run) continue;
            put(std::int16_t(in.camera - e.x) >= 0 ? alarm_left : alarm_right, 0x140, 0xA);
            break;
        }
        gauge(0xCA, 0xD0, g.oil_lit, g.oil_needle);
        gauge(0x1A8, 0x1AF, g.fuel_lit, g.fuel_needle);
        put_opaque(weapon_icons + in.weapon, 0x10, 0xB, false);
        clip_rows(0x13, 0x1C);
        put(digit_strip, 0x2A, 0x1C + g.load_tens);
        put(digit_strip, 0x42, 0x1C + g.load_units);
        clip_rows(0x13, 0x1C);
        put(digit_strip, 0x7D, 0x13 + g.spares_roll);
        // The score: "%07ld" (format at -$4936).
        clip_rows(0xB, 0x12);
        char text[16];
        std::snprintf(text, sizeof text, "%07ld", in.score);
        int x = 0x200;
        for (const char* c = text; *c; ++c, x += 0xE) digit(*c - '0', x, 0xB);
        // Enemy planes shot down: two digits and up to two rows of seven bars.
        const int kills = std::min(in.kills, 99);
        clip_rows(0x14, 0x1C);
        digit(kills / 10, 0x1FC, 0x15);
        digit(kills % 10, 0x20A, 0x15);
        clip_rows(0x13, 0x1F);
        int left = kills;
        for (int y : {0x13, 0x19}) {
            const int bars = std::min(left, 7);
            for (int b = 0; b < bars; ++b) put(kill_bar, 0x216 + b * 0xD, y, false);
            left -= 7;
        }
        // (The arrow toward an attacking enemy plane, $10F21A: not rebuilt yet.)
    }

    IndexedCanvas& canvas;
    const data::Assets& assets;
    const DashboardTables& t;
    const DashboardInput& in;
    const data::ShapeLibrary* parts{};
    bool overlay_seen = false; // -$3902
    int last_surface = 0;      // -$5971
};

} // namespace

DashboardTables read_dashboard_tables(const amiga::LoadedImage& wings) {
    DashboardTables t;
    for (std::uint32_t at = a4 - 0x6E36; wings.u32(at) != 0; at += 4) {
        std::string name;
        for (std::uint32_t c = 0; c < 4; ++c) name += char(wings.u8(at + c));
        t.part_names.push_back(name);
    }
    for (std::size_t i = 0; i < t.band_distance.size(); ++i) t.band_distance[i] = wings.s16(a4 - 0x6948 - std::uint32_t(2 * i));
    for (std::size_t i = 0; i < t.band_frame.size(); ++i) t.band_frame[i] = wings.u8(a4 - 0x6946 + std::uint32_t(i));
    for (std::size_t i = 0; i < t.ship_frame.size(); ++i) t.ship_frame[i] = wings.u8(a4 - 0x693A + std::uint32_t(i));
    // -$692F runs up to the next table (-$68C9).
    for (std::uint32_t at = a4 - 0x692F; at < a4 - 0x68C9; ++at) t.deck_band.push_back(wings.u8(at));
    for (std::size_t i = 0; i < 16; ++i) {
        t.deck_y[i] = wings.u8(a4 - 0x68B9 + std::uint32_t(i));
        t.deck_clip[i] = wings.u8(a4 - 0x68C9 + std::uint32_t(i));
        t.deck_frame[i] = wings.u8(a4 - 0x68A9 + std::uint32_t(i));
    }
    for (std::size_t i = 0; i < t.digit_y.size(); ++i) t.digit_y[i] = std::int8_t(wings.u8(a4 - 0x6402 + std::uint32_t(i)));
    for (int size = 0; size < 3; ++size) {
        for (std::uint32_t side = 0; side < 2; ++side) {
            for (std::uint32_t i = 0; i < 28; ++i) {
                std::string name;
                for (std::uint32_t c = 0; c < 4; ++c) name += char(wings.u8((side ? 0x1160DC : 0x11606C) + i * 4 + c));
                name[2] = char('1' + size);
                t.enemy_names.push_back(name);
            }
        }
    }
    for (std::size_t i = 0; i < t.enemy_band.size(); ++i) t.enemy_band[i] = wings.u8(a4 - 0x696C + std::uint32_t(i));
    return t;
}

void DashboardGauges::set_load(int load) {
    load &= 0xFF;
    load_tens = 0x50 - (load / 10) * 8;
    load_units = 0x50 - (load % 10) * 8;
    load_settled = -1;
}

void DashboardGauges::set_spares(int spares) {
    spares = std::clamp(spares, 0, 9);
    spares_roll = 0x59 - spares * 8;
    spares_settled = -1;
}

void DashboardGauges::step(const DashboardInput& in, int random) {
    const bool live = in.mode == 0 || in.mode == 1 || in.mode == 7; // -$30A2
    const auto ease = [](int& needle, int target) {
        if (needle != target) needle += needle < target ? 4 : -4;
    };
    // A warning light: lit while its counter runs (9 frames), dark for two.
    const auto blink = [](int& counter, int period) {
        if (--counter > 0) return true;
        if (counter != 0) counter = period;
        return false;
    };
    int oil = std::max(in.oil - 0x60, 0) * 2 & ~3;
    if (in.mode <= 1 && in.engine) oil += 0x18;
    ease(oil_needle, oil);
    oil_lit = live && in.oil < 0x74 && blink(oil_blink, 0xA);

    int fuel = std::min(std::max(in.fuel, 0) >> 1, 0x58) & ~3;
    if (fuel == 0 && live) fuel = (std::uint16_t(random << 3 | random >> 13) & 4);
    ease(fuel_needle, fuel);
    fuel_lit = live && in.fuel <= 0x40 && blink(fuel_blink, 8);

    // The load: the units strip turns, the tens strip turns with it while
    // the units pass 0 (positions 1..8), as an odometer.
    const int load = in.load & 0xFF;
    if (load == 0xFF || load != load_settled) {
        int units_at = 0x50 - (load % 10) * 8, tens_at = 0x50 - (load / 10) * 8;
        if (load == 0xFF) units_at = tens_at = 0x64;
        if (load_tens == tens_at && load_units == units_at) {
            load_settled = load;
        } else {
            // ($10F07C: unsigned compares: a position below 0, as $10EDBC
            // leaves the tens for a load of $FF, wraps to 1 at once.)
            if (std::uint16_t(++load_units) > 0x50) load_units = 1;
            if (load_tens != tens_at && std::uint16_t(load_units) <= 8 && std::uint16_t(++load_tens) > 0x50) load_tens = 1;
        }
    }
    const int spares = std::clamp(in.spare_planes, 0, 9);
    if (spares != spares_settled) {
        const int at = 0x59 - spares * 8;
        if (spares_roll == at) spares_settled = spares;
        else spares_roll += spares_roll < at ? 1 : -1;
    }
}

IndexedCanvas draw_dashboard(const data::Assets& assets, const DashboardTables& tables, const DashboardGauges& gauges,
                             const DashboardInput& in) {
    const amiga::IlbmPicture* picture = nullptr;
    for (const auto& p : assets.pictures) {
        if (p.name == (in.night ? "nightdash" : "iff-dash")) picture = &p.picture;
    }
    IndexedCanvas canvas(640, 37);
    if (picture != nullptr) {
        for (int y = 0; y < std::min(picture->height, 37); ++y) {
            for (int x = 0; x < std::min(picture->width, 640); ++x) {
                canvas.pixels[std::size_t(y * 640 + x)] = picture->pixels[std::size_t(y * picture->width + x)];
            }
        }
    }
    Dash dash(canvas, assets, tables, in);
    dash.forward_view();
    dash.instruments(gauges);
    return canvas;
}

} // namespace wings::game
