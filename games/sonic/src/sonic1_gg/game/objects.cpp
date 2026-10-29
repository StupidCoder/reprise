// The object system: placement, the activation scan, the type -> handler
// table, and helpers shared by all object handlers (contact tests, platforms,
// animation, projectiles, destruction).
//
// The handlers themselves are in common_objects.cpp (objects found in several
// zones) and in the zone files.

#include "sonic1_gg/game/arithmetic.hpp"
#include "sonic1_gg/game/game.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace sonic1_gg {

using namespace body_flag;
namespace type = object_type;

void Game::reset_objects() {
    for (auto& o : objects) {
        o = {};
        o.width = o.height = o.flags = 0;
    }
    // Placement records are (type, cell X, cell Y) in 32-pixel cells and fill
    // the slots from 1 upward.
    for (std::size_t i = 0; i < level.placements.size() / 3; ++i) {
        auto& o = objects[i + 1];
        o.type = level.placements[i * 3];
        o.x = level.placements[i * 3 + 1] * 32 * 256;
        o.y = level.placements[i * 3 + 2] * 32 * 256;
    }
}

// Activation and update of all objects (2BD8, 2C8D).
//
// An object only runs while it is near the camera. Each type has four
// activation distances (left, right, above, below; Level::active_bounds). The
// scan does not test every slot every update: it refreshes four of the 32
// slots per update, so a given slot is re-tested every eighth update. All
// active objects then update in slot order, before Sonic.
void Game::update_objects() {
    foreground_clouds = 0;
    for (unsigned i = (ticks & 7) * 4; i < (ticks & 7) * 4 + 4; ++i) {
        auto& o = objects[i];
        const bool was_active = o.active;
        o.active = o.staged = false;
        if (o.type >= type::first_inactive) {
            continue;
        }
        auto bound = [&](int index) {
            const auto at = o.type * 8 + index * 2;
            return level.active_bounds[at] | (level.active_bounds[at + 1] << 8);
        };
        // The original copies the left distance with six LDI instructions,
        // which decrement BC, so its effective distance is 6 pixels shorter
        // than the table value.
        auto inside = [&](int camera_x, int left, int right, int top, int bottom) {
            return o.pixel_x() > std::max(0, camera_x - (bound(0) - 6) - left) &&
                   o.pixel_x() <= camera_x + bound(1) + right &&
                   o.pixel_y() > std::max(0, camera.y - bound(2) - top) &&
                   o.pixel_y() <= camera.y + bound(3) + bottom;
        };
        o.active = inside(camera.x, 0, 0, 0, 0);
        if (widescreen()) {
            // Widescreen enhancement (widescreen.cpp). In an auto-scrolling act
            // Sonic may run ahead of the LCD crop, so also test the bounds
            // around the camera that would follow him. Only there: elsewhere
            // the original clamp lets Sonic overshoot an arena edge by a pixel,
            // which must not wake the capsule or goal early.
            const int sonic_x = sonic.pixel_x();
            const int reach_x = !horizontal_scroll              ? camera.x
                                : sonic_x + 16 > camera.x + 208 ? sonic_x - 88
                                : sonic_x < camera.x + 48       ? sonic_x - 80
                                                                : camera.x;
            o.active =
                wide_activation(o, o.active, inside(reach_x, 0, 0, 0, 0),
                                inside(camera.x, wide_left + wide_slack, wide_right + wide_slack,
                                       wide_top + wide_slack, wide_bottom + wide_slack),
                                was_active);
        }
    }
    for (unsigned i = 1; i < objects.size(); ++i) {
        auto& o = objects[i];
        if (o.type == type::none) {
            continue;
        }
        if (o.staged && widescreen()) {
            stage_object(o);
            continue;
        }
        if (!o.active) {
            continue;
        }
        update_object(o);
        integrate_body(o);
    }
}

// Table-driven object animation (7C75). A sequence is a list of (frame,
// duration) byte pairs ending in 0xFF, which loops. The frame selects one of the
// 18-byte sprite layouts starting at `frames`. State byte 6 is the position in
// the sequence and byte 5 the timer; the timer advances AFTER the frame is
// selected.
void Game::animate_object(Object& o, int sequence, int frames) {
    if (level.data(sequence + o.state[6]) == 255) {
        o.state[6] = 0;
    }
    o.mapping = frames + 18 * level.data(sequence + o.state[6]);
    if (++o.state[5] > level.data(sequence + o.state[6] + 1)) {
        o.state[5] = 0;
        o.state[6] += 2;
    }
}

// Contact test between an object and Sonic (3328). Edges are inclusive, and the
// insets shrink only the object's left and top edges, so most hit boxes are
// smaller than the object's width and height suggest.
bool Game::overlaps(const Object& o, int inset_x, int inset_y) const {
    if (sonic.dead) {
        return false;
    }
    const int ox = o.pixel_x(), oy = o.pixel_y();
    const int sx = sonic.pixel_x(), sy = sonic.pixel_y();
    return ox + o.width >= sx && sx + sonic.width >= ox + inset_x && oy + o.height >= sy &&
           sy + sonic.height >= oy + inset_y;
}

// Puts Sonic on top of a platform (7CF5): his feet at the platform's top plus
// `top`, moved with the platform's vertical motion `dy`. The vertical speed is
// set to minus the gravity added this update, which the next movement cancels,
// so he rides the platform exactly. The fractional part of Y is kept.
void Game::land_on_platform(const Object& o, int top, int dy) {
    sonic.set_pixel_y(pixel(o.y + dy) + top - sonic.height);
    sonic.vy = -sonic.previous_gravity;
    sonic.flags |= grounded;
}

// Claims a free slot for a new object (7CAF). The slot keeps whatever state
// bytes its previous occupant left behind unless the caller clears them; some
// objects rely on that. The new object still has to be activated by the scan.
Object* Game::allocate_object(int new_type) {
    for (unsigned i = 1; i < objects.size(); ++i) {
        if (objects[i].type == type::none) {
            auto& o = objects[i];
            o.type = new_type;
            return &o;
        }
    }
    return nullptr;
}

// Adds rings (337E; the original counts in packed BCD). 100 rings give an extra
// life and wrap the counter.
void Game::add_rings(int amount) {
    rings += amount;
    if (rings >= 100) {
        rings -= 100;
        ++lives;
        sound(Effect::ExtraLife);
    } else {
        sound(Effect::Ring);
    }
}

// Destroys an enemy (309A): it turns into an explosion in place, moved by the
// given offset, and awards 100 points. Its velocity is kept until the
// explosion's first update.
void Game::kill_object(Object& o, int offset_x, int offset_y) {
    sound(Effect::Break);
    o.type = type::explosion;
    o.set_pixel_x(o.pixel_x() + offset_x);
    o.set_pixel_y(o.pixel_y() + offset_y);
    o.mapping = 0;
    add_score(100);
}

// The usual enemy contact: touching the hit box while curled or invincible
// destroys the enemy (moved by kill_dx/kill_dy before it explodes); otherwise
// Sonic is hurt.
void Game::enemy_contact(Object& o, int inset_x, int inset_y, int kill_dx, int kill_dy) {
    if (overlaps(o, inset_x, inset_y)) {
        if (sonic.invincible_ticks || (sonic.flags & curled)) {
            kill_object(o, kill_dx, kill_dy);
        } else {
            sonic.take_hit(*this);
        }
    }
}

// Starts a projectile at whole-pixel position (x, y). Its state word 2 is the
// gravity added each update; the other state bytes are cleared except byte 1,
// which keeps whatever the reused slot held. Returns false if no slot is free.
bool Game::fire_projectile(int x, int y, int vx, int vy, int gravity) {
    auto* shot = allocate_object(type::projectile);
    if (!shot) {
        return false;
    }
    shot->x = x * 256;
    shot->y = y * 256;
    shot->vx = vx;
    shot->vy = vy;
    shot->state[0] = 0;
    for (int j = 2; j < 7; ++j) {
        shot->state[j] = 0;
    }
    shot->state[2] = gravity;
    return true;
}

// Adds a hardware sprite at an offset from the object. Objects made of several
// independently placed pieces (flames, spikes, sparks) are drawn this way.
void Game::emit_sprite(Object& o, int tile, int dx, int dy) {
    o.extra_sprites.push_back({tile, word(o.pixel_x() + dx), word(o.pixel_y() + dy)});
}

// The update routine of every object type. The type number is the index;
// unlisted types do not occur in this game.
namespace {

using Handler = void (Game::*)(Object&);

constexpr std::array<Handler, 256> make_handlers() {
    std::array<Handler, 256> h{};
    // Monitors and collectables.
    h[type::ring_monitor] = &Game::update_monitor;
    h[type::shoes_monitor] = &Game::update_monitor;
    h[type::life_monitor] = &Game::update_monitor;
    h[type::shield_monitor] = &Game::update_monitor;
    h[type::invincibility_monitor] = &Game::update_monitor;
    h[type::checkpoint_monitor] = &Game::update_monitor;
    h[type::continue_monitor] = &Game::update_monitor;
    h[type::chaos_emerald] = &Game::update_chaos_emerald;
    h[type::goal_sign] = &Game::update_goal_sign;
    h[type::lost_rings] = &Game::update_lost_rings;
    // Common enemies and effects.
    h[type::crab] = &Game::update_crab;
    h[type::explosion] = &Game::update_explosion;
    h[type::projectile] = &Game::update_projectile;
    h[type::flying_shooter] = &Game::update_flying_shooter;
    h[type::ground_bug] = &Game::update_ground_bug;
    h[type::wall_shooter] = &Game::update_wall_shooter;
    h[type::leaping_fish] = &Game::update_leaping_fish;
    h[type::freed_animal_pausing] = &Game::update_animal;
    h[type::freed_animal_bouncing] = &Game::update_animal;
    h[type::capsule] = &Game::update_capsule;
    // Platforms and scenery.
    h[type::path_platform] = &Game::update_path_platform;
    h[type::sinking_platform] = &Game::update_sinking_platform;
    h[type::sliding_platform] = &Game::update_sliding_platform;
    h[type::cycling_platform] = &Game::update_cycling_platform;
    h[type::paired_platform] = &Game::update_paired_platform;
    h[type::floating_log] = &Game::update_floating_log;
    h[type::launch_trigger] = &Game::update_launch_trigger;
    h[type::flowers] = &Game::update_flowers;
    // Bosses.
    h[type::green_hill_boss] = &Game::update_green_hill_boss;
    h[type::jungle_boss] = &Game::update_jungle_boss;
    h[type::bridge_boss] = &Game::update_bridge_boss;
    h[type::labyrinth_boss] = &Game::update_labyrinth_boss;
    h[type::scrap_brain_chase] = &Game::update_scrap_brain_chase;
    h[type::final_boss] = &Game::update_final_boss;
    h[type::final_boss_orb] = &Game::update_final_boss_orb;
    h[type::final_boss_flame] = &Game::update_final_boss_flame;
    // Bridge.
    h[type::spiked_walker] = &Game::update_spiked_walker;
    h[type::collapsing_plank] = &Game::update_collapsing_plank;
    h[type::rising_platform] = &Game::update_rising_platform;
    h[type::seesaw] = &Game::update_seesaw;
    // Jungle.
    h[type::falling_platform] = &Game::update_falling_platform;
    h[type::boss_bomb] = &Game::update_boss_bomb;
    // Labyrinth.
    h[type::pressure_switch] = &Game::update_pressure_switch;
    h[type::air_bubble] = &Game::update_air_bubble;
    h[type::homing_trail] = &Game::update_homing_trail;
    h[type::homing_shot] = &Game::update_homing_shot;
    h[type::spike_ball] = &Game::update_spike_ball;
    h[type::loose_spike] = &Game::update_loose_spike;
    h[type::swimmer] = &Game::update_swimmer;
    h[type::path_hazard] = &Game::update_path_hazard;
    h[type::spike_chain] = &Game::update_spike_chain;
    h[type::spear] = &Game::update_spear;
    h[type::water_level] = &Game::update_water_level;
    h[type::bubble_source] = &Game::update_bubble_source;
    h[type::small_bubble] = &Game::update_small_bubble;
    h[type::unused_67] = &Game::update_nothing;
    h[type::jumping_swimmer] = &Game::update_jumping_swimmer;
    h[type::buoyant_platform] = &Game::update_buoyant_platform;
    // Scrap Brain and Sky Base.
    h[type::area_exit] = &Game::update_area_exit;
    h[type::electric_right] = &Game::update_electric_bolt;
    h[type::electric_left] = &Game::update_electric_bolt;
    h[type::flame_jet] = &Game::update_flame_jet;
    h[type::door_from_left] = &Game::update_door;
    h[type::door_from_right] = &Game::update_door;
    h[type::door_both_sides] = &Game::update_door;
    h[type::switch_door] = &Game::update_door;
    h[type::electric_arc] = &Game::update_electric_arc;
    h[type::bomb_thrower] = &Game::update_bomb_thrower;
    h[type::bouncing_bomb] = &Game::update_bouncing_bomb;
    h[type::crawler] = &Game::update_crawler;
    h[type::cloud] = &Game::update_cloud;
    h[type::spark_pair] = &Game::update_spark_pair;
    h[type::mine] = &Game::update_mine;
    h[type::ceiling_launcher] = &Game::update_ceiling_launcher;
    h[type::launcher_shot] = &Game::update_launcher_shot;
    h[type::rotating_gun] = &Game::update_rotating_gun;
    h[type::sky_platform] = &Game::update_sky_platform;
    h[type::crusher] = &Game::update_crusher;
    h[type::hidden_gun] = &Game::update_hidden_gun;
    // Special stages.
    h[type::moving_bumper] = &Game::update_moving_bumper;
    h[type::flipper] = &Game::update_flipper;
    // Ending.
    h[type::ending_flyby] = &Game::update_ending_flyby;
    h[type::ending_emerald] = &Game::update_ending_emerald;
    return h;
}

constexpr auto handlers = make_handlers();

} // namespace

// Behaviour of one object for one update. The object moves afterwards
// (integrate_body), so velocities set here take effect in this same update.
void Game::update_object(Object& o) {
    o.extra_sprites.clear();
    const Handler handler = handlers[std::size_t(o.type) & 255];
    if (!handler) {
        throw std::runtime_error("Unimplemented object type " + std::to_string(o.type));
    }
    (this->*handler)(o);
}

// Type 67's routine is a bare RET (8F71).
void Game::update_nothing(Object&) {}

} // namespace sonic1_gg
