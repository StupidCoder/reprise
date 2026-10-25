// Frame structure, the title and options screens, and the game's flow from
// a new game to the debrief.

#include "fort_apocalypse_c64/game/bcd.hpp"
#include "fort_apocalypse_c64/game/game.hpp"
#include <algorithm>
#include <cstdio>

namespace fort_apocalypse::game {

Game::Game(const data::Assets& assets, const data::GameTables& tables)
    : assets_(assets), tables_(tables) {
    // Init ($8927): the options' defaults are in Settings; a new game is set
    // up once ($897D calls $8D6B), then the title.
    glyphs_ = assets_.playfield_charset;
    new_game();
    mode_ = Mode::title;
    for (int i = 2; i < 5; ++i) {
        sprite_regs_[std::size_t(i)].colour = 1;
    }
    draw_title();
}

std::string Game::debug_line() const {
    char line[512];
    std::snprintf(line, sizeof line,
                  "9D=%02X 15=%02X 56=%02X 57=%02X 58=%02X 59=%02X 64=%02X 65=%02X 66=%02X "
                  "67=%02X 69=%02X 6A=%02X 11=%02X 46=%02X 68=%02X 73=%02X 6E=%02X 76=%02X 77=%02X "
                  "16=%02X 51=%02X 50=%02X F5=%02X 9C4E=%02X 78=%02X 79=%02X 7A=%02X 7B=%02X 7C=%02X "
                  "7E=%02X 7F=%02X 5B=%02X 8E=%02X 9B=%02X 9C=%02X 99=%02X 5C=%02X EB=%02X "
                  "F9=%02X ED=%02X 91=%02X 92=%02X 93=%02X 97=%02X 98=%02X 9A=%02X 3F=%02X 45=%02X "
                  "4D=%02X build=%d",
                  unsigned(mode_), frame_, camera_column_, fine_x_, camera_row_, fine_y_,
                  unsigned(craft_), x_, y_, bank_, column_, row_, copy_now_, copy_timer_,
                  sprite_hits_, background_hits_, enemy_.state, enemy_.column, enemy_.row,
                  stick_, attract_step_, play_kind_, gravity_mask_, spawn_grace_, bullets_[0].state,
                  bullets_[1].state, bullets_[2].state, bullets_[0].x, bullets_[1].x, bullets_[0].y,
                  bullets_[1].y, fire_latch_, sweep_direction_, fuel_[0], fuel_[1], engine_, level_,
                  men_left_, lives_, fort_, score_[0], score_[1], score_[2], bonus_[0], bonus_[1],
                  depot_stage_, crash_timer_, message_timer_, teleport_grace_, build_frames());
    return line;
}

bool Game::debug_poke(std::uint16_t address, u8 value) {
    switch (address) {
    case 0x5C: level_ = value; return true;
    case 0x5D: checkpoint_.camera_column = value; return true;
    case 0x5E: checkpoint_.camera_row = value; return true;
    case 0x5F: checkpoint_.fine_x = value; return true;
    case 0x60: checkpoint_.fine_y = value; return true;
    case 0x61: checkpoint_.x = value; return true;
    case 0x62: checkpoint_.y = value; return true;
    case 0x63: checkpoint_.bank = value; return true;
    case 0x9B: fuel_[0] = value; return true;
    case 0x9C: fuel_[1] = value; return true;
    case 0xEB: men_left_ = value; return true;
    case 0xED: fort_ = value; return true;
    case 0xF9: lives_ = value; return true;
    case 0x6E: enemy_.state = value; return true;
    case 0x76: enemy_.column = value; return true;
    case 0x77: enemy_.row = value; return true;
    case 0xBE: case 0xBF: case 0xC0: case 0xC1: case 0xC2: case 0xC3:
        tanks_[std::size_t(address - 0xBE)].state = value; // $BE,X
        return true;
    default: return false;
    }
}

std::array<Game::TankView, 6> Game::tank_views() const {
    std::array<TankView, 6> views{};
    for (std::size_t i = 0; i < tanks_.size(); ++i) {
        const auto& t = tanks_[i];
        auto& v = views[i];
        v.on_map = t.state == 2 || t.state == 4;
        v.exploding = t.state == 4;
        v.body = {t.column - 5, t.row}; // object columns are map column + 5
        v.saved = t.saved;
    }
    return views;
}

std::array<Game::MineView, 39> Game::mine_views() const {
    std::array<MineView, 39> views{};
    for (std::size_t i = 0; i < mines_.size(); ++i) {
        const auto& m = mines_[i];
        views[i] = {(m.state & 0x0F) == 2, {m.column - 5, m.row}, (m.state >> 4) & 3};
    }
    return views;
}

std::array<Game::ShellView, 6> Game::shell_views() const {
    std::array<ShellView, 6> views{};
    for (std::size_t i = 0; i < missiles_.size(); ++i) {
        const auto& m = missiles_[i];
        views[i] = {m.state == 4 || m.state == 8, {m.column - 5, m.row},
                    u8(m.state == 4 ? 0x71 : 0x72)};
    }
    return views;
}

std::vector<Game::MapCell> Game::depot_cells() const {
    // As refuel draws them (object columns are map column + 5).
    if (level_ == 1) {
        return {{0x82 - 5, 0x0B}};
    }
    return {{0x17 - 5, 0x0B}, {0xEE - 5, 0x0B}};
}

std::array<Game::PrisonerView, 8> Game::prisoner_views() const {
    std::array<PrisonerView, 8> views{};
    for (std::size_t i = 0; i < prisoners_.size(); ++i) {
        const auto& p = prisoners_[i];
        views[i] = {p.state != 1, {p.column - 5, p.row}, (p.motion & 0x80) != 0}; // running or boarding
    }
    return views;
}

std::string Game::debug_objects() const {
    std::string out = "mines";
    char item[48];
    for (const auto& m : mines_) {
        std::snprintf(item, sizeof item, " %02X:%02X,%02X", m.state, m.column, m.row);
        out += item;
    }
    out += " | tanks";
    for (const auto& t : tanks_) {
        std::snprintf(item, sizeof item, " %02X:%02X,%02X", t.state, t.column, t.row);
        out += item;
    }
    out += " | missiles";
    for (const auto& m : missiles_) {
        std::snprintf(item, sizeof item, " %02X:%02X,%02X", m.state, m.column, m.row);
        out += item;
    }
    out += " | prisoners";
    for (const auto& p : prisoners_) {
        std::snprintf(item, sizeof item, " %02X:%02X,%02X", p.state, p.column, p.row);
        out += item;
    }
    std::snprintf(item, sizeof item, " | cursor %02X", mine_cursor_);
    return out + item;
}

u8 Game::noise(std::uint16_t site) {
    if (noise_source_) {
        return noise_source_(site);
    }
    // xorshift32; the original reads SID voice 3's noise output ($D41B).
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return u8(rng_ >> 24);
}

// --- frame ----------------------------------------------------------------------

const c64::Picture& Game::tick(const Controls& controls, const Options& options) {
    controls_ = controls;
    options_ = options;
    happenings_.impacts.clear();
    happenings_.tanks.clear();
    happenings_.mines.clear();
    happenings_.shells.clear();
    top_ = vic_;
    const bool steps_now = scheduled_ && main_loop_running_ && !main_loop_starting_;
    if (steps_now) {
        run_scheduled_steps(steps_early_);
    }
    split_active_ = playfield_part_installed_;
    if (split_active_) {
        playfield_interrupt();
    }
    render();
    if (steps_now) {
        run_scheduled_steps(steps_before_);
    }
    if (miss_interrupt_) {
        miss_interrupt_ = false; // observed: the redraw keeps interrupts off past $F9
    } else if (title_irq_) {
        title_interrupt();
    } else {
        hud_interrupt();
    }
    if (main_loop_starting_) {
        main_loop_starting_ = false; // the loop's first pass waits for the next frame
    } else if (main_loop_running_) {
        main_loop();
    }
    return picture_;
}

void Game::read_joystick() {
    stick_ = u8((controls_.up ? 0 : 1) | (controls_.down ? 0 : 2) | (controls_.left ? 0 : 4) |
                (controls_.right ? 0 : 8));
    fire_ = controls_.fire;
}

void Game::read_keyboard() {
    // Nothing while joystick 2 drives the lines; the space bar's column is
    // latched on change ($19); column 0 gives the function key ($18).
    if (controls_.up || controls_.down || controls_.left || controls_.right || controls_.fire) {
        key_ = FunctionKey::none;
        return;
    }
    if (controls_.space != space_down_) {
        space_down_ = controls_.space;
        space_event_ = controls_.space;
    }
    key_ = controls_.key;
}

// --- title ($8AA2) -----------------------------------------------------------------

void Game::draw_title() {
    // $89C7-$8A9D.
    vic_.shared2 = 1;
    vic_.shared1 = 2;
    fill_colours(0x0C);
    clear_play_area();
    for (int i = 1; i < 0xF0; ++i) {
        screen_[std::size_t(i)] = 0;
    }
    u8 code = 0x3C;
    for (int column = 0; column < 40; ++column) {
        code ^= 1;
        screen_[std::size_t(6 * 40 + column)] = code;
        screen_[std::size_t(24 * 40 + column)] = code;
    }
    code = 0x3C;
    for (int row = 6; row <= 23; ++row) {
        code ^= 1;
        screen_[std::size_t(row * 40 + 39)] = code;
        screen_[std::size_t(row * 40 + 40)] = code;
    }
    title_irq_ = true;
    playfield_part_installed_ = false;
    title_timer_ = 0;
    const auto& t = assets_.texts;
    draw_text(t.title[0], 5, 3);
    draw_text(t.title[1], 6, 6);
    draw_text(t.title[2], 2, 8);
    draw_text(t.title[3], 6, 10);
    draw_text(t.copyright, 5, 13);
    for (std::size_t i = 0; i < t.year.size(); ++i) {
        screen_[0x312 + i] = t.year[i]; // $8A76
    }
    draw_text(t.title[4], 4, 15);
    for (int i = 1; i <= 0x78; ++i) {
        colour_ram_[std::size_t(0x167 + i)] = 0x0F;
        colour_ram_[std::size_t(0x2F8 + i)] = 0x0B;
    }
    main_loop_running_ = false;
    task_ = {};
}

void Game::title_interrupt() {
    vic_.columns40 = true; // $D016 = $18
    vic_.xscroll = 0;
    vic_.yscroll = 3;      // $D011 = $1B
    vic_.rows25 = true;
    ++frame_;
    read_joystick();
    read_keyboard();
    if ((frame_ & 3) == 0) {
        std::swap(vic_.shared1, vic_.shared2);
    }
    if ((frame_ & 7) == 0) {
        ++title_timer_;
    }
    // $8AD0: two sawtooth voices follow the timer, a beat apart.
    sid_.write(0x04, 0x21);
    sid_.write(0x0B, 0x21);
    sid_.write(0x06, 0x50);
    sid_.write(0x0D, 0x50);
    const u8 tone = title_timer_, tone2 = u8(title_timer_ + 1);
    sid_.write(0x00, u8((tone & 3) << 6));
    sid_.write(0x01, u8(tone >> 2));
    sid_.write(0x07, u8((tone2 & 3) << 6));
    sid_.write(0x08, u8(tone2 >> 2));

    if (title_timer_ == 0xD3) { // $8B1C: time for the attract game
        settings_.gravity = 0;
        play_kind_ = 0xFF;
        start_play_screens();
        return;
    }
    if (fire_ || key_ == FunctionKey::f7) {
        mode_ = Mode::new_game;
    } else if (key_ == FunctionKey::none) {
        return;
    } else {
        mode_ = Mode::options; // F3 or F5
    }
    selected_ = 0;
    play_kind_ = 1;
    start_play_screens();
}

void Game::start_play_screens() {
    // $8B97: bullet colours, the HUD, the game's interrupt, the main loop.
    for (int i = 2; i < 5; ++i) {
        sprite_regs_[std::size_t(i)].colour = 1;
    }
    copy_hud();
    clear_play_area();
    title_irq_ = false;
    main_loop_running_ = true;
    main_loop_starting_ = true;
}

// --- the game's interrupt at raster $F9 ($9BD4) -----------------------------------

void Game::hud_interrupt() {
    vic_.columns40 = true; // $D016 = $18
    vic_.xscroll = 0;
    vic_.yscroll = 7;      // $D011 = $1F
    vic_.rows25 = true;
    ++frame_;
    read_keyboard();
    read_joystick();
    vic_.playfield_charset = false; // $D018 = $14
    vic_.border = vic_.background = 0;
    vic_.sprites = 1;
    sprite_hits_ = latched_sprite_hits_; // reading the latches clears them
    background_hits_ = latched_background_hits_;
    graphics_hits_ = latched_graphics_hits_;
    latched_sprite_hits_ = latched_background_hits_ = latched_graphics_hits_ = 0;
    camera();
    if (!interrupt_cut_short_) {
        draw_counters();
        player_sprite();
        enemy_sprite();
        fire();
        if (spawn_grace_) {
            --spawn_grace_;
        }
        if (mode_ == Mode::playing) {
            bullets();
            terrain_contact();
            movement();
        }
    }
    interrupt_cut_short_ = false;
    playfield_part_installed_ = split_layout_; // $9C2E
}

// --- the main loop ($8BB1) ------------------------------------------------------------

void Game::main_loop() {
    const bool new_pass = !play_pass_ && pass_step_ == 0 && !step_waiting_;
    if (task_.kind != Task::none) {
        run_task();
        if (task_.kind != Task::none || !scheduled_ || (!play_pass_ && pass_step_ == 0)) {
            return; // a pass outside play waits for the next frame first
        }
    } else if (new_pass) {
        main_loop_pass(); // outside play it waits for each frame
        return;
    }
    if (scheduled_) {
        run_scheduled_steps(steps_after_);
        return;
    }
    pass_credit_ += pass_rate_;
    while (pass_credit_ >= 1.0) {
        pass_credit_ -= 1.0;
        main_loop_pass();
        if (mode_ != Mode::playing || task_.kind != Task::none) {
            pass_credit_ = 0;
            break;
        }
    }
}

void Game::run_scheduled_steps(Steps steps) {
    // What the schedule ends while a wait still runs here comes after it.
    steps_pending_.runs += steps.runs;
    steps_pending_.returns += steps.returns;
    auto routine_returns = [&] {
        step_waiting_ = false;
        if (pass_step_ == 0) {
            play_pass_ = mode_ == Mode::playing;
        }
    };
    while (task_.kind == Task::none) {
        if (!play_pass_ && pass_step_ == 0 && !step_waiting_) {
            steps_pending_ = {}; // outside play the loop waits for each frame (main_loop)
            return;
        }
        if (step_waiting_ && steps_pending_.returns > 0) {
            --steps_pending_.returns;
            routine_returns();
            continue;
        }
        if (steps_pending_.runs == 0) {
            break;
        }
        if (step_waiting_) {
            routine_returns(); // the next step started, so the routine has returned
        }
        --steps_pending_.runs;
        run_pass_step();
        step_scheduled_ = true;
    }
    if (task_.kind == Task::none && !step_waiting_) {
        steps_pending_.returns = 0;
    }
}

void Game::main_loop_pass() {
    if (mode_ == Mode::paused && pass_step_ == 0) {
        pause_loop_check();
        return;
    }
    step_waiting_ = false;
    do {
        run_pass_step();
    } while (pass_step_ != 0 && task_.kind == Task::none);
}

void Game::run_pass_step() {
    if (pass_step_ == 0 && !play_pass_) {
        pass_step_ = rest_step; // $8BBD: only passes started in play run the objects
    }
    const int step = pass_step_;
    switch (step) {
    case mines_step: mines(); break;
    case tanks_step: tanks(); break;
    case missiles_step: missiles(); break;
    case prisoners_step: prisoners(); break;
    case radar_step: radar_window(); break;
    case refuel_step: refuel(); break;
    case fort_step_: fort_explosion(); break;
    case exits_step: level_exits(); break;
    default: pass_rest(); break;
    }
    pass_step_ = step + 1 == pass_steps ? 0 : step + 1;
    // A routine that waits ($9B99) returns into the pass afterwards.
    step_waiting_ = task_.kind != Task::none;
    step_scheduled_ = false;
    if (pass_step_ == 0 && task_.kind == Task::none) {
        play_pass_ = mode_ == Mode::playing;
    }
}

void Game::pass_rest() {
    teleport();
    if (task_.kind != Task::none) {
        return;
    }
    switch (mode_) {
    case Mode::new_game:
        new_game();
        break;
    case Mode::debrief:
        debrief();
        break;
    case Mode::get_ready:
        enter_level();
        break;
    case Mode::life_start:
        start_life();
        break;
    default:
        break;
    }
    if (task_.kind != Task::none) {
        return;
    }
    handle_keys();
    if (static_cast<std::int8_t>(play_kind_) < 0) { // $8BE4: the attract game
        ++play_kind_;
        mode_ = Mode::new_game;
    }
    if ((mode_ == Mode::title || mode_ == Mode::options) && (frame_ & 4)) {
        if (--idle_ == 0) {
            draw_title();
            miss_interrupt_ = true; // observed: the next interrupt is lost
        }
    }
}

void Game::wait(Task::Kind kind, int stage, int frames) {
    task_ = {kind, stage, frames, mode_};
}

void Game::run_task() {
    // $9B99: the keys are still handled every frame; a pause holds the wait,
    // a mode change abandons the routine (the original resets its stack).
    if (mode_ == Mode::paused) {
        pause_loop_check();
        return;
    }
    if (task_.frames > 0) {
        handle_keys();
        if (mode_ == Mode::paused) {
            return;
        }
        if (mode_ != task_.mode) {
            task_ = {}; // the pass starts over
            pass_step_ = 0;
            step_waiting_ = false;
            play_pass_ = mode_ == Mode::playing;
            return;
        }
        if (--task_.frames > 0) {
            return;
        }
        spawn_grace_ = 0x0F; // $9BAF
    }
    const auto kind = task_.kind;
    const int stage = task_.stage;
    task_ = {};
    continue_task(kind, stage);
    if (task_.kind == Task::none) {
        // The routine returns into its pass.
        if (!step_scheduled_) {
            step_waiting_ = false;
        }
        if (pass_step_ == 0 && !step_waiting_) {
            play_pass_ = mode_ == Mode::playing;
        }
    }
}

void Game::continue_task(Task::Kind kind, int stage) {
    switch (kind) {
    case Task::level_build:
        mode_ = Mode::life_start; // $9101
        break;
    case Task::life_start:
        finish_life_start(stage);
        break;
    case Task::bonus:
        after_bonus(stage);
        break;
    case Task::sink:
        sink_step(stage);
        break;
    case Task::fort:
        fort_step(stage);
        break;
    case Task::teleport:
        teleport_land();
        break;
    case Task::none:
        break;
    }
}

// --- keys ($92E2) -------------------------------------------------------------------

void Game::handle_keys() {
    if (key_ != last_key_) {
        last_key_ = key_;
        idle_ = 0;
        if (key_ == FunctionKey::f7) {
            mode_ = Mode::new_game; // $92F2
            play_kind_ = 3;
            return;
        }
        if (mode_ == Mode::options) {
            options_key();
            return;
        }
        if (key_ == FunctionKey::f3 || key_ == FunctionKey::f5) {
            // $930F: the options screen, also from a game.
            mode_ = Mode::options;
            play_kind_ = 9;
            clear_play_area();
            selected_ = 0;
            options_key();
            return;
        }
    }
    if (space_event_) { // $9322: pause
        space_event_ = false;
        paused_mode_ = mode_;
        mode_ = Mode::paused;
        sound_off();
    }
}

void Game::pause_loop_check() {
    // $9333: until space again, a function key, or fire.
    sound_off();
    if (space_event_ || key_ != FunctionKey::none || fire_) {
        space_event_ = false;
        mode_ = paused_mode_;
        sound_engine_on();
    }
}

void Game::options_key() {
    // $9351: F3 selects, F5 changes; the screen is drawn again.
    if (key_ == FunctionKey::f3) {
        selected_ = (selected_ + 1) % 3;
    } else if (key_ == FunctionKey::f5) {
        auto& value = settings_.value(selected_);
        value = (value + 1) % 3;
    }
    draw_options();
}

void Game::draw_options() {
    const auto& t = assets_.texts;
    draw_text(t.options, 13, 1);
    draw_text(t.option, 0, 3);
    draw_text(t.select, 28, 3);
    draw_text(t.setting_names[0], 0, 7);
    draw_text(t.setting_names[1], 0, 9);
    draw_text(t.setting_names[2], 0, 11);
    for (int i = 1; i <= 0xC8; ++i) {
        colour_ram_[std::size_t(0x207 + i)] = 0x0D; // $93E5
    }
    const int start = 13 * 40 + 80 * selected_; // $94CC: rows 13, 15 or 17
    for (int i = 0; i < 0x50; ++i) {
        colour_ram_[std::size_t(start + i)] = 0x0C;
    }
    for (int s = 0; s < 3; ++s) {
        draw_text(t.setting_values[std::size_t(s)][std::size_t(settings_.value(s))], 28, 7 + 2 * s);
    }
}

// --- a new game ($8D6B) -----------------------------------------------------------------

void Game::new_game() {
    clear_play_area();
    level_ = 0;
    score_ = {};
    bonus_ = {};
    barrier_a_ = 0;
    fuel_ = {};
    rank_ = 0;
    rescued_ = 0;
    sweep_timer_ = 1;
    tank_countdown_ = 1;
    missile_countdown_ = 1;
    barrier_b_ = 0x80;
    fort_ = 2;
    barriers_off_ = 2;
    engine_ = 9;
    enemy_.state = 1;
    const auto& d = tables_.difficulty;
    gravity_mask_ = d.gravity_mask[std::size_t(settings_.gravity)];
    lives_ = play_kind_ == 0 ? 2 : d.lives[std::size_t(settings_.robo_pilots)];
    const auto skill = std::size_t(settings_.pilot_skill);
    barrier_step_ = d.barrier_step[skill];
    last_mine_ = d.last_mine[skill];
    enemy_mask_ = d.enemy_mask[skill];
    tank_period_ = d.tank_period[skill];
    missile_period_ = d.missile_period[skill];
    sweep_period_ = d.sweep_period[skill];
    choose_gate_wall();
    mode_ = Mode::get_ready;
}

void Game::choose_gate_wall() {
    // $8DD3: of the reactor's two gate walls (characters $59, $5A) one is
    // solid, chosen at random.
    glyphs_[0x59] = {};
    glyphs_[0x5A] = {};
    auto& wall = static_cast<std::int8_t>(noise(0x8DE4)) < 0 ? glyphs_[0x5A] : glyphs_[0x59];
    wall.fill(0x55);
}

// --- entering a level ($8EDD) --------------------------------------------------------

void Game::enter_level() {
    clear_play_area();
    draw_text(tables_.entering, 12, 6);
    if (level_ != 0 && enemy_mask_ == 3) {
        enemy_mask_ = 1; // later levels: the enemy is faster
    }
    draw_text(level_ == 1 ? tables_.crystalline : tables_.vaults, 2, 8);
    const auto& start = tables_.levels[std::min<std::size_t>(level_, 2)];
    level_colour_ = start.multicolour;
    vic_.shared1 = level_colour_;
    camera_column_ = start.camera_column;
    camera_row_ = start.camera_row;
    x_ = start.sprite_x;
    y_ = start.sprite_y;
    fine_x_ = 0;
    fine_y_ = level_ == 2 ? 0 : 7;
    bank_ = 8;
    save_checkpoint();
    bonus_ = {0x99, 0x99};
    for (int i = 5; i >= 0; --i) {
        auto& t = tanks_[std::size_t(i)];
        t.home_column = start.tank_column[std::size_t(i)];
        t.home_row = start.tank_row[std::size_t(i)];
        t.state = 7;
        missiles_[std::size_t(i)].state = 1;
    }
    enemy_.state = 1;
    for (auto& m : mines_) {
        m.state = 1;
    }
    for (int i = 0; i <= last_mine_; ++i) {
        mines_[std::size_t(i)].state = 7;
    }
    build_level();
    wait(Task::level_build, 0, build_frames());
}

int Game::build_frames() const {
    // Measured on the original ($8F8F to $9101, three levels, six builds):
    // decoding takes a base time per level plus about 3.9 raster lines per
    // random rock draw (draws of 0 are repeated); the prisoner scan about
    // 0.29 lines per map cell it looks at. The build starts at raster $89
    // of the next frame ("ENTERING ..." is drawn first). The pass then ends
    // a line later and the next one waits for the next frame, so the life
    // starts after the first HUD interrupt (raster $F9) past that. Builds of
    // the same level vary by some 10 lines, so an end within that of the
    // interrupt can come out a frame early or late.
    constexpr double decode_base[3] = {38.75, 46.37, 38.75};
    const double end = 1 + 0x89 / 312.0 + decode_base[level_ % 3] + 0.0125 * rock_draws_ +
                       0.00093 * scanned_cells_;
    const int frame = int(end);
    const double raster = (end - frame) * 312;
    return raster + 1 < 0xF9 ? frame - 1 : frame;
}

void Game::build_level() {
    // $8F8F: the map with random rock and the wrap copy, the level-2 change,
    // the radar picture with its wrap copy, and the prisoners.
    mine_cursor_ = 0;
    prisoner_cursor_ = 0;
    rock_draws_ = scanned_cells_ = 0;
    depot_shown_stage_ = 4; // the levels' depots stand up
    sink_view_.active = false;
    const auto& level = assets_.levels[level_ % 3];
    for (int row = 0; row < 40; ++row) {
        for (int column = 0; column < 256; ++column) {
            u8 code = level.terrain.cells[std::size_t(row)][std::size_t(column)];
            if (code == data::random_rock_a || code == data::random_rock_b) {
                u8 r;
                do {
                    r = noise(code == data::random_rock_a ? 0x8FCA : 0x8FDA) & 3;
                    ++rock_draws_;
                } while (r == 0);
                code = u8((code == data::random_rock_a ? 0x61 : 0x64) + r);
            }
            map_[std::size_t(row * 256 + column)] = code;
        }
    }
    spill_terrain_ = level.terrain_spill;
    for (int row = 0; row < 40; ++row) {
        for (int column = 0; column < 40; ++column) {
            map_[std::size_t(row * 256 + 215 + column)] = map_[std::size_t(row * 256 + column)];
        }
    }
    if (level_ == 2) { // $9017: the opening in the surface is closed
        for (int row = 0; row < 3; ++row) {
            for (int i = 0; i < 14; ++i) {
                map_set(object_index(0x7E, 0x13 + row) + i, 0);
            }
        }
    }
    for (int y = 0; y < 40; ++y) {
        radar_[std::size_t(y)] = level.radar.bytes[std::size_t(y)];
        for (int x = 0; x < 13; ++x) {
            radar_[std::size_t(y)][std::size_t(27 + x)] = radar_[std::size_t(y)][std::size_t(x)];
        }
    }
    // The radar stream's last run spills over the mines' column and row
    // tables ($3440-$3532).
    for (int i = 0; i < level.radar_spill.count; ++i) {
        const int address = 0x3440 + i;
        if (address >= 0x3500 && address < 0x3527) {
            mines_[std::size_t(address - 0x3500)].column = level.radar_spill.value;
        } else if (address >= 0x3527 && address < 0x354E) {
            mines_[std::size_t(address - 0x3527)].row = level.radar_spill.value;
        }
    }
    men_left_ = 8;
    for (auto& p : prisoners_) {
        p.state = 1;
    }
    if (level_ == 2) {
        return;
    }
    // $90A4: prisoners where floor $48 has $48 to its right and rock $1F
    // above, each spot taken by chance; the scan repeats until 8 are placed.
    int slot = 7;
    while (slot >= 0) {
        for (int row = 0; row < 40 && slot >= 0; ++row) {
            for (int y = 0; y < 256 && slot >= 0; ++y) {
                ++scanned_cells_;
                const int at = row * 256 + y;
                if (map_[std::size_t(at)] != 0x48 ||
                    map_[std::size_t(row * 256 + ((y + 1) & 255))] != 0x48 ||
                    (row == 0 ? 0 : map_[std::size_t(at - 256)]) != 0x1F) {
                    continue;
                }
                const u8 chance = noise(0x90C8);
                if (chance < 0x0A || chance >= 0x32) {
                    continue;
                }
                auto& p = prisoners_[std::size_t(slot)];
                p.column = u8(y + 5);
                p.row = u8(row);
                map_[std::size_t(at)] = 0x01;
                p.state = 2;
                p.motion = 0x10;
                --slot;
            }
        }
    }
}

// --- the start of a life ($8E11) --------------------------------------------------------

void Game::start_life() {
    fill_colours(0x0D);
    clear_play_area();
    bool borrow = false;
    lives_ = bcd_sub(lives_, 1, borrow);
    if (lives_ == 0x99) {
        mode_ = Mode::debrief;
        return;
    }
    sprite_regs_[0].colour = 7;
    if (engine_ == 9) {
        engine_ = 8;
        fuel_ = {0x00, 0x01};
    }
    draw_text(tables_.get_ready, 4, 8);
    draw_text(tables_.pilots_left, 5, 10);
    int at = 0x29C, digits = 1;
    bool started = false;
    attract_step_ = 0;
    draw_number(lives_, at, digits, started);
    for (int i = 0; i < 32; ++i) {
        glyphs_[std::size_t(0x7C + i / 8)][std::size_t(i % 8)] = tables_.shimmer[std::size_t(i)];
    }
    // 120 frames ($9B99, X = $78); see finish_life_start for the rest.
    // Observed: the drawing above takes about a frame. After a crash this
    // runs at the end of a pass that started in play, wherever the beam is,
    // and the wait mostly counts from the second HUD interrupt after; from
    // the loop's wait for the next frame (after "ENTERING ...") it runs
    // just after an interrupt and counts from the next one.
    wait(Task::life_start, 0, play_pass_ ? 0x79 : 0x78);
}

void Game::finish_life_start(int stage) {
    if (stage == 1) {
        // Building the sprite shapes ($B044, $B0B0) took this frame; then
        // the craft appears and play begins.
        craft_ = Craft::appearing;
        mode_ = Mode::playing;
        sprite_hits_ = background_hits_ = graphics_hits_ = 0;
        latched_sprite_hits_ = latched_background_hits_ = latched_graphics_hits_ = 0;
        sound_engine_on();
        return;
    }
    camera_column_ = checkpoint_.camera_column;
    camera_row_ = checkpoint_.camera_row;
    fine_x_ = checkpoint_.fine_x;
    fine_y_ = checkpoint_.fine_y;
    x_ = checkpoint_.x;
    y_ = checkpoint_.y;
    bank_ = checkpoint_.bank;
    enemy_blip_on_ = 0;
    clear_play_area();
    split_layout_ = true; // $9AA9: $12 = 2
    copy_timer_ = 2;
    // Observed: play starts two frames later (the camera already runs).
    task_ = {Task::life_start, 1, 2, mode_};
}

// --- the bonus ($9B81) and the level exits ($8C08) ----------------------------------------

void Game::collect_bonus() {
    add_score(bonus_[0], bonus_[1]);
    bonus_ = {};
    bool carry = false;
    lives_ = bcd_add(lives_, 1, carry);
}

void Game::level_exits() {
    if (level_ == 0) {
        // The pad at the bottom: on to the fortress once all men are out.
        if (craft_ != Craft::landed || row_ < 0x23 || column_ < 0x7E || column_ >= 0x89) {
            return;
        }
        if (men_left_ != 0) {
            show_men_to_rescue();
            return;
        }
        ++level_;
        collect_bonus();
        wait(Task::bonus, 0, 2);
    } else if (level_ == 1) {
        // Out through the top once the fort is destroyed.
        if (fort_ != 1 || row_ >= 2 || column_ < 0x7E || column_ >= 0x87) {
            return;
        }
        if (men_left_ != 0) {
            show_men_to_rescue();
            return;
        }
        ++level_;
        collect_bonus();
        wait(Task::bonus, 1, 2);
    } else {
        // Back on the surface: landing ends the mission.
        if (craft_ != Craft::landed || row_ >= 0x0D || column_ < 0x15 || column_ >= 0xF4) {
            return;
        }
        collect_bonus();
        wait(Task::bonus, 2, 2);
    }
}

void Game::after_bonus(int stage) {
    switch (stage) {
    case 0:
        clear_message_row();
        sound_off();
        mode_ = Mode::locked;
        sink_index_ = object_index(0x82, 0x28);
        // For presentation: the block as it was before it moves.
        sink_view_ = {true, {0x82 - 5, 0x28 - 4}, {}, 0};
        for (int row = 0; row < 5; ++row) {
            for (int i = 0; i < 6; ++i) {
                sink_view_.look[std::size_t(row * 6 + i)] =
                    map_at(sink_index_ - (4 - row) * 256 + i);
            }
        }
        sink_step(0);
        break;
    case 1:
        mode_ = Mode::get_ready;
        break;
    case 2:
        ++level_;
        mode_ = Mode::debrief;
        break;
    default:
        fort_step(0);
        break;
    }
}

void Game::sink_step(int stage) {
    // $8C57: three times, the 6x5 block under the pad moves down a row, then
    // six steps of {wait 5 frames, level the bank, sink 1 pixel}.
    const int shift = stage / 7, step = stage % 7;
    if (shift == 3) {
        mode_ = Mode::get_ready;
        sink_view_.active = false;
        return;
    }
    if (step == 0) {
        ++sink_view_.rows;
        int at = sink_index_;
        for (int row = 4; row >= 0; --row) {
            for (int i = 5; i >= 0; --i) {
                map_set(at + i, map_at(at - 256 + i));
            }
            at -= 256;
        }
    } else {
        level_bank();
        ++y_;
        if (step == 6) {
            sink_step((shift + 1) * 7);
            return;
        }
    }
    wait(Task::sink, stage + 1, 5);
}

// --- the cavern teleport ($9892) -------------------------------------------------------

void Game::teleport() {
    if (mode_ != Mode::teleport) {
        return;
    }
    mode_ = Mode::locked;
    playfield_colour_ = 0x0F;
    wait(Task::teleport, 0, 2);
}

void Game::teleport_land() {
    playfield_colour_ = 0;
    const auto& p = tables_.drop_points[noise(0x98A9) & 3];
    fine_x_ = p.fine_x;
    fine_y_ = p.fine_y;
    camera_column_ = p.camera_column;
    camera_row_ = p.camera_row;
    x_ = p.sprite_x;
    y_ = p.sprite_y;
    bank_ = 8;
    background_hits_ = graphics_hits_ = 0;
    mode_ = Mode::playing;
}

// --- the fort's destruction ($97B0) -------------------------------------------------

void Game::fort_explosion() {
    if (fort_ != 5) {
        return;
    }
    add_score(0x00, 0x50);
    collect_bonus();
    wait(Task::bonus, 3, 2);
}

void Game::fort_step(int stage) {
    // stage = phase * 17 + flash: draw the phase's pattern of explosion
    // cells, then 16 colour flashes two frames apart.
    const int phase = stage / 17, flash = stage % 17;
    if (stage == 0) {
        mode_ = Mode::locked;
        bonus_ = {0x99, 0x99};
        checkpoint_ = {0x6E, 0x11, 0x07, 0x96, 0x76, 0xA0, 0x08};
        glyphs_[0x59] = {}; // both gate walls open
        glyphs_[0x5A] = {};
    }
    if (phase == 4) {
        mode_ = Mode::playing;
        fort_ = 1;
        barriers_off_ = 1;
        return;
    }
    if (flash == 0) {
        const auto& pattern = tables_.fort_explosion[std::size_t(phase)];
        int at = object_index(0x79, 0x14);
        for (int row = 0; row < 6; ++row) {
            for (int sub = 0; sub < 3; ++sub) {
                u8 bits = pattern[std::size_t(row)];
                int y = 0x17;
                for (int bit = 0; bit < 8; ++bit) {
                    const u8 code = (bits & 1) ? 0x20 : 0x00;
                    bits = u8(bits >> 1);
                    for (int k = 0; k < 3; ++k) {
                        map_set(at + y--, code);
                    }
                }
                at += 256;
            }
        }
        playfield_colour_ = 0x10;
        wait(Task::fort, stage + 1, 2);
        return;
    }
    ++playfield_colour_;
    sound_explosion();
    sound_missile();
    if (flash == 16) {
        playfield_colour_ = 0;
        fort_step((phase + 1) * 17);
        return;
    }
    wait(Task::fort, stage + 1, 2);
}

// --- the debrief ($916E) ---------------------------------------------------------------

void Game::debrief() {
    clear_play_area();
    // The rank adds up byte-wise, then is clamped to 0-15.
    u8 rank = rank_;
    auto add = [&](u8 value) { rank = u8(rank + value); };
    add(u8(rescued_ >> 2));
    if (fort_ == 1) {
        add(3);
    }
    if (level_ == 3) {
        add(1);
    }
    add(level_); // $918B: the level is still in A
    add(score_[2]);
    add(u8(tables_.difficulty.rank_adjust[std::size_t(settings_.gravity)]));
    add(u8(~u8(1 - settings_.robo_pilots))); // 2 - $F8 - 1, inverted
    add(u8(tables_.difficulty.rank_adjust[std::size_t(settings_.pilot_skill)]));
    if (rank & 0x80) {
        rank = 0;
    }
    if (rank >= 0x10) {
        rank = 0x0F;
    }
    rank_ = rank;
    const bool higher = score_[2] != high_score_[2] ? score_[2] > high_score_[2]
                        : score_[1] != high_score_[1] ? score_[1] > high_score_[1]
                                                      : score_[0] >= high_score_[0];
    if (higher) {
        high_score_ = score_;
    }
    draw_text(tables_.high_score, 2, 0);
    int at = 0x108, digits = 5;
    bool started = false;
    draw_number(high_score_[2], at, digits, started);
    draw_number(high_score_[1], at, digits, started);
    draw_number(high_score_[0], at, digits, started);
    draw_text(tables_.mission, 3, 5);
    draw_text(level_ == 3 ? tables_.completed : tables_.aborted, 0x15, 5);
    draw_text(tables_.your_rank, 7, 8);
    draw_text(tables_.class_text, 0x15, 10);
    const u8 digit = u8((((rank_ & 3) ^ 3) + 1) | 0x10);
    const int class_at = 240 + 10 * 40 + 0x15 + 0x0C; // after "CLASS"
    screen_[std::size_t(class_at)] = digit;
    screen_[std::size_t(class_at + 1)] = u8(digit == 0x10 ? 0x0A : (digit & 0x8F));
    draw_text(tables_.birds[std::size_t((rank_ >> 2) & 3)], 3, 10);
    idle_ = 0xFF;
    mode_ = Mode::title;
    play_kind_ = 1;
}

} // namespace fort_apocalypse::game
