#include "mindwalker_amiga/landscape/landscape_stage.hpp"
#include <algorithm>
#include <cstdlib>

namespace mindwalker::landscape {
namespace {

// A's data (research layout).
constexpr std::uint32_t form_heights = 0x206B1C;   // sprite rows by form (4 longs)
constexpr std::uint32_t form_colour17 = 0x206B30;  // colour 17 by form (4 words)
constexpr std::uint32_t form_colour18 = 0x206B38;  // colour 18 by form
constexpr std::uint32_t form_kinds = 0x206B50;     // the surface kind of each form (4 words)
constexpr std::uint32_t form_heads = 0x206B40;     // the head: rows above the feet, by form (4 longs)
constexpr std::uint32_t walker_marker = 0x20B2B4;  // 8 sprite rows (2 words each) shown when the walker is hidden
constexpr std::uint32_t catch_damage = 0x203E2C;   // damage per catch by stage / 7 (6 longs)
constexpr int damage_per_life = 1600;              // h14: damage / 100 reaching 16 costs a life
constexpr std::uint32_t neighbour_dx = 0x205CEC, neighbour_dy = 0x205D0C; // 8 longs each
constexpr std::uint32_t fidget_seed = 0x20B2B0;    // A's shift register h51, as stored
constexpr int speed_limit_base = 4;                // $206B6C (plus $2083AE, 0 at the start)
constexpr int speed_period = 5;                    // $206B70
constexpr int water_speed = 4;                     // $206B74
constexpr int click_wait = 300;                    // $203F58
constexpr int view_top = 10;                       // the view is shown from display row 10
constexpr int map_gadget = 4;                      // the emblem (GRELRIGHT, 32 wide, a toggle)
constexpr int map_close_frames = 31;               // $200D02 on the A500 (measured)

// The four forms' shared image sets (A $200040 keeps +$76, +$1C6, +$E6, +$156
// for forms 0-3), as Assets::figures orders them (+$76, +$E6, +$156, +$1C6).
constexpr int form_figure_set[4] = {0, 3, 1, 2};

} // namespace

LandscapeStage::LandscapeStage(const data::GameFiles& files, const data::Assets& assets, const Start& start)
    : a_(files.program(data::Stage::landscape)), assets_(assets), world_(start.seed, a_),
      stage_(start.stage), lives_(start.lives), score_(start.score), bar_(files), painter_(a_),
      creatures_(a_, assets), interlude_(a_) {
    home_x_ = world_.start_x();
    home_y_ = world_.start_y();
    fidget_.state = a_.u32(fidget_seed);
    // $20ADF6: the bolt's mixer ($20648E, seeded by h47) from two draws.
    const auto high = fidget_.next();
    bolt_.seed(high, fidget_.next());
    // $20AEE4 (new game): facing north, x 120 (column 1), z 40 (row 0),
    // level 0, the view's first column left of the start cell.
    direction_ = 0;
    walker_.x = 120;
    walker_.z = 40;
    walker_.level = 0;
    walker_.cell_x = home_x_;
    walker_.cell_y = home_y_;
    walker_.terrain = 0x4BE;
    walker_.next_terrain = 0;
    view_x_ = std::uint16_t((home_x_ - 1) & 0xFFF);
    view_y_ = home_y_;
    // $205A14: the map window around the start.
    centre_map(home_x_, home_y_);
    // $206944: the form from the surface of the view's cell [2][0] (built for
    // the cell left of the start); $206A2E: its height and colours.
    inputs_.stage = stage_;
    inputs_.home_x = home_x_;
    inputs_.home_y = home_y_;
    inputs_.visited = [this](std::uint16_t x, std::uint16_t y) { return visited(x, y); };
    colours_ = stored_colours(a_);
    inputs_.colour17 = colours_[17];
    inputs_.colour18 = colours_[18];
    rebuild_view();
    walker_.form = 0;
    while (walker_.form < 4 &&
           (cells_.terrain[2][0] & 0xC000) != a_.u16(form_kinds + std::uint32_t(walker_.form * 2)))
        ++walker_.form;
    walker_.form &= 3; // (4 never matches: the four kinds cover the field)
    walker_.height = a_.s32(form_heights + std::uint32_t(walker_.form * 4));
    inputs_.colour17 = a_.u16(form_colour17 + std::uint32_t(walker_.form * 2));
    inputs_.colour18 = a_.u16(form_colour18 + std::uint32_t(walker_.form * 2));
    walker_.frame = 0;
    walker_.frame_base = 4;
    // $200262: the view again (now with the form's colours), then drawn.
    rebuild_view();
    bar_.set(lives_, direction_, stage_);
    repaint();
    place_sprite();
    // $201492(2): the view opens from the middle over the empty display.
    Display empty;
    bar_.draw(empty.planes);
    empty.colours = colours_;
    Display next = current_display();
    std::fill(next.plane4.begin(), next.plane4.end(), 0);
    interlude_.start_reveal(empty, next);
}

Display LandscapeStage::current_display() const {
    Display d;
    d.planes = view_canvas_;
    d.plane4 = creatures_.tethers();
    d.colours = colours_;
    return d;
}

void LandscapeStage::rebuild_view() {
    inputs_.previous_x = view_x_;
    inputs_.previous_y = view_y_;
    cells_ = build_view(world_, a_, view_x_, view_y_, inputs_, colours_);
    colours_ = cells_.colours;
}

// $201492 / $201602 (`full`): the whole view drawn again, the score printed
// as it stands and the colours loaded ($2004AC: the aiming colours go). A
// cell's marker alone ($204F48) leaves both as they were.
void LandscapeStage::repaint(bool full) {
    if (full) {
        printed_score_ = score_;
        aim_colours_ = false;
    }
    view_canvas_.clear(0);
    bar_.draw(view_canvas_);
    if (lives_ > 0) // ($201492 draws no view once the last life is gone)
        painter_.paint(view_canvas_, cells_, direction_, world_.floor_level(), world_.ceiling_level(), view_top, printed_score_);
    if (lives_ > 0) masks_ = painter_.depth_masks(cells_, direction_, world_.floor_level(), world_.ceiling_level());
}

Creatures::Scene LandscapeStage::creature_scene() const {
    Creatures::Scene scene;
    scene.walker_x = walker_.screen_x;
    scene.walker_y = walker_.screen_y;
    scene.head = a_.s32(form_heads + std::uint32_t(walker_.form * 4));
    scene.stage = stage_;
    scene.masks = &masks_;
    return scene;
}

// $205A36 / $205ACA: the window is c - 32 .. c + 31 each way (wrapping) and
// starts empty.
void LandscapeStage::centre_map(std::uint16_t x, std::uint16_t y) {
    map_centre_x_ = x;
    map_centre_y_ = y;
    map_x0_ = (x - 0x20) & 0xFFF;
    map_x1_ = (x + 0x1F) & 0xFFF;
    map_wrap_x_ = map_x1_ < map_x0_;
    map_y0_ = (y - 0x20) & 0xFFF;
    map_y1_ = (y + 0x1F) & 0xFFF;
    map_wrap_y_ = map_y1_ < map_y0_;
    map_.fill(0);
}

bool LandscapeStage::in_map(std::uint16_t x, std::uint16_t y) const {
    const auto inside = [](unsigned v, unsigned lo, unsigned hi, bool wrap) {
        return wrap ? (lo <= v || v <= hi) : (lo <= v && v <= hi);
    };
    return inside(x, map_x0_, map_x1_, map_wrap_x_) && inside(y, map_y0_, map_y1_, map_wrap_y_);
}

bool LandscapeStage::visited(std::uint16_t x, std::uint16_t y) const {
    if (!in_map(x, y)) return false;
    const unsigned col = (x - map_x0_) & 63, row = (y - map_y0_) & 63;
    return (map_[row * 8 + col / 8] >> (7 - col % 8) & 1) != 0;
}

// $20596A: a cell may join the map when a neighbour is on it or is the map's
// centre.
bool LandscapeStage::may_mark() const {
    for (std::uint32_t k = 0; k < 8; ++k) {
        const auto nx = std::uint16_t((a_.s32(neighbour_dx + k * 4) + walker_.cell_x) & 0xFFF);
        const auto ny = std::uint16_t((a_.s32(neighbour_dy + k * 4) + walker_.cell_y) & 0xFFF);
        if (visited(nx, ny) || (nx == map_centre_x_ && ny == map_centre_y_)) return true;
    }
    return false;
}

void LandscapeStage::frame(const InputReader& read, int clicked) {
    // Gadget messages wait in the window's port until the loop reads it.
    if (clicked >= 0) queued_gadget_ = clicked;
    if (interlude_.active()) {
        interlude_.advance();
        return;
    }
    if (out_of_lives_ || finished_) return; // the main loop has ended
    if (map_view_.open() && map_view_.busy()) { // drawing squares: no input read
        map_view_.work();
        return;
    }
    if (map_view_.open() && map_frames_left_ > 1) { // the map's turn waits two frames
        --map_frames_left_;
        return;
    }
    if (map_closing_ > 0) { // $200D02's work: the map stays on screen
        --map_closing_;
        return;
    }
    // $203F58: a new press of the left button makes the stage wait (or, on
    // the map, is the map's click, $204250); $203FD0: a gadget message ends
    // the wait and is handled.
    const auto input = read(InputRead::button);
    const bool button = input.button;
    if (button != button_was_down_) {
        if (button && frozen_ == 0 && !map_view_.open()) frozen_ = click_wait;
        if (map_view_.open()) map_view_.button(button, input.pointer_x, input.pointer_y);
    }
    button_was_down_ = button;
    const int gadget = queued_gadget_;
    queued_gadget_ = -1;
    if (gadget >= 0) {
        frozen_ = 0;
        if (gadget < 4 && !map_view_.open()) face(gadget);
        if (gadget == map_gadget) toggle_map();
    }
    if (map_view_.open()) { // $200D92 instead of the walker's frame
        map_frames_left_ = map_view_.turn(input.pointer_x, input.pointer_y);
        return;
    }
    if (frozen_ > 0) {
        --frozen_;
        return;
    }
    follow_height(read);
    creatures_tick();
}

// The emblem ($2042A0): $20071C shows the map (the walker and creatures
// hidden, the cells' objects and creature places kept, $h5); $200D02
// closes it: the view built again (from the colours the map's building
// left), its objects and creature places restored, the tether layer
// cleared, the view drawn and the creatures shown.
void LandscapeStage::toggle_map() {
    if (!map_view_.open()) {
        map_palette_ = colours_;
        map_view_.show(view_canvas_, view_x_, view_y_, [this](std::uint16_t x, std::uint16_t y) {
            auto in = inputs_;
            in.previous_x = view_x_;
            in.previous_y = view_y_;
            auto cells = build_view(world_, a_, x, y, in, map_palette_);
            map_palette_ = cells.colours;
            return cells;
        });
        creatures_.hide_all(creature_scene());
        creatures_.wipe_layer(); // $20071C's RectFill clears all five planes, the tethers' too
        return;
    }
    map_view_.close();
    map_closing_ = map_close_frames;
    const auto objects = cells_.objects;
    const int places = cells_.creature_places;
    colours_ = map_palette_;
    rebuild_view();
    cells_.objects = objects;
    cells_.creature_places = places;
    creatures_.wipe_layer();
    repaint();
    creatures_.show_present(creature_scene());
    place_sprite();
}

// $2003C0 (each frame the walker moves): colour 13 flashes while the view
// holds something of note, tethers are cleared and redrawn now and then, and
// the creatures move.
void LandscapeStage::creatures_tick() {
    if (cells_.landmarks != 0 || cells_.beacon || cells_.creature_places != 0 || cells_.find) {
        ++flash_;
        const std::uint16_t grey = (flash_ & 4) ? 8 : (flash_ & 8) ? 0xF : 0;
        colours_[13] = std::uint16_t(grey << 8 | grey << 4 | grey);
    }
    creatures_.tick_tethers();
    if (creatures_.update(creature_scene())) caught_ = true;
}

// $209D8A's other branch, the frame after a catch: every sprite hidden, the
// views forgotten, the damage counted (h14, whose picture is not rebuilt);
// 1600 costs a life and empties the view of creatures, otherwise they return.
void LandscapeStage::caught() {
    auto scene = creature_scene();
    creatures_.hide_all(scene);
    creatures_.forget_views();
    damage_ += a_.s32(catch_damage + std::uint32_t(std::min(stage_ / 7, 5) * 4));
    const Display before = current_display();
    const int damage = damage_;
    creatures_.clear_tethers();
    const bool lost = damage_ / 100 >= damage_per_life / 100;
    if (lost) {
        damage_ = 0;
        if (--lives_ == 0) out_of_lives_ = true;
        bar_.set(lives_, direction_, stage_);
    }
    repaint();
    Display next = current_display();
    std::fill(next.plane4.begin(), next.plane4.end(), 0);
    interlude_.start_catch(before, damage, lost, next);
    if (lost) {
        creatures_.reset();
        creatures_.clear_presence(scene);
        creatures_.record_view();
    } else {
        creatures_.show_present(scene);
    }
    caught_ = false;
}

void LandscapeStage::face(int direction) {
    direction_ = direction & 3;
    bar_.set(lives_, direction_, stage_);
    repaint();
    place_sprite();
}

// $209D8A: on its cell's level the walker steps; otherwise its level closes in
// on the cell's by halving the distance twice a frame (snapping when the
// halving no longer moves it), and the cell is taken on arrival.
void LandscapeStage::follow_height(const InputReader& read) {
    walker_.column = walker_.x / 80;
    walker_.row = walker_.z / 80;
    const int target = walker_.next_terrain & 0xFF;
    const bool water = (walker_.next_terrain & 0xC000) == 0xC000 && (walker_.next_terrain & 0x100) == 0;
    if (!water && walker_.form == 1 && walker_.frame > 0x1B) walker_.frame = 0x18;
    if (caught_) {
        walker_.in_water = water;
        caught();
    } else if (walker_.level == target) {
        walker_.in_water = water;
        step(read);
    } else {
        int halvings = 2; // $206B82, and 2 when going down
        if (walker_.form == 1 && walker_.in_water && water) halvings = 0;
        int level = target;
        for (int k = 0; k < halvings; ++k) level = (level + walker_.level) >> 1;
        // (Water only counts once the walker was already in it.)
        walker_.in_water = walker_.in_water && water;
        if (level == walker_.level || halvings == 0) {
            walker_.level = target;
            take_cell();
        } else {
            walker_.level = level;
        }
    }
    place_sprite();
}

// $20AB4E.
void LandscapeStage::take_cell() {
    walker_.cell_x = walker_.next_cell_x;
    walker_.cell_y = walker_.next_cell_y;
    walker_.x = walker_.next_x;
    walker_.z = walker_.next_z;
    walker_.terrain = walker_.next_terrain;
    walker_.objects = walker_.next_objects;
    walker_.column = walker_.next_column;
    walker_.row = walker_.next_row;
}

// $20A45C (without fire): cell events when the walker's cell changed, the
// stick turned to face the view, the frame (idle fidgets or a walking
// cycle), the speed (2, growing by one every 6 frames held in the same
// direction up to 4; 4 in water), the move, shifting the view by 4 cells
// when the walker leaves it, and the new cell, taken unless it is higher.
void LandscapeStage::step(const InputReader& read) {
    if (walker_.column != walker_.last_column || walker_.row != walker_.last_row) cell_events();
    const auto stick = read(InputRead::stick);
    const bool fire = read(InputRead::fire).fire;
    int sx = 0, sz = stick.up ? 1 : 0;
    if (stick.down) sz = -1;
    if (stick.left) sx = -1;
    if (stick.right) sx = 1;
    int dx = 0, dz = 0;
    switch (direction_) {
    case 0: dz = sz; dx = sx; break;
    case 1: dx = sz; dz = -sx; break;
    case 2: dz = -sz; dx = -sx; break;
    default: dx = -sz; dz = sx; break;
    }
    if (fire) {
        aim(sx, sz);
        return;
    }
    if (walker_.aiming) aim_colours_ = false; // LoadRGB4 of $2004AC: colours 19-31 back
    walker_.aiming = false;

    const int limit = speed_limit_base + creatures_.attached(); // attached creatures speed the walker up
    if (dx == 0 && dz == 0) {
        if (walker_.frame < 0x18 || (++walker_.frame_counter & 0x1F) == 0)
            walker_.frame = walker_.frame == 0x18 ? (fidget_.next() & 3) + 0x18 : 0x18;
    } else {
        const int shift = 3 - (walker_.speed >> 1 & 3);
        if (walker_.form == 2) {
            // $20AC7C: eight frames per side.
            walker_.frame_base = sx == 0 ? 0 : sx < 0 ? 0x10 : 8;
            walker_.frame = (++walker_.frame_counter >> shift & 7) + walker_.frame_base;
        } else if (!walker_.in_water || walker_.form != 1) {
            // $20ABB6: four frames each for up, down, the sides and the diagonals.
            if (sx == 0) walker_.frame_base = sz < 0 ? 8 : 0xC;
            else if (sz < 1) walker_.frame_base = sx < 0 ? 4 : 0;
            else walker_.frame_base = sx < 0 ? 0x14 : 0x10;
            walker_.frame = (++walker_.frame_counter >> shift & 3) + walker_.frame_base;
        } else {
            // $20AC24: swimming, eight frames.
            int s = shift + 1;
            if (sx == 0) walker_.frame_base = sz < 0 ? 0x2C : 0x34;
            else {
                ++s;
                walker_.frame_base = sx < 0 ? 0x24 : 0x1C;
            }
            walker_.frame = (++walker_.frame_counter >> s & 7) + walker_.frame_base;
        }
    }
    if (!walker_.in_water) {
        if (dx == walker_.last_dx && dz == walker_.last_dz) {
            if (speed_period < ++walker_.speed_counter) {
                walker_.speed_counter = 0;
                ++walker_.speed;
            }
            walker_.speed = std::min(walker_.speed, limit);
        } else {
            walker_.last_dx = dx;
            walker_.last_dz = dz;
            walker_.speed_counter = 2;
            walker_.speed = 2;
        }
    } else {
        walker_.speed = water_speed;
        walker_.speed_counter = 0;
    }
    dx *= walker_.speed;
    dz *= walker_.speed;
    int x = dx + walker_.x, z = dz + walker_.z;
    bool new_view = false;
    if (cells_.creature_places == 0) {
        // Leaving the window sideways from a row other than the front one
        // re-anchors the view on the walker ($20B18A); then the window
        // shifts by 4 cells when the walker leaves it ($20ACFA/$20AD78).
        const int across = (direction_ & 1) ? z : x;
        if ((across < 0 || across > 319) && walker_.view_row != 0) {
            anchor(direction_);
            new_view = true;
            x = dx + walker_.x;
            z = dz + walker_.z;
        }
        const auto wrap = [&](int v, std::uint16_t& origin) {
            if (unsigned(v) > 319) new_view = true;
            if (v < 0) {
                origin = std::uint16_t((origin - 4) & 0xFFF);
                return 319;
            }
            if (v > 319) {
                origin = std::uint16_t((origin + 4) & 0xFFF);
                return 0;
            }
            return v;
        };
        x = wrap(x, view_x_);
        z = wrap(z, view_y_);
        if (new_view) rebuild_view();
    } else {
        // With creatures in view the walker stays inside it ($20ACC4).
        x = std::clamp(x, 0, 319);
        z = std::clamp(z, 0, 319);
    }
    walker_.next_x = x;
    walker_.next_z = z;
    walker_.next_column = x / 80;
    walker_.next_row = z / 80;
    walker_.next_cell_x = std::uint16_t((walker_.next_column + view_x_) & 0xFFF);
    walker_.next_cell_y = std::uint16_t((walker_.next_row + view_y_) & 0xFFF);
    walker_.next_terrain = cells_.terrain[std::size_t(walker_.next_column)][std::size_t(walker_.next_row)];
    walker_.next_objects = cells_.objects[std::size_t(walker_.next_column)][std::size_t(walker_.next_row)];
    walker_.next_mix = cells_.mix[std::size_t(walker_.next_column)][std::size_t(walker_.next_row)];
    if (walker_.level <= (walker_.next_terrain & 0xFF) || new_view) take_cell();
    if (new_view) {
        // $206544 / $2070A8: the last view's creature bonus, then the new
        // view's creatures, from the platform block at the view's middle.
        score_ += std::uint32_t(creatures_.score_bonus());
        const auto mx = std::uint16_t((view_x_ + 2) & 0xFFF), my = std::uint16_t((view_y_ + 2) & 0xFFF);
        const auto block = world_.platform(mx, my);
        // $2093B0: how far the middle is from its block's centre (u, v each 40 in 64).
        const int distance = std::abs(((mx + my) & 0x3F) - 0x28) + std::abs(((mx - my) & 0x3F) - 0x28);
        creatures_.enter_view(view_x_, view_y_, cells_.creature_places, block.height, distance, block.marked,
                              creature_scene());
        repaint();
    }
}

// $20A45C with fire held: the walker stands, the cursor (starting at the
// walker) moves 15 pixels a frame with the stick, the walker faces it, a
// bolt joins them, and a creature at the cursor is zapped.
void LandscapeStage::aim(int sx, int sz) {
    if (!walker_.aiming) {
        walker_.aiming = true;
        aim_colours_ = true;
        walker_.cursor_x = walker_.screen_x;
        walker_.cursor_y = walker_.screen_y;
    }
    walker_.cursor_x = std::clamp(walker_.cursor_x + sx * 15, 0, 319);
    walker_.cursor_y = std::clamp(walker_.cursor_y - sz * 15, 0, 199);
    const int dx = walker_.cursor_x - walker_.screen_x, dy = walker_.cursor_y - walker_.screen_y;
    if (walker_.form == 2) walker_.frame = 5;
    else if (std::abs(dy) < std::abs(dx)) walker_.frame = dx < 0 ? 4 : 0;
    else walker_.frame = dy < 0 ? 0xC : 8;
    place_sprite();
    // $20578A: the walker's point again (its level, no water), the bolt from
    // 10 rows above it ($2061BA), then its sprite's left edge ($209D4E).
    int depth = 0, across = 0;
    switch (direction_) {
    case 0: depth = walker_.z; across = walker_.x - 160; break;
    case 1: depth = walker_.x; across = 160 - walker_.z; break;
    case 2: depth = 319 - walker_.z; across = 160 - walker_.x; break;
    default: depth = 319 - walker_.x; across = walker_.z - 160; break;
    }
    const auto point = FixedCamera().project(across, walker_.level, depth + 0x18);
    creatures_.hasten();
    bolt_.draw(creatures_.layer(), point.x, std::max(point.y - 10, 0), walker_.cursor_x, walker_.cursor_y);
    walker_.screen_x = std::clamp(point.x - 8, 0, 0x12F);
    walker_.screen_y = point.y;
    creatures_.zap(walker_.cursor_x, walker_.cursor_y, creature_scene());
}

// $20B18A: the view moved so that the walker's cell is in its front row,
// facing `direction`.
void LandscapeStage::anchor(int direction) {
    switch (direction & 3) {
    case 0: view_y_ = walker_.cell_y; walker_.z = 40; break;
    case 1: view_x_ = walker_.cell_x; walker_.x = 40; break;
    case 2: view_y_ = std::uint16_t((walker_.cell_y - 3) & 0xFFF); walker_.z = 280; break;
    default: view_x_ = std::uint16_t((walker_.cell_x - 3) & 0xFFF); walker_.x = 280; break;
    }
    rebuild_view();
}

// $209F34: what entering a cell does. Rebuilt: the marked block's ramp ends
// the stage, landmarks re-centre the map, beacons mark their block, and a
// cell of the form's own surface joins the map (one point) when a neighbour
// is on it. Not yet: finds, creatures.
void LandscapeStage::cell_events() {
    walker_.last_column = walker_.column;
    walker_.last_row = walker_.row;
    const auto t = walker_.terrain;
    if ((t & 0x100) && (t & 0x800) && !beacon_taken_) finished_ = true;
    if ((t & 0x400) && (walker_.cell_x != map_centre_x_ || walker_.cell_y != map_centre_y_)) {
        centre_map(walker_.cell_x, walker_.cell_y);
        rebuild_view();
        repaint();
    }
    if ((walker_.objects & 2) && !(t & 0x100) && may_mark()) {
        // The beacon marks its block and becomes home ($209376, event 0).
        home_x_ = walker_.cell_x;
        home_y_ = walker_.cell_y;
        inputs_.home_x = home_x_;
        inputs_.home_y = home_y_;
        const int u = (walker_.cell_x + walker_.cell_y) & 0xFFF, v = (walker_.cell_x - walker_.cell_y) & 0xFFF;
        world_.marked_u = std::uint16_t(u >> 6);
        world_.marked_v = std::uint16_t(v >> 6);
        beacon_taken_ = false;
        for (auto& column : cells_.terrain)
            for (auto& cell : column) cell |= 0x100;
        walker_.terrain |= 0x100;
        repaint();
    }
    const int form_kind = a_.u16(form_kinds + std::uint32_t(walker_.form * 2));
    if (!(walker_.objects & 1) && !(walker_.terrain & 0x1D00) && (walker_.terrain & 0xC000) == form_kind &&
        may_mark() && in_map(walker_.cell_x, walker_.cell_y)) {
        // $205B90: on the map; $206544: a point; $20A12C: its place, facing.
        const unsigned col = (walker_.cell_x - map_x0_) & 63, row = (walker_.cell_y - map_y0_) & 63;
        map_[row * 8 + col / 8] |= std::uint8_t(0x80 >> (col % 8));
        ++score_;
        fidget_.next(); // $209F34 draws a random sound length ($20C89C)
        switch (direction_) {
        case 0: walker_.view_row = walker_.row; walker_.view_column = walker_.column; break;
        case 1: walker_.view_row = walker_.column; walker_.view_column = 3 - walker_.row; break;
        case 2: walker_.view_row = 3 - walker_.row; walker_.view_column = 3 - walker_.column; break;
        default: walker_.view_row = 3 - walker_.column; walker_.view_column = walker_.row; break;
        }
        auto& objects = cells_.objects[std::size_t(walker_.column)][std::size_t(walker_.row)];
        objects |= 1;
        walker_.objects = objects;
        repaint(false); // $204F48 draws only the cell's marker: no new score, no colours
    }
}

// $209B54 / $2057D0: the walker's place turned to the view, projected; feet
// on the projected point (in water at the floor level, shortened).
void LandscapeStage::place_sprite() {
    int depth = 0, across = 0;
    switch (direction_) {
    case 0: depth = walker_.z; across = walker_.x - 160; break;
    case 1: depth = walker_.x; across = 160 - walker_.z; break;
    case 2: depth = 319 - walker_.z; across = 160 - walker_.x; break;
    default: depth = 319 - walker_.x; across = walker_.z - 160; break;
    }
    // $2057D0: its row and column facing the view.
    walker_.view_row = depth / 80;
    walker_.view_column = (across + 160) / 80;
    depth += 0x18;
    walker_.depth = depth;
    const FixedCamera camera;
    const int level = walker_.in_water ? world_.floor_level() : walker_.level;
    const auto p = camera.project(across, level, depth);
    walker_.screen_x = p.x;
    walker_.screen_y = p.y;
    int rows = walker_.height;
    if (walker_.in_water) {
        if (walker_.form == 1) {
            rows = 6;
        } else {
            rows = std::min(rows, walker_.height - (walker_.level - world_.floor_level()));
        }
        walker_.screen_y += walker_.height - rows;
    }
    walker_.sprite_rows = std::max(rows, 0);
    // $209D4E: the sprite's left edge, 8 left of the point, kept on screen.
    if (walker_.sprite_rows < 1) return;
    walker_.screen_x = std::clamp(walker_.screen_x - 8, 0, 0x12F);
    // h11: the sprite masked by the scenery in front of it; wholly hidden
    // for more than 8 placings, the marker shows instead ($209D04).
    if (lives_ <= 0) return;
    ViewPainter::WalkerPlace place;
    place.x = walker_.screen_x;
    place.top = std::max(walker_.screen_y - walker_.height, 0);
    place.rows = walker_.height;
    place.feet = walker_.screen_y;
    place.view_row = walker_.view_row;
    place.view_column = walker_.view_column;
    place.depth = walker_.depth;
    place.terrain = walker_.terrain;
    place.beacon_taken = beacon_taken_;
    walker_.visible = painter_.walker_mask(cells_, direction_, world_.floor_level(), place, masks_);
    const auto& bitmap = walker_bitmap();
    bool any = false;
    const int top = walker_.screen_y - walker_.height;
    for (int y = 0; y < std::min(walker_.sprite_rows, bitmap.height); ++y) {
        const int r = top + y - place.top;
        if (r < 0 || r >= int(walker_.visible.size())) continue;
        for (int x = 0; x < 16; ++x)
            any |= bitmap.at(x, y) != 0 && (walker_.visible[std::size_t(r)] >> (15 - x) & 1);
    }
    walker_.hidden_frames = any ? 0 : walker_.hidden_frames + 1;
}

const data::Bitmap& LandscapeStage::walker_bitmap() const {
    const auto& set = assets_.figures[std::size_t(form_figure_set[walker_.form])];
    return set.frames[std::size_t(set.sequence[std::size_t(walker_.frame)])];
}

platform::Image LandscapeStage::picture() const {
    std::vector<screen::Rgb4> colours;
    if (map_view_.open() || map_closing_ > 0) {
        // The map: its colours 0-29, no sprites (and no tethers: opening
        // cleared their plane); the emblem complemented (GADGHCOMP, selected).
        for (const auto c : map_view_.colours()) colours.push_back(c & 0xFFF);
        colours.push_back(colours_[30] & 0xFFF);
        colours.push_back(colours_[31] & 0xFFF);
        auto planes = map_view_.planes();
        if (map_view_.open()) // (hypothesis: Intuition's GADGHCOMP; a dump cannot show it)
            for (int y = 0; y < view_top; ++y)
                for (int x = 288; x < 320; ++x) planes.plot(x, y, planes.pen(x, y) ^ 31);
        return planes.picture(colours);
    }
    if (interlude_.active()) {
        // A sequence: its display and the walker (the creatures are hidden).
        const auto& d = interlude_.display();
        for (const auto c : d.colours) colours.push_back(c & 0xFFF);
        auto image = d.planes.picture(colours);
        for (int y = view_top; y < 200; ++y)
            for (int x = 0; x < 320; ++x)
                if (d.plane4[std::size_t(y * 320 + x)])
                    image.put(x, y, screen::to_color(colours[16u | d.planes.pen(x, y)]));
        draw_walker(image, colours);
        return image;
    }
    for (const auto c : colours_) colours.push_back(c & 0xFFF);
    if (aim_colours_) // SetRGB4 at the aim's start: colours 19-31 lit (F, i - 16, F)
        for (unsigned i = 19; i < 32; ++i) colours[i] = screen::Rgb4(0xF0F | (i - 16) << 4);
    auto image = view_canvas_.picture(colours);
    // The tether layer: display plane 4 over the view (pens + 16).
    const auto& tethers = creatures_.tethers();
    for (int y = view_top; y < 200; ++y)
        for (int x = 0; x < 320; ++x)
            if (tethers[std::size_t(y * 320 + x)])
                image.put(x, y, screen::to_color(colours[16u | view_canvas_.pen(x, y)]));
    // The creatures: attached sprite pairs in colours 16-31, masked by the
    // rows they fly behind.
    for (const auto& c : creatures_.all()) {
        if (c.kind < 0 || c.shown_x <= -16 || c.shown_x >= 320) continue;
        const auto& a = creatures_.image_a(c.kind);
        const auto& b = creatures_.image_b(c.kind);
        for (int y = 0; y < a.height; ++y) {
            const int sy = c.shown_y + view_top + y;
            if (sy < view_top || sy >= 200) continue;
            for (int x = 0; x < 16; ++x) {
                const int pen = b.at(x, y) << 2 | a.at(x, y);
                if (pen == 0 || !(c.visible[std::size_t(y)] >> (15 - x) & 1)) continue;
                image.put(c.shown_x + x, sy, screen::to_color(colours[16u + unsigned(pen)]));
            }
        }
    }
    draw_walker(image, colours);
    return image;
}

void LandscapeStage::draw_walker(platform::Image& image, const std::vector<screen::Rgb4>& colours) const {
    // The walker: a hardware sprite in colours 17-19, its feet on its screen
    // point, masked by the scenery in front (h11). Hidden for more than 8
    // placings, its first 8 rows show the marker ($20B2B4, unmasked).
    const auto rows = walker_sprite();
    const int top = walker_.screen_y - walker_.height + view_top;
    for (std::size_t y = 0; y < rows.size(); ++y)
        for (int x = 0; x < 16; ++x) {
            const unsigned v = (rows[y] >> (31 - x) & 1) | (rows[y] >> (15 - x) & 1) << 1;
            if (v) image.put(walker_.screen_x + x, top + int(y), screen::to_color(colours[16u + v]));
        }
}

std::vector<std::uint32_t> LandscapeStage::walker_sprite() const {
    std::vector<std::uint32_t> out;
    const auto& bitmap = walker_bitmap();
    const int top = walker_.screen_y - walker_.height;
    const int mask_top = std::max(top, 0);
    for (int y = 0; y < std::min(walker_.sprite_rows, bitmap.height); ++y) {
        const int r = top + y - mask_top;
        const std::uint16_t shown = r >= 0 && r < int(walker_.visible.size()) ? walker_.visible[std::size_t(r)] : 0xFFFF;
        std::uint16_t a = 0, b = 0;
        for (int x = 0; x < 16; ++x) {
            const int v = bitmap.at(x, y);
            a = std::uint16_t(a << 1 | (v & 1));
            b = std::uint16_t(b << 1 | (v >> 1 & 1));
        }
        a &= shown;
        b &= shown;
        if (walker_.hidden_frames > 8 && y < 8) {
            a = a_.u16(walker_marker + std::uint32_t(y * 4));
            b = a_.u16(walker_marker + std::uint32_t(y * 4 + 2));
        }
        out.push_back(std::uint32_t(a) << 16 | b);
    }
    return out;
}

} // namespace mindwalker::landscape
