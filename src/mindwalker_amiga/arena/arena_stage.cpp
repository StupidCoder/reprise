#include "mindwalker_amiga/arena/arena_stage.hpp"
#include "amiga/intuition_imagery.hpp"
#include "mindwalker_amiga/screen/area_fill.hpp"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>

namespace mindwalker::arena {
namespace {

// C's data (research layout).
constexpr std::uint32_t palette = 0x40042C;       // h3: 32 colours
constexpr std::uint32_t colour_steps = 0x400494, first_colours = 0x4004A4, last_colours = 0x4004B4; // by seed & 7
constexpr std::uint32_t maze_keys = 0x401C50;     // 3 words
constexpr std::uint32_t room_creature_counts = 0x401C56; // 8 longs
// The generators' first values (XOR the world's seed) and what replaces a zero.
constexpr std::uint32_t fidget_seed = 0x4025FC, wind_seed_a = 0x402EBC, wind_seed_b = 0x402EC0,
                        pulse_seed = 0x402ED4, creature_seed = 0x4045EC, edge_seed = 0x4045F0,
                        spawn_seed = 0x4045F4;
constexpr std::uint32_t scroll_x = 0x401C76, scroll_y = 0x401C86; // a room change's scroll, by direction
constexpr std::uint32_t figure_rows = 0x40259C;   // the player's sprite rows by form (longs)
constexpr std::uint32_t figure_colour_a = 0x4025AC, figure_colour_b = 0x4025B4; // colours 29 and 30 by form
constexpr std::uint32_t aim_frames = 0x4025BC;    // the figure's frame while aiming, by direction (longs)
constexpr std::uint32_t first_direction = 0x402600; // $402600's first value (the opening scroll)
constexpr std::uint32_t sines = 0x402E4C;         // h15: 40 sin, 16 directions (+4: cosine)
constexpr std::uint32_t score_label = 0x402EE0, gauge_labels = 0x403060, score_text = 0x404A66,
                        huge_text = 0x404A82;     // IntuiTexts
constexpr std::uint32_t first_pattern = 0x403608; // the wind line's pattern
constexpr std::uint32_t marker_offsets = 0x4035F8; // the aim marker's four directions
constexpr std::uint32_t creature_order = 0x4045F8; // which sprites a room's creatures take
constexpr std::uint32_t hit_x = 0x404610, hit_y = 0x404620; // where the beam is looked for around a creature
constexpr std::uint32_t spawn_masks = 0x404630;   // a draw & mask == 0 brings a creature in, by difficulty
constexpr std::uint32_t creature_images = 0x404AE4; // h27: four frames, 17 rows of two planes (68 bytes)
constexpr std::uint32_t victory_periods = 0x4067E8; // h39
constexpr std::uint32_t new_window = 0x400E2E, screen_title = 0x400DEA;
// The Image data pointers C sets to S's icons (shared +$2FC, +$300, +$304).
constexpr std::uint32_t first_life_data = 0x400D0C, pause_box_data = 0x400CCE, knob_data = 0x400CBA;
constexpr std::uint32_t s_life = 0x700A2C, s_pause_box = 0x700A40, s_knob = 0x700A54;
constexpr std::uint32_t s_noise_frames = 0x702540; // +$1C6's frames: the noise channel's data
constexpr int pause_gadget = 0x10;
constexpr int top = 10;             // $401086: the window's BorderTop + 1 (dumps arena0-4: the room and the panel from row 10)
constexpr int title_text_pen = 0, title_bar_pen = 0x12; // C's NewScreen's DetailPen and BlockPen
constexpr int room_pixels_x = 256;
constexpr int yellow = 0xFF0;

std::vector<std::uint8_t> bytes_at(const amiga::LoadedImage& program, std::uint32_t at, std::size_t count) {
    std::vector<std::uint8_t> out(count);
    for (std::size_t i = 0; i < count; ++i) out[i] = program.u8(at + std::uint32_t(i));
    return out;
}

int decay(int v) { return v < 0 ? v + 1 : v > 0 ? v - 1 : 0; } // $403BEA

world::ShiftRegister seeded(const amiga::LoadedImage& c, std::uint32_t at, std::uint32_t seed, std::uint32_t if_zero) {
    const std::uint32_t v = c.u32(at) ^ seed;
    return {v != 0 ? v : if_zero};
}

// PrintIText: a chain of IntuiTexts (JAM2 cells in their BackPen), topaz 8.
void print_itext(screen::Canvas& canvas, const amiga::LoadedImage& c, std::uint32_t at, int x, int y,
                 const std::string* replacement = nullptr) {
    for (; at != 0; at = c.u32(at + 16)) {
        std::string text;
        if (replacement) {
            text = *replacement;
        } else {
            for (std::uint32_t p = c.u32(at + 12); c.u8(p) != 0; ++p) text.push_back(char(c.u8(p)));
        }
        canvas.text(x + c.s16(at + 4), y + c.s16(at + 6) + 6, text, c.u8(at), screen::TextFont::topaz8,
                    screen::plain, c.u8(at + 2) == 1, c.u8(at + 1));
    }
}

// Bresenham along the longer axis, as the blitter's line mode draws.
template <typename Plot> void each_point(int x0, int y0, int x1, int y1, Plot plot) {
    const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    const int sx = x1 >= x0 ? 1 : -1, sy = y1 >= y0 ? 1 : -1;
    const bool steep = dy > dx;
    const int major = steep ? dy : dx, minor = steep ? dx : dy;
    int error = 2 * minor - major;
    int x = x0, y = y0;
    for (int i = 0; i <= major; ++i) {
        plot(i, x, y);
        if (error >= 0) {
            (steep ? x : y) += steep ? sx : sy;
            error -= 2 * major;
        }
        error += 2 * minor;
        (steep ? y : x) += steep ? sy : sx;
    }
}

} // namespace

ArenaStage::ArenaStage(const data::GameFiles& files, const data::Assets& assets, Start start)
    : c_(files.program(data::Stage::arena)), assets_(assets),
      figure_(assets.figures[std::size_t(std::array{0, 3, 1, 2}[std::size_t(start.form & 3)])]),
      stage_(start.stage), lives_(start.lives), form_(start.form & 3), seed_(start.seed), score_(start.score) {
    // $400832: the window's gadgets (S's icons; lives as in A and B).
    const auto& s = files.program(data::Stage::setup);
    std::map<std::uint32_t, std::vector<std::uint8_t>> icons = {{pause_box_data, bytes_at(s, s_pause_box, 20)},
                                                                {knob_data, bytes_at(s, s_knob, 24)}};
    for (std::uint32_t i = 0; i < 5; ++i) icons[first_life_data + i * 20] = bytes_at(s, s_life, 20);
    bar_ = screen::GadgetBar(c_, new_window, std::move(icons));
    for (auto& gadget : bar_.gadgets())
        if (gadget.proportional) gadget.horizontal_pot = std::uint16_t(stage_ * 0x618);
    show_lives();
    // $4003AA: the walls' thirteen colours are steps of one colour picked by
    // the seed, as are colours 0 and 15; $402366: the figure's two colours.
    for (std::uint32_t i = 0; i < 32; ++i) colours_[i] = c_.u16(palette + i * 2);
    const std::uint32_t pick = (seed_ & 7) * 2;
    const auto step = c_.u16(colour_steps + pick);
    for (int i = 1; i < 14; ++i) colours_[std::size_t(i)] = std::uint16_t(i * step);
    colours_[0] = c_.u16(first_colours + pick);
    colours_[15] = c_.u16(last_colours + pick);
    colours_[29] = c_.u16(figure_colour_a + std::uint32_t(form_ * 2));
    colours_[30] = c_.u16(figure_colour_b + std::uint32_t(form_ * 2));
    load_colours();
    figure_rows_ = c_.s32(figure_rows + std::uint32_t(form_ * 4));
    line_pattern_ = c_.u16(first_pattern);
    direction_ = c_.s32(first_direction) & 3;
    for (int f = 0; f < 4; ++f)
        for (int w = 0; w < 17; ++w) { // stored plane by plane; the sprite takes them interleaved
            const std::uint32_t at = creature_images + std::uint32_t(f * 68);
            creature_frames_[std::size_t(f)][std::size_t(w * 2)] = c_.u16(at + std::uint32_t(w * 2));
            creature_frames_[std::size_t(f)][std::size_t(w * 2 + 1)] = c_.u16(at + 34 + std::uint32_t(w * 2));
        }
    // $404436, $402366, $402A9E: the generators.
    creature_random_ = seeded(c_, creature_seed, seed_, 0x7F9213EE);
    edge_random_ = seeded(c_, edge_seed, seed_, 0x0904FFD3);
    spawn_random_ = seeded(c_, spawn_seed, seed_, 0x59AD87DA);
    fidget_random_ = seeded(c_, fidget_seed, seed_, 0x79A9C7DA);
    // h8: the maze.
    MazeTables tables;
    for (std::uint32_t i = 0; i < 3; ++i) tables.keys[i] = c_.u16(maze_keys + i * 2);
    for (std::uint32_t i = 0; i < 8; ++i) tables.room_creatures[i] = c_.s32(room_creature_counts + i * 4);
    map_ = ArenaMap::generate(seed_, tables, maze_random_);
    // $402A9E: the wind's eight generators and its four slots.
    wind_seed_a_ = seeded(c_, wind_seed_a, seed_, 0x679FEDED);
    wind_seed_b_ = seeded(c_, wind_seed_b, seed_, 0xDEF21234);
    pulse_random_ = seeded(c_, pulse_seed, seed_, 0x5429F4ED);
    for (int i = 0; i < 4; ++i) {
        auto high = std::uint16_t(wind_seed_a_.next() << i);
        wind_turn_[std::size_t(i)] = world::Mixer::seeded(high, std::uint16_t(i ^ wind_seed_b_.next()));
        high = std::uint16_t(wind_seed_b_.next() << i);
        wind_pull_[std::size_t(i)] = world::Mixer::seeded(high, std::uint16_t(i ^ wind_seed_a_.next()));
        wind_direction_[std::size_t(i)] = (wind_seed_a_.next() ^ (i << 1)) & 0xF;
        wind_strength_[std::size_t(i)] = 0xF;
    }
    // h29: the drone (C h33) on channels 0 and 1, the squeal (h36) on 2, S's
    // image memory as noise on 3.
    drone_ = assets_.samples[1].data;
    squeal_ = assets_.samples[2].data;
    const std::uint32_t noise_at = s_noise_frames + std::uint32_t(assets_.figures[3].sequence.front() * 80);
    for (std::uint32_t i = 0; i < 0x514 * 2; ++i) noise_.push_back(std::int8_t(s.u8(noise_at + i)));
    voices_.set_samples(0, drone_);
    voices_.set_period(0, 900);
    voices_.set_volume(0, 0x3F);
    voices_.set_samples(1, std::span(drone_).first(1000));
    voices_.set_period(1, 900);
    voices_.set_volume(1, 0);
    voices_.set_samples(3, noise_);
    voices_.set_period(3, 0x7C);
    voices_.set_volume(3, 0);
    voices_.set_samples(2, squeal_);
    voices_.set_period(2, 1000);
    voices_.set_volume(2, 0);
    voices_.set_dma(true);
    // h8 / $401446: the home room scrolls in; then $402A9E: the wind, the panel.
    enter_room(true);
    sequence_.push_back([this] {
        draw_panel();
        std::fill(overlay_.begin(), overlay_.end(), 0);
        wind_step();
    });
}

// $40052C: life figure i shown while i + 1 < lives (PlanePick 8).
void ArenaStage::show_lives() {
    for (auto& gadget : bar_.gadgets())
        if (gadget.images.size() == 5)
            for (std::size_t i = 0; i < 5; ++i) {
                gadget.images[i].depth = int(i) + 1 < lives_ ? 1 : 0;
                gadget.images[i].plane_pick = int(i) + 1 < lives_ ? 8 : 0;
            }
}

void ArenaStage::describe(const std::function<void(const std::string&, long)>& report) const {
    report("x", player_.x);
    report("y", player_.y);
    report("room x", player_.room_x);
    report("room y", player_.room_y);
    report("aim", player_.aim);
    report("target", player_.has_target);
    report("frame", player_.frame);
    report("strength", player_.strength);
    report("stand counter", stand_counter_);
    report("score", long(score_));
    report("lives", lives_);
    report("frozen", frozen_);
    report("difficulty", map_.difficulty);
    report("maze seed", long(maze_random_.seed.state));
    report("columns", long(maze_random_.columns.state));
    report("targets", long(maze_random_.targets.state));
    report("rows", long(maze_random_.rows.state));
    report("creature random", long(creature_random_.state));
    report("edge random", long(edge_random_.state));
    report("spawn random", long(spawn_random_.state));
    report("pulse random", long(pulse_random_.state));
    report("wind seed a", long(wind_seed_a_.state));
    report("wind seed b", long(wind_seed_b_.state));
    report("pulse", pulse_);
    report("pulse wait", pulse_wait_);
    report("pulse runs", pulse_runs_);
    report("pulse colour", pulse_colour_);
    for (std::size_t i = 0; i < 4; ++i) {
        const auto n = std::to_string(i);
        report("wind direction " + n, wind_direction_[i]);
        report("wind strength " + n, wind_strength_[i]);
        report("wind turn " + n, long(wind_turn_[i].reg));
        report("wind pull " + n, long(wind_pull_[i].reg));
    }
    for (std::size_t i = 0; i < 6; ++i) {
        const auto n = std::to_string(i);
        const auto& c = creatures_[i];
        report("creature active " + n, c.active);
        if (!c.active) continue;
        report("creature x " + n, c.x);
        report("creature y " + n, c.y);
        report("creature behaviour " + n, long(c.behaviour));
        report("creature vx " + n, c.vx);
        report("creature vy " + n, c.vy);
        report("creature dying " + n, c.dying);
        report("creature anim " + n, long(c.anim));
    }
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y)
            report("room creatures " + std::to_string(x) + "," + std::to_string(y), map_.creatures[std::size_t(x)][std::size_t(y)]);
    report("bolt frames", bolt_frames_);
    report("bolt x", bolt_x_);
    report("bolt y", bolt_y_);
    report("bolt turn", bolt_turn_);
    report("sound wait", sound_wait_);
    report("hit noise", hit_noise_);
    report("death noise", death_noise_);
    report("death pitch", death_pitch_);
    for (std::size_t i = 0; i < 4; ++i) report("drone " + std::to_string(i), period_history_[i]);
}

int ArenaStage::sine(int direction, int radius) const {
    return c_.s32(sines + std::uint32_t(direction * 4)) * radius / 32;
}

int ArenaStage::gadget_at(int x, int y) const {
    if (y < 0 || y >= 10) return -1;
    if (x >= 0 && x < amiga::WindowCloseGadget::width) return -2;
    for (const auto& gadget : bar_.gadgets())
        if (x >= gadget.x() && x < gadget.x() + gadget.width && y >= gadget.top && y < gadget.top + gadget.height)
            return gadget.id;
    return -1;
}

void ArenaStage::frame(const ArenaReader& read, int gadget) {
    if (!sequence_.empty()) {
        const auto step = std::move(sequence_.front());
        sequence_.erase(sequence_.begin());
        step();
        return;
    }
    if (outcome_ != Outcome::playing) return;
    if (paused_) { // $40070C: Wait for the P box again (no sound)
        if (gadget == pause_gadget) resume();
        else if (gadget == -2) outcome_ = Outcome::quit;
        return;
    }
    sound_tick(); // $404E46, every pass (waiting too)
    // $4005A8: the left button pressed starts a wait of 300 frames.
    const bool button = read(ArenaRead::button).button;
    if (button && !button_was_down_ && frozen_ == 0) frozen_ = 300;
    button_was_down_ = button;
    if (gadget == -2) { // CLOSEWINDOW ($400618)
        outcome_ = Outcome::quit;
        return;
    }
    if (gadget >= 0) {
        frozen_ = 0; // any message ends the wait
        if (gadget == pause_gadget) {
            pause();
            return;
        }
    }
    if (frozen_ > 0) {
        --frozen_;
        return;
    }
    tick(read);
}

// One pass of the loop at $400040 (after the button).
void ArenaStage::tick(const ArenaReader& read) {
    std::fill(overlay_.begin(), overlay_.end(), 0);
    wind_step();
    // h11: the stick ($40251E); fire held turns the aim, else the player walks.
    const auto stick = read(ArenaRead::stick);
    fire_ = read(ArenaRead::fire).fire;
    stick_up_ = stick.up;
    stick_down_ = stick.down;
    stick_left_ = stick.left;
    stick_right_ = stick.right;
    if (fire_) {
        if (++aim_counter_ & 1) { // $4030F6: every other frame
            if (stick_left_) player_.aim = (player_.aim + 1) & 0xF;
            else if (stick_right_) player_.aim = (player_.aim - 1) & 0xF;
        }
    } else {
        move_player();
    }
    if (outcome_ == Outcome::won) return;
    const auto finish = [this] {
        pick_frame();
        beam();
        creatures();
        lightning();
        if (outcome_ == Outcome::playing && sequence_.empty()) {
            // $400338: the overlay becomes display plane 4 (rows 12-199).
            for (int y = 12; y < 200; ++y)
                for (int x = 0; x < 320; ++x)
                    screen_.plot(x, y, (screen_.pen(x, y) & 15) | overlay_[std::size_t(y * 320 + x)] << 4);
        }
        if (outcome_ != Outcome::playing || !sequence_.empty()) return;
        // $4028C0: the pulse; standing on a wall while it runs kills.
        if ((pulse_runs_ & 2) < ++pulse_wait_) {
            pulse_wait_ = 0;
            if (pulse_step() &&
                map_.at(player_.x / 32 + player_.room_x * 8, player_.y / 32 + player_.room_y * 6) == Cell::wall)
                wall_death();
        }
    };
    if (sequence_.empty()) finish();
    else sequence_.push_back(finish); // after the room's scroll
}

// $401D24: four pixels a frame across, three up and down; off an edge into
// the next room (wrapping round the map). In the room: the target taken, or
// the exit's middle reached.
void ArenaStage::move_player() {
    int change = 0, room_x = player_.room_x, room_y = player_.room_y;
    if (stick_right_) {
        player_.x += 4;
        if (player_.x > 0xFF) {
            ++room_x;
            player_.x = 0;
            change = 1;
        }
    } else if (stick_left_) {
        player_.x -= 4;
        if (player_.x < 0) {
            --room_x;
            player_.x = 0xFF;
            change = 2;
        }
    }
    if (stick_up_) {
        player_.y -= 3;
        if (player_.y < 0) {
            --room_y;
            player_.y = 0xBF;
            change = 3;
        }
    } else if (stick_down_) {
        player_.y += 3;
        if (player_.y > 0xBF) {
            ++room_y;
            player_.y = 0;
            change = 4;
        }
    }
    if (change != 0) {
        direction_ = change - 1;
        // $403C0C: the room keeps its live creatures for the next visit.
        int alive = 0;
        for (const auto& c : creatures_) alive += c.active && c.dying == 0;
        map_.creatures[std::size_t(player_.room_x)][std::size_t(player_.room_y)] = alive;
        player_.room_x = room_x & 7;
        player_.room_y = room_y & 7;
        enter_room(true);
        return;
    }
    cell_x_ = player_.x / 32 + player_.room_x * 8;
    cell_y_ = player_.y / 32 + player_.room_y * 6;
    const Cell cell = map_.at(cell_x_, cell_y_);
    if (cell == Cell::target) {
        player_.has_target = true;
        screen_.fill(player_.x & 0xE0, top + (player_.y & 0xE0), (player_.x & 0xE0) + 0x1F,
                     top + (player_.y & 0xE0) + 0x1F, 0);
        map_.set(cell_x_, cell_y_, Cell::open);
        map_.wall_in_home();
        if (lives_ < 6) {
            ++lives_;
            show_lives();
        }
        score_ += 500;
        print_score();
    } else if (cell == Cell::exit) {
        if (player_.x > 0x8A && player_.x < 0x96 && player_.y > 0x6A && player_.y < 0x73) {
            if (!player_.has_target) {
                outcome_ = Outcome::left; // $401080: back to the landscape
            } else {
                sprite_shown_ = false;
                victory();
            }
        } else if (player_.has_target) {
            stop_spawning_ = true;
        }
    }
}

// $40167C: the player hidden, the room's creatures placed, the room drawn.
void ArenaStage::enter_room(bool transition) {
    sprite_shown_ = false;
    spawn_room_creatures();
    draw_room(transition);
}

// $403DAC: the room's creatures, each on a random open cell's middle (up
// to 1,000 tries), in the sprites C's order gives; the others free.
void ArenaStage::spawn_room_creatures() {
    const int count = map_.creatures[std::size_t(player_.room_x)][std::size_t(player_.room_y)];
    for (auto& c : creatures_) c.shown = false; // $404040
    for (int i = 0; i < 6; ++i) {
        const int slot = c_.s32(creature_order + std::uint32_t(i * 4));
        if (i >= count) {
            creatures_[std::size_t(slot)].active = false;
            continue;
        }
        int cx = 0, cy = 0;
        for (int tries = 0; tries < 1000; ++tries) {
            cx = random_column(maze_random_, player_.room_x * 8);
            cy = random_row(maze_random_, player_.room_y * 6);
            if (map_.at(cx, cy) == Cell::open) break;
        }
        place_creature(slot, (cx - player_.room_x * 8) * 32 + 16, (cy - player_.room_y * 6) * 32 + 16);
        creatures_[std::size_t(slot)].shown = false;
    }
}

// $40168C: the room's 8 x 6 cells drawn behind the screen (open: blank;
// wall: one of eight tiles, half of them the room's own; the exit and the
// target over a blank), then scrolled in from the side the player came in
// at, 32 pixels a frame ($4018D2), then shown whole.
void ArenaStage::draw_room(bool transition) {
    std::fill(overlay_.begin(), overlay_.end(), 0);
    for (int y = 12; y < 200; ++y) // $400338 with the cleared overlay
        for (int x = 0; x < 320; ++x) screen_.plot(x, y, screen_.pen(x, y) & 15);
    // The room's look: the same each visit (a generator seeded by the room, $401CC0).
    const int rx = player_.room_x, ry = player_.room_y;
    world::ShiftRegister look{std::uint32_t(6 * (ry & 7) ^ ry << 10 ^ rx << 6 ^ ry << 2 ^ rx) ^ seed_};
    const int own_tile = look.next() & 3;
    back_.clear();
    for (int row = 0; row < 6; ++row)
        for (int column = 7; column >= 0; --column) {
            const Cell cell = map_.at(column + player_.room_x * 8, row + player_.room_y * 6);
            int image = 0;
            if (cell == Cell::open) {
                image = 10;
            } else if (cell == Cell::wall) {
                image = look.next() & 0xF;
                if (image > 7) image = own_tile;
            } else {
                image = int(cell) + 6; // 8 the exit, 9 the target, over the blank
            }
            const int x0 = column * 32, y0 = row * 32;
            back_.fill(x0, y0, x0 + 31, y0 + 31, 0);
            if (image < 8) { // 0-3 four planes; 4-7 the same tiles' first three planes
                const auto& tile = assets_.arena_images[std::size_t(image & 3)];
                for (int y = 0; y < 32; ++y)
                    for (int x = 0; x < 32; ++x) back_.plot(x0 + x, y0 + y, tile.at(x, y) & (image < 4 ? 15 : 7));
            } else if (image < 10) {
                const auto& tile = assets_.arena_images[std::size_t(image - 4)]; // 32 x 10 at +18, 32 x 15 at +8
                const int dy = image == 8 ? 18 : 8;
                for (int y = 0; y < tile.height; ++y)
                    for (int x = 0; x < 32; ++x) back_.plot(x0 + x, y0 + dy + y, tile.at(x, y));
            }
        }
    const int dx = c_.s32(scroll_x + std::uint32_t(direction_ * 4)) * 32;
    const int dy = c_.s32(scroll_y + std::uint32_t(direction_ * 4)) * 32;
    const auto blit = [this](int sx, int sy, int x, int y, int w, int h) { // ClipBlit, planes 0-3
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i)
                if (y + j < 200) screen_.plot(x + i, y + j, (screen_.pen(x + i, y + j) & 16) | back_.pen(sx + i, sy + j));
    };
    const auto show = [this, blit] {
        blit(0, 0, 0, top, room_pixels_x, 200 - top);
        for (auto& c : creatures_) c.shown = c.active; // $403FD2
        std::fill(overlay_.begin(), overlay_.end(), 0);
        draw_strength();
    };
    if (!transition) {
        show();
        return;
    }
    scroll(dx, dy, 0, top, 0xFF, 199); // at the fifth row of cells ($40168C)
    const int strips = direction_ < 2 ? 7 : 5;
    for (int k = 0; k < strips; ++k) {
        sequence_.push_back([this, k, dx, dy, blit] {
            if (k > 0) scroll(dx, dy, 0, top, 0xFF, 199);
            switch (direction_) {
            case 0: blit(k * 32, 0, 0xE0, top, 32, 200 - top); break;
            case 1: blit(0xE0 - k * 32, 0, 0, top, 32, 200 - top); break;
            case 2: blit(0, 0xA0 - k * 32, 0, top, room_pixels_x, 32); break;
            default: blit(0, k * 32, 0, 0xA8, room_pixels_x, 32); break;
            }
            sound_tick();
        });
    }
    sequence_.push_back([this, dx, dy, show] {
        scroll(dx, dy, 0, top, 0xFF, 199);
        show();
    });
}

// ScrollRaster: the rectangle's contents move by (-dx, -dy); what is left
// uncovered becomes pen 0.
void ArenaStage::scroll(int dx, int dy, int x0, int y0, int x1, int y1) {
    const screen::Canvas before = screen_;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const int sx = x + dx, sy = y + dy;
            screen_.plot(x, y, sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1 ? before.pen(sx, sy) : 0);
        }
}

// $402720: the panel right of the room: frame, "Score", the strength gauge
// (left) and the temperature gauge (right) with their labels.
void ArenaStage::draw_panel() {
    screen_.fill(0x100, top, 0x101, 199, 0);
    screen_.fill(0x102, top, 0x13F, 199, 15);
    screen_.fill(0x103, top + 1, 0x13E, 0xC6, 0);
    screen_.fill(0x108, 0x25, 0x110, 0xC3, 14);
    screen_.fill(0x12E, 0x25, 0x136, 0xC3, 14);
    screen_.fill(0x130, 0x26, 0x134, 0xC2, 0);
    print_itext(screen_, c_, score_label, 0x105, 0xD);
    print_itext(screen_, c_, gauge_labels, 0x114, 0x25);
    draw_strength();
    print_score();
}

// h14: the strength gauge, 350 the full height.
void ArenaStage::draw_strength() {
    int y = 0xC2 - player_.strength * 10 / 0x17;
    if (y > 0xC1) y = 0xC2;
    screen_.fill(0x10A, y, 0x10E, 0xC2, 15);
    screen_.fill(0x10A, 0x26, 0x10E, y, 0);
}

// $404908: the score without leading zeros; from ten million "Huge!!!".
void ArenaStage::print_score() {
    if (score_ >= 10000000) {
        print_itext(screen_, c_, huge_text, 0x105, 0x17);
        return;
    }
    const std::string digits = score_ == 0 ? std::string() : std::to_string(score_);
    print_itext(screen_, c_, score_text, 0x105, 0x17, &digits);
}

// $402058: the figure's frame: aiming, a frame by the aim; walking, four
// frames by the stick's direction (form 2: eight); standing, frame $18
// and now and then a fidget ($18-$1B).
void ArenaStage::pick_frame() {
    int& frame = player_.frame;
    if (fire_) {
        frame = form_ == 2 ? 5 : c_.s32(aim_frames + std::uint32_t(player_.aim * 4));
    } else if (!stick_up_ && !stick_down_ && !stick_left_ && !stick_right_) {
        if (frame < 0x18 || (++stand_counter_ & 0x1F) == 0) frame = frame == 0x18 ? (fidget_random_.next() & 3) + 0x18 : 0x18;
    } else {
        ++stand_counter_;
        const bool across = stick_left_ || stick_right_;
        if (form_ == 2) { // $4022B8
            frame = (stand_counter_ & 7) + (!across ? 0 : stick_left_ ? 0x10 : 8);
        } else { // $402248
            int base = 0;
            if (!across) base = stick_down_ ? 8 : 0xC;
            else if (!stick_up_) base = stick_left_ ? 4 : 0;
            else base = stick_left_ ? 0x14 : 0x10;
            frame = (stand_counter_ & 3) + base;
        }
    }
    sprite_shown_ = true;
}

// $403382: the beam. Its root lies out along the aim (15 pixels, 13 when
// the aim is not in line with the wind); from there a fan of three points
// in the aim's mirror about the wind, as far out as the wind is strong
// (fire held: seven times as far), filled in the overlay. Creatures it
// touches die ($403C72). Then the wind's line from the root to the
// screen's edge, dotted, the dots marching.
void ArenaStage::beam() {
    const int d = player_.aim + 8 - wind_direction_[0];
    const bool in_line = std::abs(d) < 4 || (std::abs(d) > 12 && std::abs(d) < 20); // $4034C2
    const int fan = (d + player_.aim) & 0xF;
    const int length = in_line ? 15 : 13;
    const int root_x = std::clamp(sine(player_.aim, length) + player_.x, 0, 319);
    const int root_y = std::clamp(sine(player_.aim + 4, length) + top + player_.y, top, 199);
    int reach = wind_strength_[0] * 5;
    if (!fire_) reach /= 7;
    std::vector<screen::AreaPoint> points{{root_x, root_y}};
    for (int k = 0; k < 3; ++k) {
        const int dir = (k + fan - 1) & 0xF;
        points.push_back({std::clamp(sine(dir, reach) + root_x, 0, 319),
                          std::clamp(sine(dir + 4, reach) + root_y, top, 199)});
    }
    screen::fill_area(points, [this](int x, int y) {
        if (x >= 0 && x < 320 && y >= 0 && y < 200) overlay_[std::size_t(y * 320 + x)] = 1;
    });
    // $403C72: a creature with the beam at any of four points round it dies.
    for (int i = 0; i < 6; ++i) {
        auto& c = creatures_[std::size_t(i)];
        if (!c.active || c.dying != 0) continue;
        bool hit = false;
        for (std::uint32_t k = 0; k < 4 && !hit; ++k) {
            const int x = std::clamp(c_.s32(hit_x + k * 4) + c.x, 0, 0xFF);
            const int y = std::clamp(c_.s32(hit_y + k * 4) + top + c.y, top, 0xBF);
            hit = overlay_[std::size_t(y * 320 + x)] != 0;
        }
        if (hit) {
            death_pitch_ = creature_random_.next() & 0x3FF;
            death_noise_ = 0x1F;
            c.dying = 1;
            score_ += std::uint32_t(stage_ / 2 + 1);
            print_score();
        }
    }
    // $4032DA: the wind's line, from the root against the wind to the edge.
    const int against = (wind_direction_[0] + 8) & 0xF;
    int end_x = root_x, end_y = root_y;
    for (int x = root_x, y = root_y, r = 5; x > 0 && x < 320 && y > 0 && y < 200; r += 0x19) {
        end_x = x;
        end_y = y;
        x = sine(against, r) + root_x;
        y = sine(against + 4, r) + root_y;
    }
    line_pattern_ = std::uint16_t(line_pattern_ << 15 | line_pattern_ >> 1);
    overlay_line(end_x, end_y, root_x, root_y, line_pattern_);
    // $403518: the aim marker, an arc of four points round the player.
    int px = 0, py = 0;
    for (std::uint32_t k = 0; k < 4; ++k) {
        const int dir = (c_.s32(marker_offsets + k * 4) + player_.aim) & 0xF;
        const int x = std::clamp(sine(dir, 15) + player_.x, 0, 319);
        const int y = std::clamp(sine(dir + 4, 15) + top + player_.y, top, 199);
        if (k > 0) overlay_line(px, py, x, y);
        px = x;
        py = y;
    }
}

void ArenaStage::overlay_line(int x0, int y0, int x1, int y1, std::uint16_t pattern) {
    each_point(x0, y0, x1, y1, [&](int i, int x, int y) {
        if ((pattern >> (15 - (i & 15)) & 1) && x >= 0 && x < 320 && y >= 0 && y < 200)
            overlay_[std::size_t(y * 320 + x)] = 1;
    });
}

// $4036CE: each sprite a creature: a free one may come in at an edge
// (h20: by the difficulty's odds; not once the target is taken and the
// player has been by the exit).
void ArenaStage::creatures() {
    const unsigned mask = c_.u16(spawn_masks + std::uint32_t((map_.difficulty >> 2 & 3) * 2));
    for (int i = 0; i < 6; ++i) {
        if (creatures_[std::size_t(i)].active) {
            creature_step(i);
        } else if ((mask & spawn_random_.next()) == 0 && !stop_spawning_) {
            spawn_at_edge(i);
        }
    }
}

// $403AAC: on a random point of the room's edge, if that cell is open.
void ArenaStage::spawn_at_edge(int slot) {
    int x = 0, y = 0;
    if ((creature_random_.next() & 0x20) == 0) {
        x = (creature_random_.next() & 0x100) == 0 ? 0xFF : 0;
        y = edge_random_.next() & 0xBF;
    } else {
        x = edge_random_.next() & 0xFF;
        y = (creature_random_.next() & 0x100) == 0 ? 0xBF : 0;
    }
    if (map_.at(x / 32 + player_.room_x * 8, y / 32 + player_.room_y * 6) == Cell::open) {
        place_creature(slot, x, y);
        creatures_[std::size_t(slot)].shown = true;
    }
}

// $403EDE: a creature in a slot, with a random drift and behaviour.
void ArenaStage::place_creature(int slot, int x, int y) {
    auto& c = creatures_[std::size_t(slot)];
    c.active = true;
    c.x = x;
    c.y = y;
    c.behaviour = creature_random_.next() & 3;
    c.vx = (creature_random_.next() & 0xF) - 8;
    c.vy = (creature_random_.next() & 0xF) - 8;
    c.dying = 0;
    c.anim = spawn_random_.next();
    set_creature_image(slot);
}

void ArenaStage::set_creature_image(int slot) { // $404080: one of four frames
    auto& c = creatures_[std::size_t(slot)];
    c.sprite = creature_frames_[std::size_t(c.anim >> 1 & 3)];
}

// $40370E: a live creature turns now and then to a new behaviour; touching
// the player drains strength; bit 0 / bit 1 of its behaviour steer it
// towards the player across / up and down (2 a frame, up to 20), else its
// drift dies away; it moves a quarter of its speed, and bounces back off
// cells that are not open. A dying one dissolves over 15 frames.
void ArenaStage::creature_step(int slot) {
    auto& c = creatures_[std::size_t(slot)];
    if (c.dying != 0) {
        if (++c.dying < 0x10) {
            for (unsigned w = unsigned(c.dying) & 1; w < 34; w += 2) { // a random part of every other word cleared
                const unsigned a = creature_random_.next(), b = edge_random_.next();
                c.sprite[w] &= std::uint16_t((b << (w >> 2)) | ((w << 3) ^ w ^ a));
            }
        } else {
            c.active = false;
            c.shown = false;
        }
        return;
    }
    if ((++c.anim & 1) == 0) set_creature_image(slot);
    if ((creature_random_.next() & 0x250) == 0) c.behaviour = creature_random_.next();
    if (c.x - player_.x < 8 && c.x - player_.x > -8 && c.y - player_.y < 10 && c.y - player_.y > -10) hit_player();
    if (c.vx == 0 && c.vy == 0 && (c.behaviour & 3) == 0) c.behaviour = creature_random_.next();
    const int vx = (c.behaviour & 1) ? std::clamp(c.vx + (c.x < player_.x ? 2 : -2), -0x14, 0x14) : decay(c.vx);
    const int vy = (c.behaviour & 2) ? std::clamp(c.vy + (c.y < player_.y ? 2 : -2), -0x14, 0x14) : decay(c.vy);
    const int x = std::clamp(c.x + vx / 4, 0, 0xFF);
    const int y = std::clamp(c.y + vy / 4, top, 0xBF);
    if (map_.at(x / 32 + player_.room_x * 8, y / 32 + player_.room_y * 6) == Cell::open) {
        c.x = x;
        c.y = y;
        c.vx = vx;
        c.vy = vy;
        c.shown = true;
    } else {
        c.vx = -vx;
        c.vy = -vy;
    }
}

// $40412C: a creature's touch: strength down by the stage's slider
// (pot / $666 + 1), a lightning bolt round the player, the noise.
void ArenaStage::hit_player() {
    if (player_.strength <= 0) return;
    player_.strength -= (stage_ * 0x618 & 0xFFFF) / 0x666 + 1;
    if (player_.strength < 0) {
        bolt_frames_ = 0;
        player_.strength = 0;
    }
    draw_strength();
    if (bolt_frames_ == 0) {
        bolt_x_ = creature_random_.next() & 0x1F;
        bolt_y_ = spawn_random_.next() & 0x1F;
        bolt_frames_ = 3;
    } else {
        ++bolt_frames_;
    }
    if (hit_noise_ < 8) hit_noise_ = 0xF;
}

// $4041D2: the bolt, one side of a diamond round the player a frame,
// growing; no strength left: death.
void ArenaStage::lightning() {
    if (bolt_frames_ != 0) {
        --bolt_frames_;
        bolt_x_ += 5;
        bolt_y_ += 3;
        if (bolt_x_ > 0x46) bolt_x_ = bolt_y_ = 5;
        const int x = std::clamp(player_.x, 0, 319), y = std::clamp(top + player_.y, top, 199);
        const int xs[4] = {x, std::clamp(bolt_x_ + player_.x, 0, 319), x, std::clamp(player_.x - bolt_x_, 0, 319)};
        const int ys[4] = {std::clamp(y - bolt_y_, top, 199), y, std::clamp(bolt_y_ + y, top, 199), y};
        const int k = (++bolt_turn_ >> 2) & 3, next = (k + 1) & 3;
        bolt(xs[k], ys[k], xs[next], ys[next], 0x3F);
    }
    if (player_.strength < 1) die();
}

// $404322: a crooked line: the middle moved up or down by a random amount
// under `amplitude`, each half again with half of it, down to 5 pixels.
void ArenaStage::bolt(int x0, int y0, int x1, int y1, int amplitude) {
    const int mid_x = (x0 + x1) >> 1;
    if (std::abs(mid_x - x0) < 5) {
        overlay_line(x0, y0, x1, y1);
        return;
    }
    const int mix = maze_random_.bolt.step();
    int offset = amplitude > 0 ? mix % amplitude : 0; // (the division's remainder)
    if ((maze_random_.bolt.step() & 0x40) == 0) offset = -offset;
    const int mid_y = std::clamp(((y0 + y1) >> 1) + offset, 0, 199);
    bolt(x0, y0, mid_x, mid_y, amplitude >> 1);
    bolt(mid_x, mid_y, x1, y1, amplitude >> 1);
}

// $402930: the pulse's step: 32 steps round, the first 13 light the walls'
// colours 1-13 yellow one after the other; three times in four a round is
// skipped at its start. Returns whether a colour is lit.
bool ArenaStage::pulse_step() {
    const int k = ++pulse_ & 0x1F;
    if (k >= 0xE) return false;
    if (k == 0 && (pulse_random_.next() & 3) != 0) {
        pulse_ = 0x1B;
        return false;
    }
    shown_colours_[std::size_t(pulse_colour_)] = colours_[std::size_t(pulse_colour_)]; // SetRGB4 back
    if (k >= 0xD) return false;
    pulse_colour_ = k + 1;
    shown_colours_[std::size_t(pulse_colour_)] = yellow;
    if (k == 0) ++pulse_runs_;
    return true;
}

// $402C34: the wind turns now and then by one of 16 directions and its
// strength wanders towards a random 0-15 (from 0 down it wraps to 15);
// $402D16: the temperature gauge from the drone's pitch, in the overlay.
void ArenaStage::wind_step() {
    for (std::size_t i = 0; i < 4; ++i) {
        const unsigned turn = wind_turn_[i].step();
        if ((turn & 0x6E) == 0) wind_direction_[i] = (wind_direction_[i] + ((turn & 0x200) ? 1 : -1)) & 0xF;
        wind_direction_[i] &= 0xF;
        const int pull = wind_pull_[i].step() & 0xF;
        wind_strength_[i] = (wind_strength_[i] + (wind_strength_[i] < pull ? 1 : -1)) & 0xF;
    }
    // (Unsigned: below 150 the gauge is empty.)
    unsigned y = unsigned(period_history_[3] - 0x96) * 10u / 0x30u + 0x26u;
    const int y0 = int(std::clamp<unsigned>(y, 0x26, 0xC2));
    for (int yy = y0; yy <= 0xC2; ++yy)
        for (int x = 0x130; x <= 0x134; ++x) overlay_[std::size_t(yy * 320 + x)] = 1;
}

// $404E46: every 21 frames the drone's pitch follows the distance to the
// target (afterwards: to the exit, higher), averaged over four; channel 1
// sounds with the pulse; the hit's noise and the kill's squeal fade.
void ArenaStage::sound_tick() {
    if (++sound_wait_ > 0x14) {
        sound_wait_ = 0;
        const auto distance = [this](int x, int y) { return (std::abs(cell_x_ - x) * 3 + std::abs(cell_y_ - y) * 4) * 2; };
        int pitch = 0;
        if (!player_.has_target) {
            pitch = std::min(distance(map_.target_x, map_.target_y) * 2 + 0xFA, 900);
        } else {
            pitch = distance(ArenaMap::exit_x, ArenaMap::exit_y) / 4 + 0x96;
        }
        const int sum = period_history_[1] + period_history_[2] + period_history_[3];
        period_history_[0] = period_history_[1];
        period_history_[1] = period_history_[2];
        period_history_[2] = period_history_[3];
        period_history_[3] = (sum + pitch) / 4;
        voices_.set_period(0, period_history_[3]);
        voices_.set_volume(0, 0x3F);
        voices_.set_period(1, period_history_[3]);
    }
    voices_.set_volume(1, (pulse_ & 0x1F) < 0xD ? (0xD - (pulse_ & 0x1F)) * 4 : 0);
    if (hit_noise_ != 0) voices_.set_period(3, hit_noise_-- * 32 + 0xAA);
    voices_.set_volume(3, hit_noise_ * 4);
    if (death_noise_ != 0) voices_.set_period(2, ((0x20 - death_noise_) & 7) * 64 + death_pitch_ + 1000), --death_noise_;
    voices_.set_volume(2, death_noise_);
}

// $401A90: a life lost: the sprites gone, the walls' last colours dimmed,
// the death's picture on every cell, and the room drawn in to its middle
// over 44 frames; out of lives, the game is over.
void ArenaStage::die() {
    for (int c = 0; c < 2; ++c) { // $404D36
        voices_.set_period(c, 0x7C);
        voices_.set_volume(c, 0x3F);
    }
    voices_.set_volume(2, 0);
    voices_.set_volume(3, 0);
    for (auto& c : creatures_) c.shown = false;
    sprite_shown_ = false;
    const auto step = c_.u16(colour_steps + (seed_ & 7) * 2);
    colours_[14] = std::uint16_t(step * 14);
    colours_[15] = std::uint16_t(step * 15);
    load_colours();
    --lives_;
    show_lives();
    outcome_ = Outcome::died;
    const auto& picture = assets_.arena_images[6];
    for (int row = 0; row < 6; ++row)
        for (int column = 0; column < 8; ++column)
            for (int y = 0; y < picture.height; ++y)
                for (int x = 0; x < picture.width; ++x)
                    screen_.plot(column * 32 + 9 + x, top + row * 32 + 8 + y,
                                 (screen_.pen(column * 32 + 9 + x, top + row * 32 + 8 + y) & 16) | picture.at(x, y));
    for (int k = 0; k < 0x2C; ++k)
        sequence_.push_back([this, k] {
            for (int c = 0; c < 2; ++c) { // $404D8C
                voices_.set_volume(c, 0x2B - k);
                voices_.set_period(c, k * 16 + 0x7C);
            }
            scroll(-3, -3, 0, top, 0x7F, 0x6A);
            scroll(3, -3, 0x80, top, 0xFF, 0x6A);
            scroll(3, 3, 0x80, 0x6B, 0xFF, 199);
            scroll(-3, 3, 0, 0x6B, 0x7F, 199);
        });
}

// $402A2C: on a wall in the pulse: the pulse races (two steps a frame) for
// 44 frames while the drone falls, then the death.
void ArenaStage::wall_death() {
    for (int c = 0; c < 2; ++c) {
        voices_.set_period(c, 0x7C);
        voices_.set_volume(c, 0x3F);
    }
    voices_.set_volume(2, 0);
    voices_.set_volume(3, 0);
    for (int k = 0; k < 0x2C; ++k)
        sequence_.push_back([this, k] {
            for (int c = 0; c < 2; ++c) {
                voices_.set_volume(c, 0x3F);
                voices_.set_period(c, (0xB - k / 4) * 16 + 0x7C);
            }
            pulse_step();
            if ((pulse_ & 0x1F) > 0xF) pulse_ = 0;
            pulse_step();
            if (k == 0x2B) die();
        });
}

// $40653A: the exit with the target: 7,000 points, then 158 frames of
// colours rotating through the walls' thirteen and flickering in the
// sprites', colour 0 throbbing, a falling chord from a waveform rebuilt
// each frame ($406642).
void ArenaStage::victory() {
    score_ += 7000;
    print_score();
    for (int c = 0; c < 4; ++c) {
        voices_.set_samples(c, std::span(victory_wave_).first(c < 2 ? 64 : 32));
        voices_.set_volume(c, 0);
        voices_.set_period(c, 300);
    }
    voices_.set_dma(true);
    auto fall = std::make_shared<int>(0x7F), count = std::make_shared<int>(0x9E), wait = std::make_shared<int>(0);
    outcome_ = Outcome::won;
    for (int f = 0; f < 0x9E; ++f)
        sequence_.push_back([this, fall, count, wait] {
            if (8 - (*fall >> 4) < ++*wait) {
                *wait = 0;
                colours_[0] = std::uint16_t(*count << 4 ^ *count);
                for (int i = 16; i < 32; ++i) colours_[std::size_t(i)] = std::uint16_t(creature_random_.next() & 0xFFF);
                const auto last = colours_[15];
                for (int i = 15; i > 1; --i) colours_[std::size_t(i)] = colours_[std::size_t(i - 1)];
                colours_[1] = last;
            }
            load_colours();
            // $406642.
            std::fill(victory_wave_.begin(), victory_wave_.end(), 0);
            ++victory_count_;
            for (int i = 0; i < 4; ++i)
                for (int c = 0; c < 4; ++c) {
                    auto& v = victory_wave_[std::size_t(((victory_count_ & 0xFFFF) >> i) + c & 0x3F)];
                    v = std::int8_t(v + c * 10);
                }
            for (int c = 0; c < 4; ++c) voices_.set_samples(c, std::span(victory_wave_).first(c < 2 ? 64 : 32));
            const int period = *fall <= 0x3F ? 0x21A : *fall * 8 + 0x22;
            const int volume = *fall <= 0x1F ? *fall * 2 : 0x3F;
            voices_.set_period(0, period);
            voices_.set_volume(0, volume);
            voices_.set_period(1, period * 3 / 2);
            voices_.set_volume(1, volume);
            for (int c = 0; c < 2; ++c) {
                voices_.set_period(2 + c, c_.u16(victory_periods + std::uint32_t(((c * 2 + (victory_count_ >> 1)) & 7) * 2)));
                voices_.set_volume(2 + c, std::min(*count, 0x3F));
            }
            if (*fall != 0) --*fall;
            if (*count != 0) --*count;
            if (*count == 0) voices_.set_dma(false);
        });
}

// $40070C: the P box: silence, the sprites gone, the window down a bar's
// height under the screen's title.
void ArenaStage::pause() {
    paused_ = true;
    for (int c = 0; c < 4; ++c) voices_.set_volume(c, 0);
    voices_.set_dma(false);
    for (auto& c : creatures_) c.shown = false;
    sprite_shown_ = false;
}

// $40077E: the window back up, 30 frames, the screen cleared and the room
// scrolled in again, the panel; the sound back.
void ArenaStage::resume() {
    paused_ = false;
    for (int f = 0; f < 29; ++f) sequence_.push_back([] {});
    sequence_.push_back([this] {
        screen_.fill(0, top, 0x13F, 199, 0);
        draw_room(true);
        sequence_.push_back([this] {
            draw_panel();
            voices_.set_dma(true);
        });
    });
}

platform::Image ArenaStage::picture(bool sprites) const {
    std::vector<screen::Rgb4> colours;
    for (const auto c : shown_colours_) colours.push_back(c & 0xFFF);
    screen::Canvas canvas = screen_;
    if (paused_) {
        // The screen's title bar (rows 0-9), the window moved down over
        // rows 10-21.
        screen::Canvas bar;
        bar_.draw(bar, title_bar_pen);
        const auto& close = amiga::window_close_gadget();
        for (int y = 0; y < close.height; ++y)
            for (int x = 0; x < close.width; ++x)
                bar.plot(x, y, (close.planes[0][std::size_t(y)] >> (15 - x) & 1) | (close.planes[1][std::size_t(y)] >> (15 - x) & 1) << 1);
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 320; ++x) canvas.plot(x, y + 10, bar.pen(x, y));
        canvas.fill(0, 0, 319, 9, title_bar_pen);
        std::string title;
        for (std::uint32_t c = screen_title; c_.u8(c) != 0; ++c) title.push_back(char(c_.u8(c)));
        canvas.text(2, 7, title, title_text_pen);
        for (const auto& gadget : amiga::screen_depth_gadgets()) {
            const int x0 = 319 + gadget.left;
            for (int y = 0; y < gadget.height; ++y)
                for (int x = 0; x < gadget.width; ++x)
                    canvas.plot(x0 + x, y, int(gadget.planes[0][std::size_t(y)] >> (31 - x) & 1) |
                                               int(gadget.planes[1][std::size_t(y)] >> (31 - x) & 1) << 1);
        }
        return canvas.picture(colours);
    }
    // The window: its gadget bar over rows 0-10 (row 11 on is the screen's).
    screen::Canvas bar;
    bar_.draw(bar, title_bar_pen);
    const auto& close = amiga::window_close_gadget();
    for (int y = 0; y < close.height; ++y)
        for (int x = 0; x < close.width; ++x)
            bar.plot(x, y, (close.planes[0][std::size_t(y)] >> (15 - x) & 1) | (close.planes[1][std::size_t(y)] >> (15 - x) & 1) << 1);
    for (int y = 0; y < top; ++y)
        for (int x = 0; x < 320; ++x) canvas.plot(x, y, bar.pen(x, y));
    auto image = canvas.picture(colours);
    if (!sprites) return image;
    // Sprites: the player (sprite 7, colours 29-31) behind the creatures
    // (sprites 1-6, lower in front).
    const auto put = [&](int x, int y, int colour) {
        if (x >= 0 && x < 320 && y >= 0 && y < 200) image.put(x, y, screen::to_color(colours[std::size_t(colour)]));
    };
    if (sprite_shown_ && outcome_ != Outcome::won) {
        const auto& bitmap = figure_.frames[std::size_t(figure_.sequence[std::size_t(player_.frame)])];
        const int sx = player_.x - 8, sy = player_.y - (figure_rows_ >> 1) + top;
        for (int y = 0; y < std::min(figure_rows_, bitmap.height); ++y)
            for (int x = 0; x < 16; ++x)
                if (const int v = bitmap.at(x, y)) put(sx + x, sy + y, 28 + v);
    }
    for (int i = 5; i >= 0; --i) {
        const auto& c = creatures_[std::size_t(i)];
        if (!c.active || !c.shown) continue;
        const int base = 16 + ((i + 1) >> 1) * 4;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const int v = (c.sprite[std::size_t(y * 2)] >> (15 - x) & 1) | (c.sprite[std::size_t(y * 2 + 1)] >> (15 - x) & 1) << 1;
                if (v) put(c.x - 8 + x, c.y - 8 + top + y, base + v);
            }
    }
    return image;
}

} // namespace mindwalker::arena
