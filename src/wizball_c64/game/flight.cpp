#include "wizball_c64/game/play.hpp"

// A place painted in all three colours ($AE11): the level is flown through
// once in full colour, the view scrolling at speed 6 with a point a frame,
// under "WOT A WIZACE! / LEVEL n COMPLETED" drawn into six sprites ($8DE4;
// the last of them is cauldron 0's, which the panel swaps for the empty
// cauldron meanwhile). Then the next level's places are set up and play goes
// on at the furthest place through a place change; when places 0-7 are all
// painted the round is complete ($AF07).
namespace wizball::game {
namespace {
constexpr std::uint16_t wizace_text = 0xB846, completed_text = 0xB858;
constexpr std::uint16_t level_digit = 0xB85E; // inside the second line
constexpr int text_sprites = 6;               // $B4-$B9
} // namespace

std::uint8_t& Play::sprite_memory(int pointer, int offset) {
    // $ED00 on: sprites $B4-$B8 are the work sprites; $B9 is cauldron 0.
    if (pointer == 0xB9) {
        if (offset >= 63) return unshown_byte_ = 0; // byte 63 is not shown
        return cauldrons_[0].rows[std::size_t(offset / 3)][std::size_t(offset % 3)];
    }
    return work_sprites_[std::size_t((pointer - 0xB4) * 0x40 + offset)];
}

void Play::draw_sprite_text(std::uint16_t address, int first_row, int first_sprite, bool big) {
    // $8DE4: three characters to a sprite, eight rows each; $8E32 the big
    // font's characters (2 x 2 cells, $B591), a column of two cells after
    // another. The glyphs as they look upright (the inverted font in memory
    // is flipped back by $B276/$B277).
    int column = 0;
    for (int i = 0;; ++i) {
        std::uint8_t ch = address + i == level_digit ? level_digit_ : image_.byte(std::uint16_t(address + i));
        if (ch == 0xFF) return;
        auto put = [&](int glyph, int row_offset) {
            const auto& g = assets_.front_charset[std::size_t(glyph)];
            for (int r = 0; r < 8; ++r)
                sprite_memory(first_sprite + column / 3, (first_row + row_offset + r) * 3 + column % 3) = g[std::size_t(r)];
        };
        if (!big) {
            put(ch & 0x3F, 0);
            ++column;
            continue;
        }
        const int code = 0x40 + image_.byte(std::uint16_t(0xB591 + (ch & 0x3F)));
        put(code, 0);
        put(code + 0x20, 8);
        ++column;
        put(code + 1, 0);
        put(code + 0x21, 8);
        ++column;
    }
}

void Play::flight_colours() {
    // $B12F: all sprites through $B61C, a step every fourth frame.
    if ((++message_flash_ & 3) == 0 && ++message_colour_ == 0x0C) message_colour_ = 0;
    flight_colour_ = image_.byte(std::uint16_t(0xB61C + message_colour_));
}

void Play::start_flight() {
    // $AE11
    panel_slot1_ = 0x38;
    sound_->set_mode(0);
    wiz_.set_speed_index(0);
    playfield_.set_view_column(0);
    playfield_.draw_first_screen(place_, 0x38, false);
    for (int i = 0; i < 8; ++i) playfield_.prepare_step();
    set_up_place();
    for (int p = 0xB4; p < 0xB4 + text_sprites; ++p)
        for (int o = 0; o < 0x40; ++o) sprite_memory(p, o) = 0; // $8F15
    level_digit_ = std::uint8_t(place_ + 0x31);
    draw_sprite_text(wizace_text, 0, 0xB4, false);
    draw_sprite_text(completed_text, 11, 0xB4, false);
    sprite_mask_ = 0xFC;
    flying_text_ = true;
    // $AE61: sprites 0-7 from the table; sprite 1's are the catellite's registers.
    catellite_.show_icon(image_.byte(0xB4E7), image_.byte(0xB4E8), image_.byte(0xB4FE), image_.byte(0xB4F6));
    sound_->start_effect(7);
    pending_ = Pending::flight_intro;
    pending_count_ = 50;
    stage_ = Stage::flight;
}

void Play::flight_wait_step() {
    marker(0x6E92); // $6E92 (which moves the stars)
    playfield_.animate_stars(wiz_.speed_index(), random_);
    flight_colours();
    latch_sprites();
    if (--pending_count_ != 0) return;
    if (pending_ == Pending::flight_intro) {
        wiz_.set_speed_index(6);
        pending_ = Pending::none;
        return;
    }
    pending_ = Pending::none;
    finish_flight();
}

void Play::flight_frame(const Ports&) {
    // $AE9C
    marker(0x6E82); // the end of the $6E71 wait
    flight_colours();
    playfield_.scroll(wiz_.speed_index());
    playfield_.prepare_step();
    if (!sound_->slot_busy(1) || ++hum_clock_ == 0) sound_->start_effect(0x18); // $AAA6
    if ((random_.next() & 0x0F) == 8) sound_->start_effect(0x19);
    playfield_.animate_stars(wiz_.speed_index(), random_);
    add_points(1);
    latch_sprites();
    if (playfield_.view_column() != 0xBC) return;
    wiz_.set_speed_index(0);
    pending_ = Pending::flight_outro;
    pending_count_ = 50;
}

void Play::finish_flight() {
    // $AECA: on to the furthest place.
    place_ = furthest_;
    sprite_mask_ = 0;
    flying_text_ = false;
    // $B158: spawn lists for the furthest place and the two before it; the
    // places behind them lose theirs.
    int x = furthest_;
    for (int k = 0; k < 3; ++k, --x)
        if (slot_of_place_[std::size_t(x)] == 0) new_spawn_list(x);
    for (; x >= 0; --x) slot_of_place_[std::size_t(x)] = 0;
    ++levels_done_;
    for (int i = 0; i < 0x15; ++i) update_cauldrons();
    panel_slot1_ = 0xB9;
    bool all = true;
    for (int p = 0; p < 8; ++p) all = all && painted_[std::size_t(p)] == 3;
    if (all) {
        start_round_end();
        return;
    }
    stage_ = Stage::play;
    start_arrival();
    arrival_->back_to_play = true;
}

void Play::start_round_end() {
    // $AEFD: all eight painted: a new round from place 0.
    for (int p = 0; p < 8; ++p) painted_[std::size_t(p)] = 0;
    ++rounds_;
    place_ = 0;
    reset_spawn_lists();
    // $AF20: place 10's room; its shared colour flashes at random for 510
    // frames, with random sound effects.
    clear_for_lab();
    draw_room(10);
    place_ = lab_return_place_;
    shared1_ = 6;
    shared2_ = 0x0E;
    character_ = 9;
    fill_field_colours();
    lab_settings();
    pending_ = Pending::round_end;
    pending_count_ = 0xFF;
    pending_inner_ = 2;
    stage_ = Stage::round_end;
}

void Play::round_end_step() {
    marker(0x6E92); // (which moves the stars)
    playfield_.animate_stars(wiz_.speed_index(), random_);
    shared1_ = random_.next();
    latch_sprites();
    if (--pending_inner_ != 0) return;
    sound_->start_effect(random_.next() & 0x0F);
    pending_inner_ = 2;
    if (--pending_count_ != 0) return;
    pending_ = Pending::none;
    columns40_ = false; // $AF75
    after_lab();        // $AF15: JMP $ADEF
}

// --- game over ($69F2) ------------------------------------------------------------

void Play::start_game_over() {
    // $69FD: "GAME OVER" in the big font, the player's name under it.
    panel_slot1_ = 0x38;
    for (int p = 0xB4; p < 0xB4 + text_sprites; ++p)
        for (int o = 0; o < 0x40; ++o) sprite_memory(p, o) = 0; // $8F15
    draw_sprite_text(image_.word(0x6B0F), 0, 0xB4, true);
    const int line = (player_ - 1) * 4 + (controls_[std::size_t(player_ - 1)] - 1) * 2;
    draw_sprite_text(image_.word(std::uint16_t(0x6B11 + line)), 14, 0xB5, false); // $8EDB = $2A
    // $6A49: the eight sprites from the table at $B4E5 (positions, colours
    // $B4F5 on, pointers); sprite 1's are the catellite's registers.
    catellite_.show_icon(image_.byte(0xB4E7), image_.byte(0xB4E8), image_.byte(0xB4FE), image_.byte(0xB4F6));
    flight_colour_ = image_.byte(0xB4F7);
    sprite_mask_ = 0xFC;
    flying_text_ = true;
    erase_player_shots(); // $7090
    for (int r = 6; r < 9; ++r)
        for (int c = 11; c < 29; ++c) set_cell_colour(c, r, 0); // $6A6F ($D972 + 1-18): the cells behind it black
    game_over_timer_ = 0xC8;
    game_over_clock_ = 5;
    sound_->silence();
    sound_->reset_music();
    sound_->start_tune(0x36, 2);
    stage_ = Stage::game_over;
    latch_sprites();
}

void Play::game_over_frame(const Ports& ports) {
    // $6A94
    marker(0x6E92);
    playfield_.animate_stars(wiz_.speed_index(), random_);
    read_keys(ports);
    // Fire counts once the tune has ended.
    if (!sound_->music().playing()) {
        const auto now = std::uint8_t((ports.port2 & ports.port1) | 0xE0);
        const bool changed = now != edge_last_;
        edge_last_ = now;
        if (changed && !(now & 0x10)) {
            end_game_over();
            return;
        }
    }
    // $6AA6: the text in the title's newest colour.
    step_colour_run();
    flight_colour_ = colour_run_[0];
    latch_sprites();
    if (--game_over_clock_ == 0) {
        game_over_clock_ = 5;
        if (--game_over_timer_ == 0) end_game_over();
    }
}

void Play::end_game_over() {
    // $6ABE
    cauldron_clock_[0] = 0;
    panel_slot1_ = 0xB9;
    flying_text_ = false;
    stage_ = Stage::play;
    game_over_done_ = true;
}

} // namespace wizball::game
