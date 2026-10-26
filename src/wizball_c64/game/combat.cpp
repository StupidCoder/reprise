#include "wizball_c64/game/play.hpp"

// The wiz's and catellite's shots ($8444-$8557, $6F0D-$70D2), what the wiz
// runs into ($7E63), its shield ($7EA5), the death ($7DB8), the tokens
// ($72AA, $72DF) and the next wave of a place ($9EFD).
namespace wizball::game {

// --- cells -----------------------------------------------------------------------

std::uint8_t Play::field_colour() const {
    return flash_colours_ ? 8 : character_; // $894D
}

void Play::set_cell_colour(int column, int row, std::uint8_t colour) {
    if (column < 0 || column >= 40 || row < 0 || row >= Playfield::rows) return;
    cell_colours_[std::size_t(row * 40 + column)] = colour;
}

void Play::restore_cell(int screen, int column, int row, std::uint8_t code) {
    playfield_.put(screen, column, row, code);
    set_cell_colour(column, row, field_colour());
}

void Play::fill_field_colours() {
    // $7D1A
    cell_colours_.fill(field_colour());
}

// --- player shots ------------------------------------------------------------------

void Play::erase_player_shots() {
    // $7090: last drawn first.
    for (int k = 5; k >= 0; --k) {
        auto& s = shots_[std::size_t(k)];
        if (s.code == 0) continue;
        restore_cell(s.drawn_screen, s.drawn_column, s.drawn_row, s.saved);
        if (s.second_code != 0) restore_cell(s.drawn_screen, s.drawn_column + 1, s.drawn_row, s.second_saved);
    }
}

void Play::fire_shots() {
    // $8444: fire counts on the press, not while held.
    fire_pressed_ = false;
    if (fire_latch_ == 0) {
        fire_latch_ = wiz_stick_.bits & 0x10;
        return;
    }
    fire_latch_ = wiz_stick_.bits & 0x10;
    if (fire_latch_ == 0) fire_pressed_ = true;
}

void Play::launch_shots() {
    // $8467
    if (!fire_pressed_) return;
    if (spray_step_ == 0) {
        spray_clock_ = 0;
        spray_step_ = 1;
    }
    const int row = std::uint8_t(wiz_.y() - 0x40) >> 3;
    const int column = std::uint8_t((wiz_.x() & 0xFF) - 0x10) >> 3; // $8485: the low byte of x
    for (int k = 0; k < 2 && !carnage_; ++k) { // (carnage mode's gun takes the place of these)
        auto& s = shots_[std::size_t(k)];
        if (s.code != 0) continue;
        s.column = std::uint8_t(column);
        s.row = std::uint8_t(row);
        int direction = wiz_.last_horizontal() >= 0 ? 1 : 0;
        if (powers_.beam) {
            beam_side_ ^= 1;
            direction = beam_side_;
        }
        s.dx = direction ? 1 : -1;
        s.dy = 0;
        s.second_code = 0;
        std::uint8_t colour = 7;
        std::uint8_t code = image_.byte(std::uint16_t(0xB8D3 + (wiz_.y() & 7)));
        if (powers_.doubled) {
            code = std::uint8_t(code + 3);
            colour = 0x0C;
            s.second_code = code;
        }
        s.code = code;
        s.colour = colour;
        sound_->start_effect(0);
        break;
    }
    if (powers_.blazers && !powers_.blazers_on_catellite) launch_blazers(column, row);
}

void Play::launch_blazers(int column, int row) {
    // $850A: three at once, when the three slots are free.
    for (int k = 3; k < 6; ++k)
        if (shots_[std::size_t(k)].code != 0) return;
    blazer_set_ ^= 9;
    int y = blazer_set_;
    for (int k = 3; k < 6; ++k) {
        auto& s = shots_[std::size_t(k)];
        s.column = std::uint8_t(column);
        s.row = std::uint8_t(row);
        s.dx = std::int8_t(image_.byte(std::uint16_t(0xB8C1 + y)));
        s.dy = std::int8_t(image_.byte(std::uint16_t(0xB8C2 + y)));
        s.code = image_.byte(std::uint16_t(0xB8C3 + y));
        s.colour = 0x0B;
        s.second_code = 0;
        y += 3;
    }
}

void Play::draw_player_shots() {
    // $6F0D
    for (int k = 0; k < 6; ++k) {
        auto& s = shots_[std::size_t(k)];
        if (s.code == 0) continue;
        const int step = playfield_.step();
        if (step > 0) --s.column;
        else if (step < 0) ++s.column;
        s.column = std::uint8_t(s.column + s.dx);
        if (s.column >= 0x27) {
            s.code = 0;
            continue;
        }
        s.row = std::uint8_t(s.row + s.dy);
        if (s.row >= 0x14) {
            s.code = 0;
            continue;
        }
        // $6FAE: an object at the shot's point is hit.
        if (object_at((s.column * 4 + 0x0C) & 0xFF, (s.row * 8 + 0x4B) & 0xFF) >= 0) {
            s.code = 0;
            continue;
        }
        const int screen = playfield_.shown_screen();
        const auto under = playfield_.code_on(screen, s.column, s.row);
        if (under & 0x80) {
            s.code = 0;
            continue;
        }
        s.saved = under;
        s.drawn_screen = screen;
        s.drawn_column = s.column;
        s.drawn_row = s.row;
        playfield_.put(screen, s.column, s.row, s.code);
        set_cell_colour(s.column, s.row, s.colour);
        if (s.second_code != 0) {
            const auto under2 = s.column + 1 < 40 ? playfield_.code_on(screen, s.column + 1, s.row) : std::uint8_t(0);
            if (under2 & 0x80) {
                s.second_code = 0;
            } else {
                s.second_saved = under2;
                playfield_.put(screen, s.column + 1, s.row, s.code);
                set_cell_colour(s.column + 1, s.row, s.colour);
            }
        }
    }
}

// --- the wiz against things ----------------------------------------------------------

void Play::wiz_touches() {
    // $7E63: the point under the wiz.
    const int s = object_at((wiz_.x() >> 1) & 0xFF, (wiz_.y() + 0x0B) & 0xFF);
    if (s < 0) return;
    auto& o = objects_[std::size_t(s)];
    if (std::uint8_t(o.state - 2) & 0x80) { // CMP #2 / BMI: an enemy
        ++wiz_hit_;
        return;
    }
    if (o.state == 5) { // a pearl lights the next token
        o.y = 0;
        o.box_top = o.box_bottom = 0;
        ++pearls_;
        add_points(0x0A);
    }
}

void Play::shield_and_hits() {
    // $7EA5
    if (shield_ == 0) {
        token_pointer_[6] = 0;
    } else {
        if (++shield_clock_ == 0x32) {
            shield_clock_ = 0;
            --shield_;
        }
        if (shield_warned_ == 0 && shield_ < 8) {
            sound_->start_effect(0x12);
            ++shield_warned_;
        }
        if (shield_ < 5) {
            shield_ = 0;
            token_pointer_[6] = 0;
        }
    }
    // $7EDF
    if (wiz_hit_ == 0) return;
    wiz_hit_ = 0;
    if (shield_ != 0) {
        shield_ = std::uint8_t(shield_ - 8);
        return;
    }
    if (level_cleared_ == 0 && invincible_) return; // $BB07: the cheat
    dying_ = 1;
}

void Play::spray() {
    // $8592: rings above and below the wiz after each shot.
    if (!powers_.spray) return;
    if (spray_step_ == 0) {
        spray_top_ = spray_bottom_ = 5;
        return;
    }
    const int x = (wiz_.x() >> 1) & 0xFF;
    object_at(x, std::uint8_t(wiz_.y() - 0x0A));
    object_at(x, std::uint8_t(wiz_.y() + 0x1E));
    if ((++spray_clock_ & 3) == 0) {
        spray_clock_ = 0;
        ++spray_step_;
        ++spray_colour_;
        if ((spray_step_ & 3) == 0) {
            spray_step_ = 0;
            return;
        }
    }
    spray_top_ = std::uint8_t(spray_step_ * 2 + 0x3E);
    spray_bottom_ = std::uint8_t(spray_step_ * 2 + 0x3F);
    spray_tint_ = image_.byte(std::uint16_t(0xB881 + (spray_colour_ & 3)));
    if (spray_clock_ & 1) spray_tint_ = 0;
}

void Play::start_death() {
    // $7DB8: the wiz bursts into sixteen pieces.
    erase_enemy_shots();
    const int row = std::uint8_t(wiz_.y() - 0x40) >> 3;
    const int column = std::uint8_t((wiz_.x() & 0xFF) - 0x10) >> 3;
    for (auto& e : enemy_shots_) {
        e.row = std::uint8_t(row);
        e.column = std::uint8_t(column);
        e.dx = std::int8_t((random_.next() & 0x0F) - 7);
        e.dy = std::int8_t((random_.next() & 0x0F) - 7);
        e.clock = 0;
        e.state = 2;
    }
    marker(0x7DBE); // (the debris set-up is slow: an interrupt may come first)
    sound_->stop_tune();
    sound_->start_effect(0x15);
    sound_->start_effect(0x16);
    sound_->start_effect(0x17);
    wiz_.set_speed_index(0);
}

void Play::death_step() {
    // $7DDA
    if (--death_clock_ == 0) death_clock_ = 1;
    if (level_cleared_ == 0) move_enemy_shots();
    sprite_mask_ &= 0xFE;
}

// --- the catellite -------------------------------------------------------------------

void Play::update_catellite() {
    // $8640
    marker(0x8640);
    if (!powers_.catellite) {
        catellite_.lose();
        return;
    }
    catellite_body();
}

void Play::catellite_body() {
    // $8652
    if (catellite_.wandering()) {
        bool beep = false;
        catellite_stick_.bits = catellite_.wander_stick(random_, beep);
        if (beep) sound_->start_effect(0x0F);
    }
    if (catellite_.hits() == 9) { // lost: token 3 can be taken again
        powers_.catellite = false;
        token_pointer_[2] = 2;
        sound_->start_effect(0x0F);
        return;
    }
    catch_with_catellite();
    if (catellite_.shield_step()) sound_->start_effect(0x13);
    if (catellite_hit_ != 0) {
        catellite_hit_ = 0;
        if (!catellite_protected_ && catellite_.hit()) sound_->start_effect(4);
    }
    catellite_.move(catellite_stick_, wiz_, in_lab_);
    if (carnage_) carnage_flail();
    catellite_.animate(wander_colours_);
}

void Play::catch_with_catellite() {
    // $88B5: what the catellite runs into. Enemies hurt it; falling
    // droplets are caught, and what a droplet does depends on its colour.
    const int s = object_at(catellite_.x(), std::uint8_t(catellite_.y() + 0x0B));
    if (s < 0) return;
    auto& o = objects_[std::size_t(s)];
    if (std::uint8_t(o.state - 2) & 0x80) {
        if (!(carnage_ && flail_on_)) ++catellite_hit_; // (carnage's flail smashes them unhurt)
        return;
    }
    if (o.state != 2) return;
    o.y = 0;
    o.box_top = o.box_bottom = 0;
    switch (o.kind) {
    case 2: case 5: case 6: // paint, whatever its colour, for the place's colour ($B194)
        ++colour_[std::size_t(image_.byte(std::uint16_t(0xB194 + place_)) % 3)];
        add_points(5);
        break;
    case 1: // white: an extra life
        ++lives_[std::size_t(player_)];
        sound_->start_effect(0x11);
        break;
    case 4: // purple: the catellite goes wandering
        catellite_.start_wandering();
        break;
    case 0x0E: // light blue: a burst of fillers to the warning tune
        filler_burst_ = 6;
        sound_->start_tune(5, 1);
        break;
    case 0: // black: the landscape goes black
        flash_colours_ = true;
        flash_show_ = 1;
        fill_field_colours();
        break;
    default:
        catellite_.set_hits(0x80); // it no longer counts hits
        break;
    }
}

void Play::catellite_fire() {
    // $881F: it fires with the stick's fire, one shot at a time (shot 2).
    if (!powers_.catellite || (catellite_stick_.bits & 0x10)) return;
    const auto cx = catellite_.x(), cy = catellite_.y();
    // $8558: the blazers when they are switched over to the catellite.
    if (powers_.blazers_on_catellite) launch_blazers(std::uint8_t(cx - 0x0C) >> 2, std::uint8_t(cy - 0x40) >> 3);
    auto& s = shots_[2];
    if (s.code != 0) return;
    s.column = std::uint8_t(std::uint8_t(cx - 8) >> 2);
    s.row = std::uint8_t(std::uint8_t(cy - 0x44) >> 3);
    s.dx = wiz_.last_horizontal() >= 0 ? 1 : -1;
    if (!catellite_.following()) s.dx = catellite_.stick_dx() >= 0 ? 1 : -1;
    s.dy = 0;
    s.second_code = 0;
    std::uint8_t colour = 7;
    std::uint8_t code = image_.byte(std::uint16_t(0xB8D3 + ((cy + 4) & 7)));
    if (powers_.doubled) {
        code = std::uint8_t(code + 3);
        colour = 0x0C;
        s.second_code = code;
    }
    s.code = code;
    s.colour = colour;
    sound_->start_effect(catellite_.hits() == 8 ? 4 : 0);
}

// --- tokens ---------------------------------------------------------------------------

void Play::tokens() {
    // $72AA: each pearl moves the light one token on; four waggles take it.
    if (pearls_ != 0) {
        --pearls_;
        if (++token_selected_ == 8) token_selected_ = 1;
        sound_->start_effect(9);
        return;
    }
    if (waggles_ == 4) activate_token();
}

void Play::activate_token() {
    // $72DF
    const int t = token_selected_;
    if (t == 0 || token_pointer_[std::size_t(t - 1)] == 9) return;
    if (weapon_select_ == 0) sound_->start_effect(t == 6 ? 0x0D : 8); // quiet while re-arming ($B074)
    token_selected_ = 0;
    auto& icon = token_pointer_[std::size_t(t - 1)];
    switch (t) {
    case 1:
        if (!powers_.thrust) {
            powers_.thrust = true;
            icon = 3;
        } else {
            powers_.antigravity = true;
            icon = 9;
        }
        break;
    case 2:
        if (!powers_.spray) {
            powers_.spray = true;
            icon = 4;
        } else {
            powers_.beam = true;
            icon = 9;
        }
        break;
    case 3:
        powers_.catellite = true;
        icon = 9;
        catellite_.reset();
        break;
    case 4:
        powers_.doubled = true;
        icon = 9;
        break;
    case 5:
        if (!powers_.blazers) {
            powers_.blazers = true;
            icon = 0x0A;
        } else {
            powers_.blazers_on_catellite = !powers_.blazers_on_catellite;
            icon = powers_.blazers_on_catellite ? 0x0A : 8;
        }
        break;
    case 6: // the smart bomb
        flash_background_ = 1;
        if (carnage_) carnage_smart_bomb();
        for (auto& o : objects_) o.hit = 1;
        erase_enemy_shots();
        for (auto& e : enemy_shots_) e = EnemyShot{};
        break;
    case 7:
        shield_ = 0x28;
        shield_clock_ = 0;
        shield_warned_ = 0;
        catellite_.raise_shield();
        icon = 9;
        break;
    default: break;
    }
}

// --- waves ------------------------------------------------------------------------------

void Play::open_next_wave() {
    // $9EFD: all of a place's list done and nothing on screen: a new list.
    if (level_cleared_ != 0) return;
    if (aliens_left_ != 0 || (sprite_mask_ & 0xFC) != 0) return;
    if (std::int8_t(std::uint8_t(furthest_ - place_ - 3)) >= 0) return;
    new_spawn_list(place_);
    sound_->start_effect(0x14);
    add_points(0x64);
    cursor_ = 0;
    count_aliens();
}

} // namespace wizball::game
