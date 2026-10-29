#include "wizball_c64/game/play.hpp"

// The Wizlab ($AB20), place 9's screen shown in 40 columns. The token bar
// offers the five weapons; the stick moves the light and fire keeps one
// for good (it goes in the list $B116-$B11E that every life start re-arms,
// $6E4F). Meanwhile the wiz walks in on a scripted stick, the paint pots
// pour the completed colour out of the cauldrons into the place, a little
// at a time for points, and, if the catellite was out, it flies a scripted
// path to its perch. Then the place is one colour more painted ($ADEC) and
// play goes on through a place change, or, when it is fully painted, the
// level is flown through for points ($AE11).
namespace wizball::game {
namespace {
constexpr std::uint16_t lab_icons = 0xB4DE;        // the token bar's icons in the lab
constexpr std::uint16_t wiz_walk = 0xB17C;         // sticks, seven frames each, 0 = done
constexpr std::uint16_t catellite_path = 0xAC43;   // the same for the catellite
constexpr std::uint16_t perch_frames = 0xAC4B;
constexpr std::uint16_t select_text = 0xB02F;      // "SELECT PERMANENT" / "WEAPON" (16 + 16)
constexpr std::uint16_t chosen_text = 0xB0FC;      // "ALL SYSTEMS GO!!"
} // namespace

void Play::rearm_weapons(bool lab) {
    // $6E4F (life start) and $B002 (lab): every kept weapon taken again,
    // silently ($B074 set), last first.
    weapon_select_ = lab ? 6 : 0x23;
    for (int y = 8; y >= 0; --y) {
        token_selected_ = retained_[std::size_t(y)];
        activate_token();
    }
    if (!lab) weapon_select_ = 0;
}

void Play::clear_for_lab() {
    // $AAC7
    sprite_mask_ = 0;
    catellite_.hide();
    spray_step_ = 0;
    dying_ = death_clock_ = 0;
    spray_top_ = spray_bottom_ = 5;
    for (auto& e : enemy_shots_) e = EnemyShot{};
    lab_return_place_ = std::uint8_t(place_);
    wiz_.set_speed_index(0);
    playfield_.set_view_column(0);
    level_cleared_ = 0;
}

void Play::draw_room(int room) {
    // $75EA with $AB9C = 0: the room's screen, characters and colours only.
    place_ = room;
    playfield_.draw_first_screen(place_, 5, false);
    font_in_field_ = false; // $77C6: the room's characters
    charset_place_ = place_;
    fill_field_colours();
    for (int i = 0; i < 8; ++i) playfield_.prepare_step();
}

void Play::lab_settings() {
    // $AB02
    playfield_.set_fine_scroll(7);
    columns40_ = true;
    perch_state_ = 0;
    lab_wiz_state_ = 0;
    shield_ = 0;
    catellite_.drop_shield();
}

void Play::start_lab() {
    clear_for_lab();
    draw_room(9); // $AB25
    for (auto& o : objects_) o = Object{}; // $9CD5
    cursor_ = 0;
    sprite_mask_ &= 3;
    place_ = lab_return_place_;
    level_cleared_ = 6;
    shared1_ = 6;
    shared2_ = 0x0E;
    character_ = 0x0B;
    fill_field_colours();
    lab_settings();
    sprite_shared_ = {5, 1};
    marker(0xAB58); // (the set-up's frame waits come before the flag, $AB55)
    in_lab_ = true;
    // $AFD5: the powers put by; the bar shows the weapons.
    saved_powers_ = powers_;
    powers_ = {};
    weapon_chosen_ = false;
    for (int i = 0; i < 6; ++i) {
        saved_icons_[std::size_t(i)] = token_pointer_[std::size_t(i)];
        token_pointer_[std::size_t(i)] = image_.byte(std::uint16_t(lab_icons + i));
    }
    saved_token_ = token_selected_;
    rearm_weapons(true);
    token_selected_ = 1;
    for (int i = 0; i < 16; ++i) {
        hud_text_[0][std::size_t(i)] = image_.byte(std::uint16_t(select_text + i)) & 0x3F;
        hud_text_[1][std::size_t(i)] = image_.byte(std::uint16_t(select_text + 16 + i)) & 0x3F;
    }
    hud_text_shown_ = true;
    marker(0xAB5B); // (the re-arming is slow: interrupts may come first)
    sound_->reset_music();
    sound_->set_mode(0);
    sound_->set_speed(0);
    if (bonus_tune_toggle_ != 0) sound_->start_tune(0x3D, 2, 2);
    stage_ = Stage::lab;
}

void Play::lab_frame(const Ports& ports) {
    // $AB7B
    marker(0x6E82); // the end of the $6E71 wait
    choose_weapon(ports);
    // $AAA6: the hum, and now and then effect $19.
    if (!sound_->slot_busy(1) || ++hum_clock_ == 0) sound_->start_effect(0x18);
    if ((random_.next() & 0x0F) == 8) sound_->start_effect(0x19);
    playfield_.animate_stars(wiz_.speed_index(), random_);
    perch_catellite();
    lab_wiz();
    if (stage_ != Stage::lab) return;
    for (int i = 0; i < 10; ++i) update_cauldrons(); // $AB8D: the cauldrons drawn ten rows a frame
    latch_sprites();
}

void Play::choose_weapon(const Ports& ports) {
    // $B076
    if (weapon_chosen_) return;
    read_keys(ports); // $9420 scans the keys first
    // $9420: the sticks only count when they change.
    const auto now = std::uint8_t(ports.port2 & ports.port1);
    if (now == stick_last_) return;
    stick_last_ = now;
    if (!(now & 8) && token_selected_ != 5) ++token_selected_;
    if (!(now & 4) && token_selected_ != 1) --token_selected_;
    if (now & 0x10) return;
    if (token_pointer_[std::size_t(token_selected_ - 1)] == 9) return; // had already
    for (;;) {
        for (int i = 0; i < 9; ++i) {
            if (retained_[std::size_t(i)] != 0) continue;
            chosen_weapon_ = token_selected_;
            retained_[std::size_t(i)] = token_selected_;
            activate_token();
            weapon_chosen_ = true;
            token_selected_ = 0;
            for (int c = 0; c < 16; ++c) {
                hud_text_[0][std::size_t(c)] = image_.byte(std::uint16_t(chosen_text + c)) & 0x3F;
                hud_text_[1][std::size_t(c)] = 0;
            }
            return;
        }
        // $B0BA: the list is full: two kept blazers make room. Without
        // them the original loops for ever at $B0D2; so does this.
        int removed = 0;
        for (int i = 0; i < 9 && removed < 2; ++i) {
            if (retained_[std::size_t(i)] == 5) {
                retained_[std::size_t(i)] = 0;
                ++removed;
            }
        }
        if (removed < 2) {
            hung_ = true;
            return;
        }
    }
}

void Play::perch_catellite() {
    // $AB9D: only when it was out.
    if (!saved_powers_.catellite) return;
    if (perch_state_ == 0) {
        sprite_mask_ |= 2;
        catellite_.place_at(0x76, 0x3C);
        catellite_.stop();
        perch_clock_ = perch_step_ = 0;
        perch_state_ = 1;
        if (!(catellite_.hits() & 0x80)) catellite_.set_hits(0);
    }
    if (perch_state_ != 2) {
        if (++perch_clock_ == 7) {
            ++perch_step_;
            perch_clock_ = 0;
        }
        const auto stick = image_.byte(std::uint16_t(catellite_path + perch_step_));
        if (stick != 0) {
            catellite_stick_.bits = std::uint8_t(stick | 0x10);
            catellite_body(); // $8652
            return;
        }
        perch_state_ = 2;
        sprite_mask_ |= 4;
        lab_sprites_[0] = {0x5B, 0xC2, lab_sprites_[0].pointer, lab_sprites_[0].colour};
        perch_step_ = 0x3C;
    }
    // $AC22: the perch's light fades.
    ++perch_clock_;
    lab_sprites_[0].pointer = image_.byte(std::uint16_t(perch_frames + ((perch_clock_ >> 4) & 1)));
    const auto a = perch_step_;
    if (a != 0) --perch_step_;
    lab_sprites_[0].colour = a;
}

void Play::lab_wiz() {
    // $AC4F
    if (lab_wiz_state_ == 0) {
        playfield_.set_fine(0x4B);
        playfield_.set_fraction(0);
        wiz_.enter_lab();
        lab_clock_ = lab_step_ = 0;
        lab_tick_ = 0;
        lab_wait_ = 5;
        lab_wiz_state_ = 1;
        wiz_routine();
        sprite_mask_ |= 1;
    }
    auto& pot = lab_sprites_[1];
    auto& pour = lab_sprites_[2];
    if (lab_wiz_state_ == 1) {
        if (++lab_clock_ == 7) {
            ++lab_step_;
            lab_clock_ = 0;
        }
        const auto stick = image_.byte(std::uint16_t(wiz_walk + lab_step_));
        if (stick != 0) {
            wiz_stick_.bits = std::uint8_t(stick | 0x10);
            wiz_routine();
            return;
        }
        // $ACBC: the paint pots, where the wiz stands.
        lab_wiz_state_ = 2;
        sprite_mask_ |= 0x18;
        pot = {wiz_.x() & 0xFF, 0xC3, 0x68, 8};
        pour = {wiz_.x() & 0xFF, 0xC3, pour.pointer, 8};
    }
    if (lab_wiz_state_ == 2) {
        // $ACE7: they rise and slide left.
        if (++lab_clock_ & 1) {
            --pour.x;
            --pot.x;
        }
        if (pot.y != 0xAE) --pot.y;
        ++lab_step_;
        pour.pointer = std::uint8_t(0x69 + ((lab_step_ >> 3) & 1));
        if (pot.x != 0x9A) return;
        lab_wiz_state_ = 3;
        wiz_.set_speed_index(0);
        for (int i = 0; i < 3; ++i)
            paint_left_[std::size_t(i)] = image_.byte(std::uint16_t(0xB2C0 + panel_extra_ * 3 + i));
    }
    // $AD3B: pouring: every 20 frames one unit of the completed colour.
    int a = 0;
    if (colour_[3] != 0) {
        ++lab_step_;
        a = (lab_step_ >> 2) & 2;
    }
    pot.pointer = std::uint8_t(0x6B + a);
    pour.pointer = std::uint8_t(pot.pointer + 1);
    ++lab_tick_;
    const int odd = lab_tick_ & 1;
    if (lab_tick_ == 0x14) {
        lab_tick_ = 0;
        if (colour_[3] == 0 && --lab_wait_ == 0) {
            leave_lab();
            return;
        }
        for (int i = 0; i < 3; ++i) {
            if (paint_left_[std::size_t(i)] == 0) continue;
            --paint_left_[std::size_t(i)];
            --colour_[std::size_t(i)];
            --colour_[3];
            break;
        }
    }
    if (colour_[3] != 0) add_points(odd);
}

void Play::leave_lab() {
    // $AD98
    sprite_mask_ = 0;
    columns40_ = false;
    level_cleared_ = 0;
    in_lab_ = false;
    // $B04F
    powers_ = saved_powers_;
    for (int i = 0; i < 6; ++i) token_pointer_[std::size_t(i)] = saved_icons_[std::size_t(i)];
    token_selected_ = saved_token_;
    weapon_select_ = 0;
    hud_text_shown_ = false; // $70EC: the score lines back
    hud_band_colour_.reset();
    add_points(0);
    sprite_shared_ = {0x0B, 0x0D};
    if (!weapon_chosen_)
        for (int p = place_; p != 9; ++p) add_points(0x64); // no weapon taken: 1000 points a place to go
    dying_ = death_clock_ = 0;
    wiz_.leave_lab();
    wiz_stick_.bits = 0xFF;
    for (auto& o : objects_) o = Object{}; // $9CD5
    cursor_ = 0;
    sprite_mask_ &= 3;
    ++painted_[std::size_t(place_)];
    after_lab();
}

void Play::after_lab() {
    // $ADEF
    set_furthest();
    sound_->reset_music();
    sound_->set_mode(1);
    sound_->set_speed(0);
    if (painted_[std::size_t(place_)] != 3) {
        start_arrival();
        arrival_->back_to_play = true;
        stage_ = Stage::play;
        return;
    }
    start_flight(); // the place fully painted
}

} // namespace wizball::game
