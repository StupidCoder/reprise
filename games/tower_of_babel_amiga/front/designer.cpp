#include "tower_of_babel_amiga/front/front_end.hpp"
#include <algorithm>

// The tower designer ($E386-$110D2): the screen, its four menus and pages.
// The editing rules and the tower's packing are in design/tower_design.
namespace babel::front {
namespace {

constexpr std::uint32_t designer_palette = var(0x2474); // 48 colours (colour 10: the brightness option)
constexpr std::uint32_t designer_text = var(0x5334);
constexpr std::uint32_t designer_areas = var(0x3B78);
constexpr std::uint32_t object_icons = 0x1C7D0;     // main data: 16 x 16 icons, $A0 bytes each (+$2A per floor below)
constexpr std::uint32_t plan_icons = 0x18200 + 0xAED0; // main data: 8 x 8 icons, 40 bytes each
constexpr std::uint32_t support_mark = 0x23760, path_marks = 0x23788; // main data: the checks' 8 x 8 marks
constexpr std::uint32_t palette_min = var(0xDB8 + 0x18), palette_max = var(0xDB8 + 0x30);
constexpr std::uint32_t no_password = var(0x53B8);
constexpr std::uint32_t spider_marks = 0x10CC4; // three (x, y) for the Z P G marks

// File menu.
constexpr std::uint32_t file_text = var(0x53C1), file_areas = var(0x3BE8);
constexpr std::uint32_t get_text = var(0x5497), put_text = var(0x379F), swap_text = var(0x37BF);
constexpr std::uint32_t get_areas = var(0x3C76), put_areas = var(0x3CDC), swap_areas = var(0x3D42);
constexpr std::uint32_t get_question = var(0x378E), put_question = var(0x37AE), swap_question = var(0x37CF);
constexpr std::uint32_t name_window_text = var(0x58BB), locked_text = var(0x37E1);
constexpr std::uint32_t correct_text = var(0x587B), incorrect_text = var(0x5891);
constexpr std::uint32_t lock_text = var(0x54A6), exit_question = var(0x5868);
constexpr std::uint32_t load_group_title = var(0x54DA), save_group_title = var(0x5540), load_tower_title = var(0x550D);
constexpr std::uint32_t save_tower_title = var(0x5573), delete_group_title = var(0x55A6), delete_tower_title = var(0x55DF);
constexpr std::uint32_t prepare_text = var(0x5764), prepare_ok = var(0x57BE), erase_question = var(0x57C6);
constexpr std::uint32_t writing_text = var(0x57E5), ready_text = var(0x57FC), ok_areas = var(0x3B62);
// Options.
constexpr std::uint32_t options_text = var(0x58D0), options_areas = var(0x3E60);
constexpr std::uint32_t effects_text = var(0x5950), effects_areas = var(0x3EA8);
constexpr std::uint32_t sound_text = var(0x5A63), sound_areas = var(0x3F68);
constexpr std::uint32_t moon_text = var(0x5B05), moon_areas = var(0x3FC4);
constexpr std::uint32_t behaviour_text = var(0x5BB5), behaviour_areas = var(0x4066);
constexpr std::uint32_t finishing_text = var(0x5C41), finishing_areas = var(0x40B8), no_limit_text = var(0x5D2B);
constexpr std::uint32_t name_text = var(0x5D30), author_text = var(0x5D46);
// Toolbox.
constexpr std::uint32_t toolbox_text = var(0x5D60), toolbox_areas = var(0x4170);
constexpr std::uint32_t colours_text = var(0x5DE3), colours_areas = var(0x41C2);
constexpr std::uint32_t presets_text = var(0x5E5E), presets_areas = var(0x4200), presets = var(0x263A);
constexpr std::uint32_t step_text = var(0x5F40), step_areas = var(0x4298);
constexpr std::uint32_t scroll_text = var(0x601E), scroll_areas = var(0x4312);
constexpr std::uint32_t rotate_text = var(0x609F), rotate_areas = var(0x435A);
constexpr std::uint32_t copy_text = var(0x6126), copy_to = var(0x614D), copy_areas = var(0x4398);
constexpr std::uint32_t swap_level_text = var(0x6173), swap_with = var(0x6195), swap_level_areas = var(0x43C2);
constexpr std::uint32_t clear_level_question = var(0x61BD), clear_all_question = var(0x61CE);
// Extras.
constexpr std::uint32_t extras_text = var(0x61DD), extras_areas = var(0x43EC);
constexpr std::uint32_t setup_text = var(0x625E), setup_areas = var(0x443E);
constexpr std::uint32_t statistics_text = var(0x631B), statistics_areas = var(0x44AE);
constexpr std::uint32_t support_text = var(0x6388), support_areas = var(0x44BA);
constexpr std::uint32_t path_text = var(0x63A0), not_found = var(0x63B7), path_areas = var(0x44C6);
constexpr std::uint32_t scaling_text = var(0x63C1), scaling_areas = var(0x44D2), style_text = var(0x63E9), style_areas = var(0x44FC);

constexpr std::uint32_t fixed_colours = 0x80073FFF; // bit 31 - n set: colour n cannot be changed
constexpr std::uint32_t steppable = 0x31FFE;        // colours the steps change (bit n)

int message_for(design::Message m) { return 0x2D + int(m); }

} // namespace

// --- packing --------------------------------------------------------------

void FrontEnd::unpack_design(const std::array<std::uint8_t, data::tower_record_size>& record) {
    design_ = design::TowerDesign::from_record(record);
    time_text_ = std::to_string(design_.time_limit); // $11C64
}

std::array<std::uint8_t, data::tower_record_size> FrontEnd::pack_design() const {
    // $112C0: up to five digits of the typed time.
    auto d = design_;
    int t = 0;
    for (std::size_t i = 0; i < time_text_.size() && i < 5 && time_text_[i] >= '0' && time_text_[i] <= '9'; ++i)
        t = t * 10 + (time_text_[i] - '0');
    d.time_limit = t & 0x3FFF;
    std::array<std::uint8_t, data::tower_record_size> r{};
    d.to_record(r);
    return r;
}

void FrontEnd::snapshot() {
    undo_ = pack_design();
    undo_ready_ = true;
}

Flow FrontEnd::edit_field(LineEditor& e) {
    e.cursor = int(e.text.size());
    for (;;) {
        draw_editor(e);
        const int end_x = screen_.cursor.x;
        co_await next_frame();
        const int key = in_.key;
        if (e.digits && key >= 0x20 && (key < '0' || key > '9')) continue;
        if (!edit_key(e, key, end_x)) break;
        if (in_.left && !previous_.left) {
            if (in_.x >= e.box_x0 && in_.x < e.box_x1 && in_.y >= e.box_y0 && in_.y < e.box_y1) edit_click(e, in_.x);
            else break;
        }
    }
    draw_editor(e, false); // $12304 once more, the cursor gone
    co_await wait_release();
}

// --- the screen -----------------------------------------------------------

void FrontEnd::designer_screen() {
    std::array<data::Colour, 48> colours{};
    for (std::size_t i = 0; i < 48; ++i) colours[i] = assets_.program.u16(designer_palette + std::uint32_t(i) * 2);
    colours[10] = data::Colour(bright_ ? 0x777 : 0x555); // $10368
    current_ = colours;
    screen_.palette = current_;
    screen_.title_bar();
    screen_.text_at(designer_text);
    screen_.frame(0x82, 0x28, 0x13E, 0x6C, 10);
    for (int s = 0; s < 24; ++s) draw_palette_icon(s);
}

void FrontEnd::draw_palette_icon(int slot) {
    const int kind = palette_[std::size_t(slot)];
    screen_.icon16(assets_.main, object_icons + std::uint32_t(kind) * 0xA0, 0x84 + 24 * (slot & 7), 0x2A + 24 * (slot >> 3));
}

void FrontEnd::draw_palette_cursor() {
    screen_.frame(0x84 + 24 * (slot_ & 7), 0x2A + 24 * (slot_ >> 3), 0x94 + 24 * (slot_ & 7), 0x3A + 24 * (slot_ >> 3), 3);
    screen_.cursor.x = 0x80;
    screen_.cursor.y = 0x72;
    screen_.message(palette_[std::size_t(slot_)]);
    screen_.fill(screen_.cursor.x, 0x72, 0x13F, 0x79, 0);
}

void FrontEnd::designer_message(int message) {
    screen_.cursor.x = 0x80;
    screen_.cursor.y = 0x7D;
    screen_.message(message);
    screen_.fill(screen_.cursor.x, 0x7D, 0x13F, 0x84, 0);
}

void FrontEnd::draw_plans() {
    // $10E44: four 64 x 64 plans, row 0 at the bottom, 8 x 8 icons.
    for (int x0 = 0x10; x0 < 0x101; x0 += 0x50) screen_.fill(x0, 0x88, x0 + 0x3F, 0xC7, 10);
    for (int f = 0; f < 4; ++f)
        for (int z = 0; z < 8; ++z)
            for (int x = 0; x < 8; ++x) {
                const int i = design::TowerDesign::index(f, x, z);
                const int px = 0x10 + 0x50 * f + 8 * x, py = 0xC0 - 8 * z;
                for (const int k : {int(design_.cells[std::size_t(i)]), int(design_.objects[std::size_t(i)])}) {
                    if (k < 0) break;
                    screen_.icon8(assets_.main, plan_icons + std::uint32_t(k) * 40, px, py);
                }
            }
}

void FrontEnd::draw_view_window() {
    // $10EFA: the floors up to the chosen one, each 3 pixels further up and
    // left, 16 x 16 icons every 14 pixels; with depth cue the lower floors
    // in their darker icons (+$2A a floor), the chosen one framed.
    screen_.fill(0, 0x0C, 0x7A, 0x86, 0);
    for (int f = 0; f <= floor_; ++f) {
        const int x0 = 9 - 3 * f, y0 = 12 + x0;
        if (f == floor_) screen_.frame(x0 + 1, y0 + 2, x0 + 0x71, y0 + 0x71, 10);
        else if (!depth_cue_) continue;
        const int shade = (floor_ - f) * 0x2A;
        for (int row = 0; row < 8; ++row)
            for (int x = 0; x < 8; ++x) {
                const int i = design::TowerDesign::index(f, x, 7 - row);
                for (const int k : {int(design_.cells[std::size_t(i)]), int(design_.objects[std::size_t(i)])}) {
                    if (k < 0) continue;
                    screen_.icon16(assets_.main, object_icons + std::uint32_t(k + shade) * 0xA0, x0 + 14 * x, y0 + 14 * row);
                }
            }
    }
}

void FrontEnd::draw_password() {
    screen_.fill(0x80, 0x1C, 0xC8, 0x23, 0);
    screen_.cursor.x = 0x80;
    screen_.cursor.y = 0x1C;
    if (!design_.flags[3]) screen_.text(design::TowerDesign::text(design_.password)); // locked: the password shows
    else screen_.text_at(no_password);
}

void FrontEnd::draw_spiders() {
    design_.update_spiders();
    for (int s = 0; s < 3; ++s) {
        screen_.cursor.x = assets_.program.u16(spider_marks + std::uint32_t(s) * 4);
        screen_.cursor.y = assets_.program.u16(spider_marks + std::uint32_t(s) * 4 + 2);
        screen_.glyph(design_.flags[std::size_t(s)] ? 0x83 : 0x84);
    }
}

void FrontEnd::draw_floor_boxes() {
    for (int f = 0; f < 4; ++f) screen_.frame(0x50 * f, 0x88, 0x50 * f + 0xF, 0x97, f == floor_ ? 3 : 0);
}

void FrontEnd::designer_views() {
    draw_plans();
    draw_view_window();
    draw_password();
    draw_spiders();
    draw_floor_boxes();
    draw_palette_cursor();
}

void FrontEnd::designer_edit(bool left) {
    // $1098E: the cell under the pointer in the view window; the left button
    // puts the chosen kind there, the right takes what is there.
    if (hover_x_ >= 8 || hover_y_ >= 8) return;
    const int cell = floor_ * 64 + (7 - hover_y_) * 8 + hover_x_;
    const int kind = palette_[std::size_t(slot_)];
    design_.update_spiders();
    design::Message m;
    if (left) {
        const int before = design_.cells[std::size_t(cell)];
        m = design_.place(cell, kind);
        if (m == design::ok && kind == design::floor_section && before != design::floor_section && auto_repeat_) painting_ = true;
    } else {
        m = design_.remove(cell);
    }
    designer_message(message_for(m));
}

// --- the designer's loop ---------------------------------------------------

Flow FrontEnd::designer() {
    fast_fades_ = true;
    screen_.clear();
    screen_.copper = CopperMode::split;
    if (designer_group_.empty()) {
        for (std::size_t i = 0; i < 9; ++i) {
            std::array<std::uint8_t, data::tower_record_size> r{};
            const auto b = assets_.main.bytes(0x5F512 + std::uint32_t(i) * 0x120, 0x120);
            std::copy(b.begin(), b.end(), r.begin());
            designer_group_.push_back(r);
        }
        unpack_design(designer_group_[0]);
        design_.cells.fill(-1);
        design_.objects.fill(-1);
        design::TowerDesign::set_text(design_.author, name_); // $8CD4: the player's name as author
        time_text_ = "120";                                    // $F58 as the program starts
        for (int s = 0; s < 24; ++s) palette_[std::size_t(s)] = std::int8_t(assets_.program.u8(var(0xDB8) + std::uint32_t(s)));
    }
    // The settings, flags and colours are the game's own variables: they
    // are those of the tower last unpacked (a game or a demonstration).
    if (!last_tower_) {
        // None yet: the values the program image starts with.
        const auto& p = assets_.program;
        const auto w = [&](std::uint32_t o) { return int(std::int16_t(p.u16(var(o)))); };
        auto& d = design_;
        for (std::size_t i = 0; i < 10; ++i) d.flags[i] = w(0xE1A + std::uint32_t(i) * 2) != 0;
        d.bomb_speed = w(0xE2E);
        d.klondikes = w(0xE30);
        d.destroy = w(0xE32);
        d.stars = w(0xE34);
        d.pattern = w(0xE36);
        d.sky_type = w(0xE38);
        d.moon_heading = w(0xE3A);
        d.moon_type = w(0xE3C);
        d.moon_x = w(0xE3E);
        d.moon_y = w(0xE40);
        d.moon_rotation = w(0xE42);
        d.background = w(0xA4);
        d.no_footsteps = w(0xA6) != 0;
        d.no_effects = w(0xA8) != 0;
        for (std::size_t i = 0; i < 18; ++i) d.colours[i] = p.u16(var(0x2414) + std::uint32_t(i) * 2);
    } else {
        const auto& t = *last_tower_;
        auto& d = design_;
        d.flags = t.flags;
        d.klondikes = t.klondikes;
        d.destroy = t.objects_to_destroy;
        d.bomb_speed = t.settings[0];
        d.stars = t.settings[1];
        d.pattern = t.settings[2];
        d.sky_type = t.settings[3];
        d.moon_heading = t.settings[4];
        d.moon_type = t.settings[5];
        d.moon_x = t.settings[6];
        d.moon_y = t.settings[7];
        d.moon_rotation = t.settings[8];
        d.background = t.settings[9];
        d.no_footsteps = t.settings[10] != 0;
        d.no_effects = t.settings[11] != 0;
        d.colours = t.colours;
        d.colours[15] = data::Colour(d.colours[15] | 0xFFF); // $1353E
    }
    designer_screen();
    designer_views();
    Menu menu(assets_.program, designer_areas);
    bool exit = false;
    bool wait = true;
    while (!exit) {
        if (wait) co_await wait_release(); // $E400
        wait = true;
        int choice = -1;
        for (;;) {
            co_await next_frame();
            const int x = in_.pointer ? in_.x : 0x1F40, y = in_.pointer ? in_.y : 0x8000;
            menu.hover(screen_, x, y);
            choice = menu.at(x, y);
            // $10D9E: the view window's cell under the pointer, and the cursor on it.
            screen_.marker.reset();
            hover_x_ = hover_y_ = 8;
            if (choice == 10) {
                const int x0 = 9 - 3 * floor_;
                const int cx = x - x0, cy = y - 12 - x0;
                if (cx >= 0 && cy >= 0 && cx / 14 < 8 && cy / 14 < 8) {
                    hover_x_ = cx / 14;
                    hover_y_ = cy / 14;
                    screen_.marker = std::array<int, 2>{x0 + 14 * hover_x_, 12 + x0 + 14 * hover_y_};
                }
            }
            if (choice < 0) continue;
            if (choice == 10) {
                if ((in_.left || in_.right) && hover_x_ < 8 && hover_y_ < 8) break;
            } else if (in_.left) {
                break;
            }
        }
        menu.unhover(screen_);
        screen_.marker.reset();
        switch (choice) {
        case 0:
            co_await designer_file(exit);
            break;
        case 1:
            co_await designer_options();
            break;
        case 2:
            co_await designer_toolbox();
            break;
        case 3:
            co_await designer_extras();
            break;
        case 4: case 5: case 6: case 7:
            floor_ = choice - 4;
            break;
        case 8: { // the palette: a slot, or the same slot again for its next kind
            const int col = (in_.x - 0x84) / 24, row = (in_.y - 0x2A) / 24;
            const int s = row * 8 + col;
            if (in_.x < 0x84 || in_.y < 0x2A || s >= 24) break;
            screen_.frame(0x84 + 24 * (slot_ & 7), 0x2A + 24 * (slot_ >> 3), 0x94 + 24 * (slot_ & 7), 0x3A + 24 * (slot_ >> 3), 0);
            if (s == slot_) {
                int k = palette_[std::size_t(s)] + 1;
                if (k > int(assets_.program.u8(palette_max + std::uint32_t(s)))) k = assets_.program.u8(palette_min + std::uint32_t(s));
                palette_[std::size_t(s)] = std::int8_t(k);
                draw_palette_icon(s);
            }
            slot_ = s;
            draw_palette_cursor();
            continue; // (redraws only the cursor)
        }
        case 9:
            designer_message(0x2D);
            wait = false;
            continue;
        case 10:
            // While floor is being painted the button may stay down ($E0E bit 7).
            designer_edit(in_.left);
            wait = !std::exchange(painting_, false);
            break;
        default:
            break;
        }
        designer_views();
    }
    fast_fades_ = false;
}

// --- File ---------------------------------------------------------------

void FrontEnd::letter_grid(int y) {
    // $E538: the nine towers' letters in framed boxes.
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            const int x0 = 8 + 32 * c, y0 = y + 24 * r;
            screen_.frame(x0, y0, x0 + 0x17, y0 + 0x0F, 13);
            screen_.cursor.x = x0 + 9;
            screen_.cursor.y = y0 + 4;
            screen_.glyph('A' + r * 3 + c);
        }
}

void FrontEnd::tower_name_window(int slot) {
    screen_.open_window(8, 0x84, 0x137, 0xA9, 0);
    screen_.text_at(name_window_text);
    const auto t = design::TowerDesign::from_record(designer_group_[std::size_t(slot)]);
    const auto name = design::TowerDesign::text(t.name);
    screen_.centre(name);
    screen_.text(name);
}

Flow FrontEnd::choose_slot(int title_x0, int y0, int y1, std::uint32_t title, std::uint32_t areas, std::uint32_t question,
                           int& slot) {
    // Get / Put / Swap Tower: a letter, its name below, then Yes/No.
    (void)title_x0;
    screen_.open_window(0, y0, 0x67, y1, 0);
    letter_grid(y0 + 0x1A);
    screen_.text_at(title);
    slot = -1;
    for (;;) {
        int c = 0;
        co_await select(areas, c);
        if (c == 0) break;
        tower_name_window(c - 1);
        auto q = screen_.string_at(question);
        q.back() = char('A' + c - 1);
        bool yes = false;
        {
            // $110D2 with the letter put into the question.
            co_await wait_release();
            screen_.open_window(0x4C, 0x48, 0xF3, 0x7D, 0);
            screen_.text(q);
            screen_.text_at(var(0x6409));
            screen_.frame(0x8F, 0x6B, 0xB1, 0x78, 13);
            int choice = 0;
            co_await select(var(0x4526), choice);
            screen_.close_window();
            co_await wait_release();
            yes = choice == 0;
        }
        if (!yes) {
            screen_.close_window();
            continue;
        }
        slot = c - 1;
        break;
    }
}

Flow FrontEnd::tower_unlocked(const std::array<std::uint8_t, data::tower_record_size>& record, int letter, bool& allowed) {
    // $EBD2: a locked tower asks for its password.
    const auto t = design::TowerDesign::from_record(record);
    allowed = true;
    if (t.flags[3]) co_return;
    screen_.open_window(0x24, 0x54, 0xBF, 0x9D, 0);
    auto s = screen_.string_at(locked_text);
    const auto at = s.find("Tower A");
    if (at != std::string::npos) s[at + 6] = char('A' + std::max(letter, 0));
    screen_.text(s);
    LineEditor e{0x34, 0x7A, 8, 0xBE, 0x24, 0x54, 0xBF, 0x9D, {}, 0, false};
    co_await edit_field(e);
    const auto password = design::TowerDesign::text(t.password).substr(0, 8);
    allowed = e.text.substr(0, 8) == password;
    screen_.text_at(allowed ? correct_text : incorrect_text);
    co_await wait_frames(50);
    screen_.close_window();
}

Flow FrontEnd::designer_file(bool& exit) {
    screen_.open_window(0, 0, 0x5F, 0xB4, 0);
    screen_.text_at(file_text);
    for (;;) {
        int c = 0;
        co_await select(file_areas, c);
        co_await wait_release();
        bool close = true;
        switch (c) {
        case 0:
            break;
        case 1: { // Get Tower
            int s = -1;
            co_await choose_slot(0, 0x18, 0x79, get_text, get_areas, get_question, s);
            if (s >= 0) {
                bool ok = false;
                co_await tower_unlocked(designer_group_[std::size_t(s)], s, ok);
                if (ok) {
                    snapshot();
                    unpack_design(designer_group_[std::size_t(s)]);
                }
                screen_.close_window();
            }
            screen_.close_window();
            break;
        }
        case 2: { // Put Tower
            int s = -1;
            co_await choose_slot(0, 0x24, 0x85, put_text, put_areas, put_question, s);
            if (s >= 0) {
                design_.update_spiders();
                designer_group_[std::size_t(s)] = pack_design();
                screen_.close_window();
            }
            screen_.close_window();
            break;
        }
        case 3: { // Swap Tower
            int s = -1;
            co_await choose_slot(0, 0x24, 0x91, swap_text, swap_areas, swap_question, s);
            if (s >= 0) {
                bool ok = false;
                co_await tower_unlocked(designer_group_[std::size_t(s)], s, ok);
                if (ok) {
                    design_.update_spiders();
                    const auto here = pack_design();
                    unpack_design(designer_group_[std::size_t(s)]);
                    designer_group_[std::size_t(s)] = here;
                }
                screen_.close_window();
            }
            screen_.close_window();
            break;
        }
        case 4: { // Lock Tower
            screen_.open_window(0, 0x3C, 0xAB, 0x6D, 0);
            screen_.text_at(lock_text);
            LineEditor e{0x14, 0x62, 8, 0x140, 0, 0x3C, 0xAB, 0x6D, {}, 0, false};
            co_await edit_field(e);
            if (!e.text.empty()) {
                design::TowerDesign::set_text(design_.password, e.text);
                design_.flags[3] = false;
            }
            co_await wait_frames(25);
            screen_.close_window();
            break;
        }
        case 5: // Unlock Tower
            design_.flags[3] = true;
            break;
        case 6: // Load Group (the File menu stays, as with the other disk commands)
            close = false;
            co_await requester(FileKind::group, load_group_title, [&](const std::string& n, bool) {
                const auto f = disk_.read(FileKind::group, n);
                if (!f) return 6;
                if (f->size() < 9 * data::tower_record_size) return 0xB;
                for (std::size_t i = 0; i < 9; ++i) std::copy_n(f->begin() + long(i * data::tower_record_size), data::tower_record_size, designer_group_[i].begin());
                return 0;
            });
            break;
        case 7: // Save Group
            close = false;
            co_await requester(FileKind::group, save_group_title, [&](const std::string& n, bool replace) {
                if (!replace && confirm_replace_ && disk_.exists(FileKind::group, n)) return -1;
                std::vector<std::uint8_t> bytes;
                for (const auto& r : designer_group_) bytes.insert(bytes.end(), r.begin(), r.end());
                return disk_.write(FileKind::group, n, bytes) ? 0 : 8;
            });
            break;
        case 8: { // Load Tower
            close = false;
            std::array<std::uint8_t, data::tower_record_size> loaded{};
            bool got = false;
            co_await requester(FileKind::tower, load_tower_title, [&](const std::string& n, bool) {
                const auto f = disk_.read(FileKind::tower, n);
                if (!f) return 6;
                if (f->size() < data::tower_record_size) return 0xB;
                std::copy_n(f->begin(), data::tower_record_size, loaded.begin());
                got = true;
                return 0;
            });
            if (got) {
                bool ok = false;
                co_await tower_unlocked(loaded, 0, ok);
                if (ok) {
                    snapshot();
                    unpack_design(loaded);
                }
            }
            break;
        }
        case 9: // Save Tower
            close = false;
            co_await requester(FileKind::tower, save_tower_title, [&](const std::string& n, bool replace) {
                if (!replace && confirm_replace_ && disk_.exists(FileKind::tower, n)) return -1;
                design_.update_spiders();
                const auto r = pack_design();
                return disk_.write(FileKind::tower, n, {r.begin(), r.end()}) ? 0 : 8;
            });
            break;
        case 10:
        case 11: { // Delete Group / Delete Tower
            close = false;
            const auto kind = c == 10 ? FileKind::group : FileKind::tower;
            co_await requester(kind, c == 10 ? delete_group_title : delete_tower_title, [&](const std::string& n, bool) {
                return disk_.remove(kind, n) ? 0 : 6;
            });
            break;
        }
        case 12: { // Prepare Disk
            close = false;
            screen_.open_window(0x50, 0x30, 0xEF, 0x92, 0);
            screen_.text_at(prepare_text);
            screen_.text_at(prepare_ok);
            screen_.frame(0x8B, 0x83, 0xB4, 0x90, 13);
            int ok = 0;
            co_await select(ok_areas, ok);
            if (ok != 0) {
                bool yes = false;
                co_await confirm(erase_question, yes);
                if (yes) {
                    screen_.fill(0x51, 0x48, 0xEE, 0x91, 0);
                    screen_.text_at(writing_text);
                    disk_.erase();
                    screen_.text_at(ready_text);
                    screen_.text_at(prepare_ok);
                    screen_.frame(0x8B, 0x83, 0xB4, 0x90, 13);
                    co_await select(ok_areas, ok);
                }
            }
            screen_.close_window();
            break;
        }
        case 13: { // Exit Designer
            bool yes = false;
            co_await confirm(exit_question, yes);
            exit = yes;
            break;
        }
        default:
            close = false;
            break;
        }
        if (close) break;
    }
    screen_.close_window();
}

// --- Options -----------------------------------------------------------------

Flow FrontEnd::designer_options() {
    screen_.open_window(0x50, 0, 0xDF, 0x60, 0);
    screen_.text_at(options_text);
    for (;;) {
        co_await wait_release();
        int c = 0;
        co_await select(options_areas, c);
        if (c == 0) break;
        if (c == 1) co_await effects_page();
        else if (c == 2) co_await behaviour_page();
        else if (c == 3) co_await finishing_page();
        else if (c == 4) {
            LineEditor e{0x14, 0x56, 0x3C, 0x132, 4, 0x3C, 0x13B, 0x6C, design::TowerDesign::text(design_.name), 0, false};
            co_await text_page(0x3C, name_text, e);
            design::TowerDesign::set_text(design_.name, e.text);
        } else if (c == 5) {
            LineEditor e{0x14, 0x62, 0x3C, 0x132, 4, 0x48, 0x13B, 0x78, design::TowerDesign::text(design_.author), 0, false};
            co_await text_page(0x48, author_text, e);
            design::TowerDesign::set_text(design_.author, e.text);
        } else if (c == 6) {
            screen_.close_window();
            co_await test_design();
            co_return;
        }
        // (the menu stays open behind its pages)
    }
    screen_.close_window();
}

Flow FrontEnd::text_page(int y0, std::uint32_t title, LineEditor& e) {
    screen_.open_window(4, y0, 0x13B, y0 + 0x30, 0);
    screen_.text_at(title);
    co_await edit_field(e);
    co_await wait_frames(25);
    screen_.close_window();
}

Flow FrontEnd::effects_page() {
    screen_.open_window(0x46, 0x18, 0x117, 0xC0, 0);
    screen_.text_at(effects_text);
    for (;;) {
        auto& d = design_;
        screen_.radio(var(0x47D6), d.stars);
        screen_.radio(var(0x47E8), d.flags[6] ? 1 : 0);
        screen_.radio(var(0x47F2), d.flags[7] ? 1 : 0);
        screen_.radio(var(0x47FC), d.sky_type);
        screen_.radio(var(0x480A), d.pattern);
        int c = 0;
        co_await select(effects_areas, c);
        if (c == 0) break;
        if (c == 1) co_await sound_page();
        else if (c == 2) co_await moon_page();
        else if (c <= 6) d.stars = c - 3;
        else if (c <= 8) d.flags[6] = c == 8;
        else if (c <= 10) d.flags[7] = c == 10;
        else if (c <= 13) d.sky_type = c - 11;
        else d.pattern = c - 14;
    }
    screen_.close_window();
}

Flow FrontEnd::sound_page() {
    screen_.open_window(0x28, 0x30, 0x137, 0x90, 0);
    screen_.text_at(sound_text);
    for (;;) {
        screen_.radio(var(0x4608), design_.background);
        screen_.radio(var(0x461A), design_.no_footsteps ? 1 : 0);
        screen_.radio(var(0x4624), design_.no_effects ? 1 : 0);
        int c = 0;
        co_await select(sound_areas, c);
        if (c == 0) break;
        if (c <= 4) design_.background = c - 1;
        else if (c <= 6) design_.no_footsteps = c == 6;
        else design_.no_effects = c == 8;
    }
    screen_.close_window();
}

Flow FrontEnd::moon_page() {
    screen_.open_window(0x40, 0x3C, 0x107, 0xB4, 0);
    screen_.text_at(moon_text);
    const auto draw_sky = [&] {
        screen_.fill(0xA0, 0x6E, 0xE5, 0x89, 13);
        screen_.line(0xA0, 0x7C, 0xE5, 0x7C, 14);
        if (design_.moon_heading != 4) screen_.dot(0xA0 + design_.moon_x * 4, 0x6E + design_.moon_y / 4, 2);
    };
    draw_sky();
    for (;;) {
        screen_.radio(var(0x462E), design_.moon_heading);
        screen_.radio(var(0x4644), design_.moon_type);
        screen_.radio(var(0x465E), design_.moon_rotation);
        int c = 0;
        co_await select(moon_areas, c);
        if (c == 0) break;
        if (c <= 5) {
            design_.moon_heading = c - 1;
            draw_sky();
        } else if (c == 6) {
            const int dx = in_.x - 0xA0, dy = in_.y - 0x6E;
            if (design_.moon_heading != 4 && dx >= 0 && dx < 0x47 && dy >= 0 && dy < 0x0E) {
                design_.moon_x = dx / 4;
                design_.moon_y = dy * 4;
                draw_sky();
            }
        } else if (c <= 12) {
            design_.moon_type = c - 7;
        } else {
            design_.moon_rotation = c - 13;
        }
    }
    screen_.close_window();
}

Flow FrontEnd::behaviour_page() {
    screen_.open_window(0x48, 0x24, 0xE7, 0x84, 0);
    screen_.text_at(behaviour_text);
    for (;;) {
        co_await wait_release();
        screen_.radio(var(0x476E), design_.flags[4] ? 1 : 0);
        screen_.radio(var(0x4778), design_.flags[8] ? 1 : 0);
        screen_.radio(var(0x4782), design_.bomb_speed);
        int c = 0;
        co_await select(behaviour_areas, c);
        if (c == 0) break;
        if (c <= 2) design_.flags[4] = c == 2;
        else if (c <= 4) design_.flags[8] = c == 4;
        else design_.bomb_speed = c - 5;
    }
    screen_.close_window();
}

Flow FrontEnd::finishing_page() {
    screen_.open_window(0x40, 0x30, 0x127, 0xA8, 0);
    screen_.text_at(finishing_text);
    for (;;) {
        auto& d = design_;
        d.update_spiders();
        // The status line ($F0CE).
        int klondikes = 0, others = 0;
        for (const auto k : d.objects)
            if (k >= 7 && k < 0x28) (k == design::klondike ? klondikes : others)++;
        int status = 0x3E;
        if (d.klondikes + d.destroy == 0) status = 0x3F;
        else if (klondikes < d.klondikes) status = 0x40;
        else if (others < d.destroy) status = 0x41;
        else if (d.klondikes && d.flags[2]) status = 0x42;
        else if (d.destroy && d.flags[0]) status = 0x43;
        screen_.cursor.x = 0x48;
        screen_.cursor.y = 0x9E;
        screen_.message(status);
        screen_.fill(screen_.cursor.x, 0x9E, 0xD0, 0xA5, 0);
        screen_.radio(var(0x4790), d.flags[5] ? 1 : 0);
        screen_.cursor.x = 0xAC;
        screen_.cursor.y = 0x56;
        if (d.flags[5]) screen_.text_at(no_limit_text);
        else screen_.text(time_text_);
        screen_.fill(screen_.cursor.x, 0x56, 0xD0, 0x5D, 0);
        screen_.radio(var(0x479A), d.klondikes);
        screen_.radio(var(0x47B8), d.destroy);
        int c = 0;
        co_await select(finishing_areas, c);
        if (c == 0) break;
        if (c <= 2) {
            d.flags[5] = c == 2;
        } else if (c == 3) {
            if (!d.flags[5]) {
                LineEditor e{0xAC, 0x56, 4, 0xD4, 0x48, 0x54, 0xC8, 0x61, time_text_, 0, true};
                co_await edit_field(e);
                time_text_ = e.text;
                if (e.text.empty()) d.flags[5] = true;
            }
        } else if (c <= 10) {
            d.klondikes = c - 4;
        } else {
            d.destroy = c - 11;
        }
    }
    screen_.close_window();
}

Flow FrontEnd::test_design() {
    // $F2DE: the tower as it would be packed, played (game type 3) or, with
    // enemy movement off, only looked at; then the designer again.
    design_.update_spiders();
    const auto record = pack_design();
    const auto tower = data::decode_tower(record);
    fast_fades_ = false;
    int reason = 0;
    co_await play_tower(tower, 3, reason, !enemy_movement_);
    fast_fades_ = true;
    screen_.clear();
    screen_.copper = CopperMode::split;
    designer_screen();
}

// --- Toolbox -----------------------------------------------------------------

Flow FrontEnd::designer_toolbox() {
    screen_.open_window(0xA0, 0, 0x12F, 0x6C, 0);
    screen_.text_at(toolbox_text);
    // The menu closes once a command has done something ($E0E bit 3).
    for (bool done = false; !done;) {
        co_await wait_release();
        int c = 0;
        co_await select(toolbox_areas, c);
        switch (c) {
        case 0:
            done = true;
            break;
        case 1:
            screen_.close_window();
            co_await colours_page();
            co_return; // (the screen was drawn again)
        case 2:
        case 3:
            co_await scroll_page(c == 3, done);
            break;
        case 4:
        case 5:
            co_await level_page(c == 5, done);
            break;
        case 6: {
            bool yes = false;
            co_await confirm(clear_level_question, yes);
            if (yes) {
                snapshot();
                design_.clear_level(floor_);
                done = true;
            }
            break;
        }
        case 7: {
            bool yes = false;
            co_await confirm(clear_all_question, yes);
            if (yes) {
                snapshot();
                design_.clear_all();
                time_text_.clear();
                done = true;
            }
            break;
        }
        default:
            break;
        }
    }
    screen_.close_window();
}

Flow FrontEnd::scroll_page(bool rotate, bool& done) {
    const int y0 = rotate ? 0x30 : 0x30;
    screen_.open_window(0x98, y0, 0x12F, 0x84, 0);
    screen_.text_at(rotate ? rotate_text : scroll_text);
    screen_.glyph('1' + floor_);
    for (;;) {
        screen_.radio(rotate ? var(0x45FE) : var(0x45F4), whole_tower_ ? 0 : 1);
        co_await wait_release();
        int c = 0;
        co_await select(rotate ? rotate_areas : scroll_areas, c);
        if (c == 0) break;
        if (c <= 2) {
            whole_tower_ = c == 1;
            continue;
        }
        snapshot();
        const int level = whole_tower_ ? -1 : floor_;
        if (rotate) {
            static constexpr int turns[3] = {3, 1, 2}; // clockwise, anticlockwise, 180 degrees
            for (int i = 0; i < turns[c - 3]; ++i) design_.rotate(level);
        } else {
            static constexpr int dx[4] = {-1, 1, 0, 0}, dz[4] = {0, 0, 1, -1}; // left, right, up, down
            design_.scroll(level, dx[c - 3], dz[c - 3]);
        }
        done = true;
        break;
    }
    screen_.close_window();
}

Flow FrontEnd::level_page(bool swap, bool& done) {
    const int y0 = swap ? 0x48 : 0x3C;
    screen_.open_window(0x98, y0, 0x12F, swap ? 0x90 : 0x84, 0);
    screen_.text_at(swap ? swap_level_text : copy_text);
    screen_.glyph('1' + floor_);
    screen_.text_at(swap ? swap_with : copy_to);
    for (int i = 0; i < 3; ++i) {
        screen_.cursor.x = 0xEB;
        screen_.cursor.y = (swap ? 0x6E : 0x62) + 12 * i;
        screen_.glyph('1' + (i < floor_ ? i : i + 1));
    }
    int c = 0;
    co_await select(swap ? swap_level_areas : copy_areas, c);
    if (c > 0) {
        const int other = c - 1 < floor_ ? c - 1 : c;
        snapshot();
        if (swap) design_.swap_levels(floor_, other);
        else design_.copy_level(floor_, other);
        done = true;
    }
    screen_.close_window();
}

Flow FrontEnd::colours_page() {
    // $F38E: a page of its own: the 18 colours, the chosen one's name, its
    // R, G and B in 16 steps; presets, steps and swaps; a test of the tower.
    for (;;) {
        screen_.clear();
        screen_.copper = CopperMode::split;
        for (std::size_t i = 0; i < 15; ++i) current_[33 + i] = assets_.program.u16(var(0x23F2) + std::uint32_t(i) * 2);
        for (std::size_t i = 0; i < 18; ++i) current_[i] = design_.colours[i];
        screen_.palette = current_;
        screen_.title_bar();
        screen_.text_at(colours_text);
        screen_.frame(0x1E, 0x1E, 0x11F, 0x42, 13);
        screen_.frame(0x1E, 0x46, 0x11F, 0x6A, 13);
        screen_.frame(6, 0x90, 0x13A, 0xC3, 13);
        screen_.line(0x36, 0x90, 0x36, 0xC3, 13);
        for (int k = 0; k < 9; ++k) {
            const int x = 0x23 + 28 * k;
            const int lower = assets_.program.u16(0xF914 + std::uint32_t(k + 9) * 2), upper = assets_.program.u16(0xF914 + std::uint32_t(k) * 2);
            screen_.fill(x, 0x5A, x + 0x17, 0x5A + 0x0B, lower);
            screen_.fill(x, 0x32, x + 0x17, 0x32 + 0x0B, upper);
            screen_.cursor.x = 0x2B + 28 * k;
            screen_.cursor.y = 0x26;
            screen_.number(k, 1 + 0);
            screen_.cursor.x = 0x2B + 28 * k - (k + 9 >= 10 ? 4 : 0);
            screen_.cursor.y = 0x4E;
            if (k + 9 >= 10) screen_.number(k + 9, 2);
            else screen_.glyph(0x8A + k + 9);
        }
        for (int i = 0; i < 16; ++i)
            for (int r = 0; r < 3; ++r) {
                screen_.cursor.x = assets_.program.u16(0xF938 + std::uint32_t(i) * 4);
                screen_.cursor.y = 0x96 + 16 * r;
                screen_.glyph(assets_.program.u16(0xF93A + std::uint32_t(i) * 4));
            }
        bool leave = false, test = false;
        while (!leave && !test) {
            // The chosen colour: number, name, frames, RGB, the boxes.
            screen_.cursor.x = 0x78;
            screen_.cursor.y = 0x6E;
            screen_.number(colour_, 2);
            screen_.fill(0x78, 0x86, 0x120, 0x8D, 0);
            screen_.cursor.x = 0x78;
            screen_.cursor.y = 0x86;
            screen_.message(colour_ + 0x45);
            for (int k = 0; k < 9; ++k) {
                screen_.frame(0x21 + 28 * k, 0x30, 0x21 + 28 * k + 0x1B, 0x30 + 0x0F, k == colour_ ? 15 : 0);
                screen_.frame(0x21 + 28 * k, 0x58, 0x21 + 28 * k + 0x1B, 0x58 + 0x0F, k + 9 == colour_ ? 15 : 0);
            }
            for (std::size_t i = 0; i < 18; ++i) current_[i] = design_.colours[i];
            screen_.palette = current_;
            const auto c = design_.colours[std::size_t(colour_)];
            screen_.cursor.x = 0x78;
            screen_.cursor.y = 0x7A;
            screen_.number(c >> 8 & 15, 2);
            screen_.glyph('/');
            screen_.number(c >> 4 & 15, 2);
            screen_.glyph('/');
            screen_.number(c & 15, 2);
            for (int comp = 0; comp < 3; ++comp) {
                const int value = c >> (4 * comp) & 15, y = 0xB2 - 16 * comp;
                for (int i = 0; i < 16; ++i) screen_.frame(0x38 + 16 * i, y, 0x38 + 16 * i + 0x0F, y + 0x0F, i == value ? 15 : 0);
            }
            co_await wait_release();
            int choice = 0;
            co_await select(colours_areas, choice);
            if (choice == 0) { // presets
                screen_.open_window(0, 0, 0x7F, 0xC0, 0);
                screen_.text_at(presets_text);
                int p = 0;
                co_await select(presets_areas, p);
                if (p == 1) { // Step/Swap Colours
                    screen_.open_window(0, 0x18, 0x8F, 0xB4, 0);
                    screen_.text_at(step_text);
                    for (;;) {
                        co_await wait_release();
                        int st = 0;
                        co_await select(step_areas, st);
                        if (--st < 0) break;
                        if (st < 3) { // swap two components of every colour ($1247A)
                            for (auto& col : design_.colours) {
                                int r = col >> 8 & 15, g = col >> 4 & 15, b = col & 15;
                                if (st == 0) std::swap(r, g);
                                if (st == 1) std::swap(r, b);
                                if (st == 2) std::swap(g, b);
                                col = data::Colour(r << 8 | g << 4 | b);
                            }
                        } else if (st < 11) { // one step up or down ($12420)
                            static constexpr int steps[8][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1},
                                                                {-1, 0, 0}, {0, -1, 0}, {0, 0, -1}, {-1, -1, -1}};
                            const auto& s = steps[st - 3];
                            for (std::size_t i = 0; i < 18; ++i) {
                                if (!(steppable >> i & 1)) continue;
                                auto& col = design_.colours[i];
                                const int r = std::clamp((col >> 8 & 15) + s[0], 0, 15), g = std::clamp((col >> 4 & 15) + s[1], 0, 15),
                                          b = std::clamp((col & 15) + s[2], 0, 15);
                                col = data::Colour(r << 8 | g << 4 | b);
                            }
                        }
                        for (std::size_t i = 0; i < 18; ++i) current_[i] = design_.colours[i];
                        screen_.palette = current_;
                    }
                    screen_.close_window();
                } else if (p >= 2 && p < 14) { // a preset, the old colours kept for Restore
                    previous_colours_ = design_.colours;
                    for (std::size_t i = 0; i < 18; ++i)
                        design_.colours[i] = assets_.program.u16(presets + std::uint32_t(p - 2) * 0x24 + std::uint32_t(i) * 2);
                } else if (p == 14) {
                    std::swap(previous_colours_, design_.colours);
                }
                screen_.close_window();
            } else if (choice == 1) {
                test = true;
            } else if (choice == 2) {
                leave = true;
            } else if (choice == 3 || choice == 4) {
                const int k = (in_.x - 0x21) / 0x1C;
                if (in_.x >= 0x21 && k < 9) colour_ = k + (choice == 4 ? 9 : 0);
            } else if (choice == 5) {
                if (!(fixed_colours << colour_ & 0x80000000u)) {
                    const int value = ((in_.x - 0x38) >> 4) & 15, shift = ((0xC1 - in_.y) >> 2) & 0xC;
                    auto& col = design_.colours[std::size_t(colour_)];
                    col = data::Colour((col & ~(15 << shift)) | value << shift);
                }
            }
        }
        if (test) {
            co_await test_design();
            continue;
        }
        break;
    }
    screen_.clear();
    screen_.copper = CopperMode::split;
    designer_screen();
}

// --- Extras ------------------------------------------------------------------

Flow FrontEnd::designer_extras() {
    screen_.open_window(0xC8, 0, 0x13F, 0x6C, 0);
    screen_.text_at(extras_text);
    for (bool done = false; !done;) {
        co_await wait_release();
        int c = 0;
        co_await select(extras_areas, c);
        switch (c) {
        case 0: done = true; break;
        case 1: co_await designer_setup(); done = true; break;
        case 2: co_await statistics_page(); break;
        case 3: co_await support_page(); break;
        case 4: co_await path_page(); break;
        case 5: // Undo: the copy and the tower change places
            if (undo_ready_) {
                const auto here = pack_design();
                unpack_design(undo_);
                undo_ = here;
                done = true;
            }
            break;
        case 6: co_await mouse_page(false); break;
        case 7: co_await mouse_page(true); break;
        default: break;
        }
    }
    screen_.close_window();
}

Flow FrontEnd::designer_setup() {
    screen_.open_window(0xC0, 0x18, 0x13F, 0xAA, 0);
    screen_.text_at(setup_text);
    for (;;) {
        screen_.radio(var(0x4820), depth_cue_ ? 0 : 1);
        screen_.radio(var(0x482A), enemy_movement_ ? 0 : 1);
        screen_.radio(var(0x4834), auto_repeat_ ? 0 : 1);
        screen_.radio(var(0x483E), confirm_replace_ ? 0 : 1);
        screen_.radio(var(0x4848), bright_ ? 0 : 1);
        co_await wait_release();
        int c = 0;
        co_await select(setup_areas, c);
        if (c == 0) break;
        const bool on = (c & 1) != 0;
        if (c <= 2) depth_cue_ = on;
        else if (c <= 4) enemy_movement_ = on;
        else if (c <= 6) auto_repeat_ = on;
        else if (c <= 8) confirm_replace_ = on;
        else {
            bright_ = on;
            current_[10] = data::Colour(0x555 + (bright_ ? 0x222 : 0)); // $10368
            screen_.palette = current_;
        }
    }
    screen_.close_window();
}

Flow FrontEnd::statistics_page() {
    screen_.open_window(0xB0, 0x24, 0x13F, 0x84, 0);
    screen_.text_at(statistics_text);
    const auto s = design_.statistics();
    const int values[6] = {s.percent_used, s.zappers, s.pushers, s.klondikes, s.others, s.total};
    for (int i = 0; i < 6; ++i) {
        screen_.cursor.x = 0x11C;
        screen_.cursor.y = 0x3E + 12 * i;
        screen_.number(values[i], 3);
        if (i == 0) screen_.glyph('%');
    }
    int c = 0;
    co_await select(statistics_areas, c);
    screen_.close_window();
}

Flow FrontEnd::support_page() {
    screen_.open_window(0xC0, 0x30, 0x13F, 0x60, 0);
    screen_.text_at(support_text);
    const auto cursor = screen_.cursor;
    std::vector<int> reached;
    const bool passed = design_.support_check(reached);
    for (const int i : reached) {
        const int x = i & 7, z = i >> 3 & 7, f = i >> 6;
        const int c = design_.cells[std::size_t(i)];
        const int shift = (c == design::block || c == design::lift_top) ? 1 : 0;
        screen_.icon8(assets_.main, support_mark, 0x10 + 8 * x + 0x50 * f - shift, 0xC0 - 8 * z - shift);
        co_await wait_frames(3); // $126AC: 6 timer ticks a cell
    }
    screen_.cursor = cursor;
    screen_.message(passed ? 0x3A : 0x39);
    int c = 0;
    co_await select(support_areas, c);
    screen_.close_window();
    draw_plans();
}

Flow FrontEnd::path_page() {
    screen_.open_window(0x80, 0x18, 0x10F, 0x78, 0);
    screen_.text_at(path_text);
    std::array<int, 256> marks{};
    for (int s = 0; s < 3; ++s) {
        screen_.message(0x3B + s);
        co_await wait_frames(25);
        std::vector<int> reached;
        bool placed = false;
        const bool passed = design_.path_check(s, reached, placed);
        if (!placed) {
            screen_.text_at(not_found);
        } else {
            for (const int i : reached) {
                marks[std::size_t(i)] |= 1 << s;
                const int x = i & 7, z = i >> 3 & 7, f = i >> 6;
                const int shift = design_.objects[std::size_t(i)] == 0x19 ? 1 : 0;
                screen_.icon8(assets_.main, path_marks + std::uint32_t(marks[std::size_t(i)]) * 0x28, 0x10 + 8 * x + 0x50 * f - shift,
                              0xC0 - 8 * z - shift);
                co_await wait_frames(3);
            }
            screen_.message(passed ? 0x3A : 0x39);
        }
        co_await wait_frames(25);
    }
    int c = 0;
    co_await select(path_areas, c);
    screen_.close_window();
    draw_plans();
}

Flow FrontEnd::mouse_page(bool style) {
    screen_.open_window(style ? 0xC0 : 0xA8, style ? 0x60 : 0x54, 0x13F, style ? 0x84 : 0x78, 0);
    screen_.text_at(style ? style_text : scaling_text);
    for (;;) {
        screen_.radio(style ? var(0x451C) : var(0x4852), style ? mouse_style_ : mouse_scaling_);
        co_await wait_release();
        int c = 0;
        co_await select(style ? style_areas : scaling_areas, c);
        if (c == 0) break;
        (style ? mouse_style_ : mouse_scaling_) = c - 1;
    }
    screen_.close_window();
}

} // namespace babel::front
