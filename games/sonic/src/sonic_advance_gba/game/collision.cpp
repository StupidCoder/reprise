#include "sonic_advance_gba/game/collision.hpp"

namespace sonic_advance_gba {

StageMap StageMap::load(const Assets& a, u32 t) {
    StageMap m;
    m.heights = a.u32(t + 0x00);
    m.angles = a.u32(t + 0x04);
    m.blocks = a.u32(t + 0x08);
    m.layouts[0] = a.u32(t + 0x0C);
    m.layouts[1] = a.u32(t + 0x10);
    m.tile_flags = a.u32(t + 0x14);
    m.layout_width = a.u16(t + 0x18);
    m.layout_height = a.u16(t + 0x1A);
    m.width = a.u16(t + 0x1C);
    m.height = a.u16(t + 0x1E);
    return m;
}

u16 map_entry(const Assets& a, const StageMap& m, s32 x, s32 y, unsigned layer) {
    // Blocks are 12 tiles wide and high. (The original keeps the last
    // division's result in a small cache, 03000408/03000418.)
    const s32 tx = x >> 3, ty = y >> 3;
    const s32 bx = tx / 12, ix = tx - bx * 12;
    const s32 by = ty / 12, iy = ty - by * 12;
    const u8 block = a.u8(u32(s32(m.layouts[layer & 1]) + by * m.layout_width + bx));
    return a.u16(m.blocks + block * 0x120u + u32(iy * 12 + ix) * 2);
}

namespace {

s32 clamp_to(s32 v, s32 hi) {
    if (v < 0) {
        return 0;
    }
    return v > hi ? hi : v;
}

// Tile flag bit 0 (two bits per tile, 8 tiles per halfword) makes the tile
// passable for probes with layer bit 7 set.
bool passable(const Assets& a, const StageMap& m, u16 entry, u8 layer) {
    const u32 tile = entry & 0x3FF;
    return (layer & 0x80) && ((a.u16(m.tile_flags + (tile >> 3) * 2) >> ((entry & 7) << 1)) & 1);
}

} // namespace

s32 floor_sensor(const Assets& a, const StageMap& m, s32 y, s32 x, u8 layer, u8& angle) {
    x = clamp_to(x, m.width - 1);
    y = clamp_to(y, m.height - 1);
    const u16 e = map_entry(a, m, x, y, layer & 1);
    const u32 tile = e & 0x3FF;
    s32 column = x & 7;
    if (e & 0x400) {
        column = 7 - column;
    }
    s32 v = a.s8(m.heights + tile * 8 + u32(column)) >> 4;
    if (v == -8) {
        v = 8;
    }
    if (passable(a, m, e, layer)) {
        v = 0;
    }
    s32 r = v;
    if ((e & 0x800) && v != 8 && v != 0) {
        r = v > 0 ? v - 8 : v + 8;
    }
    s8 ang = a.s8(m.angles + tile);
    if (e & 0x400) {
        ang = s8(-ang);
    }
    if ((e & 0x800) && r != 0) {
        ang = s8(-0x80 - ang);
    }
    angle = u8(ang);
    return r;
}

s32 wall_sensor(const Assets& a, const StageMap& m, s32 x, s32 y, u8 layer, u8& angle) {
    x = clamp_to(x, m.width - 1);
    y = clamp_to(y, m.height - 1);
    const u16 e = map_entry(a, m, x, y, layer & 1);
    const u32 tile = e & 0x3FF;
    s32 row = y & 7;
    if (e & 0x800) {
        row = 7 - row;
    }
    s32 v = a.u8(m.heights + tile * 8 + u32(row)) & 0xF;
    if (v & 8) {
        v |= ~0xF;
    }
    if (v == -8) {
        v = 8;
    }
    if (passable(a, m, e, layer)) {
        v = 0;
    }
    s32 r = v;
    if ((e & 0x400) && v != 8 && v != 0) {
        r = v > 0 ? v - 8 : v + 8;
    }
    s8 ang = a.s8(m.angles + tile);
    if (e & 0x800) {
        ang = s8(-0x80 - ang);
    }
    if ((e & 0x400) && r != 0) {
        ang = s8(-ang);
    }
    angle = u8(ang);
    return r;
}

namespace {

s32 sense(const Assets& a, const StageMap& m, Sensor s, s32 pos, s32 other, u8 layer, u8& angle) {
    return s == Sensor::floor ? floor_sensor(a, m, pos, other, layer, angle)
                              : wall_sensor(a, m, pos, other, layer, angle);
}

} // namespace

s32 probe(const Assets& a, const StageMap& m, s32 pos, s32 other, u8 layer, s32 step, u8* out,
          Sensor s) {
    u8 spare = 0, ang = 0;
    u8& angle = out ? *out : spare;
    const s32 f = pos & 7;
    s32 r = sense(a, m, s, pos, other, layer, ang);
    if (r == 0) {
        // Empty here: look ahead one and two tiles.
        r = sense(a, m, s, pos + step, other, layer, ang);
        if (r == 0) {
            r = sense(a, m, s, pos + step * 2, other, layer, ang);
            if (r == 0) {
                return step < 1 ? f + 0x11 : 0x18 - f;
            }
            angle = ang;
            if (r == 8) {
                return step < 1 ? f + 8 : ~f + 0x10;
            }
            return r < 1 ? r + f + 0x10 : (r - f) + 0xF;
        }
        angle = ang;
        if (r == 8) {
            return step < 1 ? f : ~f + 8;
        }
        return r < 1 ? r + f + 8 : (r - f) + 7;
    }
    angle = ang;
    if (r == 8) {
        // Solid here: look back one and two tiles.
        r = sense(a, m, s, pos - step, other, layer, ang);
        if (r == 8) {
            angle = ang;
            r = sense(a, m, s, pos - step * 2, other, layer, ang);
            if (r == 8) {
                angle = ang;
                return step < 1 ? f - 0x18 : ~f - 0x10;
            }
            if (r == 0) {
                return step < 1 ? f - 0x10 : -f - 9;
            }
            angle = ang;
            return r < 1 ? (r + f) - 0x10 : (r - f) - 0x11;
        }
        if (r == 0) {
            return step < 1 ? (pos | ~7) : ~f;
        }
        angle = ang;
        return r < 1 ? (r + f) - 8 : (r - f) - 9;
    }
    return r < 1 ? r + f : (r - 1) - f;
}

s32 probe_short(const Assets& a, const StageMap& m, s32 pos, s32 other, u8 layer, s32 step, u8* out,
                Sensor s) {
    u8 spare = 0, ang = 0;
    u8& angle = out ? *out : spare;
    const s32 f = pos & 7;
    const s32 r = sense(a, m, s, pos, other, layer, ang);
    if (r == 0) {
        return step < 1 ? f + 1 : 8 - f;
    }
    angle = ang;
    if (r == 8) {
        return step < 1 ? (pos | ~7) : ~f;
    }
    return r < 1 ? r + f : (r - 1) - f;
}

} // namespace sonic_advance_gba
