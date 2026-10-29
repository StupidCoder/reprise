#include "gba/display.hpp"
#include <algorithm>

namespace gba {

u16 Background::control() const {
    return u16((priority & 3) | ((tile_block & 3) << 2) | (mosaic ? 0x40 : 0) |
               (colors_256 ? 0x80 : 0) | ((map_block & 0x1F) << 8) | (wrap ? 0x2000 : 0) |
               ((size & 3) << 14));
}

void Background::set_control(u16 v) {
    priority = u8(v & 3);
    tile_block = u8((v >> 2) & 3);
    mosaic = v & 0x40;
    colors_256 = v & 0x80;
    map_block = u8((v >> 8) & 0x1F);
    wrap = v & 0x2000;
    size = u8(v >> 14);
}

u16 Blend::control() const {
    return u16((first & 0x3F) | ((mode & 3) << 6) | ((second & 0x3F) << 8));
}

void Blend::set_control(u16 v) {
    first = u8(v & 0x3F);
    mode = u8((v >> 6) & 3);
    second = u8((v >> 8) & 0x3F);
}

u16 Settings::control() const {
    u16 v = u16(mode & 7);
    v |= second_frame ? 0x10 : 0;
    v |= sprite_tiles_1d ? 0x40 : 0;
    v |= forced_blank ? 0x80 : 0;
    for (int i = 0; i < 4; ++i) {
        v |= bg_shown[std::size_t(i)] ? u16(0x100 << i) : 0;
    }
    v |= sprites_shown ? 0x1000 : 0;
    v |= window0 ? 0x2000 : 0;
    v |= window1 ? 0x4000 : 0;
    v |= sprite_window ? 0x8000 : 0;
    return v;
}

void Settings::set_control(u16 v) {
    mode = u8(v & 7);
    second_frame = v & 0x10;
    sprite_tiles_1d = v & 0x40;
    forced_blank = v & 0x80;
    for (int i = 0; i < 4; ++i) {
        bg_shown[std::size_t(i)] = v & (0x100 << i);
    }
    sprites_shown = v & 0x1000;
    window0 = v & 0x2000;
    window1 = v & 0x4000;
    sprite_window = v & 0x8000;
}

std::array<u16, 3> Sprite::attributes() const {
    u16 a0 = y;
    a0 |= rotated ? 0x100 : 0;
    a0 |= (rotated ? double_size : hidden) ? 0x200 : 0;
    a0 |= u16((mode & 3) << 10);
    a0 |= mosaic ? 0x1000 : 0;
    a0 |= colors_256 ? 0x2000 : 0;
    a0 |= u16((shape & 3) << 14);
    u16 a1 = u16(x & 0x1FF);
    if (rotated) {
        a1 |= u16((parameters & 0x1F) << 9);
    } else {
        a1 |= flip_x ? 0x1000 : 0;
        a1 |= flip_y ? 0x2000 : 0;
    }
    a1 |= u16((size & 3) << 14);
    const u16 a2 = u16((tile & 0x3FF) | ((priority & 3) << 10) | ((palette & 0xF) << 12));
    return {a0, a1, a2};
}

void Sprite::set_attributes(u16 a0, u16 a1, u16 a2) {
    y = u8(a0);
    rotated = a0 & 0x100;
    hidden = !rotated && (a0 & 0x200);
    double_size = rotated && (a0 & 0x200);
    mode = u8((a0 >> 10) & 3);
    mosaic = a0 & 0x1000;
    colors_256 = a0 & 0x2000;
    shape = u8(a0 >> 14);
    x = u16(a1 & 0x1FF);
    parameters = rotated ? u8((a1 >> 9) & 0x1F) : 0;
    // Attribute bits 9-11 of a rotated sprite are its parameter set; the
    // flip bits are read only when it is not rotated.
    flip_x = !rotated && (a1 & 0x1000);
    flip_y = !rotated && (a1 & 0x2000);
    if (rotated) {
        flip_x = false;
        flip_y = false;
    }
    size = u8(a1 >> 14);
    tile = u16(a2 & 0x3FF);
    priority = u8((a2 >> 10) & 3);
    palette = u8(a2 >> 12);
}

namespace {

// Sprite sizes by shape and size (GBATEK "OBJ Attribute 0/1").
constexpr int sprite_dims[3][4][2] = {
    {{8, 8}, {16, 16}, {32, 32}, {64, 64}},
    {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
    {{8, 16}, {8, 32}, {16, 32}, {32, 64}},
};

} // namespace

int Sprite::width() const {
    return shape < 3 ? sprite_dims[shape][size][0] : 8;
}

int Sprite::height() const {
    return shape < 3 ? sprite_dims[shape][size][1] : 8;
}

platform::Color to_rgba(u16 c) {
    const auto expand = [](unsigned v) {
        return std::uint8_t((v << 3) | (v >> 2));
    };
    return {expand(c & 31), expand((c >> 5) & 31), expand((c >> 10) & 31), 255};
}

namespace {

// Layer numbers used while composing a pixel.
constexpr u8 layer_sprites = 4, layer_backdrop = 5;

struct Pixel {
    u16 color;
    u8 priority;
    u8 layer;
    bool semi_transparent;
};

u16 blend_alpha(u16 a, u16 b, int eva, int evb) {
    u16 r = 0;
    for (int shift = 0; shift < 15; shift += 5) {
        const int v = std::min(31, (((a >> shift) & 31) * eva + ((b >> shift) & 31) * evb) >> 4);
        r = u16(r | (v << shift));
    }
    return r;
}

u16 brighten(u16 a, int e) {
    u16 r = 0;
    for (int shift = 0; shift < 15; shift += 5) {
        const int v = (a >> shift) & 31;
        r = u16(r | ((v + (((31 - v) * e) >> 4)) << shift));
    }
    return r;
}

u16 darken(u16 a, int e) {
    u16 r = 0;
    for (int shift = 0; shift < 15; shift += 5) {
        const int v = (a >> shift) & 31;
        r = u16(r | ((v - ((v * e) >> 4)) << shift));
    }
    return r;
}

// A coordinate within [first, last) of a window edge pair; a first edge
// beyond the last wraps around the screen.
bool in_span(int v, int first, int last, int limit) {
    if (first <= last) {
        return v >= first && v < std::min(last, limit);
    }
    return v >= first || v < last;
}

bool in_window(const Window& w, int x, int y) {
    return in_span(x, w.left, w.right, screen_width) && in_span(y, w.top, w.bottom, screen_height);
}

class LineRenderer {
public:
    LineRenderer(const Display& d) : d_(d) {}

    void render(platform::Image& out) {
        const Settings* s = &d_.settings;
        const std::array<u16, 512>* colors = &d_.colors;
        std::size_t next_setting = 0, next_color = 0;
        // Rotated layers' running reference points (BG2, BG3).
        s32 ref[2][2] = {{s->bg[2].origin_x, s->bg[2].origin_y},
                         {s->bg[3].origin_x, s->bg[3].origin_y}};
        for (int y = 0; y < screen_height; ++y) {
            while (next_setting < d_.setting_changes.size() &&
                   d_.setting_changes[next_setting].line <= y) {
                const Settings& n = d_.setting_changes[next_setting].settings;
                for (int i = 0; i < 2; ++i) {
                    const Background& was = s->bg[std::size_t(2 + i)];
                    const Background& now = n.bg[std::size_t(2 + i)];
                    if (now.origin_x != was.origin_x) {
                        ref[i][0] = now.origin_x;
                    }
                    if (now.origin_y != was.origin_y) {
                        ref[i][1] = now.origin_y;
                    }
                }
                s = &n;
                ++next_setting;
            }
            while (next_color < d_.color_changes.size() && d_.color_changes[next_color].line <= y) {
                colors = &d_.color_changes[next_color].colors;
                ++next_color;
            }
            line(*s, *colors, y, ref, out);
            for (int i = 0; i < 2; ++i) {
                ref[i][0] += s->bg[std::size_t(2 + i)].pb;
                ref[i][1] += s->bg[std::size_t(2 + i)].pd;
            }
        }
    }

private:
    void line(const Settings& s, const std::array<u16, 512>& colors, int y, const s32 ref[2][2],
              platform::Image& out) {
        if (s.forced_blank) {
            for (int x = 0; x < screen_width; ++x) {
                out.put(x, y, {255, 255, 255, 255});
            }
            return;
        }
        bool shown[4]{};
        for (int bg = 0; bg < 4; ++bg) {
            if (!s.bg_shown[std::size_t(bg)]) {
                continue;
            }
            bool exists = false;
            switch (s.mode) {
            case 0:
                exists = true;
                break;
            case 1:
                exists = bg <= 2;
                break;
            case 2:
                exists = bg >= 2;
                break;
            default:
                exists = bg == 2;
                break;
            }
            if (!exists) {
                continue;
            }
            shown[bg] = true;
            std::fill(std::begin(opaque_[bg]), std::end(opaque_[bg]), false);
            if (s.mode == 0 || (s.mode == 1 && bg < 2)) {
                text_layer(s, colors, bg, y);
            } else if (s.mode <= 2) {
                rotated_layer(s, colors, bg, ref[bg - 2]);
            } else {
                bitmap_layer(s, colors, y);
            }
        }
        std::fill(std::begin(sprite_opaque_), std::end(sprite_opaque_), false);
        std::fill(std::begin(sprite_window_), std::end(sprite_window_), false);
        if (s.sprites_shown) {
            sprites(s, colors, y);
        }
        const bool windows = s.window0 || s.window1 || s.sprite_window;
        const u16 backdrop = colors[0];
        for (int x = 0; x < screen_width; ++x) {
            LayerMask enable = 0x3F;
            if (windows) {
                enable = s.outside;
                if (s.sprite_window && sprite_window_[x]) {
                    enable = s.inside_sprite_window;
                }
                if (s.window1 && in_window(s.windows[1], x, y)) {
                    enable = s.inside[1];
                }
                if (s.window0 && in_window(s.windows[0], x, y)) {
                    enable = s.inside[0];
                }
            }
            Pixel top{backdrop, 4, layer_backdrop, false},
                second{backdrop, 4, layer_backdrop, false};
            const auto consider = [&](Pixel p) {
                if (p.priority < top.priority ||
                    (p.priority == top.priority && p.layer < top.layer)) {
                    second = top;
                    top = p;
                } else if (p.priority < second.priority ||
                           (p.priority == second.priority && p.layer < second.layer)) {
                    second = p;
                }
            };
            for (int bg = 0; bg < 4; ++bg) {
                if (shown[bg] && opaque_[bg][x] && (enable & (1 << bg))) {
                    consider({bg_color_[bg][x], u8(s.bg[std::size_t(bg)].priority), u8(bg), false});
                }
            }
            if (sprite_opaque_[x] && (enable & 0x10)) {
                // A sprite is in front of backgrounds of the same priority.
                const Pixel p{sprite_color_[x], sprite_priority_[x], layer_sprites,
                              sprite_semi_[x]};
                if (p.priority <= top.priority) {
                    second = top;
                    top = p;
                } else if (p.priority <= second.priority) {
                    second = p;
                }
            }
            u16 c = top.color;
            const Blend& b = s.blend;
            const int eva = std::min<int>(16, b.eva), evb = std::min<int>(16, b.evb);
            const int evy = std::min<int>(16, b.evy);
            const bool first_target = b.first & (1 << top.layer);
            const bool second_target = b.second & (1 << second.layer);
            if (top.layer == layer_sprites && top.semi_transparent && second_target) {
                c = blend_alpha(top.color, second.color, eva, evb);
            } else if ((enable & 0x20) && first_target) {
                if (b.mode == 1 && second_target) {
                    c = blend_alpha(top.color, second.color, eva, evb);
                } else if (b.mode == 2) {
                    c = brighten(c, evy);
                } else if (b.mode == 3) {
                    c = darken(c, evy);
                }
            }
            out.put(x, y, to_rgba(c));
        }
    }

    void text_layer(const Settings& s, const std::array<u16, 512>& colors, int bg, int y) {
        const Background& l = s.bg[std::size_t(bg)];
        const u32 tiles = u32(l.tile_block) * 0x4000;
        const u32 map = u32(l.map_block) * 0x800;
        const int w = (l.size & 1) ? 512 : 256, h = (l.size & 2) ? 512 : 256;
        int my = y;
        int mosaic_w = 1;
        if (l.mosaic) {
            my = y - y % (s.mosaic_bg_y + 1);
            mosaic_w = s.mosaic_bg_x + 1;
        }
        const int py = (my + l.scroll_y) & (h - 1);
        for (int x = 0; x < screen_width; ++x) {
            const int mx = x - x % mosaic_w;
            const int px = (mx + l.scroll_x) & (w - 1);
            u32 block = 0;
            if (w == 512 && px >= 256) {
                block += 1;
            }
            if (h == 512 && py >= 256) {
                block += w == 512 ? 2 : 1;
            }
            const u32 at =
                (map + block * 0x800 + u32(((py & 255) >> 3) * 32 + ((px & 255) >> 3)) * 2) &
                0xFFFF;
            const u16 e = d_.halfword(at);
            int tx = px & 7, ty = py & 7;
            if (e & 0x400) {
                tx = 7 - tx;
            }
            if (e & 0x800) {
                ty = 7 - ty;
            }
            const u32 tile = e & 0x3FF;
            if (l.colors_256) {
                const u32 a = tiles + tile * 64 + u32(ty * 8 + tx);
                const u8 ci = a < 0x10000 ? d_.memory[a] : 0;
                if (ci) {
                    bg_color_[bg][x] = colors[ci];
                    opaque_[bg][x] = true;
                }
            } else {
                const u32 a = tiles + tile * 32 + u32(ty * 4 + tx / 2);
                const u8 ci = a < 0x10000 ? u8((d_.memory[a] >> ((tx & 1) * 4)) & 0xF) : 0;
                if (ci) {
                    bg_color_[bg][x] = colors[std::size_t((e >> 12) * 16 + ci)];
                    opaque_[bg][x] = true;
                }
            }
        }
    }

    void rotated_layer(const Settings& s, const std::array<u16, 512>& colors, int bg,
                       const s32 ref[2]) {
        const Background& l = s.bg[std::size_t(bg)];
        const u32 tiles = u32(l.tile_block) * 0x4000;
        const u32 map = u32(l.map_block) * 0x800;
        const int size = 128 << l.size;
        s32 fx = ref[0], fy = ref[1];
        for (int x = 0; x < screen_width; ++x, fx += l.pa, fy += l.pc) {
            int px = fx >> 8, py = fy >> 8;
            if (l.wrap) {
                px &= size - 1;
                py &= size - 1;
            } else if (px < 0 || py < 0 || px >= size || py >= size) {
                continue;
            }
            const u8 tile = d_.memory[(map + u32((py >> 3) * (size >> 3) + (px >> 3))) & 0xFFFF];
            const u8 ci = d_.memory[(tiles + tile * 64u + u32((py & 7) * 8 + (px & 7))) & 0xFFFF];
            if (ci) {
                bg_color_[bg][x] = colors[ci];
                opaque_[bg][x] = true;
            }
        }
    }

    void bitmap_layer(const Settings& s, const std::array<u16, 512>& colors, int y) {
        const u32 page = s.second_frame ? 0xA000 : 0;
        for (int x = 0; x < screen_width; ++x) {
            if (s.mode == 3) {
                bg_color_[2][x] = d_.halfword(u32(y * 240 + x) * 2);
                opaque_[2][x] = true;
            } else if (s.mode == 4) {
                const u8 ci = d_.memory[page + u32(y * 240 + x)];
                if (ci) {
                    bg_color_[2][x] = colors[ci];
                    opaque_[2][x] = true;
                }
            } else if (s.mode == 5 && x < 160 && y < 128) {
                bg_color_[2][x] = d_.halfword(page + u32(y * 160 + x) * 2);
                opaque_[2][x] = true;
            }
        }
    }

    void sprites(const Settings& s, const std::array<u16, 512>& colors, int y) {
        const bool bitmap = s.mode >= 3;
        for (std::size_t i = 0; i < d_.sprites.size(); ++i) {
            const Sprite& o = d_.sprites[i];
            if (o.hidden || o.shape == 3) {
                continue;
            }
            const int w = o.width(), h = o.height();
            const int bw = o.double_size ? w * 2 : w, bh = o.double_size ? h * 2 : h;
            const int oy = o.y, ox = o.x >= 240 ? int(o.x) - 512 : int(o.x);
            // The 8-bit Y wraps: a sprite near the bottom also shows at the
            // top. A 128-pixel-tall one (double size 64) below Y 128 is
            // taken as above the screen: shown at the top only (GBATEK,
            // OBJ attribute 0).
            int ly = (y - oy) & 0xFF;
            if (ly >= bh || (bh == 128 && oy > 128 && y >= oy)) {
                continue;
            }
            if (o.mosaic) {
                ly -= ly % (s.mosaic_sprite_y + 1);
            }
            if (bitmap && o.tile < 512) {
                continue;
            }
            s32 pa = 256, pb = 0, pc = 0, pd = 256;
            if (o.rotated) {
                const Rotation& r = d_.rotations[o.parameters];
                pa = r.pa;
                pb = r.pb;
                pc = r.pc;
                pd = r.pd;
            }
            for (int lx = 0; lx < bw; ++lx) {
                const int sx = ox + lx;
                if (sx < 0 || sx >= screen_width) {
                    continue;
                }
                int tx, ty;
                const int mlx = o.mosaic ? lx - lx % (s.mosaic_sprite_x + 1) : lx;
                if (o.rotated) {
                    const int cx = mlx - bw / 2, cy = ly - bh / 2;
                    tx = ((pa * cx + pb * cy) >> 8) + w / 2;
                    ty = ((pc * cx + pd * cy) >> 8) + h / 2;
                    if (tx < 0 || ty < 0 || tx >= w || ty >= h) {
                        continue;
                    }
                } else {
                    tx = o.flip_x ? w - 1 - mlx : mlx;
                    ty = o.flip_y ? h - 1 - ly : ly;
                }
                u32 t;
                if (s.sprite_tiles_1d) {
                    t = o.tile + u32((ty >> 3) * (w >> 3) + (tx >> 3)) * (o.colors_256 ? 2 : 1);
                } else {
                    t = o.tile + u32((ty >> 3) * 32) + u32(tx >> 3) * (o.colors_256 ? 2 : 1);
                }
                t &= 0x3FF;
                u8 ci;
                if (o.colors_256) {
                    ci = d_.memory[0x10000 + t * 32 + u32((ty & 7) * 8 + (tx & 7))];
                } else {
                    ci = u8((d_.memory[0x10000 + t * 32 + u32((ty & 7) * 4 + (tx & 7) / 2)] >>
                             ((tx & 1) * 4)) &
                            0xF);
                }
                if (!ci) {
                    continue;
                }
                if (o.mode == 2) {
                    sprite_window_[sx] = true;
                    continue;
                }
                if (sprite_opaque_[sx] && sprite_priority_[sx] <= o.priority) {
                    continue;
                }
                sprite_color_[sx] =
                    colors[std::size_t(256 + (o.colors_256 ? ci : o.palette * 16 + ci))];
                sprite_priority_[sx] = o.priority;
                sprite_opaque_[sx] = true;
                sprite_semi_[sx] = o.mode == 1;
            }
        }
    }

    const Display& d_;
    u16 bg_color_[4][screen_width]{};
    bool opaque_[4][screen_width]{};
    u16 sprite_color_[screen_width]{};
    u8 sprite_priority_[screen_width]{};
    bool sprite_opaque_[screen_width]{}, sprite_semi_[screen_width]{},
        sprite_window_[screen_width]{};
};

} // namespace

platform::Image Display::render() const {
    platform::Image out(screen_width, screen_height);
    LineRenderer r(*this);
    r.render(out);
    return out;
}

} // namespace gba
