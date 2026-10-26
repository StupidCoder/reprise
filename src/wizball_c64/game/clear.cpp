#include "wizball_c64/game/play.hpp"

// A place's colour completed ($9E0D sets $B874 from the fourth cauldron):
// the end of the place ($646F). The wiz fades out in rings while the
// playfield's colours cycle and the music fades; then, on a starfield with
// the place's landscape blacked out, the bonus wave ($6576): the wiz has
// thrust and antigravity, and enemies come in from a wave script ($3902,
// played from a copy at $ED02 by $9CEA); each kill counts. A hit or the
// script's end stops it and the kills are counted up at 40 points each in a
// message; then the Wizlab ($AB20).
namespace wizball::game {
namespace {
constexpr std::uint16_t wave_script_source = 0x3902, bonus_template_table = 0x39DC;
constexpr std::uint16_t message_table = 0x6739; // pointers by message number
constexpr std::uint16_t message_colours = 0xB61C;
constexpr std::uint16_t kill_digits = 0xB841;   // three digits inside the text of messages 2 and 3
} // namespace

void Play::load_bonus_templates() {
    for (int kind = 0; kind < int(bonus_templates_.size()); ++kind) {
        const auto address = image_.word(std::uint16_t(bonus_template_table + 2 * kind));
        auto& t = bonus_templates_[std::size_t(kind)];
        const auto bytes = image_.bytes(address, 0x17);
        std::copy(bytes.begin(), bytes.end(), t.bytes.begin());
        const int count = image_.byte(std::uint16_t(address + 0x17));
        for (int i = 0; i < count; ++i) {
            const auto r = image_.bytes(std::uint16_t(address + 0x18 + 3 * i), 3);
            t.random.push_back({r[0], r[1], r[2]});
        }
    }
}

void Play::level_end_drift() {
    // $7F0B: with the place cleared the view stands; the 8.8 step moves
    // $B1F2 instead, which places the wiz, within $87-$53.
    if (level_cleared_ == 0) return;
    playfield_.nudge(wiz_.speed_index());
    playfield_.set_wiz_offset(std::int8_t(playfield_.fine()));
    const auto f = playfield_.fine();
    if (f & 0x80) {
        if (f < 0x87) {
            playfield_.set_fine(0x87);
            wiz_.stop_horizontal();
        }
    } else if (f >= 0x54) {
        playfield_.set_fine(0x53);
        wiz_.stop_horizontal();
    }
}

void Play::grant_flight() {
    // $7379 twice: thrust, then antigravity.
    for (int k = 0; k < 2; ++k) {
        if (!powers_.thrust) {
            powers_.thrust = true;
            token_pointer_[0] = 3;
        } else {
            powers_.antigravity = true;
            token_pointer_[0] = 9;
        }
    }
}

void Play::start_vanish() {
    // $646F
    sound_->fade(0xFF);
    wiz_.start_vanish();
    sound_->patch_effect(5, 0, 2);
    flash_colours_ = false;
    cursor_ = 0;
    wiz_.set_speed_index(0);
    bonus_speed_ = bonus_speed_fraction_ = 0;
    accelerating_ = 0;
    playfield_.stop_for_clear();
    kills_ = 0;
    beeping_ = 0;
    erase_enemy_shots();
    erase_player_shots();
    remove_objects_for_place();
    sprite_mask_ = 3;
    // $9CB2: no objects; the wave script copied fresh.
    for (auto& o : objects_) o = Object{};
    cursor_ = 0;
    sprite_mask_ &= 3;
    wave_wait_ = wave_at_ = 0;
    wave_script_.fill(0);
    const auto script = image_.bytes(wave_script_source, 0xFE);
    std::copy(script.begin(), script.end(), wave_script_.begin());
    grant_flight();
    flash_background_ = 1;
    stage_ = Stage::vanish;
}

void Play::vanish_frame() {
    // $64CF: one frame of the wiz fading out.
    marker(0x6E82); // the end of the $6E71 wait
    playfield_.animate_stars(wiz_.speed_index(), random_);
    wiz_stick_.bits = 0xFF;
    catellite_stick_.bits = 0xFA; // up and left: the catellite flies off
    wiz_routine();
    update_catellite();
    tokens();
    ++shared1_; // $64EB: the playfield's shared colours cycle
    ++shared2_;
    if (sound_->volume() == 0) {
        sound_->fade(1);
        sound_->start_tune(0x13, 2, 0);
    }
    marker(0x682F); // the end of the $681D wait
    latch_sprites();
    if (!wiz_.appearing()) {
        // $6512
        sound_->fade(0xFF);
        sprite_mask_ = 0;
        flash_background_ = 0;
        pending_ = Pending::bonus_dark;
    }
}

void Play::bonus_setup_step() {
    marker(0x6E92); // $6788 and $678A each wait a frame (which moves the stars)
    playfield_.animate_stars(wiz_.speed_index(), random_);
    if (pending_ == Pending::bonus_dark) {
        cell_colours_.fill(0);
        enter_place();
        for (auto& e : enemy_shots_) e = EnemyShot{}; // $9CCB
        pending_ = Pending::bonus_colours;
        return;
    }
    cell_colours_.fill(0x0C);
    shared1_ = 6;
    shared2_ = 0x0E;
    wiz_.start_appear();
    sprite_mask_ = 3;
    message_state_ = 1;
    font_in_field_ = true; // $6743: the font over characters $A0-$DF
    field_font_ = assets_.front_charset;
    if (letters_inverted_)
        for (int c = 0; c < 0x40; ++c)
            for (auto& b : field_font_[std::size_t(c)]) b = std::uint8_t(~b);
    marker(0x654E); // (the copy is long: the frame's interrupt usually comes before the music starts)
    sound_->fade(1);
    sound_->reset_music();
    // $6556: the two bonus tunes take turns.
    bonus_tune_toggle_ ^= 2;
    marker(0x6573); // (where the frame's interrupts fall around the start varies)
    if (bonus_tune_toggle_ != 0) sound_->start_tune(0x0C, 2, 1);
    else sound_->start_tune(0x21, 1, 0);
    pending_ = Pending::none;
    stage_ = Stage::bonus;
}

void Play::bonus_frame(const Ports& ports) {
    // $6576
    marker(0x6E91); // the end of the $6E83 wait
    read_sticks(ports);
    if (dying_ == 0) erase_enemy_shots();
    erase_player_shots();
    if (pause_check()) return; // $6587
    bonus_frame_rest(ports);
}

void Play::bonus_frame_rest(const Ports& ports) {
    const bool playing = !(message_state_ & 2);
    if (playing) {
        wiz_routine();
        update_catellite();
        catellite_fire();
    }
    bonus_speed();
    wiz_.set_speed_index(std::int8_t(bonus_speed_));
    if (bonus_speed_ != 0) playfield_.animate_stars(bonus_speed_, random_);
    draw_player_shots();
    if (dying_ == 0) move_enemy_shots();
    tokens();
    messages();
    marker(0x682F); // the end of the $681D wait
    latch_sprites(); // what follows shows a frame later
    if (playing) wave_script();
    update_objects();
    read_keys(ports); // $65C9
    if (dying_ == 0) return;
    // $65D1: a hit, or the script's end ($36).
    const auto why = dying_;
    wiz_routine();
    if (why >= 0x80) {
        if (message_clock_ == 0) start_lab();
        return;
    }
    if (why == 0x36) {
        message_state_ = 3;
    } else {
        if (message_state_ != 0) {
            message_clock_ = 1;
            messages();
        }
        message_state_ = 2;
    }
    dying_ = 0x80;
}

void Play::bonus_speed() {
    // $6755: the starfield speeds up to 6 or slows to 0 in steps of 1/16.
    if (accelerating_) {
        if (bonus_speed_ != 6) {
            const unsigned sum = bonus_speed_fraction_ + 0x10;
            bonus_speed_fraction_ = std::uint8_t(sum);
            if (sum > 0xFF) ++bonus_speed_;
        }
    } else if (bonus_speed_ != 0) {
        const int diff = bonus_speed_fraction_ - 0x10;
        bonus_speed_fraction_ = std::uint8_t(diff);
        if (diff < 0) --bonus_speed_;
    } else {
        bonus_speed_fraction_ = 0;
    }
}

void Play::wave_script() {
    // $9CEA. Records: [kind (bit 7: wait for an empty screen), delay] spawns
    // after `delay` passes when a slot is free; [target + 2, marker, count]
    // repeats from `target` `count` times (the count is used up in the
    // copy); $FF / $FE speed the stars up / down; $FD ends the wave.
    const auto loop_marker = image_.byte(0xBB04);
    for (;;) {
        if (dying_ != 0) return;
        auto at = [&](int i) -> std::uint8_t& { return wave_script_[std::size_t(std::uint8_t(wave_at_ + i))]; };
        if (at(1) == loop_marker) {
            if (at(2) != 0) {
                --at(2);
                wave_at_ = std::uint8_t(at(0) - 2);
            } else {
                wave_at_ = std::uint8_t(wave_at_ + 3);
            }
            continue;
        }
        const auto op = at(0);
        if (op == 0xFD) {
            dying_ = 0x36;
            return;
        }
        if (op == 0xFF || op == 0xFE) {
            accelerating_ = op == 0xFF ? 0xFF : 0;
            ++wave_at_;
            return;
        }
        if (op & 0x80) {
            for (const auto& o : objects_)
                if (o.active()) return;
        } else if (free_slot() < 0) {
            return;
        }
        if (at(1) != wave_wait_) {
            ++wave_wait_;
            return;
        }
        wave_wait_ = 0;
        wave_at_ = std::uint8_t(wave_at_ + 2);
        const int kind = op & 0x7F;
        if (kind == 0x63) return; // a pause only
        const int s = free_slot();
        if (s < 0) return;
        spawn_template(s, bonus_templates_[std::size_t(kind)]);
        return;
    }
}

// --- messages -----------------------------------------------------------------

void Play::messages() {
    // $6615
    if (message_state_ == 0) return;
    if (!(message_state_ & 0x80)) {
        message_clock_ = 0;
        message_colour_ = message_flash_ = 0;
        if (message_state_ == 1) {
            message_id_ = 1; // "COLOUR COMPLETED / LABORATORY IMMINENT"
        } else {
            message_id_ = message_state_ == 2 ? 3 : 2; // the count, after "CONGRATULATIONS!" when the wave was beaten
            // $6648: only sprite 1, as the bonus icon beside the count.
            sprite_mask_ = 2;
            catellite_.show_icon(0x6D, 0x8C, 0x60, 5);
            kills_counted_ = 0;
            kill_digits_ = {0x20, 0x20, 0x30};
        }
    }
    message_state_ |= 0x80;
    if (message_state_ & 2) count_kills();
    show_message();
    if (--message_clock_ == 0) {
        message_state_ = 0;
        message_chars_ = 0; // $6685: printed again over the sky characters in black
        show_message();
        message_chars_ = 0xA0;
    }
}

void Play::count_kills() {
    // $6693: every second frame one more kill counted, 40 points each.
    if (kills_ == kills_counted_) return;
    message_clock_ = 0xFF;
    if (++kill_toggle_ & 1) return;
    add_points(4);
    ++kills_counted_;
    auto& d = kill_digits_;
    if (++d[2] != 0x3A) return;
    d[2] = 0x30;
    if (++d[1] == 0x21) d[1] = 0x31; // a blank tens digit becomes 1
    if (d[1] != 0x3A) return;
    d[1] = 0x30;
    if (++d[0] == 0x21) d[0] = 0x31;
}

void Play::show_message() {
    // $66FC: the colours run through $B61C while it shows; black otherwise.
    std::uint8_t colour = 0;
    if (message_clock_ != 0) {
        if ((++message_flash_ & 1) == 0 && ++message_colour_ == 0x0C) message_colour_ = 0;
        colour = image_.byte(std::uint16_t(message_colours + message_colour_));
    }
    print_text(image_.word(std::uint16_t(message_table + 2 * message_id_)), colour);
}

void Play::print_text(std::uint16_t address, std::uint8_t colour) {
    // $8CFA: lines of [column, row, characters..., $FF], ended by $FF, into
    // the screen shown (the place's characters $A0 on carry the font).
    auto text = [&](std::uint16_t a) -> std::uint8_t {
        if (a >= kill_digits && a < kill_digits + 3) return kill_digits_[std::size_t(a - kill_digits)];
        return image_.byte(a);
    };
    const int screen = playfield_.shown_screen();
    for (;;) {
        const auto column = text(address);
        if (column == 0xFF) return;
        const int row = text(std::uint16_t(address + 1)) - 3;
        address = std::uint16_t(address + 2);
        int c = column;
        for (;;) {
            const auto ch = text(address);
            address = std::uint16_t(address + 1);
            if (ch == 0xFF) break;
            playfield_.put(screen, c, row, std::uint8_t((ch & 0x3F) + message_chars_));
            set_cell_colour(c, row, colour);
            ++c;
        }
    }
}

} // namespace wizball::game

