#include "bruce_lee_c64/game/game.hpp"
#include "bruce_lee_c64/game/screen.hpp"

namespace bruce_lee::game {
namespace {

// The main loop runs once every three frames ($1BDD sets $48 = 3 and waits
// for the interrupts to count it down).
constexpr int frames_per_pass = 3;

constexpr std::uint16_t title_time = 0x0960;   // $06EC: frames before the attract mode
constexpr int start_delay = 0x10;              // $08F1
constexpr int option_delay = 0x04;             // $0913
constexpr int attract_leave_delay = 0x06;      // $19FB
constexpr int player_screen_time = 0x40;       // $0F33 (twice as long for two players)
constexpr int game_over_time = 0xFE;           // $197F
constexpr int after_game_over_time = 0x40;     // $1994
constexpr std::uint8_t title_end_action = 3;   // $06CC: the tune list goes round
constexpr std::uint8_t sound_give_up = 0x8A;   // $1789

// Key bits for new presses.
constexpr std::uint8_t key_f1 = 1, key_f3 = 2, key_f5 = 4, key_f7 = 8, key_space = 16;

std::uint8_t keys_of(const app::GameInput& in) {
    return std::uint8_t((in.f1 ? key_f1 : 0) | (in.f3 ? key_f3 : 0) | (in.f5 ? key_f5 : 0) | (in.f7 ? key_f7 : 0) |
                        (in.space ? key_space : 0));
}

std::uint8_t stick_bits(bool up, bool down, bool left, bool right) {
    std::uint8_t stick = 0x0F;
    if (up) stick &= 0x0E;
    if (down) stick &= 0x0D;
    if (left) stick &= 0x0B;
    if (right) stick &= 0x07;
    return stick;
}

} // namespace

Game::Game(const data::Assets& assets) : world(assets), assets_(assets), effects_(assets), music_(assets) {}

void Game::start() {
    restart();
}

// $16BA / $16C6: the C64 reads a 0 bit for a pressed direction; a
// left-handed player's stick is turned a quarter ($4A92).
std::uint8_t Game::stick_of(const app::GameInput& in, int which) const {
    std::uint8_t stick = which == 0 ? stick_bits(in.up, in.down, in.left, in.right)
                                    : stick_bits(in.up2, in.down2, in.left2, in.right2);
    if (options.left_handed[std::size_t(which)]) {
        stick = std::uint8_t((stick >> 2) | assets_.left_hand_bits[stick & 3]);
    }
    return stick;
}

bool Game::run_wait(const app::GameInput& in) {
    if (!wait_.active()) {
        return false;
    }
    if (wait_.release) {
        if (in.fire || in.fire2) {
            return true;
        }
        wait_.release = false;
    }
    if (wait_.frames > 0) {
        --wait_.frames;
        return true;
    }
    const Next then = wait_.then;
    wait_ = Wait{};
    step(then);
    return true;
}

void Game::step(Next next) {
    switch (next) {
    case Next::title_again:
        countdown_ = title_time;
        break;
    case Next::start_game:
        start_game();
        break;
    case Next::new_game:
        new_game();
        break;
    case Next::restart:
        restart();
        break;
    case Next::resume:
    case Next::resume_swapped:
        if (next == Next::resume_swapped) {
            const auto mine = world.player_state();
            world.set_player_state(other_player_);
            other_player_ = mine;
        }
        world.restart_room();
        request_sounds();
        phase_ = Phase::play;
        frames_to_pass_ = 1;
        break;
    case Next::after_game_over:
        if (options.alternate && !(other_player_.lives & 0x80)) {
            next_player();
        } else {
            wait_ = {false, after_game_over_time, Next::title};
        }
        break;
    case Next::title:
        enter_title();
        break;
    case Next::none:
        break;
    }
}

// $050F: from a demo or an abandoned game back to the title: the music
// stopped, the random counter 0, a new game prepared.
void Game::restart() {
    music_.stop(sid_);
    world.random.counter = 0;
    enter_title();
}

// $0584-$06FD
void Game::enter_title() {
    phase_ = Phase::title;
    paused_ = false;
    effects_.silence(sid_);
    title_text_.build(assets_, options.alternate, options.yamo_player);
    if (options.title_music && --music_wait_ < 0) {
        music_wait_ = 4;
        music_.start_list(sid_, title_end_action, {0x00, 0xFF});
    }
    countdown_ = title_time;
}

// $154A: the title's interrupt counts down; when the low byte reaches 0
// the high byte is taken at once (one frame of each 256 is skipped).
void Game::count_title_frame() {
    std::uint8_t lo = countdown_ & 0xFF, hi = countdown_ >> 8;
    if (lo != 0) {
        if (--lo == 0 && hi != 0) {
            --hi;
            --lo;
        }
    }
    countdown_ = std::uint16_t(hi << 8 | lo);
}

// $08A3: the title waits for fire or a key; after $0960 frames the demo.
void Game::title_frame_tick(const app::GameInput& in) {
    music_.frame(sid_);
    count_title_frame();
    const std::uint8_t pressed = std::uint8_t(keys_of(in) & ~last_keys_);
    if (run_wait(in)) {
        return;
    }
    if (countdown_ == 0) {
        start_attract();
        return;
    }
    auto option_changed = [&] {
        wait_ = {true, option_delay, Next::title_again};
    };
    if (pressed & key_f7) {
        wait_ = {true, start_delay, Next::start_game};
        return;
    }
    if (pressed & key_f5) {
        options.yamo_player = !options.yamo_player;
        title_text_.toggle_yamo_player(options.yamo_player);
        option_changed();
        return;
    }
    if (pressed & key_f3) {
        options.alternate = !options.alternate;
        title_text_.toggle_alternate(options.alternate);
        option_changed();
        return;
    }
    if ((countdown_ >> 8) == 0x08 && (countdown_ & 0xFF) < 2) {
        title_text_.print(assets_, 0x0E, 0x0A, 2); // the joystick message goes
    }
    if (in.fire) {
        wait_ = {true, start_delay, Next::start_game};
    }
}

// $0700: the music stops, player 1, the player screen, then the game.
void Game::start_game() {
    music_.stop(sid_);
    world.player = 0;
    show_player_screen(Next::new_game);
}

// $0E9B: "PLAYER n" for $40 frames ($80 with two alternating players).
void Game::show_player_screen(Next then) {
    effects_.silence(sid_);
    phase_ = Phase::player_screen;
    player_shown_ = world.player;
    player_frames_ = options.alternate ? 2 * player_screen_time : player_screen_time;
    wait_ = {false, player_frames_, then};
}

// $0F69, $0755, $09AE
void Game::new_game() {
    world.two_players = options.yamo_player;
    world.new_game();
    other_player_ = world.player_state(); // $0FCE
    world.enter_room(0, assets_.start_x[0], assets_.start_y[0], false);
    request_sounds();
    phase_ = Phase::play;
    paused_ = false;
    pause_guard_ = 0;
    yamo_idle_ = 0;
    frames_to_pass_ = 1;
}

// $0531: the demo: a new game played by the recording from $F300, with the
// two-player options off meanwhile.
void Game::start_attract() {
    music_.stop(sid_);
    world.two_players = false;
    world.new_game();
    world.playing = false; // $0556
    world.demo_position = 0;
    world.enter_room(0, assets_.start_x[0], assets_.start_y[0], false);
    request_sounds();
    phase_ = Phase::attract;
    frames_to_pass_ = 1;
}

void Game::request_sounds() {
    for (const auto effect : world.pending_sounds) {
        effects_.request(sid_, effect);
    }
    world.pending_sounds.clear();
}

// $1830: a fall lost. In the demo, back to the title; with none left, the
// game-over screen; else the player screen and the room again (two players
// taking turns: the other one's turn, if still in the game).
void Game::lose_life() {
    effects_.silence(sid_);
    if (phase_ == Phase::attract) {
        music_wait_ = 0; // $1838
        enter_title();
        return;
    }
    if (!world.lose_life()) {
        game_over();
        return;
    }
    next_player();
}

// $1842
void Game::next_player() {
    const bool swap = options.alternate && !(other_player_.lives & 0x80);
    if (swap) {
        world.player ^= 1;
    }
    show_player_screen(swap ? Next::resume_swapped : Next::resume);
}

// $18A6: "GAME OVER" for $FE frames; then the other player's turn, or $40
// frames more and the title.
void Game::game_over() {
    phase_ = Phase::game_over;
    game_over_score_ = world.score;
    player_shown_ = world.player;
    wait_ = {false, game_over_time, Next::after_game_over};
}

// The demo: fire or F1 ends it ($19D3).
void Game::attract_tick(const app::GameInput& in) {
    play_tick(in);
}

// Play: the frame interrupt's work ($15C0) and a main-loop pass every third
// frame (after a room is drawn, the next pass follows at once).
void Game::play_tick(const app::GameInput& in) {
    const std::uint8_t pressed = std::uint8_t(keys_of(in) & ~last_keys_);
    const bool demo = phase_ == Phase::attract;
    Actor& bruce = world.actors.slots[0];
    // $16BA-$171B: the sticks, read every frame (in play only).
    if (!demo) {
        bruce.stick = stick_of(in, 0);
        bruce.fire = in.fire ? 0 : 1;
        if (world.two_players) {
            Actor& yamo = world.actors.slots[1];
            yamo.stick = stick_of(in, 1);
            yamo.fire = in.fire2 ? 0 : 1;
        }
    }
    bool redrawn = false;
    if (--frames_to_pass_ <= 0) {
        frames_to_pass_ = frames_per_pass;
        // $1BE1: the demo ends on fire or F1.
        if (demo && (in.fire || (pressed & key_f1))) {
            wait_ = {true, attract_leave_delay, Next::restart};
            last_keys_ = keys_of(in);
            return;
        }
        if (!demo) {
            // $1B44: F1 abandons the game, SPACE pauses; $1A23: fire ends a
            // pause once its guard has run out.
            if (pressed & key_f1) {
                restart();
                last_keys_ = keys_of(in);
                return;
            }
            if (pause_guard_ > 0) {
                --pause_guard_;
            }
            const bool toggle = (pressed & key_space) || (paused_ && pause_guard_ == 0 && !bruce.fire);
            if (toggle) {
                paused_ = !paused_;
                pause_guard_ = 3;
                if (paused_) {
                    world.print(0x0B, 1, 0x0F, true); // "PAUSED" ($1BBF)
                    world.start_blink();
                } else {
                    world.print(0x0C, 1, 0x0F, true); // $1BD1
                    world.draw_status();
                    if (world.idle & 0x80) {
                        world.idle = 0; // $1032
                    }
                }
            }
        }
        if (!paused_) {
            const auto events = world.pass();
            request_sounds();
            const auto& assets = world.assets;
            if (events.room_change) {
                effects_.silence(sid_); // $09E5
                world.enter_room(*events.room_change, bruce.x, bruce.y, true);
                request_sounds();
            } else if (events.bruce_died) {
                lose_life();
                last_keys_ = keys_of(in);
                return;
            } else if (events.back_to_start) {
                effects_.silence(sid_);
                world.enter_room(world.room, assets.start_x[std::size_t(world.room)],
                                 assets.start_y[std::size_t(world.room)], false);
                request_sounds();
            }
            redrawn = events.room_change || events.back_to_start;
            if (redrawn) {
                frames_to_pass_ = 1;
            }
        }
    }
    if (!redrawn) {
        // $15C0 (the room is drawn with interrupts off)
        effects_.frame(sid_);
        for (int v = 0; v < 3; ++v) {
            world.effect_streams[std::size_t(v)] = effects_.stream(v);
        }
        if (!paused_) {
            world.frame();
        }
        // $173B: any stick or fire in play keeps the idle count at 0; at
        // $80 (128 x 256 frames) the game pauses itself ($164C).
        const Actor& yamo = world.actors.slots[1];
        const bool moved = bruce.stick != 0x0F || !bruce.fire ||
                           (world.two_players && (yamo.stick != 0x0F || !yamo.fire));
        if (demo || (!paused_ && moved)) {
            world.idle = 0;
        } else if (world.idle == 0x80) {
            paused_ = true;
            pause_guard_ = 3;
            world.print(0x0B, 1, 0x0F, true);
            world.start_blink();
            world.idle = 0xFE; // $1022 (the colour cycling that follows is not rebuilt)
        }
        // $1606 / $1614: an "L" at row 1 for a left-handed joystick (the
        // second's only with a second player).
        world.field.at(2, 1).code = options.left_handed[0] ? 0x2C : 0x00;
        const bool second = options.yamo_player || options.alternate;
        world.field.at(37, 1).code = second && options.left_handed[1] ? 0x2C : 0x00;
        // $1774: the Yamo's player holding fire with the stick at rest for
        // 7 x 256 frames gives up: one player again, the room's clock late,
        // five falls less for both.
        if (!demo && !paused_ && world.two_players) {
            const Actor& yamo = world.actors.slots[1];
            if (yamo.stick == 0x0F && !yamo.fire) {
                if (++yamo_idle_ >= 7 * 256) {
                    world.two_players = false;
                    options.yamo_player = false;
                    world.clock = 0x20;
                    effects_.request(sid_, sound_give_up);
                    world.lives = world.lives >= 5 ? std::uint8_t(world.lives - 5) : 0;
                    other_player_.lives = other_player_.lives >= 5 ? std::uint8_t(other_player_.lives - 5) : 0;
                    world.draw_status();
                    yamo_idle_ = 0;
                }
            } else {
                yamo_idle_ = 0;
            }
        }
    }
    last_keys_ = keys_of(in);
}

void Game::tick(const app::GameInput& in) {
    c64::TextFrame frame;
    switch (phase_) {
    case Phase::title:
        title_frame_tick(in);
        break;
    case Phase::attract:
    case Phase::play:
        if (!run_wait(in)) {
            phase_ == Phase::attract ? attract_tick(in) : play_tick(in);
        }
        break;
    case Phase::player_screen:
    case Phase::game_over:
        if (phase_ == Phase::game_over) {
            music_.frame(sid_); // the title's interrupt plays (nothing)
        }
        run_wait(in);
        break;
    }
    last_keys_ = keys_of(in);
    switch (phase_) {
    case Phase::title:
        frame = title_frame(assets_, title_text_);
        break;
    case Phase::player_screen:
        frame = player_frame(assets_, player_shown_);
        break;
    case Phase::game_over:
        frame = game_over_frame(assets_, player_shown_, game_over_score_);
        break;
    default:
        frame = world_frame(world);
        break;
    }
    c64::Collisions collisions;
    picture_ = frame.rasterise(&collisions);
    if (phase_ == Phase::play || phase_ == Phase::attract) {
        world.collision_latch |= collisions.sprite_sprite;
    }
}

} // namespace bruce_lee::game
