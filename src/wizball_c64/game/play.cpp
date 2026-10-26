#include "wizball_c64/game/play.hpp"
#include <algorithm>

namespace wizball::game {
namespace {
constexpr int spawn_list_limit = 0x76; // $A056: entries end at offset $76
} // namespace

Play::Play(const data::Assets& assets, const data::GameImage& image, RandomSource& random, Sound& sound)
    : assets_(assets), image_(image), random_(random), playfield_(assets, image), wiz_(image), sound_(&sound) {
    for (int i = 0; i < 8; ++i) wander_colours_[std::size_t(i)] = image.byte(std::uint16_t(0xB5DF + i));
    load_bonus_templates();
    bonus_tune_toggle_ = image.byte(0xBB0C);
    shot_volume_ = image.byte(0xBA81);
    stick_last_ = image.byte(0x944B);
    panel_slot1_ = image.byte(0xB51E);
    for (int kind = 0; kind < int(templates_.size()); ++kind) {
        const auto address = image.word(std::uint16_t(0x3ECF + 2 * kind));
        auto& t = templates_[std::size_t(kind)];
        const auto bytes = image.bytes(address, 0x17);
        std::copy(bytes.begin(), bytes.end(), t.bytes.begin());
        const int count = image.byte(std::uint16_t(address + 0x17));
        for (int i = 0; i < count; ++i) {
            const auto r = image.bytes(std::uint16_t(address + 0x18 + 3 * i), 3);
            t.random.push_back({r[0], r[1], r[2]});
        }
    }
    for (int i = 0; i < 8; ++i) token_pointer_[std::size_t(i)] = image.byte(std::uint16_t(0xB4DE + i));
    waggle_mask_ = image.byte(0x723A);
}

// --- game and life set-up ---------------------------------------------------

void Play::new_game(int mode) {
    std::array<std::uint8_t, 6> top{};
    for (int i = 0; i < 6; ++i) top[std::size_t(i)] = std::uint8_t(image_.byte(std::uint16_t(0xBFEB + i)) - 0x30);
    new_game(mode, image_.byte(0xAF7E), top, false);
}

void Play::new_game(int mode, int start_place, const std::array<std::uint8_t, 6>& top_score, bool cheat) {
    // $70D3: scores cleared, the high score from the table's top entry.
    for (auto& s : scores_) s = {};
    high_score_ = top_score;
    new_high_ = false;
    invincible_ = cheat;
    // $6830
    levels_done_ = rounds_ = 0;
    retained_.fill(0);
    painted_.fill(0);
    colour_.fill(0);
    place_ = start_place; // $AF7E: chosen with the keys 0-5 on the title
    for (int p = place_; p > 0; --p) {
        painted_[std::size_t(p - 1)] = 3;
        retained_[std::size_t(p - 1)] = image_.byte(std::uint16_t(0xB10C + p));
    }
    if (place_ != 0) retained_[0] = 1;
    save_progress();
    saved_[2] = saved_[1];
    lives_ = {0, 3, 3};
    player_ = 1;
    for (int i = 0; i < 3; ++i) controls_[std::size_t(i)] = image_.byte(std::uint16_t(0xB2AC + 4 * mode + i));
}

void Play::save_progress() {
    auto& p = saved_[std::size_t(player_)];
    p.retained = retained_;
    p.painted = painted_;
    p.place = std::uint8_t(place_);
    p.levels_done = levels_done_;
    p.rounds = rounds_;
    p.colour = {colour_[0], colour_[1], colour_[2]};
}

void Play::restore_progress() {
    const auto& p = saved_[std::size_t(player_)];
    retained_ = p.retained;
    painted_ = p.painted;
    place_ = p.place;
    levels_done_ = p.levels_done;
    rounds_ = p.rounds;
    colour_ = {p.colour[0], p.colour[1], p.colour[2], colour_[3]};
}

void Play::start_life() {
    // $6E42: $7CDA clears the play variables, then the player's progress.
    wiz_.reset_for_life();
    catellite_.clear_for_life();
    for (auto& o : object_sprites_) o.x = o.y = 0; // $7CE8: $E7C4-$E7D4
    powers_ = {};
    sprite_mask_ = 3;
    token_selected_ = 0;
    token_flash_clock_ = token_flash_ = 0;
    flash_background_ = 0;
    fire_held_ = catellite_fire_ = 0;
    wiz_stick_.bits = 0; // $7CDA clears $B23F too: "everything pressed" until the first reading
    catellite_stick_.bits = 0;
    waggle_idle_ = waggles_ = 0;
    letters_inverted_ = false;
    restore_progress();
    death_clock_ = 0;
    dying_ = 0;
    pearls_ = 0;
    shield_ = shield_clock_ = shield_warned_ = 0;
    fire_latch_ = 0;
    fire_pressed_ = false;
    spray_step_ = spray_clock_ = 0;
    for (auto& s : shots_) s = PlayerShot{};
    // $63CA: the place's first screen.
    level_cleared_ = 0;
    playfield_.set_wiz_offset(0);
    wiz_.set_start_x(); // $63CA: $B20D = $AC
    playfield_.clear_sky(random_);
    enter_place();
}

void Play::reset_spawn_lists() {
    // $9F5F: spawn lists for the furthest place and the two before it; in
    // the first level the start place gets its first list too.
    slot_of_place_.fill(0);
    cursor_ = 0;
    new_spawn_list(furthest_);
    new_spawn_list(furthest_ - 1);
    new_spawn_list(furthest_ - 2);
    if (levels_done_ == 0) first_spawn_list(place_);
}

void Play::set_furthest() {
    // $A9C6: two places past the first one not fully painted (of 0-4).
    int y = 0;
    while (y < 5 && painted_[std::size_t(y)] == 3) ++y;
    furthest_ = y + 2;
}

void Play::enter_place() {
    // $75E0
    playfield_.start(place_, random_);
    playfield_.draw_first_screen(place_, 0xFF, level_cleared_ != 0);
    set_up_place();
    for (int i = 0; i < 8; ++i) playfield_.prepare_step();
}

void Play::set_up_place() {
    font_in_field_ = false; // the place's characters again ($77C6)
    charset_place_ = place_;
    // $776D: the place's characters and the panel's place digit; the fourth
    // cauldron empty; the colours as far as the place is painted.
    place_digit_.fill(0);
    cauldron_clock_[3] = 0;
    draw_place_digit();
    const auto empty = assets_.sprites[0x38].rows;
    for (int r = 0; r < 21; ++r)
        for (int b = 0; b < 3; ++b) cauldrons_[3].rows[std::size_t(r)][std::size_t(b)] = empty[std::size_t(r * 3 + b)];
    shared1_ = 0x0B;
    shared2_ = 0x0C;
    character_ = 0x09;
    const int count = painted_[std::size_t(place_)];
    std::uint8_t* colours[3] = {&shared1_, &shared2_, &character_};
    int x = 4 * place_;
    for (int i = 0; i < count && i < 3; ++i) *colours[i] = image_.byte(std::uint16_t(0xB1D1 + x++));
    panel_extra_ = image_.byte(std::uint16_t(0xB1D1 + x));
    character_ |= 8;
    fill_field_colours(); // $77C3: JMP $7D1A
}

void Play::draw_place_digit() {
    // $770D: the digit's glyph (font code $31 + place) drawn as a big dotted
    // figure, three sprite pixels per bit and two rows per glyph row.
    const auto& glyph = assets_.front_charset[std::size_t(0x31 + place_)];
    const auto patterns = image_.bytes(0x76E9, 24);
    int at = 0;
    for (int row = 0; row < 7; ++row) {
        auto byte = glyph[std::size_t(row)];
        if (letters_inverted_) byte = std::uint8_t(~byte);
        for (int bit = 0; bit < 8; ++bit) {
            if (byte >> bit & 1) {
                for (int k = 0; k < 3; ++k) {
                    place_digit_[std::size_t(at + k)] |= patterns[std::size_t(bit * 3 + k)];
                    place_digit_[std::size_t(at + 3 + k)] |= patterns[std::size_t(bit * 3 + k)];
                }
            }
        }
        at += 6;
    }
    panel_flash_base_ = image_.byte(std::uint16_t(0x7701 + place_));
}

void Play::begin_play() {
    sound_->reset_music(); // $696F: the player screen's tune stops; the third voice is the tunes'
    sound_->set_mode(1);
    sound_->set_speed(0);
    sound_->silence(); // $BAAD
    // $70EC: the score lines.
    new_high_ = false;
    add_points(0);
    // $63E0
    token_pointer_ = {};
    for (int i = 0; i < 8; ++i) token_pointer_[std::size_t(i)] = image_.byte(std::uint16_t(0xB4DE + i));
    powers_ = {};
    rearm_weapons(false); // $6E4F
    set_furthest();
    reset_spawn_lists();
    playfield_.scroll(wiz_.speed_index());
    // $9DE9: all cauldrons empty.
    const auto empty = assets_.sprites[0x38].rows;
    for (auto& c : cauldrons_)
        for (int r = 0; r < 21; ++r)
            for (int b = 0; b < 3; ++b) c.rows[std::size_t(r)][std::size_t(b)] = empty[std::size_t(r * 3 + b)];
    cauldron_clock_ = {};
    set_up_place();
    // $9CD5: no objects.
    for (auto& o : objects_) o = Object{};
    cursor_ = 0;
    sprite_mask_ &= 3;
    // $8040
    wiz_.start_level();
    timer_ticks_ = timer_seconds_ = 0;
    filler_burst_ = 0;
    beeping_ = 0;
    flash_colours_ = false;
    dying_ = 0;
    wiz_hit_ = 0;
    catellite_.set_hits(0);
    for (int i = 5; i >= 1; --i) special_at_[std::size_t(i)] = random_.next() & 0xF8; // $8081
    // $9CCB: no enemy shots.
    for (auto& e : enemy_shots_) e = EnemyShot{};
    fill_field_colours();
}

// --- spawn lists --------------------------------------------------------------

void Play::prepare_kind(int kind, int place) {
    // $A070
    auto& t = templates_[std::size_t(kind)].bytes;
    std::uint8_t period = image_.byte(std::uint16_t(0xA12A + place));
    if (rounds_ != 0) period = 0x0A;
    if (t[0x0E] == 3) period = std::uint8_t(period << 1);
    t[0x0F] = period;
    if ((random_.next() & 0x0F) < 8 && t[8] < 2) {
        t[8] ^= 1;
        // Mirrored about $82; "below" is the 6502's CMP #$82 / BMI (bit 7 of y - $82).
        const auto y = t[2];
        t[2] = (std::uint8_t(y - 0x82) & 0x80) ? std::uint8_t(0x82 - y + 0x82) : std::uint8_t(0x82 - (y - 0x82));
    }
    if (t[8] < 2 && (random_.next() & 0x0F) < 3) t[9] = std::uint8_t((random_.next() & 0x3F) + 0x10);
}

void Play::new_spawn_list(int place) {
    // $9F9B: groups of 4, 6 or 12 things, 1-3 columns apart, the group
    // starts 5-20 columns after the last; kinds 1-16 or one of four.
    const int slot = image_.byte(std::uint16_t(0x9F8A + place));
    slot_of_place_[std::size_t(place)] = std::uint8_t(slot);
    auto& list = spawn_lists_[std::size_t(slot)];
    list.clear();
    int column = 0x14;
    int carry = 0; // the 6502's carry flag flows from one group into the next sum
    for (;;) {
        column = (column + (random_.next() & 0x0F) + 5 + carry) & 0xFF;
        int count = image_.byte(std::uint16_t(0x9F86 + (random_.next() & 3)));
        const int spacing = image_.byte(std::uint16_t(0x9F93 + (random_.next() & 3)));
        int kind;
        if ((random_.next() & 0x0F) < 0x0C) {
            kind = (random_.next() & 0x0F) + 1;
        } else {
            kind = image_.byte(std::uint16_t(0x9F97 + (random_.next() & 3)));
        }
        prepare_kind(kind, place);
        const bool special = kind == image_.byte(0x9F97) || kind == image_.byte(0x9F98) ||
                             kind == image_.byte(0x9F99) || kind == image_.byte(0x9F9A);
        if (special) {
            auto& t = templates_[std::size_t(kind)].bytes;
            t[0x0E] = 0;
            if (place >= 4) t[0x0E] = 1;
            if (place >= 6) {
                t[0x0E] = 3;
                t[0x0F] = std::uint8_t(t[0x0F] << 1);
            }
        }
        for (;;) {
            if (column >= 0xC8) return;
            list.push_back({std::uint8_t(column), std::uint8_t(kind)});
            if (2 + 2 * int(list.size()) == spawn_list_limit) return;
            const int sum = column + spacing;
            column = sum & 0xFF;
            carry = sum > 0xFF ? 1 : 0;
            if (--count == 0) break;
        }
    }
}

void Play::first_spawn_list(int place) {
    // $A0EC: the very first visit: eight of kind 1, 12 columns apart.
    const int slot = image_.byte(std::uint16_t(0x9F8A + place));
    slot_of_place_[std::size_t(place)] = std::uint8_t(slot);
    auto& list = spawn_lists_[std::size_t(slot)];
    list.clear();
    for (int column = 0x32; column < 0x8C; column += 0x0C) list.push_back({std::uint8_t(column), 1});
}

void Play::count_aliens() {
    // $9ECF: entries still to come in the place's list; then $9EFD.
    int n = 0;
    const int slot = slot_of_place_[std::size_t(place_)];
    if (slot != 0) {
        for (const auto& e : spawn_lists_[std::size_t(slot)]) {
            if (!(e.kind & 0x80)) ++n;
        }
    }
    aliens_left_ = std::uint8_t(n);
    open_next_wave();
}

// --- scores -------------------------------------------------------------------

void Play::add_points(int tens) {
    // $710B: hundreds, tens and units of the points into the digits above
    // the always-0 last one, then carries; every 100000 an extra life.
    auto& d = scores_[std::size_t(player_)].digits;
    const auto top = d[1];
    int a = tens;
    while (a >= 100) {
        ++d[3];
        a -= 100;
    }
    while (a >= 10) {
        ++d[4];
        a -= 10;
    }
    d[5] = std::uint8_t(d[5] + a);
    for (int i = 5; i >= 0; --i) {
        if (d[std::size_t(i)] >= 10) {
            d[std::size_t(i)] = std::uint8_t(d[std::size_t(i)] - 10);
            if (i > 0) ++d[std::size_t(i - 1)];
        }
    }
    if (d[0] != 0) {
        d[0] = 0;
        for (int i = 1; i <= 6; ++i) d[std::size_t(i)] = 9;
    }
    // A new high score?
    bool higher = false;
    for (int i = 0; i < 6; ++i) {
        if (d[std::size_t(i + 1)] != high_score_[std::size_t(i)]) {
            higher = d[std::size_t(i + 1)] > high_score_[std::size_t(i)];
            break;
        }
        if (i == 5) higher = true; // equal counts as a new high score ($7179)
    }
    if (higher) {
        for (int i = 0; i < 6; ++i) high_score_[std::size_t(i)] = d[std::size_t(i + 1)];
        new_high_ = true;
    }
    if (top != d[1]) {
        ++lives_[std::size_t(player_)];
        // sound $11 (not rebuilt yet)
    }
}

void Play::show_lives() {}

bool Play::end_life() {
    save_progress(); // $6E38
    // $6B27
    if (!infinite_lives_) --lives_[std::size_t(player_)];
    if (controls_[2] == 1) return lives_[1] != 0; // one player
    if (lives_[1] == 0 && lives_[2] == 0) return false;
    player_ ^= 3;
    if (lives_[std::size_t(player_)] == 0) player_ ^= 3;
    return true;
}

// --- the pass -----------------------------------------------------------------

void Play::frame() {
    sound_->frame(); // $B90C
    token_colour_shown_ = token_colour_;
    panel_colour_shown_ = panel_colour0_;
    // $7269: the token bar's colours, the selected token flashing (other
    // colours while the bar offers the weapons, $B074).
    for (auto& c : token_colour_) c = weapon_select_ ? 0x0A : 0x0E;
    if ((++token_flash_clock_ & 3) == 0) {
        if (++token_flash_ == 6) token_flash_ = 0;
    }
    if (token_selected_ != 0)
        token_colour_[std::size_t(token_selected_ - 1)] =
            image_.byte(std::uint16_t(0xB53D + token_flash_ + (weapon_select_ ? 6 : 0)));
    // $67A1: the score line's flashing (colours applied in picture()).
    if ((++hud_clock_ & 3) == 0) ++hud_flash_;
    if (in_lab_) {
        // $680B: the lab's words take the title's colour run's newest colour.
        step_colour_run();
        hud_band_colour_ = colour_run_[0];
    }
    // $8A02: a blacked-out landscape keeps its shared colours off except
    // for the few frames after the wiz bumps into it.
    field_dark_ = flash_colours_ && flash_show_ == 0;
    if (flash_colours_ && flash_show_ != 0) --flash_show_;
    // $8B9A: the panel's first sprite cycles through the place's colours.
    if ((++panel_clock_ & 1) == 0) ++panel_flash_;
    panel_colour0_ = image_.byte(std::uint16_t(0xB5CB + (panel_flash_ & 7) + panel_flash_base_));
    // $8A44: the playfield background flash runs on until bit 7.
    if (flash_background_ & 0x7F) ++flash_background_;
}

void Play::update_cauldrons() {
    // $9E0D
    for (int i = 0; i < 3; ++i) colour_[std::size_t(i)] = std::min<std::uint8_t>(colour_[std::size_t(i)], 0x14);
    // The original reads the fourth cauldron's top row ($EF01), not while
    // the wiz dies or sinks into a tunnel.
    if (dying_ == 0 && wiz_.tunnel() == 0) level_cleared_ = cauldrons_[3].rows[0][1];
    int total = 0;
    for (int i = 0; i < 3; ++i) {
        const int need = image_.byte(std::uint16_t(0xB2C0 + panel_extra_ * 3 + i));
        total += need < colour_[std::size_t(i)] ? need : colour_[std::size_t(i)];
    }
    if (!in_lab_) colour_[3] = std::uint8_t(total); // $9E50
    // $9E81: one row of each cauldron redrawn per pass, from the bottom up.
    const auto empty = assets_.sprites[0x38].rows;
    const auto full = assets_.sprites[0x39].rows;
    for (int c = 0; c < 4; ++c) {
        auto& clock = cauldron_clock_[std::size_t(c)];
        if (++clock == 0x15) clock = 0;
        const int row = 20 - clock;
        const auto& source = colour_[std::size_t(c)] >= clock ? full : empty;
        for (int b = 0; b < 3; ++b)
            cauldrons_[std::size_t(c)].rows[std::size_t(row)][std::size_t(b)] = source[std::size_t(row * 3 + b)];
    }
}

void Play::step(const Ports& ports) {
    if (hung_ || quit_) return;
    if (paused_) {
        // $6EC3: a frame of the pause; RUN/STOP again goes on where it
        // stopped, Q ends the game.
        read_keys(ports);
        if (keys_.code == keys::quit) {
            quit_ = true;
            return;
        }
        if (keys_.code != keys::run_stop) return;
        paused_ = false;
        if (stage_ == Stage::bonus) {
            bonus_frame_rest(ports);
        } else {
            pass_a_body(ports);
            if (stage_ == Stage::play && !waiting()) pass_b();
        }
        return;
    }
    if (waiting()) {
        resume(ports);
        return;
    }
    switch (stage_) {
    case Stage::play:
        pass_a(ports);
        if (stage_ == Stage::play && !waiting()) pass_b();
        break;
    case Stage::vanish: vanish_frame(); break;
    case Stage::bonus: bonus_frame(ports); break;
    case Stage::lab: lab_frame(ports); break;
    case Stage::flight: flight_frame(ports); break;
    case Stage::round_end: break; // runs on its pending steps
    case Stage::game_over: game_over_frame(ports); break;
    }
}

void Play::pass(const Ports& ports) {
    const auto before = stage_;
    step(ports);
    if (before == Stage::play && stage_ == Stage::vanish) step(ports);
}

void Play::pass_a(const Ports& ports) {
    marker(0x6E91); // the end of the $6E83 wait
    sound_->check_tune(); // $45BD
    cleared_at_loop_top_ = level_cleared_; // $6417: kept for the score line's colours
    if (level_cleared_ != 0) { // $6414
        start_vanish();
        return;
    }
    if (pause_check()) return; // $641F
    pass_a_body(ports);
}

void Play::pass_a_body(const Ports& ports) {
    if (dying_ == 0) erase_enemy_shots();
    erase_player_shots();
    playfield_.scroll(wiz_.speed_index());
    wiz_routine();
    if (!waiting()) pass_a_rest(ports);
}

bool Play::pause_check() {
    // $6EB7
    if (keys_.code != keys::run_stop) return false;
    paused_ = true;
    return true;
}

void Play::step_colour_run() {
    // $98BE: the title's colour run moves on every third frame.
    if (++colour_run_clock_ != 3) return;
    colour_run_clock_ = 0;
    for (int i = 12; i >= 1; --i) colour_run_[std::size_t(i)] = colour_run_[std::size_t(i - 1)];
    colour_run_[0] = image_.byte(std::uint16_t(0xB5DF + (++colour_run_index_ & 7)));
}

void Play::read_keys(const Ports& ports) { keys_.update(scan_few_keys(ports.keys)); } // $7429

void Play::volume_keys() {
    // $BA82: the up arrow and = make the shots louder or quieter.
    if (keys_.code == keys::louder && shot_volume_ != 0x0F) ++shot_volume_;
    if (keys_.code == keys::quieter && shot_volume_ != 0) --shot_volume_;
}

void Play::pass_a_rest(const Ports& ports) {
    catellite_fire();
    tokens();
    playfield_.prepare_step();
    draw_player_shots();
    if (dying_ == 0) move_enemy_shots();
    playfield_.animate_stars(wiz_.speed_index(), random_);
    read_keys(ports); // $644A
    read_sticks(ports);
    update_cauldrons();
}

void Play::pass_b() {
    marker(0x682F); // the end of the $681D wait
    latch_sprites();
    update_catellite();
    spawn_from_list();
    update_objects();
    level_timer();
}

void Play::wiz_routine() {
    // $7DF4. $7DF7: the hum restarts when its voice falls silent (or every 256 passes).
    marker(0x7DF4);
    if (!sound_->slot_busy(1) || ++hum_clock_ == 0) sound_->start_effect(0x18);
    if (death_clock_ != 0) {
        death_step();
        return;
    }
    if (dying_ != 0) {
        death_clock_ = 0xC8;
        start_death();
        death_step();
        return;
    }
    // $723D: left-right waggles (they activate the lit token).
    if (wiz_stick_.bits & waggle_mask_) {
        if (++waggle_idle_ == 0x0A) {
            waggle_idle_ = 0;
            waggles_ = 0;
        }
    } else {
        waggle_mask_ ^= 0x0C;
        waggle_idle_ = 0;
        ++waggles_;
    }
    const bool bonus = in_lab_; // $AC4E: the lab's movement
    if (wiz_.tunnel() != 0) { // $7E21
        sink_step();
        return;
    }
    wiz_.move(wiz_stick_, powers_, playfield_, bonus, level_cleared_ != 0 || in_lab_);
    if (wiz_.bumped()) flash_show_ = 3; // $83BA: a blacked-out landscape shows where the wiz hits it
    if (wiz_.place_change() != 0) enter_tunnel(wiz_.place_change());
    // $80CD: the wiz's colour; it blinks black while the shield is up.
    wiz_colour_ = 5;
    if (shield_ != 0 && (++shield_blink_ & 2)) wiz_colour_ = 0;
    fire_shots();
    launch_shots();
    wiz_.record_trail();
    spray();
    if (powers_.thrust || bonus) wiz_.update_speed_index();
    wiz_touches();
    shield_and_hits();
    // $BA82: the shot sounds' sustain from the volume keys' setting.
    volume_keys();
    sound_->patch_effect(0, 23, std::uint8_t(shot_volume_ << 4));
    sound_->patch_effect(0x10, 23, std::uint8_t(shot_volume_ << 4));
    level_end_drift();
}

void Play::read_sticks(const Ports& ports) {
    // $697D: the sticks for the next pass. A team (control 2: two players
    // on one side) steers the wiz with the port whose fire started the life
    // and the catellite with the other.
    if (controls_[std::size_t(player_ - 1)] != 1) {
        wiz_stick_.bits = port_select_ ? ports.port1 : ports.port2;
        catellite_stick_.bits = port_select_ ? ports.port2 : ports.port1;
        return;
    }
    // One player on the side: the chosen port; fire held ten passes steers
    // the catellite instead.
    const std::uint8_t stick = port_select_ ? ports.port1 : ports.port2;
    if (stick & 0x10) {
        fire_held_ = 0;
        wiz_stick_.bits = stick;
        catellite_stick_.bits = std::uint8_t((stick & 0x10) | 0xEF);
    } else if (fire_held_ == 0x0A && powers_.catellite) {
        catellite_fire_ ^= 0x10;
        wiz_stick_.bits = std::uint8_t(catellite_fire_ | 0xEF);
        catellite_stick_.bits = stick;
    } else {
        if (fire_held_ == 0x0A) fire_held_ = 0;
        else ++fire_held_;
        wiz_stick_.bits = stick;
        catellite_stick_.bits = std::uint8_t((stick & 0x10) | 0xEF);
    }
}

} // namespace wizball::game
