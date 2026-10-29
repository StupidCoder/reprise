#include "sonic_advance_gba/game/solid.hpp"
#include "sonic_advance_gba/game/collision.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/object_util.hpp"
#include "sonic_advance_gba/game/stage.hpp"

namespace sonic_advance_gba {

namespace {

struct Box {
    s32 left, top, right, bottom;
};

// The player's feet move to height `y` (pixels; the fraction stays). Within
// the act's height that is it; outside, the player is put at the limit.
bool place_y(const StageState& s, Player& p, s32 y) {
    p.y = (p.y & 0xFF) + y * 0x100;
    const s32 lo = s.camera.min_y * 0x100, hi = s.camera.max_y * 0x100;
    if (lo <= p.y && p.y < hi) {
        return true;
    }
    p.y = p.y < lo ? lo : hi;
    return false;
}

bool character_hovering(const Player& p) {
    return p.character == 2 && (p.special == 1 || p.special == 3);
}

// 0800C934: landing on the top (or being stopped by the bottom).
bool collide_top(Game& g, const Sprite& s, s32 x, s32 y, Box& box, bool was_on, Player& p,
                 u32& result) {
    const StageState& st = g.stage();
    const Hitbox& h = s.hitboxes[0];
    const s32 mid_y = ((h.top + h.bottom) >> 1) + y;
    const s32 ol = x + h.left, pl = (p.x >> 8) + box.left;
    if (!spans_meet(pl, pl + (box.right - box.left), ol, ol + (h.right - h.left))) {
        return false;
    }
    const s32 py = p.y >> 8;
    const s32 ot = y + h.top, pt = py + box.top;
    if (!spans_meet(pt, pt + (box.bottom - box.top), ot, ot + (h.bottom - h.top))) {
        return false;
    }
    const bool upside_down = st.flags & stage_flag::upside_down;
    const bool below = upside_down ? py < mid_y : py > mid_y;
    if (below) {
        // Under it: the player's top goes to its bottom.
        if (!place_y(st, p, upside_down ? h.top + y + box.top : (h.bottom + y) - box.top)) {
            return false;
        }
        result |= solid::from_below;
        if (p.speed_y > 0) {
            return false;
        }
        p.speed_y = 0;
        if (!(p.flags & player_flag::in_air) && ((p.angle + 0x20u) & 0x40)) {
            p.ground_speed = 0;
        }
        return true;
    }
    if (!place_y(st, p, upside_down ? h.bottom + y + box.bottom : (h.top + y) - box.bottom)) {
        return false;
    }
    if (p.speed_y < 0) {
        return false;
    }
    box.top = -p.radius_y;
    box.bottom = p.radius_y;
    p.speed_y = 0;
    if (!(p.flags & player_flag::in_air) && ((p.angle + 0x20u) & 0x40)) {
        p.ground_speed = 0;
    }
    p.flags |= player_flag::on_object;
    result |= solid::on_top;
    if (!character_hovering(p) && !(p.character == 3 && (p.flags & 0x4000000) && p.move != 0)) {
        p.flags &= ~player_flag::in_air;
    }
    p.standing_on = &s;
    if (!was_on) {
        if (!character_hovering(p)) {
            land_player(g, p);
            p.ground_speed = p.speed_x;
        }
        p.angle = 0;
    }
    return true;
}

// 0800CBBC: pushing against a side (players on floors and ceilings only).
bool collide_side(Game& g, const Sprite& s, s32 x, s32 y, const Box& box, Player& p, u32& result) {
    if (((p.angle + 0x20u) & 0xC0) >> 6 & 1) {
        return false;
    }
    const Hitbox& h = s.hitboxes[0];
    const s32 px = p.x >> 8, py = p.y >> 8;
    if (!spans_meet(px + box.left, px + box.left + (box.right - box.left), x + h.left,
                    x + h.left + (h.right - h.left))) {
        return false;
    }
    if (!spans_meet(py + box.top, py + box.top + (box.bottom - box.top), y + h.top,
                    y + h.top + (h.bottom - h.top))) {
        return false;
    }
    const auto& a = g.assets();
    const auto& map = g.stage().map;
    const bool stuck = (p.flags & player_flag::in_air) || p.move != 0;
    const auto push = [&](bool left_side) {
        p.flags |= player_flag::pushing;
        result |= solid::pushing;
        p.flags &= ~player_flag::rolling;
        result &= ~u32(player_flag::rolling);
        if (left_side) {
            p.flags |= player_flag::facing_left;
            result |= solid::facing_left;
        } else {
            p.flags &= ~player_flag::facing_left;
            result &= ~solid::facing_left;
        }
        p.ground_speed = 0;
        p.speed_x = 0;
        set_player_radius(g, p, 6, 0xE);
    };
    if (((h.left + h.right) >> 1) + x < px) {
        // On its right: put beside it (or on the left if a wall leaves no room).
        s32 nx = (h.right + x - box.left) + 1;
        if (probe(a, map, nx, py, p.layer, 8, nullptr, Sensor::wall) < 0) {
            nx = (h.left + x) - box.right;
        }
        p.x = nx * 0x100;
        if (p.speed_x >= 1) {
            return false;
        }
        result |= solid::from_right;
        if (stuck) {
            p.speed_x = 0;
        } else if (p.speed_x < 0) {
            push(true);
        }
        return true;
    }
    s32 nx = (h.left + x) - box.right;
    if (probe(a, map, nx, py, p.layer, -8, nullptr, Sensor::wall) < 0) {
        nx = (h.right + x - box.left) + 1;
    }
    p.x = nx * 0x100;
    if (p.speed_x < 0) {
        return false;
    }
    result |= solid::from_left;
    if (stuck) {
        p.speed_x = 0;
    } else if (p.speed_x > 0) {
        push(false);
    }
    return true;
}

} // namespace

u32 collide_solid(Game& g, const Sprite& s, s16 x, s16 y, Player& p) {
    u32 result = 0;
    if (s.hitboxes[0].index == 0xFFFFFFFF || (p.flags & player_flag::dead)) {
        return 0;
    }
    // The side box is wider (5 pixels) and a pixel shorter at both ends.
    const Box side{-(p.radius_x + 5), 1 - p.radius_y, p.radius_x + 5, p.radius_y - 1};
    Box top{-p.radius_x, -p.radius_y, p.radius_x, p.radius_y};
    bool in_air = p.flags & player_flag::in_air;
    bool was_on = false;
    if ((p.flags & player_flag::on_object) && p.standing_on == &s) {
        p.flags &= ~player_flag::on_object;
        in_air = true;
        was_on = true;
    }
    if (in_air && collide_top(g, s, x, y, top, was_on, p, result)) {
        return result;
    }
    if (collide_side(g, s, x, y, side, p, result)) {
        return result;
    }
    if (collide_top(g, s, x, y, top, was_on, p, result)) {
        return result;
    }
    if (was_on && !(p.flags & player_flag::on_object)) {
        // Walked off.
        p.flags = (p.flags & ~player_flag::pushing) | player_flag::in_air;
    }
    return result;
}

u32 collide_top_bottom(Game& g, const Sprite& s, s16 x, s16 y, Player& p) {
    u32 result = 0;
    if (s.hitboxes[0].index == 0xFFFFFFFF || (p.flags & player_flag::dead)) {
        return 0;
    }
    Box top{-p.radius_x, -p.radius_y, p.radius_x, p.radius_y};
    bool in_air = p.flags & player_flag::in_air;
    bool was_on = false;
    if ((p.flags & player_flag::on_object) && p.standing_on == &s) {
        p.flags &= ~player_flag::on_object;
        in_air = true;
        was_on = true;
    }
    // (In the air the same test is made twice.)
    if (in_air && collide_top(g, s, x, y, top, was_on, p, result)) {
        return result;
    }
    if (collide_top(g, s, x, y, top, was_on, p, result)) {
        return result;
    }
    if (was_on && !(p.flags & player_flag::on_object)) {
        p.flags = (p.flags & ~player_flag::pushing) | player_flag::in_air;
    }
    return result;
}

u32 collide_wall(Game& g, const Sprite& s, s16 x, s16 y, Player& p, bool attack_counts) {
    const Hitbox& h = s.hitboxes[0];
    if (h.index == 0xFFFFFFFF || (p.flags & player_flag::dead)) {
        return 0;
    }
    const Box side{-(p.radius_x + 5), 1 - p.radius_y, p.radius_x + 5, p.radius_y - 1};
    u32 result = 0;
    const Hitbox& attack = p.body->sprite.hitboxes[1];
    const s32 px = p.x >> 8, py = p.y >> 8;
    if (attack_counts && attack.index != 0xFFFFFFFF) {
        const Hitbox size{0, s8(side.left), s8(side.top), s8(side.right), s8(side.bottom)};
        if (hitboxes_meet(x, y, h, px, py, attack) || hitboxes_meet(x, y, h, px, py, size)) {
            result |= solid::struck;
            if ((p.flags & player_flag::in_air) && p.speed_y > 0) {
                p.speed_y = s16(-p.speed_y);
            }
            return result;
        }
    }
    collide_side(g, s, x, y, side, p, result);
    return result;
}

u32 collide_breakable(Game& g, const Sprite& s, s16 x, s16 y, Player& p) {
    const Hitbox& h = s.hitboxes[0];
    if (h.index == 0xFFFFFFFF || (p.flags & player_flag::dead)) {
        return 0;
    }
    const Box side{-(p.radius_x + 5), 1 - p.radius_y, p.radius_x + 5, p.radius_y - 1};
    const Box top{-p.radius_x, -p.radius_y, p.radius_x, p.radius_y};
    u32 result = 0;
    const s32 px = p.x >> 8;
    const Hitbox& attack = p.body->sprite.hitboxes[1];
    if (attack.index != 0xFFFFFFFF && hitboxes_meet(x, y, h, px, p.y >> 8, attack)) {
        result = solid::attacked;
        if (!(p.flags & player_flag::in_air)) {
            return result;
        }
        if (p.speed_y > 0) {
            p.speed_y = s16(-p.speed_y);
        }
    }
    if (!(p.flags & player_flag::in_air)) {
        return result;
    }
    const s32 mid_x = x + ((h.left + h.right) >> 1);
    const s32 mid_y = y + ((h.top + h.bottom) >> 1);
    const bool upside_down = g.stage().flags & stage_flag::upside_down;
    if (spans_meet(px + top.left, px + top.left + (top.right - top.left), x + h.left,
                   x + h.left + (h.right - h.left))) {
        const s32 py = p.y >> 8;
        if (spans_meet(py + top.top, py + top.top + (top.bottom - top.top), y + h.top,
                       y + h.top + (h.bottom - h.top))) {
            if (upside_down ? mid_y <= py : py <= mid_y) {
                // On its top.
                if (p.character == 1 && p.special != 0) {
                    collide_solid(g, s, x, y, p); // flying Tails stands on it
                    return 0;
                }
                if (p.speed_y >= 0) {
                    result |= solid::on_top;
                    if (p.speed_y > 0) {
                        p.speed_y = s16(-p.speed_y);
                    }
                }
                return result;
            }
            // Under it.
            if (p.speed_y < 0) {
                p.speed_y = 0;
                const s32 edge = ((y + h.bottom) - top.top) * 0x100;
                if (upside_down) {
                    // (As the original computes it: the place mirrored
                    // through the whole-pixel part of the old one.)
                    p.y = s32(u32(p.y) - (u32(edge) - (u32(p.y) & 0xFFFFFF00u)));
                } else {
                    p.y = (p.y & 0xFF) + edge;
                }
                result |= solid::from_below;
            }
            return result;
        }
    }
    // The sides.
    if (!spans_meet(px + side.left, px + side.left + (side.right - side.left), x + h.left,
                    x + h.left + (h.right - h.left))) {
        return result;
    }
    const s32 py = p.y >> 8;
    if (!spans_meet(py + side.top, py + side.top + (side.bottom - side.top), y + h.top,
                    y + h.top + (h.bottom - h.top))) {
        return result;
    }
    s32 nx;
    if (mid_x < px) {
        if (p.speed_x >= 0) {
            return result;
        }
        p.speed_x = 0;
        nx = ((x + h.right) - side.left) + 1;
    } else {
        if (p.speed_x < 1) {
            return result;
        }
        p.speed_x = 0;
        nx = (x + h.left) - side.right;
    }
    p.x = nx << 8;
    return result;
}

bool breaks_block(Game&, const Sprite& s, s16 x, s16 y, Player& p) {
    const Hitbox& h = s.hitboxes[0];
    if (h.index == 0xFFFFFFFF || (p.flags & player_flag::dead)) {
        return false;
    }
    const Box box{-(p.radius_x + 5), -(p.radius_y + 1), p.radius_x + 5, p.radius_y + 1};
    const u32 f = p.flags;
    const s32 px = p.x >> 8, py = p.y >> 8;
    const bool rolled_in_air = (f & (player_flag::in_air | player_flag::rolling)) ==
                               (player_flag::in_air | player_flag::rolling);
    bool hit = rolled_in_air && p.speed_y > 0 &&
               spans_meet(px + box.left, px + box.left + (box.right - box.left), x + h.left,
                          x + h.left + (h.right - h.left)) &&
               spans_meet(py + box.top, py + box.top + (box.bottom - box.top), y + h.top,
                          y + h.top + (h.bottom - h.top));
    if (!hit) {
        const Hitbox& attack = p.body->sprite.hitboxes[1];
        if (attack.index == 0xFFFFFFFF || !hitboxes_meet(x, y, h, px, py, attack)) {
            return false;
        }
        if (f & (player_flag::in_air | player_flag::rolling)) {
            return false;
        }
    }
    if (p.speed_y > 0) {
        p.speed_y = s16(-p.speed_y);
    }
    return true;
}

bool hammer_hits(const Sprite&, s16, s16, const Player& p) {
    if (p.character != 3 || u8(p.state + 0xA9) > 3 ||
        p.body->sprite.hitboxes[1].index == 0xFFFFFFFF) {
        return false;
    }
    throw Unrecovered("Amy's hammer (0800A768)");
}

u32 collide_platform(Game& g, const Sprite& s, s16 x, s16 y, Player& p) {
    const Hitbox& h = s.hitboxes[0];
    if (h.index == 0xFFFFFFFF || (p.flags & player_flag::dead)) {
        return 0;
    }
    const s32 rx = p.radius_x, ry = p.radius_y;
    bool was_on = false;
    if ((p.flags & player_flag::on_object) && p.standing_on == &s) {
        p.flags &= ~player_flag::on_object;
        was_on = true;
    }
    const s32 ol = (s32(x) << 8) + h.left * 0x100, pl = p.x - rx * 0x100;
    const s32 ot = (s32(y) << 8) + h.top * 0x100, pt = p.y - ry * 0x100;
    const bool meets = spans_meet(pl, pl + (2 * rx) * 0x100, ol, ol + (h.right - h.left) * 0x100) &&
                       spans_meet(pt, pt + (2 * ry) * 0x100, ot, ot + (h.bottom - h.top) * 0x100);
    if (meets && p.speed_y >= 0) {
        p.flags |= player_flag::on_object;
        if (!character_hovering(p) && !(p.character == 3 && (p.flags & 0x4000000) && p.move != 0)) {
            p.flags &= ~player_flag::in_air;
        }
        if (!was_on) {
            if (!character_hovering(p)) {
                land_player(g, p);
                p.ground_speed = p.speed_x;
            }
            p.angle = 0;
        }
        p.standing_on = &s;
        p.speed_y = 0;
        const s32 top =
            (g.stage().flags & stage_flag::upside_down) ? y + h.bottom + ry : (y + h.top) - ry;
        p.y = (p.y & 0xFF) + top * 0x100;
        return solid::on_top;
    }
    if (was_on && !(p.flags & player_flag::on_object)) {
        p.flags = (p.flags & ~player_flag::pushing) | player_flag::in_air;
    }
    return 0;
}

u32 collide_spring_box(Game& g, const Sprite& s, s16 x, s16 y, Player& p) {
    const Hitbox& h = s.hitboxes[0];
    if (h.index == 0xFFFFFFFF || (p.flags & player_flag::dead)) {
        return 0;
    }
    const Box side{-(p.radius_x + 5), 1 - p.radius_y, p.radius_x + 5, p.radius_y - 1};
    Box top{-p.radius_x, -p.radius_y, p.radius_x, p.radius_y};
    u32 result = p.flags & player_flag::in_air;
    bool was_on = false;
    if ((p.flags & player_flag::on_object) && p.standing_on == &s) {
        p.flags &= ~player_flag::on_object;
        result = player_flag::in_air;
        was_on = true;
    }
    const s32 ol = x + h.left, ow = h.right - h.left;
    const s32 ot = y + h.top, oh = h.bottom - h.top;
    const auto side_y = [&] {
        const s32 py = (p.y >> 8) + side.top;
        return spans_meet(py, py + (side.bottom - side.top), ot, ot + oh);
    };
    if (result != 0) {
        // In the air (or leaving it): the top first.
        const s32 pl = (p.x >> 8) + top.left;
        bool side_first = true;
        if (spans_meet(pl, pl + (top.right - top.left), ol, ol + ow)) {
            const s32 pt = (p.y >> 8) + top.top;
            if (spans_meet(pt, pt + (top.bottom - top.top), ot, ot + oh)) {
                if (collide_top(g, s, x, y, top, was_on, p, result)) {
                    return result;
                }
                side_first = false;
            }
        }
        if (side_first) {
            const s32 sl = (p.x >> 8) + side.left;
            if (spans_meet(sl, sl + (side.right - side.left), ol, ol + ow) && side_y()) {
                if (x < (p.x >> 8)) {
                    if (p.speed_x < 1) {
                        p.speed_x = 0;
                        p.x = ((x + h.right) - side.left + 1) * 0x100;
                        result |= solid::from_right;
                    }
                } else if (p.speed_x >= 0) {
                    p.speed_x = 0;
                    p.x = ((x + h.left) - side.right) * 0x100;
                    result |= solid::from_left;
                }
            }
        }
    }
    // The sides.
    const s32 px = p.x >> 8;
    const s32 sl = px + side.left;
    bool meets;
    if (ol <= sl) {
        meets = !(x + h.right < sl && ol < sl);
    } else {
        meets = !(side.right + px < ol);
    }
    if (!meets || !side_y()) {
        return result;
    }
    s32 nx;
    if (x < px) {
        if (p.speed_x > 0) {
            return result;
        }
        if (p.speed_x == 0) {
            return result | solid::from_right;
        }
        result |= solid::from_right | solid::pushing | solid::facing_left;
        nx = ((x + h.right) - side.left) + 1;
    } else {
        if (p.speed_x < 0) {
            return result;
        }
        if (p.speed_x == 0) {
            return result | solid::from_left;
        }
        result = (result & ~solid::facing_left) | solid::from_left | solid::pushing;
        nx = (x + h.left) - side.right;
    }
    p.x = nx << 8;
    return result;
}

} // namespace sonic_advance_gba
