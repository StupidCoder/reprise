// Widescreen enhancement: how the reconstructed rules adapt to a presented
// view larger than the 160x144 LCD. Every rule is gated on such a view, so the
// original format is unaffected.
//
// The main problem is object activation. The original only runs objects near
// the camera; in a wider view, objects would visibly freeze at the edges or
// pop in. Each object type therefore gets one of three activation rules below.

#include "sonic1_gg/enhancements/widescreen.hpp"
#include "sonic1_gg/game/arithmetic.hpp"
#include "sonic1_gg/game/rom_tables.hpp"
#include <algorithm>
#include <cmath>

namespace sonic1_gg {

namespace {

namespace type = object_type;

// How an object type is activated while the view is larger than the LCD.
enum class Rule {
    // The original bounds; once running it stays active while visible. For
    // bosses, arena and exit triggers, doors and scripted sequences, whose first
    // update locks the camera, swaps art or starts timers.
    Original,
    // The original bounds widened by the extra view. For monitors, platforms,
    // hazards and enemies whose patrol or firing cycle is independent of Sonic.
    Passive,
    // Drawn in place and settled onto the ground, but frozen until the original
    // bounds (or Sonic's own reach) would wake it. For enemies whose script is
    // timed from their first update, so that it still meets Sonic as designed.
    Staged,
};

Rule rule(const Game& g, int object) {
    switch (object) {
    case type::goal_sign: // First update swaps art and ends auto-scrolling.
    case type::crab:
    case type::ground_bug:
    case type::spiked_walker: // Falls, then walks left for 640 updates.
    case type::jumping_swimmer:
    case type::capsule: // First update moves the camera limits (736B).
        return Rule::Staged;
    case type::life_monitor: // Scrap Brain 1's life monitor rolls away at once.
        return g.level.act == 12 ? Rule::Staged : Rule::Passive;
    // Monitors and the emerald.
    case type::ring_monitor:
    case type::shoes_monitor:
    case type::shield_monitor:
    case type::invincibility_monitor:
    case type::chaos_emerald:
    case type::checkpoint_monitor:
    case type::continue_monitor:
    // Platforms, the log and the seesaw.
    case type::path_platform:
    case type::sinking_platform:
    case type::falling_platform:
    case type::sliding_platform:
    case type::cycling_platform:
    case type::paired_platform:
    case type::floating_log:
    case type::sky_platform:
    case type::rising_platform:
    case type::buoyant_platform:
    case type::seesaw:
    // Allocated explosions, shots, bubbles and lost rings.
    case type::explosion:
    case type::projectile:
    case type::bouncing_bomb:
    case type::air_bubble:
    case type::homing_trail:
    case type::homing_shot:
    case type::launcher_shot:
    case type::loose_spike:
    case type::small_bubble:
    case type::lost_rings:
    // The flying shooter arms itself beyond the (widened) right edge.
    case type::flying_shooter:
    // Periodic hazards, shooters and switches.
    case type::wall_shooter:
    case type::electric_right:
    case type::electric_left:
    case type::flame_jet:
    case type::electric_arc:
    case type::bomb_thrower:
    case type::pressure_switch:
    case type::crawler:
    case type::leaping_fish:
    case type::spark_pair:
    case type::mine:
    case type::ceiling_launcher:
    case type::spike_ball:
    case type::rotating_gun:
    case type::crusher:
    case type::hidden_gun:
    case type::swimmer:
    case type::path_hazard:
    case type::spike_chain:
    case type::spear:
    case type::bubble_source:
    case type::flowers:
        return Rule::Passive;
    default:
        return Rule::Original;
    }
}

// The goal sign and capsule move camera_left with the camera each update
// (61F8, 736B). That rolling limit is not an arena.
bool finishing(const Game& g) {
    return std::any_of(g.objects.begin(), g.objects.end(), [](const Object& o) {
        return o.active && (o.type == type::goal_sign || o.type == type::capsule);
    });
}

// Whether draw_objects would show the object.
bool object_drawn(const Object& o) {
    if (!(o.active || o.staged) || o.type == type::none) {
        return false;
    }
    return o.mapping || !o.extra_sprites.empty() || o.type == type::seesaw ||
           o.type == type::flowers;
}

struct Box {
    int x{}, y{}, w{}, h{};
};

// The world box an object covers: its direct sprites, or its position and
// collision size (at least 16x16).
Box object_box(const Object& o) {
    if (!o.mapping && !o.extra_sprites.empty()) {
        int x0 = 1 << 30, y0 = 1 << 30, x1 = -x0, y1 = -y0;
        for (auto& s : o.extra_sprites) {
            x0 = std::min(x0, s[1]), y0 = std::min(y0, s[2]);
            x1 = std::max(x1, s[1] + 8), y1 = std::max(y1, s[2] + 16);
        }
        return {x0, y0, x1 - x0, y1 - y0};
    }
    return {o.pixel_x(), o.pixel_y(), std::max(o.width, 16), std::max(o.height, 16)};
}

// The box's centre lies at least 8 px inside the presented view and at least
// 8 px outside the LCD crop: only the larger view shows it there.
bool in_wide_margin(const Game& g, const Box& b) {
    const auto v = view_rect(g, std::max(g.view_width, 160), std::max(g.view_height, 144));
    const int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    const bool view =
        cx > v.x + 8 && cx < v.x + v.width - 8 && cy > v.y + 8 && cy < v.y + v.height - 8;
    const int lx = g.camera.x + 48, ly = g.camera.y + 24;
    const bool lcd = cx > lx - 8 && cx < lx + 168 && cy > ly - 8 && cy < ly + 152;
    return view && !lcd;
}

} // namespace

void ObjectFades::reset() {
    ghosts_.clear();
    fade_ = {};
    primed_ = false;
}

// Fades objects in when their original rule activates them inside the larger
// view, and keeps a fading copy ("ghost") when one is deactivated there.
void ObjectFades::tick(const Game* game, bool enabled) {
    for (auto& g : ghosts_) {
        --g.ticks;
    }
    std::erase_if(ghosts_, [](const Ghost& g) { return g.ticks <= 0; });
    if (!game || !enabled || !game->widescreen()) {
        reset();
        return;
    }
    const auto& g = *game;
    if (g.level.act != act_) {
        reset();
        act_ = g.level.act;
    }
    for (int i = 1; i < 32; ++i) {
        const auto& o = g.objects[i];
        const bool now = object_drawn(o), running = o.active || o.staged;
        if (fade_[i]) {
            --fade_[i];
        }
        if (primed_ && o.type == type_[i] && in_wide_margin(g, object_box(o))) {
            // Activated by its original bounds while already in view.
            if (now && !drawn_[i] && !running_[i]) {
                fade_[i] = fade_ticks;
            }
        }
        if (primed_ && !now && drawn_[i] && !running && o.type == type_[i] &&
            in_wide_margin(g, object_box(last_[i]))) {
            ghosts_.push_back({last_[i], fade_ticks}); // Deactivated in view.
        }
        drawn_[i] = now;
        running_[i] = running;
        type_[i] = o.type;
        if (now) {
            last_[i] = o;
        }
    }
    primed_ = true;
}

ViewRect view_rect(const Game& g, int width, int height) {
    // The LCD shows VDP pixels 48..207 and 24..167 of the camera window.
    const int lcd_x = g.camera.x + 48, lcd_y = g.camera.y + 24;
    auto fit = [](int lcd, int lcd_size, int size, int low, int high) {
        low = std::min(low, lcd);
        high = std::max(high, lcd + lcd_size);
        if (high - low <= size) {
            return low - (size - (high - low)) / 2;
        }
        return std::clamp(lcd - (size - lcd_size) / 2, low, high - size);
    };
    return {fit(lcd_x, 160, width, g.source.camera_left + 48, g.source.camera_right + 208),
            fit(lcd_y, 144, height, g.source.camera_top + 24, g.source.camera_bottom + 168), width,
            height};
}

bool Game::rolling_limits() const {
    return horizontal_scroll || finishing(*this);
}

void Game::update_wide_view() {
    wide_left = wide_right = wide_top = wide_bottom = 0;
    if (view_width <= 160 && view_height <= 144) {
        return;
    }
    const auto view = view_rect(*this, std::max(view_width, 160), std::max(view_height, 144));
    wide_left = camera.x + 48 - view.x;
    wide_right = view.x + view.width - (camera.x + 208);
    wide_top = camera.y + 24 - view.y;
    wide_bottom = view.y + view.height - (camera.y + 168);
}

bool Game::wide_activation(Object& o, bool original, bool reach, bool wide, bool was_active) {
    // The goal sign (61F8), capsule (736B) and Scrap Brain's chase (A7B6)
    // rewrite the camera limits on every update. Kept running beyond their
    // original bounds, they would override an arena's limits.
    if (o.type == type::goal_sign || o.type == type::capsule || o.type == type::scrap_brain_chase) {
        was_active = false;
    }
    switch (rule(*this, o.type)) {
    case Rule::Passive:
        return wide;
    case Rule::Staged:
        if (original || reach || (was_active && wide)) {
            return true;
        }
        o.staged = wide;
        return false;
    case Rule::Original:
        break;
    }
    return original || (was_active && wide);
}

void Game::stage_object(Object& o) {
    // Pose only: the first frame of each type's opening animation, using
    // separate counters so its script still starts from its loaded state.
    auto& s = o.state;
    const auto saved = std::array{s[5], s[6]};
    s[5] = o.staged_animation[0];
    s[6] = o.staged_animation[1];
    int gravity = 0, limit = 1024;
    switch (o.type) {
    case type::goal_sign: // 64C2 is the idle sign sequence; 6278 adds 26 each update.
        o.width = 24, o.height = 48;
        o.mapping = rom::goal_frames + 18 * level.data(rom::goal_idle);
        gravity = 26;
        break;
    case type::crab: // 65F9: first script action selects the animation.
        o.width = 16, o.height = 31;
        animate_object(o, level.word(rom::crab_animations + level.data(rom::crab_script) * 2),
                       rom::crab_frames);
        gravity = 32;
        break;
    case type::ground_bug: // 6E65: constant downward speed.
        o.width = 10, o.height = 16;
        animate_object(
            o, level.word(rom::ground_bug_animations + level.data(rom::ground_bug_script) * 2),
            rom::ground_bug_frames);
        o.vy = 512;
        break;
    case type::spiked_walker: // 82FB: left-walking sequence.
        o.width = 20, o.height = 32;
        animate_object(o, rom::walker_walk_left, rom::walker_frames);
        gravity = 32;
        break;
    case type::jumping_swimmer: // 8F72: idle sequence facing Sonic.
        o.width = 12, o.height = 32;
        animate_object(
            o, o.pixel_x() >= sonic.pixel_x() ? rom::jumper_swim_left : rom::jumper_swim_right,
            rom::jumper_frames);
        gravity = 16, limit = 768;
        break;
    case type::capsule: // 756D: closed capsule; 736B moves it down 16 px when it starts.
        o.width = 28, o.height = 64;
        o.mapping = rom::capsule_layout;
        break;
    case type::life_monitor: // 5EA4: monitor body; the renderer adds the icon.
        o.width = 20, o.height = 24;
        o.mapping = rom::monitor_with_icon;
        break;
    default:
        o.staged = false;
        break;
    }
    o.staged_animation = {s[5], s[6]};
    s[5] = saved[0];
    s[6] = saved[1];
    if (gravity) {
        o.vy = std::min(o.vy + gravity, limit);
    }
    if (gravity || o.type == type::ground_bug) {
        // Settle onto terrain without horizontal motion or conveyor drift.
        o.vx = 0;
        integrate_body(o);
        o.vx = 0;
    }
}

void ArenaBarrier::reset() {
    field_.clear();
    act_ = ~0u;
}

void ArenaBarrier::tick(const Game* game, bool enabled) {
    field_.advance();
    if (!game || !enabled || !game->widescreen() || game->special_mode) {
        field_.clear();
        return;
    }
    const auto& g = *game;
    if (g.level.act != act_) {
        field_.clear();
        act_ = g.level.act;
    }
    // 4F33-4F8C clamp Sonic to camera_left+48 .. camera_right+208-16-1.
    // Not an arena: auto-scrolling, a goal sign or capsule rolling the limits
    // along, or the act-end run (once the capsule has removed itself, its
    // last limits are still equal).
    const bool lock =
        g.level.camera_left == g.level.camera_right && !g.rolling_limits() && !g.auto_run;
    const int sonic = g.sonic.pixel_x();
    for (bool right : {false, true}) {
        const int x = right ? g.level.camera_right + 208 : g.level.camera_left + 48;
        auto* wall = field_.side(right, lock, x);
        if (!wall) {
            continue;
        }
        const bool pushing =
            right ? sonic >= x - 17 && g.current_input.right : sonic <= x && g.current_input.left;
        if (pushing && !g.sonic.dead) {
            gg::ForceField::touch(*wall, g.sonic.pixel_y() + g.sonic.height / 2);
        }
        // Bosses are not bound by 4F33 and may swoop across the limit (the
        // Green Hill boss leaves the LCD twice per cycle). The field lets them
        // through with the same ripple instead of looking painted on.
        for (const auto& o : g.objects) {
            const bool boss = o.type == type::green_hill_boss ||
                              o.type == type::scrap_brain_chase || o.type == type::jungle_boss ||
                              o.type == type::bridge_boss || o.type == type::labyrinth_boss ||
                              o.type == type::final_boss;
            if (!o.active || !boss) {
                continue;
            }
            const auto box = object_box(o);
            if (box.x < x && box.x + box.w > x) {
                gg::ForceField::touch(*wall, box.y + box.h / 2);
            }
        }
    }
}

void ArenaBarrier::draw(gg::Screen& image, const Game&, int view_x, int view_y) const {
    field_.draw(image, view_x, view_y);
}
} // namespace sonic1_gg
