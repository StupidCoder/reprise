#include "bruce_lee_c64/enhanced/wide_view.hpp"
#include "bruce_lee_c64/game/screen.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace bruce_lee::enhanced {
namespace {

constexpr int standard = c64::Picture::standard_width, height = c64::Picture::height;
constexpr int playfield_top = 24;    // picture lines above are the status rows
constexpr int playfield_height = height - playfield_top;
constexpr int transition_frames = 20;
constexpr int west = 0, east = 1;

// Each C64 colour one step darker, staying in the palette (the dimmed
// rooms; black stays black).
constexpr std::uint8_t darker[16] = {0, 15, 9, 14, 6, 11, 0, 8, 9, 0, 2, 12, 11, 5, 6, 12};
constexpr int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// Bruce's y when he walks through a door whose top cell is on this screen
// row (e.g. row 15 and y $92 in rooms 0 and 1).
int door_y(int row) {
    return row * 8 + 26;
}

// $2E76: the exit an exit cell of `type` takes (one whose x is 0 passes on
// to the next type's).
const data::Exit& exit_of(const data::Assets& assets, int room, int type) {
    const auto& exits = assets.rooms[std::size_t(room)].exits;
    while (type < 5 && exits[std::size_t(type - 2)].x == 0) {
        ++type;
    }
    return exits[std::size_t(type - 2)];
}

// Whether an exit's place is at the left or right edge of its room.
bool arrives_left(const data::Exit& e) {
    return e.x < 0x30;
}
bool arrives_right(const data::Exit& e) {
    return e.x > 0x90;
}

double smoothstep(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

} // namespace

WideSize wide_size(int canvas_width, int canvas_height, double pixel_aspect) {
    // The original screen with its PAL border (32 and 35 pixels around).
    const double narrowest = (standard + 64) * pixel_aspect / (height + 70);
    constexpr double widest = 21.0 / 9.0;
    if (canvas_width <= 0 || canvas_height <= 0) {
        return {standard, 0};
    }
    const double aspect = std::clamp(double(canvas_width) / canvas_height, narrowest, widest);
    WideSize size;
    size.width = std::max(standard, int(std::lround(aspect * height / pixel_aspect)) & ~1);
    if (size.width == standard) {
        size.margin_y = std::max(0, int(std::lround((standard * pixel_aspect / aspect - height) / 2)));
    }
    return size;
}

// The room in a copy of the world: entered with nobody in it, its script
// run for a few passes (so the walls it opens and the floors it closes for
// rooms already cleared are drawn), then drawn without sprites.
c64::Picture WideView::snapshot(const game::World& world, int room) const {
    game::World copy(world);
    copy.before_probe = nullptr;
    copy.enter_room(room, world.assets.start_x[std::size_t(room)], world.assets.start_y[std::size_t(room)], false);
    const int entered = copy.room; // (room 11 may lead on to room 13)
    for (int pass = 0; pass < 8 && copy.room == entered; ++pass) {
        for (auto& a : copy.actors.slots) {
            a.flags = 0;
        }
        copy.pass();
    }
    for (auto& f : copy.fireballs) {
        f.active = false;
    }
    game::SpritePlaces places;
    places.hidden = {true, true, true};
    return game::world_frame(copy, &places).rasterise();
}

// The room shown on one side: behind a door on that edge of the room, or
// one whose door on the facing edge leads here; a room both ways first.
// Each of its doors is an anchor: where Bruce is in this room at that door,
// and how much lower the room must be drawn to meet it there.
WideView::Glimpse WideView::glimpse(const game::World& world, int side, int hint_y, const Glimpse* forced) const {
    struct Candidate {
        int room{}, dy{}, here_y{}; // here_y: where Bruce is in this room at the door
        bool forward{};
    };
    std::vector<Candidate> candidates;
    const auto& assets = world.assets;
    const int here = world.room;
    // This room's doors on that edge: in its map, and as drawn now (scripts
    // open some; the screen may not be drawn yet as the room is entered).
    const int edge = side == west ? 0 : game::Playfield::columns - 1;
    std::array<int, 6> first_row{-1, -1, -1, -1, -1, -1};
    auto door = [&](int type, int row) {
        if (type >= 2 && type <= 5 && (first_row[std::size_t(type)] < 0 || row < first_row[std::size_t(type)])) {
            first_row[std::size_t(type)] = row;
        }
    };
    for (int m = 0; m < data::Room::rows; ++m) {
        door(assets.rooms[std::size_t(here)].at(edge, m) & 0x7F, 3 + 2 * m);
    }
    for (int row = 3; row < game::Playfield::rows; ++row) {
        door(world.field.at(edge, row).code & 0x7F, row);
    }
    for (int type = 2; type <= 5; ++type) {
        const int row = first_row[std::size_t(type)];
        if (row < 0) {
            continue;
        }
        const auto& e = exit_of(assets, here, type);
        if (e.room != here && (side == east ? arrives_left(e) : arrives_right(e))) {
            candidates.push_back({e.room, door_y(row) - e.y, door_y(row), true});
        }
    }
    // Doors of other rooms, on the facing edge of their maps, that lead here.
    const int facing = side == west ? data::Room::columns - 1 : 0;
    for (int other = 0; other < int(assets.rooms.size()); ++other) {
        if (other == here) {
            continue;
        }
        const auto& map = assets.rooms[std::size_t(other)];
        for (int m = 0; m < data::Room::rows; ++m) {
            const int type = map.at(facing, m) & 0x7F;
            if (type < 2 || type > 5) {
                continue;
            }
            const auto& e = exit_of(assets, other, type);
            if (e.room == here && (side == east ? arrives_right(e) : arrives_left(e))) {
                candidates.push_back({other, e.y - door_y(3 + 2 * m), e.y, false});
            }
        }
    }
    if (candidates.empty() && !(forced && forced->room >= 0)) {
        return Glimpse{};
    }
    auto both_ways = [&](int room) {
        bool forward = false, back = false;
        for (const auto& c : candidates) {
            (c.forward ? forward : back) |= c.room == room;
        }
        return forward && back;
    };
    const Candidate* best = nullptr;
    for (const auto& c : candidates) {
        if (forced && forced->room >= 0 && c.room != forced->room) {
            continue;
        }
        if (!best) {
            best = &c;
            continue;
        }
        const bool c_both = both_ways(c.room), best_both = both_ways(best->room);
        if (c_both != best_both) {
            if (c_both) best = &c;
            continue;
        }
        if (c.forward != best->forward) {
            if (c.forward) best = &c;
            continue;
        }
        if (std::abs(c.here_y - hint_y) < std::abs(best->here_y - hint_y)) {
            best = &c;
        }
    }
    Glimpse g;
    g.room = forced && forced->room >= 0 ? forced->room : best->room;
    if (best) {
        // This room's own doors to it if it has any, one anchor per height.
        // Where the other room has a door back, the place its exit gives
        // here is exactly where Bruce stands at this door (the door row
        // only estimates it): the anchor is that place, and the room lies
        // as far below as it lies from the place this door's exit gives.
        for (const auto& c : candidates) {
            if (c.room != best->room || c.forward != best->forward) {
                continue;
            }
            int here_y = c.here_y, dy = c.dy;
            if (c.forward) {
                const Candidate* back = nullptr;
                for (const auto& r : candidates) {
                    if (!r.forward && r.room == c.room &&
                        (!back || std::abs(r.here_y - c.here_y) < std::abs(back->here_y - c.here_y))) {
                        back = &r;
                    }
                }
                if (back && std::abs(back->here_y - c.here_y) <= 16) {
                    here_y = back->here_y;
                    dy = c.dy + (back->here_y - c.here_y); // (c.dy is the door's y less the exit's place there)
                }
            }
            if (std::none_of(g.anchors.begin(), g.anchors.end(), [&](const auto& a) { return a.first == here_y; })) {
                g.anchors.push_back({here_y, dy});
            }
        }
        std::sort(g.anchors.begin(), g.anchors.end());
    }
    // Coming through a door it stays where the scroll left it (the anchor
    // there says the same, as far as the two rooms' doors agree).
    g.shown_dy = forced && forced->room >= 0 ? forced->dy : anchored_dy(g, hint_y);
    g.dy = int(std::lround(g.shown_dy));
    g.picture = snapshot(world, g.room);
    return g;
}

// The height for Bruce at `y`: an anchor's own at its door, in between two
// anchors moving along with him, so that it meets whichever door he takes.
double WideView::anchored_dy(const Glimpse& g, int y) {
    if (g.anchors.empty()) {
        return g.shown_dy;
    }
    if (y <= g.anchors.front().first) {
        return g.anchors.front().second;
    }
    for (std::size_t i = 1; i < g.anchors.size(); ++i) {
        const auto [y0, d0] = g.anchors[i - 1];
        const auto [y1, d1] = g.anchors[i];
        if (y <= y1) {
            return d0 + double(d1 - d0) * (y - y0) / std::max(1, y1 - y0);
        }
    }
    return g.anchors.back().second;
}

void WideView::place_glimpses(const game::World& world, int hint_y, const Glimpse* came_from, int came_side) {
    for (int side = west; side <= east; ++side) {
        sides_[std::size_t(side)] = glimpse(world, side, hint_y, side == came_side ? came_from : nullptr);
    }
}

// A room picture's playfield lines into the view, its left edge at
// `left` + p.x, dithered between its dimmed and its own colours.
void WideView::draw(const Placed& p, int left) {
    const int width = picture_.width;
    const int x0 = std::max(0, left + p.x), x1 = std::min(width, left + p.x + standard);
    if (x0 >= x1) {
        return;
    }
    const double threshold = p.lit * 16;
    for (int y = playfield_top; y < height; ++y) {
        const int sy = y - p.y;
        if (sy < playfield_top || sy >= height) {
            continue;
        }
        for (int x = x0; x < x1; ++x) {
            const std::uint8_t colour = p.picture->at(x - left - p.x, sy);
            const bool lit = threshold > bayer[y & 3][(x >> 1) & 3] + 0.5;
            picture_.pixels[std::size_t(y * width + x)] = lit ? colour : darker[colour & 15];
        }
    }
}

void WideView::update(const game::Game& game, const c64::Picture& centre, int width) {
    width = std::max(width, standard);
    picture_ = c64::Picture(width);
    picture_.border = centre.border;
    std::fill(picture_.pixels.begin(), picture_.pixels.end(), centre.border);
    const int left = (width - standard) / 2;
    // The status rows (and everything, outside play) as the game shows them.
    const int copy_rows = game.in_play() ? playfield_top : height;
    for (int y = 0; y < copy_rows; ++y) {
        std::copy_n(&centre.pixels[std::size_t(y * standard)], standard,
                    &picture_.pixels[std::size_t(y * width + left)]);
    }
    if (!game.in_play()) {
        known_ = false;
        transition_.active = false;
        return;
    }
    const auto& world = game.world;
    const auto& bruce = world.actors.slots[0];
    int remaining = 0;
    for (const auto& p : world.progress) {
        remaining += p.remaining;
    }
    if (!known_) {
        place_glimpses(world, bruce.y, nullptr, -1);
    } else if (world.room != room_) {
        // An exit: scroll over when Bruce crossed an edge (sideways, or
        // falling out at the bottom); other room changes cut.
        const int ox = bruce_x_, oy = bruce_y_, nx = bruce.x, ny = bruce.y;
        const bool to_east = ox > 0x98 && nx < 0x30, to_west = ox < 0x20 && nx > 0x90;
        const bool down = oy > 0xB8 && ny < 0x70, up = oy < 0x40 && ny > 0xA0;
        const int came_side = to_east ? west : to_west ? east : -1;
        Glimpse back;
        if (to_east || to_west || down || up) {
            transition_.active = true;
            transition_.frame = 0;
            // To where the room was shown beside, if it was (the game moves
            // Bruce to the exit's place, which need not be level with where
            // he left); else beside or below so that Bruce keeps his place
            // across the edge he crossed.
            const auto& ahead = sides_[std::size_t(to_east ? east : west)];
            if ((to_east || to_west) && ahead.room == world.room) {
                transition_.dx = to_east ? standard : -standard;
                transition_.dy = ahead.dy;
            } else if (to_east || to_west) {
                transition_.dx = to_east ? standard : -standard;
                transition_.dy = oy - ny;
            } else {
                transition_.dx = 2 * (ox - nx);
                transition_.dy = down ? playfield_height : -playfield_height;
            }
            // The old room without its actors (the enemies follow Bruce in).
            transition_.from = snapshot(world, room_);
            transition_.came_side = came_side;
            transition_.far = std::move(sides_[std::size_t(to_west ? east : west)]);
            if (down || up) {
                transition_.far = Glimpse{};
            }
            back = {room_, -transition_.dy, c64::Picture()};
        } else {
            transition_.active = false;
        }
        place_glimpses(world, bruce.y, came_side >= 0 ? &back : nullptr, came_side);
    } else if (remaining != remaining_) {
        // A room cleared: the scripts of the rooms beside may open walls.
        for (auto& g : sides_) {
            if (g.room >= 0) {
                g.picture = snapshot(world, g.room);
            }
        }
    }
    if (!transition_.active) {
        // The glimpses follow Bruce's height between their doors (eased:
        // he moves in steps, and jumps when he restarts).
        for (auto& g : sides_) {
            if (g.room >= 0 && !g.anchors.empty()) {
                g.shown_dy += (anchored_dy(g, bruce.y) - g.shown_dy) * 0.25;
                g.dy = int(std::lround(g.shown_dy));
            }
        }
    }
    known_ = true;
    room_ = world.room;
    remaining_ = remaining;
    bruce_x_ = bruce.x;
    bruce_y_ = bruce.y;
    last_centre_ = centre;

    const auto& t = transition_;
    const double e = t.active ? smoothstep(double(t.frame) / transition_frames) : 1.0;
    const int cx = int(std::lround(t.active ? t.dx * e : 0)), cy = int(std::lround(t.active ? t.dy * e : 0));
    const int bx = t.active ? t.dx : 0, by = t.active ? t.dy : 0; // the room in play, from the old one
    if (t.active) {
        if (t.far.room >= 0) {
            const int fx = t.dx > 0 ? -standard : standard;
            draw({&t.far.picture, fx - cx, t.far.dy - cy, 0}, left);
        }
        draw({&t.from, -cx, -cy, 1 - e}, left);
    }
    for (int side = west; side <= east; ++side) {
        const auto& g = sides_[std::size_t(side)];
        if (g.room < 0 || (t.active && side == t.came_side)) {
            continue; // (the old room itself is drawn above)
        }
        const int gx = (side == west ? -standard : standard) + bx;
        draw({&g.picture, gx - cx, g.dy + by - cy, 0}, left);
    }
    draw({&centre, bx - cx, by - cy, e}, left);
    if (t.active && !game.paused() && ++transition_.frame >= transition_frames) {
        transition_.active = false;
        transition_.far = Glimpse{};
    }
}

} // namespace bruce_lee::enhanced
