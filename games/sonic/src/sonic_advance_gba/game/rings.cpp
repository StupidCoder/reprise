#include "sonic_advance_gba/game/rings.hpp"
#include "gba/bios.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/stage.hpp"
#include <vector>

namespace sonic_advance_gba {

namespace {

constexpr u32 ring_layouts = 0x080B7F4C; // per act: LZ77 data
constexpr u32 sparkle_song = 0x087B66AC; // played on music player 1 or 2

// The decompressed layout: a header (size, width and height in cells),
// then per cell the offset of its ring list (0: none) relative to 8 bytes
// before the table; a list holds (x, y) pairs in 8-pixel units within the
// cell, ended by FF. A collected ring's x becomes FE.
class RingLayout {
public:
    RingLayout() = default;

    explicit RingLayout(std::vector<u8> bytes) : b_(std::move(bytes)) {}

    u32 width() const { return word(4) & 0xFFFF; }

    u32 height() const { return word(8) & 0xFFFF; }

    // The offset of a cell's list in b_, or 0.
    u32 list(u32 cx, u32 cy) const {
        const u32 o = word(0xC + 4 * (cx + cy * width()));
        return o == 0 ? 0 : 0xC + o - 8;
    }

    u8& at(u32 offset) { return b_.at(offset); }

private:
    u32 word(u32 o) const {
        return u32(b_.at(o)) | u32(b_.at(o + 1)) << 8 | u32(b_.at(o + 2)) << 16 |
               u32(b_.at(o + 3)) << 24;
    }

    std::vector<u8> b_;
};

// Whether the box [left, right] x [top, bottom] meets [a, b] x [c, d], as
// the ring code compares (touching edges count).
bool spans_meet(s32 lo, s32 hi, s32 a, s32 b) {
    return lo < a ? hi >= a : lo <= b;
}

// A ring more; every hundredth is a life.
void add_ring(Game& g) {
    StageState& s = g.stage();
    const u16 before = s.rings;
    ++s.rings;
    if (s.zone != 0xD && divide(s.rings, 100) != divide(before, 100) && g.game_mode == 0) {
        ++s.lives;
        s.music_one_up = 0x10;
    }
    if (g.game_mode == 6 && s.rings > 0xFF) {
        s.rings = 0xFF;
    }
}

// 08024B00, 08024B98: a ring pulled by the magnetic shield, ever faster
// towards player 0, until it touches the player's hitbox.
class PulledRing final : public Process {
public:
    PulledRing(s16 x, s16 y) : Process(0x2000) {
        sprite_.start(0x11D00, 0x2C3, 0, x, y, 0x3C0, 0xC2000);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        const Assets& a = g.assets();
        Player& p = s.player[0];
        const s16 angle =
            octant_angle(a, s16((p.y >> 8) - u16(sprite_.y)), s16((p.x >> 8) - u16(sprite_.x)));
        speed_ = s16(speed_ + 0x40);
        sprite_.x = s16(((speed_ * sine(a, unsigned(angle + 0x100))) >> 0x16) + sprite_.x);
        sprite_.y = s16(((speed_ * sine(a, unsigned(angle))) >> 0x16) + sprite_.y);
        const s16 x = sprite_.x, y = sprite_.y;
        const Hitbox& h = p.body->sprite.hitboxes[0];
        const s32 px = p.x >> 8, py = p.y >> 8;
        if (spans_meet(px + h.left, px + h.right, x - 8, x + 8) &&
            spans_meet(py + h.top, py + h.bottom, y - 0x10, y)) {
            if (!(p.flags & player_flag::dead)) {
                add_ring(g);
                create_ring_sparkle(g, x, y);
            }
            g.scheduler().destroy(*this);
            return;
        }
        sprite_.x = s16(x - s.camera.x);
        sprite_.y = s16(y - s.camera.y);
        animate(g, sprite_);
        draw(g, sprite_);
        sprite_.x = x;
        sprite_.y = y;
    }

private:
    Sprite sprite_; // +00
    s16 speed_{};   // +30
};

// 08007E3C (the layout is run-length compressed, BIOS call 14h)
class Rings final : public Process {
public:
    explicit Rings(RingLayout layout) : Process(0x2000), layout_(std::move(layout)) {
        sprite_.start(0x11D00, 0x2C3, 0, 0, 0, 0x500, 0x2000);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        if (s.flags & stage_flag::frozen) {
            return;
        }
        animate(g, sprite_);
        const u32 frame = sprite_.frame;
        // Every player collects with player 0's size.
        const Player& p0 = s.player[0];
        const s32 left = -p0.radius_x, top = -p0.radius_y, right = p0.radius_x,
                  bottom = p0.radius_y;
        for (unsigned n = 0; n < s.players; ++n) {
            collect(g, s.player[n], left, top, right, bottom);
        }
        if (g.game_mode >= 2) {
            throw Unrecovered("rings in multiplayer (08007E3C)");
        }
        draw_visible(g, frame,
                     (s.player[0].power & player_power::magnetic_shield) && g.game_mode != 6);
    }

private:
    void collect(Game& g, const Player& p, s32 left, s32 top, s32 right, s32 bottom) {
        const s32 px = p.x >> 8, py = p.y >> 8;
        for (u32 cy = (u32(py + top) & 0xFFFFFF) >> 8;
             s32(cy) <= (py + bottom - 0x10) >> 8 && cy < layout_.height();
             cy = (cy + 1) & 0xFFFF) {
            for (u32 cx = (u32(px + left - 8) & 0xFFFFFF) >> 8;
                 s32(cx) <= (px + right - 8) >> 8 && cx < layout_.width(); cx = (cx + 1) & 0xFFFF) {
                u32 at = layout_.list(cx, cy);
                if (at == 0) {
                    continue;
                }
                for (; layout_.at(at) != 0xFF; at += 2) {
                    if (layout_.at(at) == 0xFE) {
                        continue;
                    }
                    if ((p.flags & player_flag::dead) || (p.state == 0x0F && p.hurt_timer != 0)) {
                        continue;
                    }
                    const s32 rx = s16(layout_.at(at) * 8 + cx * 0x100);
                    const s32 ry = s16(layout_.at(at + 1) * 8 + cy * 0x100);
                    if (!spans_meet(px + left, px + right, rx - 8, rx + 8) ||
                        !spans_meet(py + top, py + bottom, ry - 0x10, ry)) {
                        continue;
                    }
                    add_ring(g);
                    create_ring_sparkle(g, s16(rx), s16(ry));
                    layout_.at(at) = 0xFE;
                }
            }
        }
    }

    // The rings in view: the first drawn as a sprite, the others as copies
    // of its first sprite table entry. With the magnetic shield, rings near
    // player 0 leave the layout and fly to the player.
    void draw_visible(Game& g, u32 frame, bool magnet) {
        const StageState& s = g.stage();
        const auto& c = s.camera;
        const Assets& a = g.assets();
        unsigned drawn = 0;
        for (u32 cy = u32(c.y >> 8) & 0xFFFF; s32(cy * 0x100) < c.y + 0xA0 && cy < layout_.height();
             cy = (cy + 1) & 0xFFFF) {
            for (u32 cx = u32(c.x >> 8) & 0xFFFF;
                 s32(cx * 0x100) < c.x + 0xF0 && cx < layout_.width(); cx = (cx + 1) & 0xFFFF) {
                u32 at = layout_.list(cx, cy);
                if (at == 0) {
                    continue;
                }
                for (; layout_.at(at) != 0xFF; at += 2) {
                    if (layout_.at(at) == 0xFE) {
                        continue;
                    }
                    const s16 rx = s16(layout_.at(at) * 8 + cx * 0x100);
                    const s16 ry = s16(layout_.at(at + 1) * 8 + cy * 0x100);
                    if (u32(rx - c.x + 8) > 0x100 || ry - c.y < 0 || ry - c.y - 0x10 > 0xA0) {
                        continue;
                    }
                    if (magnet) {
                        const s32 px = s.player[0].x >> 8, py = s.player[0].y >> 8;
                        if (px >= rx - 0x40 && px <= rx + 0x40 && py >= ry - 0x48 &&
                            py <= ry + 0x38) {
                            g.scheduler().create<PulledRing>(rx, ry);
                            layout_.at(at) = 0xFE;
                            continue;
                        }
                    }
                    if (drawn == 0 || sprite_.first_entry == 0xFF) {
                        sprite_.first_entry = 0xFF;
                        sprite_.x = s16(rx - c.x);
                        sprite_.y = s16(ry - c.y);
                        draw(g, sprite_);
                    } else {
                        auto& v = g.video();
                        const auto attr = v.queued_entries[sprite_.first_entry].attributes();
                        const u16 a0 =
                            u16((attr[0] & 0xFF00) + ((ry - c.y - a.s16(frame + 10)) & 0xFF));
                        const u16 a1 =
                            u16((attr[1] & 0xFE00) + ((rx - c.x - a.s16(frame + 8)) & 0x1FF));
                        gba::Sprite e;
                        e.set_attributes(a0, a1, attr[2]);
                        if (!queue_sprite(v, (sprite_.layer & 0x7C0) >> 6, e)) {
                            return;
                        }
                    }
                    drawn = (drawn + 1) & 0xFF;
                }
            }
        }
    }

    Sprite sprite_;
    RingLayout layout_; // +30
};

// 0801E56C: drawn at its act position until its animation ends (and twice
// in its last frame).
class RingSparkle final : public Process {
public:
    RingSparkle(s16 x, s16 y, u8 player) : Process(0x2000), x_(x), y_(y), player_(player) {
        sprite_.start(0x11F00, 0x2C4, 0, x, y, 0x3C0, 0x2000);
    }

    void update(Game& g) override {
        StageState& s = g.stage();
        sprite_.x = s16(x_ - s.camera.x);
        sprite_.y = s16(y_ - s.camera.y);
        if (animate(g, sprite_) == 0) {
            draw(g, sprite_);
            --s.ring_sparkles;
            g.scheduler().destroy(*this);
        }
        draw(g, sprite_);
    }

private:
    s16 x_, y_;
    [[maybe_unused]] u8 player_; // +30 which sound player plays the sound
    Sprite sprite_;
};

} // namespace

void start_rings(Game& g) {
    if (g.game_mode == 6) {
        throw Unrecovered("the rings of the chao hunt mode (08007D30)");
    }
    const Assets& a = g.assets();
    const u32 data = a.u32(ring_layouts + 4u * u32(s32(g.stage().zone)));
    RingLayout layout(gba::rl_decompress([&](u32 at) { return a.u8(at); }, data));
    g.scheduler().create<Rings>(std::move(layout));
}

void create_ring_sparkle(Game& g, s16 x, s16 y) {
    StageState& s = g.stage();
    if (s.ring_sparkles >= 8) {
        return;
    }
    const u8 player = u8(1 + (s.rings & 1)); // alternating sides
    g.scheduler().create<RingSparkle>(x, y, player);
    g.sound().play_on(player, sparkle_song, player == 1 ? 0x40 : -0x40);
    ++s.ring_sparkles;
}

} // namespace sonic_advance_gba
