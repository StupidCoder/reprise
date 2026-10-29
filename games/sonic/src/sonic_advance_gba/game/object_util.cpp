#include "sonic_advance_gba/game/object_util.hpp"
#include "sonic_advance_gba/game/destruction.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/moon_zone.hpp"
#include "sonic_advance_gba/game/player_effects.hpp"
#include "sonic_advance_gba/game/stage.hpp"

namespace sonic_advance_gba {

bool out_of_range(const StageState& s, s32 x, s32 y, s16 sx, s16 sy) {
    const auto& c = s.camera;
    const bool far = c.x + 0x170 < x || x < c.x - 0x80 || c.y + 0x120 < y || y < c.y - 0x80;
    const bool off_view = u16(sx + 0x80) > 0x1F0 || sy + 0x80 < 0 || sy > 0x120;
    return far && off_view;
}

bool hurt_player(Game& g, Player& p) {
    if (p.invincible_timer >= 1 || p.hurt_timer >= 1) {
        return false;
    }
    p.hurt_timer = 0x78;
    const u32 f = p.flags;
    p.speed_x =
        (p.speed_x < 0 || (p.speed_x == 0 && (f & player_flag::facing_left))) ? 0x180 : -0x180;
    p.speed_y = -0x300;
    if (f & player_flag::underwater) {
        p.speed_y = -0x180;
        p.speed_x = s16(p.speed_x >> 1);
    }
    p.flags = (f & 0xFFFFFAC3) | player_flag::in_air;
    p.state = 0x0F;
    set_player_radius(g, p, 6, 0xE);
    p.special = 0;
    p.move = 0;
    if (p.flags & player_flag::riding) {
        // Knocked off a ride onto the other layer, if there is room.
        p.layer ^= 1;
        if (player_room(g, p, u8(p.angle - 0x80)) < 4) {
            p.layer ^= 1;
        } else {
            p.flags &= ~player_flag::riding;
            p.power &= 0x7F;
            p.body->sprite.flags = (p.body->sprite.flags & ~sprite_flag::priority) | 0x2000;
        }
    }
    const u8 power = p.power;
    p.power &= 0x7F;
    if (p.index == 0) {
        StageState& s = g.stage();
        if ((power & (player_power::shield | player_power::magnetic_shield)) == 0) {
            if (s.rings == 0) {
                if (!(s.flags & stage_flag::demo) && g.game_mode != 4 && g.game_mode != 5) {
                    p.flags |= player_flag::dead;
                }
            } else {
                u16 n = s.rings;
                if (g.game_mode == 6 && n > 10) {
                    n = 10;
                }
                scatter_rings(g, p.x >> 8, p.y >> 8, n);
                s.rings = u16(s.rings - n);
            }
        } else {
            // The shield goes instead.
            g.sound().start(0x77);
            p.power &= u8(~(player_power::shield | player_power::magnetic_shield));
        }
    }
    return true;
}

bool enemy_touched(Game& g, const Sprite& s, s16 x, s16 y) {
    if (s.hitboxes[0].index == 0xFFFFFFFF) {
        return false;
    }
    StageState& st = g.stage();
    if (g.game_mode >= 2) {
        throw Unrecovered("enemies in multiplayer (0800B4F8)");
    }
    for (unsigned n = 0; n < st.players; ++n) {
        Player& p = st.player[n];
        if (p.flags & player_flag::dead) {
            continue;
        }
        const Sprite& body = p.body->sprite;
        const s32 px = p.x >> 8, py = p.y >> 8;
        bool destroyed = false;
        if (body.hitboxes[1].index != 0xFFFFFFFF &&
            hitboxes_meet(x, y, s.hitboxes[0], px, py, body.hitboxes[1])) {
            // The attack box: bounced off.
            if (p.speed_y > 0) {
                p.speed_y = s16(-p.speed_y);
            }
            destroyed = true;
        } else if (body.hitboxes[0].index != 0xFFFFFFFF &&
                   hitboxes_meet(x, y, s.hitboxes[0], px, py, body.hitboxes[0])) {
            if (!(p.power & player_power::invincible)) {
                hurt_player(g, p);
                continue;
            }
            destroyed = true; // invincible
        }
        if (destroyed) {
            create_explosion(g, x, y);
            free_animal(g, x, y);
            award_enemy_points(g, x, y);
            return true;
        }
    }
    return false;
}

bool projectile_hits(Game& g, const Sprite& s, s16 x, s16 y) {
    if (s.hitboxes[0].index == 0xFFFFFFFF) {
        return false;
    }
    StageState& st = g.stage();
    bool hit = false;
    for (unsigned n = 0; n < st.players; ++n) {
        Player& p = st.player[n];
        const Sprite& body = p.body->sprite;
        if ((p.flags & player_flag::dead) || body.hitboxes[0].index == 0xFFFFFFFF) {
            continue;
        }
        if (hitboxes_meet(x, y, s.hitboxes[0], p.x >> 8, p.y >> 8, body.hitboxes[0])) {
            hurt_player(g, p);
            hit = true;
        }
    }
    return hit;
}

BossContact boss_contact(Game& g, const Sprite& s, s16 x, s16 y, Player& p) {
    const Sprite& body = p.body->sprite;
    if (s.hitboxes[0].index == 0xFFFFFFFF || s.hitboxes[1].index == 0xFFFFFFFF ||
        (p.flags & player_flag::dead)) {
        return BossContact::none;
    }
    const s32 px = p.x >> 8, py = p.y >> 8;
    if (body.hitboxes[1].index != 0xFFFFFFFF) {
        if (hitboxes_meet(x, y, s.hitboxes[1], px, py, body.hitboxes[0])) {
            hurt_player(g, p);
            return BossContact::hurt;
        }
        if (hitboxes_meet(x, y, s.hitboxes[0], px, py, body.hitboxes[1])) {
            // Bounced back (Amy keeps her direction at half speed).
            p.speed_x = p.character == 3 ? s16(p.speed_x >> 1) : s16(-p.speed_x);
            p.speed_y = s16(-p.speed_y);
            return BossContact::attacked;
        }
    }
    if (body.hitboxes[0].index != 0xFFFFFFFF &&
        hitboxes_meet(x, y, s.hitboxes[0], px, py, body.hitboxes[0])) {
        hurt_player(g, p);
        return BossContact::hurt;
    }
    return BossContact::none;
}

BossContact boss_contact_from_air(Game& g, const Sprite& s, s16 x, s16 y, Player& p) {
    if (p.flags & player_flag::dead) {
        return BossContact::none;
    }
    const Sprite& body = p.body->sprite;
    const s32 px = p.x >> 8, py = p.y >> 8;
    if (body.hitboxes[1].index != 0xFFFFFFFF && s.hitboxes[0].index != 0xFFFFFFFF &&
        (p.flags & player_flag::in_air) &&
        hitboxes_meet(x, y, s.hitboxes[0], px, py, body.hitboxes[1])) {
        p.speed_x = p.character == 3 ? s16(p.speed_x >> 1) : s16(-p.speed_x);
        p.speed_y = s16(-p.speed_y);
        return BossContact::attacked;
    }
    if (body.hitboxes[0].index == 0xFFFFFFFF) {
        return BossContact::none;
    }
    if ((s.hitboxes[0].index != 0xFFFFFFFF &&
         hitboxes_meet(x, y, s.hitboxes[0], px, py, body.hitboxes[0])) ||
        (s.hitboxes[1].index != 0xFFFFFFFF &&
         hitboxes_meet(x, y, s.hitboxes[1], px, py, body.hitboxes[0]))) {
        hurt_player(g, p);
        return BossContact::hurt;
    }
    return BossContact::none;
}

BossContact boss_contact_single(Game& g, const Sprite& s, s16 x, s16 y, Player& p) {
    if (s.hitboxes[0].index == 0xFFFFFFFF || (p.flags & player_flag::dead)) {
        return BossContact::none;
    }
    const Sprite& body = p.body->sprite;
    const s32 px = p.x >> 8, py = p.y >> 8;
    if (body.hitboxes[1].index != 0xFFFFFFFF &&
        hitboxes_meet(x, y, s.hitboxes[0], px, py, body.hitboxes[1])) {
        p.speed_x = p.character == 3 ? s16(p.speed_x >> 1) : s16(-p.speed_x);
        p.speed_y = s16(-p.speed_y);
        return BossContact::attacked;
    }
    if (body.hitboxes[0].index != 0xFFFFFFFF &&
        hitboxes_meet(x, y, s.hitboxes[0], px, py, body.hitboxes[0])) {
        hurt_player(g, p);
        return BossContact::hurt;
    }
    return BossContact::none;
}

bool hazard_hits(Game& g, const Sprite& s, s16 x, s16 y, Player& p) {
    const Sprite& body = p.body->sprite;
    if (s.hitboxes[0].index == 0xFFFFFFFF || (p.flags & player_flag::dead) ||
        body.hitboxes[0].index == 0xFFFFFFFF) {
        return false;
    }
    if (!hitboxes_meet(x, y, s.hitboxes[0], p.x >> 8, p.y >> 8, body.hitboxes[0])) {
        return false;
    }
    hurt_player(g, p);
    return true;
}

bool hazard_hits_second(Game& g, const Sprite& s, s16 x, s16 y, Player& p) {
    const Sprite& body = p.body->sprite;
    if (s.hitboxes[1].index == 0xFFFFFFFF || body.hitboxes[0].index == 0xFFFFFFFF ||
        (p.flags & player_flag::dead)) {
        return false;
    }
    if (!hitboxes_meet(x, y, s.hitboxes[1], p.x >> 8, p.y >> 8, body.hitboxes[0])) {
        return false;
    }
    if (g.stage().zone == 0xD) {
        super_sonic_hit(g, p);
    } else {
        hurt_player(g, p);
    }
    return true;
}

bool attack_bounces(Game& g, const Sprite& s, s16 x, s16 y, Player& p) {
    const Hitbox& attack = p.body->sprite.hitboxes[1];
    if (s.hitboxes[0].index == 0xFFFFFFFF || attack.index == 0xFFFFFFFF ||
        (p.flags & player_flag::dead)) {
        return false;
    }
    if (!hitboxes_meet(x, y, s.hitboxes[0], p.x >> 8, p.y >> 8, attack)) {
        return false;
    }
    if (g.stage().zone != 0xD) {
        p.speed_x = p.character == 3 ? s16(p.speed_x >> 1) : s16(-p.speed_x);
        p.speed_y = s16(-p.speed_y);
    }
    return true;
}

bool touches_player(const Sprite& s, s16 x, s16 y, const Player& p) {
    if (s.hitboxes[0].index == 0xFFFFFFFF || (p.flags & player_flag::dead)) {
        return false;
    }
    const Hitbox size{0, s8(-p.radius_x), s8(-p.radius_y), p.radius_x, p.radius_y};
    return hitboxes_meet(x, y, s.hitboxes[0], p.x >> 8, p.y >> 8, size);
}

} // namespace sonic_advance_gba
