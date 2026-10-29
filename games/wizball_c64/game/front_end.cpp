#include "wizball_c64/game/front_end.hpp"

namespace wizball::game {
namespace {
constexpr std::uint16_t title_text = 0xBEB5;      // $BFFE: the top scores block
constexpr std::uint16_t logo_text = 0xB693;       // $B7DA: "WIZBALL" in the big font
constexpr std::uint16_t top_line_text = 0xBFE8;   // $B7DC: "1. 050000"
constexpr std::uint16_t top_name_text = 0xB69B;   // $B7DE: the top name, big
constexpr std::uint16_t menu_text = 0xB6B6;       // $B7E0
constexpr std::uint16_t mega_text = 0xB633;       // $B7E2: "WHAT A MEGA DISPLAY ..."
constexpr std::uint16_t words_text = 0xB668;      // $B7E4: "AND ALSO A FEW WORDS"
constexpr std::uint16_t team_names_text = 0xB680; // $B7E6: "WIZARD  FELINE"
constexpr std::uint16_t key_table = 0x6D94;       // key number → character
constexpr int return_key = 0x3F, delete_char = 0x14;
} // namespace

FrontEnd::FrontEnd(const data::Assets& assets, const data::GameImage& image, RandomSource& random)
    : assets_(assets), image_(image), random_(random), sound_(image, random), colours_(image), scores_(image) {
    start_place_ = image.byte(0xAF7E);
    start_limit_ = image.byte(0xAF7D);
    mode_ = image.byte(0xB272);
    tips_page_ = image.byte(0x944C);
    shot_volume_ = image.byte(0xBA81);
    for (int k = 0; k < 3; ++k) {
        tips_clock_[std::size_t(k)] = image.byte(std::uint16_t(0x96EE + k));
        tips_phase_[std::size_t(k)] = image.byte(std::uint16_t(0x96F1 + k));
    }
    tips_cat_clock_ = image.byte(0x97C0);
    tips_cat_phase_ = image.byte(0x97C1);
    start_title(true);
}

// --- the frame ----------------------------------------------------------------

void FrontEnd::tick(const Controls& controls) {
    // $AF85 at line 1 of the title's and the menu's lists, on the last
    // frame's key; the wiztips pages animate at line 1.
    if (screen_ == Screen::title || screen_ == Screen::menu) title_keys();
    if (screen_ == Screen::tips) tips_line1();
    switch (screen_) {
    case Screen::title: title_frame(controls); break;
    case Screen::menu: menu_frame(controls); break;
    case Screen::tips: tips_frame(controls); break;
    case Screen::loading: loading_frame(); break;
    case Screen::player: player_frame(controls); break;
    case Screen::play: play_frame(controls); break;
    case Screen::game_over: game_over_frame(controls); break;
    case Screen::scores: scores_frame(controls); break;
    }
    // The raster interrupts' sound work, every screen's list has all four.
    // While a later life's place is set up the play's list still runs.
    if (screen_ == Screen::play || screen_ == Screen::game_over ||
        (screen_ == Screen::loading && loading_in_play_)) {
        play_->frame();
    } else {
        sound_.frame();
    }
    sound_.extra_interrupt(0xB903);
    sound_.extra_interrupt(0xB8FC);
    sound_.extra_interrupt(0xB8F3);
}

// --- shared work ----------------------------------------------------------------

void FrontEnd::read_keys(const Controls& c, bool all) {
    keys_.update(all ? scan_all_keys(c.keys) : scan_few_keys(c.keys));
}

std::uint8_t FrontEnd::stick_change(const Controls& c) {
    // $91FA: both ports together; $FF while they stay as they were.
    const auto now = std::uint8_t((c.ports.port2 & c.ports.port1) | 0xE0);
    if (now == stick_last_) return 0xFF;
    stick_last_ = now;
    return now;
}

void FrontEnd::set_timer(std::uint8_t t) {
    timer_ = t;
    timer_clock_ = 5;
}

bool FrontEnd::timer_runs() {
    // $921E: a step every fifth frame; out only on the step that reaches
    // zero (so a timer of 0 runs 256 steps).
    if (--timer_clock_ != 0) return true;
    timer_clock_ = 5;
    return --timer_ != 0;
}

void FrontEnd::move_stars() {
    // $90BD: four stars fly out from the screen's middle at random speeds
    // (8.8, doubled), a new one where one has left.
    for (auto& s : stars_) {
        if (s.x == 0) {
            s.x = std::uint8_t(((random_.next() & 0x7F) + 0x24)); // the ADC's carry is clear here
            s.y = 0x91;
            s.dx = std::uint8_t(random_.next() | 0x20);
            s.dy = std::uint8_t(random_.next() | 0x20);
        }
        // $910A
        auto step = [](std::uint8_t& whole, std::uint8_t& fraction, std::uint8_t speed) {
            const unsigned doubled = unsigned(speed) << 1;
            const unsigned sum = fraction + (doubled & 0xFF);
            fraction = std::uint8_t(sum);
            whole = std::uint8_t(whole + (doubled >> 8) + (sum >> 8));
        };
        step(s.x, s.x_fraction, s.dx);
        step(s.y, s.y_fraction, s.dy);
        const bool x_out = !(std::uint8_t(s.x - 0x0C) & 0x80) && !(std::uint8_t(s.x - 0xAC) & 0x80);
        if (x_out || !(std::uint8_t(s.y - 0xFA) & 0x80)) s.x = 0;
    }
}

void FrontEnd::title_keys() {
    // $AF85: the keys 0-5 choose the start place, up to the furthest
    // reached last game less two (the border shows it for a frame); a
    // seven-key sequence ($AFCE) turns the invincibility cheat on.
    const auto code = keys_.code;
    border_ = 0;
    for (int x = 0; x < 6; ++x) {
        if (code != image_.byte(std::uint16_t(0xAF7F + x))) continue;
        if (cheats_.any_start_place || std::int8_t(std::uint8_t(x - start_limit_)) <= 0) {
            start_place_ = std::uint8_t(x);
            border_ = std::uint8_t(x);
        }
        break;
    }
    if (code == 0) return;
    if (code != image_.byte(std::uint16_t(0xAFCE + cheat_step_))) {
        cheat_step_ = 0;
        return;
    }
    if (++cheat_step_ != 7) return;
    cheat_ = true;
    border_ = 9;
    cheat_step_ = 0;
}

// --- the text screen ($EC00) -----------------------------------------------------

void FrontEnd::clear_screen(std::uint8_t code, std::uint8_t colour) {
    codes_.fill(code);
    cells_.fill(colour);
}

std::uint8_t FrontEnd::text_byte(std::uint16_t address) const {
    if (address >= 0xB69B && address < 0xB6B6) return scores_.top_name_byte(address);
    if (address >= 0xBEB5) return scores_.block_byte(address);
    return image_.byte(address);
}

void FrontEnd::print(std::uint16_t address, std::uint8_t colour, bool big) {
    // $8CFA: lines [column, row, text..., $FF], $FF to end. Big ($8CF4):
    // each character 2 x 2 cells from $B591 ($40/$41 over $60/$61 on), one
    // cell narrower for I and L.
    for (;;) {
        const auto column = text_byte(address);
        if (column == 0xFF) return;
        const int row = text_byte(std::uint16_t(address + 1));
        address = std::uint16_t(address + 2);
        int at = row * 40 + column;
        for (;; address = std::uint16_t(address + 1)) {
            const auto ch = text_byte(address);
            if (ch == 0xFF) {
                address = std::uint16_t(address + 1);
                break;
            }
            const int c = ch & 0x3F;
            auto put = [&](int cell, std::uint8_t code) {
                if (cell < 0 || cell >= 1000) return;
                codes_[std::size_t(cell)] = code;
                cells_[std::size_t(cell)] = colour;
            };
            if (!big) {
                put(at++, std::uint8_t(c));
                continue;
            }
            put_big(at, ch, colour);
        }
    }
}

void FrontEnd::put_big(int& at, std::uint8_t ch, std::uint8_t colour) {
    // $8D99
    const int c = ch & 0x3F;
    const auto base = image_.byte(std::uint16_t(0xB591 + c));
    const std::uint8_t codes[] = {std::uint8_t(base + 0x40), std::uint8_t(base + 0x41), std::uint8_t(base + 0x60),
                                  std::uint8_t(base + 0x61)};
    const int cells[] = {at, at + 1, at + 40, at + 41};
    for (int k = 0; k < 4; ++k)
        if (cells[k] >= 0 && cells[k] < 1000) {
            codes_[std::size_t(cells[k])] = codes[k];
            cells_[std::size_t(cells[k])] = colour;
        }
    at += c == 9 || c == 0x0C ? 1 : 2;
}

// --- sprites $80-$8F ($E000) -------------------------------------------------------

std::uint8_t& FrontEnd::sprite_byte(int pointer, int offset) {
    return sprites_[std::size_t((pointer - 0x80) * 64 + offset)];
}

void FrontEnd::fill_sprites(int first, int count, std::uint8_t value) {
    for (int p = first; p < first + count; ++p)
        for (int o = 0; o < 64; ++o) sprite_byte(p, o) = value;
}

void FrontEnd::sprite_text(int pointer, std::uint16_t address, int first_row, bool big) {
    // $8DE4 / $8E32: as Play::draw_sprite_text (three columns to a sprite).
    int column = 0;
    for (int i = 0;; ++i) {
        const auto ch = text_byte(std::uint16_t(address + i));
        if (ch == 0xFF) return;
        auto put = [&](int glyph, int row_offset) {
            const auto& g = assets_.front_charset[std::size_t(glyph)];
            for (int r = 0; r < 8; ++r)
                sprite_byte(pointer + column / 3, (first_row + row_offset + r) * 3 + column % 3) = g[std::size_t(r)];
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

// --- the title ($8F61) ---------------------------------------------------------

void FrontEnd::start_title(bool with_music) {
    if (with_music) {
        // $8F61: the title tune.
        sound_.reset_music();
        sound_.start_tune(0x1A, 2, 2);
    }
    // $8F75
    keys_.code = 0;
    border_ = 0;
    clear_screen(0x20, 0); // $8F41
    letters_inverted_ = true; // $8C8D
    big_inverted_ = false;    // $8CB5
    print(title_text, 0, false);
    colours_.clear();
    fill_sprites(0x80, 5, 0);
    sprite_text(0x80, logo_text, 0, true);
    fill_sprites(0x85, 7, 0);
    sprite_text(0x85, top_line_text, 0, false);
    print(top_name_text, 1, true);
    fill_sprites(0x8C, 3, 0xFF);
    set_timer(0x96);
    screen_ = Screen::title;
}

void FrontEnd::title_frame(const Controls& c) {
    // $8FEE
    if (keys_.code == keys::space) {
        start_tips();
        return;
    }
    colours_.title_frame();
    if (!timer_runs()) {
        start_menu();
        return;
    }
    read_keys(c, true);
    const auto s = stick_change(c);
    if (s == 0xFF) return;
    if (!(s & 0x10)) start_game();
    else start_menu();
}

// --- the menu ($9017) ----------------------------------------------------------

void FrontEnd::start_menu() {
    big_inverted_ = true; // $8CBE
    clear_screen(0x20, 0);
    print(menu_text, 0, false);
    dot_sprite_ = true; // $903D: sprite 5 a dot
    set_timer(0x82);
    screen_ = Screen::menu;
}

void FrontEnd::menu_frame(const Controls& c) {
    // $906C
    colours_.run_right();
    colours_.pulse_d1();
    colours_.pulse_d6();
    move_stars();
    if (keys_.code == keys::space) {
        start_tips();
        return;
    }
    if (!timer_runs()) {
        start_title(false);
        return;
    }
    read_keys(c, true);
    const auto s = stick_change(c);
    if (s == 0xFF) return;
    // $91DB: up and down choose the mode.
    if (!(s & 1) && mode_ != 0) --mode_;
    if (!(s & 2) && mode_ != 4) ++mode_;
    set_timer(0x5A);
    if (!(s & 0x10)) start_game();
}

// --- the wiztips ($944D) ----------------------------------------------------------

void FrontEnd::tips_line1() {
    if (tips_page_ == 0) {
        // $9681: token 1 flashing as on the bar ($7269), three animations.
        if ((++token_clock_ & 3) == 0 && ++token_flash_ == 6) token_flash_ = 0;
        tips_token_colour_ = image_.byte(std::uint16_t(0xB53D + token_flash_));
        for (int k = 0; k < 3; ++k) {
            if (++tips_clock_[std::size_t(k)] != image_.byte(std::uint16_t(0x96F4 + k))) continue;
            tips_clock_[std::size_t(k)] = 0;
            tips_phase_[std::size_t(k)] = std::uint8_t((tips_phase_[std::size_t(k)] + 1) & 3);
        }
    } else if (tips_page_ == 1) {
        // $9762
        if ((++tips_cat_clock_ & 3) == 0) ++tips_cat_phase_;
    }
}

void FrontEnd::start_tips() {
    if (++tips_page_ == 3) tips_page_ = 0;
    letters_inverted_ = true;
    big_inverted_ = true;
    clear_screen(0, 0);
    print(image_.word(std::uint16_t(0x4199 + 4 * tips_page_)), 0, false);
    print(image_.word(std::uint16_t(0x419B + 4 * tips_page_)), 0, true);
    set_timer(0);
    // Printing the page takes the original about four frames before its
    // loop runs (measured: $944D to $94BE); the first page's loop then
    // makes two passes in its first frame, its wait for the bottom of the
    // screen ($6EA8) still satisfied after the first.
    tips_wait_ = tips_page_ == 0 ? 4 : 3;
    tips_double_ = tips_page_ == 0;
    screen_ = Screen::tips;
}

void FrontEnd::tips_frame(const Controls& c) {
    if (tips_wait_ != 0) {
        --tips_wait_;
        return;
    }
    tips_pass(c);
    if (tips_double_ && screen_ == Screen::tips) {
        tips_double_ = false;
        tips_pass(c);
    }
}

void FrontEnd::tips_pass(const Controls& c) {
    // $94BE
    colours_.all();
    read_keys(c, true);
    const auto s = stick_change(c);
    if (s != 0xFF) {
        if (!(s & 0x10)) start_game();
        else start_menu();
        return;
    }
    if (keys_.code == keys::space) {
        start_tips();
        return;
    }
    if (!timer_runs()) start_title(false);
}

// --- the game ($6392) ------------------------------------------------------------

std::unique_ptr<FrontEnd> FrontEnd::copy() const {
    auto c = std::make_unique<FrontEnd>(*this);
    if (c->play_) c->play_->rebind_sound(c->sound_);
    return c;
}

void FrontEnd::set_cheats(const Cheats& cheats) {
    cheats_ = cheats;
    if (!play_) return;
    play_->set_invincible(cheat_ || cheats_.invincible);
    play_->set_infinite_lives(cheats_.infinite_lives);
    play_->set_catellite_protected(cheats_.invincible);
    play_->set_carnage(cheats_.carnage);
    play_->set_catellite_ai(cheats_.catellite_ai);
}

void FrontEnd::drop_game() {
    shot_volume_ = play_->shot_volume();
    play_.reset();
}

void FrontEnd::start_game() {
    play_.emplace(assets_, image_, random_, sound_);
    play_->set_shot_volume(shot_volume_);
    play_->new_game(mode_, start_place_, scores_.top_digits(), cheat_ || cheats_.invincible);
    play_->set_infinite_lives(cheats_.infinite_lives);
    play_->set_catellite_protected(cheats_.invincible);
    play_->set_carnage(cheats_.carnage);
    play_->set_catellite_ai(cheats_.catellite_ai);
    start_life(screen_ == Screen::title ? 20 : 12, false);
}

void FrontEnd::start_life(int setup_frames, bool after_play) {
    // $639B
    sound_.fade(0xFF);
    play_->start_life();
    // Setting up the place ($63CA, mostly $75E0) keeps the original busy
    // with the screen blanked, slower while the title's raster list (the
    // logo's many splits) still runs: measured $6392 to $6893 20-22
    // frames from the title, 12 from the menu (the wiztips assumed the
    // same), and $6B27 to $6893 11 frames for later lives.
    loading_wait_ = setup_frames;
    loading_in_play_ = after_play;
    screen_ = Screen::loading;
}

void FrontEnd::loading_frame() {
    if (--loading_wait_ == 0) start_player_screen();
}

void FrontEnd::start_player_screen() {
    // $6893: the player screen.
    big_inverted_ = true; // $8CBE
    clear_screen(0xDE, 0);
    int index = 0;
    for (int x = 4; x >= 1; --x) {
        const int v = x == 4 ? play_->player() : play_->control(x);
        index = index << 1 | ((v - 1) & 1);
    }
    print(image_.word(std::uint16_t(0xB71B + 2 * index)), 0, true);
    sprite_colour_ = 1;
    player_background_ = player_background_top_ = 0x0A; // $6898
    sound_.silence();
    sound_.fade(1);
    sound_.reset_music();
    sound_.start_tune(0x28, 2, 2);
    screen_ = Screen::player;
}

void FrontEnd::player_frame(const Controls& c) {
    // $691C
    move_stars();
    colours_.run_right();
    player_background_top_ = player_background_;
    player_background_ = colours_.newest(); // $6922
    if (++sprite_flash_clock_ == 5) {
        sprite_flash_clock_ = 0;
        sprite_colour_ = image_.byte(std::uint16_t(0xB87D + (++sprite_flash_ & 3)));
    }
    read_keys(c, false);
    const auto s = stick_change(c);
    if (s == 0xFF || (s & 0x10)) return;
    // $6965: the port whose fire was pressed plays.
    play_->set_port((c.ports.port2 & 0x10) != 0);
    play_->set_right_run({colours_.right_run().clock, colours_.right_run().index, colours_.right_run().colours});
    play_->set_stick_edge(stick_last_);
    play_->begin_play();
    screen_ = Screen::play;
}

void FrontEnd::play_frame(const Controls& c) {
    Ports ports = c.ports;
    ports.keys = c.keys;
    play_->pass(ports);
    if (play_->quit()) {
        // $6ED7: Q in the pause: back to the title, start place and cheat off.
        start_place_ = 0;
        cheat_ = false;
        drop_game();
        start_title(true);
        return;
    }
    if (play_->life_over()) end_life();
}

void FrontEnd::end_life() {
    // $69F2: the player's last life: the game-over message first.
    if (play_->lives(play_->player()) == 1) {
        play_->start_game_over();
        screen_ = Screen::game_over;
        return;
    }
    finish_life();
}

void FrontEnd::game_over_frame(const Controls& c) {
    Ports ports = c.ports;
    ports.keys = c.keys;
    play_->pass(ports);
    if (play_->game_over_done()) finish_life();
}

void FrontEnd::finish_life() {
    // $63BE: $6E38, $6B27.
    const auto run = play_->right_run();
    colours_.set_right_run({run.clock, run.index, run.colours});
    stick_last_ = play_->stick_edge();
    if (play_->end_life()) {
        start_life(11, true);
        return;
    }
    game_ended();
}

void FrontEnd::game_ended() {
    // $6B5B: the start places for the next game: up to the furthest reached
    // less two; the cheat off.
    start_limit_ = std::uint8_t(play_->furthest() - 2);
    start_place_ = 0;
    cheat_ = false;
    start_scores();
}

// --- the high scores ($6B92) ------------------------------------------------------

void FrontEnd::start_scores() {
    letters_inverted_ = true; // $8C8D
    big_inverted_ = true;     // $8CBE
    clear_screen(0x20, 0);
    Scores::Entry first{play_->control(1) == 1, play_->score_digits(1)};
    Scores::Entry second{play_->control(2) == 1, play_->score_digits(2)};
    ranks_ = scores_.enter(first, play_->one_player() ? nullptr : &second);
    if (ranks_[0] == 0 && ranks_[1] == 0) {
        drop_game();
        start_title(true);
        return;
    }
    sound_.reset_music();
    sound_.start_tune(0x2F, 2, 2);
    entry_player_ = ranks_[0] != 0 ? 0 : 2;
    screen_ = Screen::scores;
    next_name();
    // The screen stays blanked ($8C44 ... $8C50) while the original sorts
    // and prints: measured $6B92 to the first $6CC5 pass 6 frames.
    scores_wait_ = 6;
}

void FrontEnd::next_name() {
    // $6BFB for the player in entry_player_ (0 or 2).
    entry_marker_ = entry_player_ == 0 ? 0x31 : 0x32;
    entry_rank_ = ranks_[std::size_t(entry_player_ / 2)];
    clear_screen(0x20, 0);
    const int line = entry_player_ | (entry_rank_ >= 3 ? 1 : 0);
    print(image_.word(std::uint16_t(0x6B19 + 2 * line)), 0, true);
    print(mega_text, 0, true);
    name_ = {};
    name_.fill(0x20);
    if (entry_rank_ < 3) {
        begin_typing(0x11, 0x11, 3);
        entry_step_ = EntryStep::short_name;
    } else {
        print(team_names_text, 0, false);
        begin_typing(0x0A, 0x11, 3);
        entry_step_ = EntryStep::wizard;
    }
}

void FrontEnd::begin_typing(int column, int row, int length) {
    // $6CF6: the typing line full of '0's; $6CBC.
    typed_.fill(0x30);
    typed_length_ = 0;
    typed_limit_ = std::uint8_t(length);
    typed_column_ = std::uint8_t(column);
    typed_row_ = std::uint8_t(row);
}

void FrontEnd::scores_frame(const Controls& c) {
    if (scores_wait_ != 0) {
        --scores_wait_;
        return;
    }
    // $6CC5
    colours_.all();
    read_keys(c, true);
    // $6D07: letters, space and ! go in; DEL takes one back.
    const auto ch = image_.byte(std::uint16_t(key_table + keys_.code));
    if (ch == delete_char) {
        if (typed_length_ != 0) --typed_length_;
        typed_[typed_length_] = 0x30;
    } else if (ch == 0x20 || ch == 0x21 || (ch >= 0x41 && ch < 0x5B)) {
        typed_[typed_length_] = ch;
        if (typed_length_ != typed_limit_) ++typed_length_;
    }
    show_typing();
    if (typed_length_ == 0 || keys_.code != return_key) return;
    for (int y = typed_length_; y < 10; ++y) typed_[std::size_t(y)] = 0x20;
    switch (entry_step_) {
    case EntryStep::short_name:
        for (int i = 0; i < 3; ++i) name_[std::size_t(i)] = typed_[std::size_t(i)];
        after_names();
        break;
    case EntryStep::wizard:
        for (int i = 0; i < 3; ++i) name_[std::size_t(i)] = typed_[std::size_t(i)];
        begin_typing(0x18, 0x11, 3);
        entry_step_ = EntryStep::feline;
        break;
    case EntryStep::feline:
        for (int i = 0; i < 3; ++i) name_[std::size_t(4 + i)] = typed_[std::size_t(i)];
        after_names();
        break;
    case EntryStep::words: {
        std::array<std::uint8_t, 10> top{};
        for (int i = 0; i < 10; ++i) top[std::size_t(i)] = typed_[std::size_t(i)];
        scores_.top_name(top);
        finish_name();
        break;
    }
    case EntryStep::done: break;
    }
}

void FrontEnd::after_names() {
    // $6C8D: the best score of all also gets a line of words.
    if (entry_rank_ & 1) {
        print(words_text, 0, true);
        begin_typing(0x0A, 0x17, 0x0A);
        entry_step_ = EntryStep::words;
        return;
    }
    finish_name();
}

void FrontEnd::finish_name() {
    // $6CB6
    std::array<std::uint8_t, 7> name{};
    for (int i = 0; i < 7; ++i) name[std::size_t(i)] = name_[std::size_t(i)];
    scores_.name(entry_marker_, name);
    if (entry_player_ == 0 && ranks_[1] != 0) {
        entry_player_ = 2;
        next_name();
        return;
    }
    entry_step_ = EntryStep::done;
    drop_game();
    start_title(false);
}

void FrontEnd::show_typing() {
    // $6D44: ten big spaces over the line ($6D82), then the typed text up
    // to its length, '0' where nothing is typed yet ($6D74).
    int at = typed_row_ * 40 + typed_column_;
    for (int i = 0; i < 10; ++i) put_big(at, 0x20, 0);
    at = typed_row_ * 40 + typed_column_;
    for (int i = 0; i < typed_limit_; ++i) put_big(at, typed_[std::size_t(i)], 0);
}

} // namespace wizball::game
