// See Texture.hpp.
#include "Texture.hpp"

#include <algorithm>

namespace captain_toad {

namespace {

const int kEtcModifiers[8][4] = {{2, 8, -2, -8},     {5, 17, -5, -17},   {9, 29, -9, -29},     {13, 42, -13, -42},
                                 {18, 60, -18, -60}, {24, 80, -24, -80}, {33, 106, -33, -106}, {47, 183, -47, -183}};

void morton(int i, int& x, int& y) {
    x = (i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4);
    y = ((i >> 1) & 1) | ((i >> 2) & 2) | ((i >> 3) & 4);
}

void pixel(int fmt, const Bytes& raw, size_t o, uint8_t* px) {
    auto set = [&](int r, int g, int b, int a) {
        px[0] = uint8_t(r);
        px[1] = uint8_t(g);
        px[2] = uint8_t(b);
        px[3] = uint8_t(a);
    };
    switch (fmt) {
    case 0: {
        int v = u8(raw, o);
        set(v, v, v, 255);
        return;
    }
    case 1:
        set(255, 255, 255, u8(raw, o));
        return;
    case 2: {
        int v = u8(raw, o), l = (v >> 4) * 17;
        set(l, l, l, (v & 15) * 17);
        return;
    }
    case 3: {
        int a = u8(raw, o), l = u8(raw, o + 1);
        set(l, l, l, a);
        return;
    }
    case 4:
        set(u8(raw, o + 1), u8(raw, o), 0, 255);
        return;
    case 5: {
        int v = u16(raw, o), r = v >> 11, g = (v >> 5) & 63, b = v & 31;
        set((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255);
        return;
    }
    case 6:
        set(u8(raw, o + 2), u8(raw, o + 1), u8(raw, o), 255);
        return;
    case 7: {
        int v = u16(raw, o), r = v >> 11, g = (v >> 6) & 31, b = (v >> 1) & 31;
        set((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), (v & 1) ? 255 : 0);
        return;
    }
    case 8: {
        int v = u16(raw, o);
        set((v >> 12) * 17, ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17);
        return;
    }
    case 9:
        set(u8(raw, o + 3), u8(raw, o + 2), u8(raw, o + 1), u8(raw, o));
        return;
    }
    throw ImportError("Unsupported texture format");
}

}   // namespace

int ctrBitsPerPixel(int f) {
    static const int bpp[] = {8, 8, 8, 16, 16, 16, 24, 16, 16, 32, 4, 8, 4, 4};
    if (f < 0 || f > 13) {
        throw ImportError("Unsupported texture format");
    }
    return bpp[f];
}

int picaToCtr(uint32_t f) {
    switch (f) {
    case 0:
        return 9;
    case 1:
        return 6;
    case 2:
        return 7;
    case 3:
        return 5;
    case 4:
        return 8;
    case 5:
        return 3;
    case 7:
        return 0;
    case 8:
        return 1;
    case 9:
        return 2;
    }
    return -1;
}

Bytes decodeEtc(const Bytes& raw, uint32_t w, uint32_t h, uint32_t fmt) {
    if ((fmt != 12 && fmt != 13) || w % 8 || h % 8) {
        throw ImportError("Unsupported ETC texture");
    }
    const size_t size = fmt == 13 ? size_t(w) * h : size_t(w) * h / 2;
    if (raw.size() < size) {
        throw ImportError("Truncated texture");
    }
    Bytes out(size_t(w) * h * 4);
    size_t offset = 0;
    const int blocks[4][2] = {{0, 0}, {0, 4}, {4, 0}, {4, 4}};
    for (uint32_t ty = 0; ty < h; ty += 8) {
        for (uint32_t tx = 0; tx < w; tx += 8) {
            for (const auto& blk : blocks) {
                const int by = blk[0], bx = blk[1];
                uint64_t alpha = ~uint64_t(0);
                if (fmt == 13) {
                    alpha = u64(raw, offset);
                    offset += 8;
                }
                const uint64_t bits = u64(raw, offset);
                offset += 8;
                const uint32_t low = uint32_t(bits), high = uint32_t(bits >> 32);
                const bool flip = high & 1, diff = high & 2;
                int colors[2][3];
                if (diff) {
                    const int shifts[3] = {27, 19, 11}, dshifts[3] = {24, 16, 8};
                    for (int k = 0; k < 3; ++k) {
                        const int c1 = (high >> shifts[k]) & 31, d = (high >> dshifts[k]) & 7;
                        const int c2 = c1 + (d < 4 ? d : d - 8);
                        if (c2 < 0 || c2 > 31) {
                            throw ImportError("Invalid ETC differential");
                        }
                        colors[0][k] = c1 * 8 + (c1 >> 2);
                        colors[1][k] = c2 * 8 + (c2 >> 2);
                    }
                } else {
                    const int s0[3] = {28, 20, 12}, s1[3] = {24, 16, 8};
                    for (int k = 0; k < 3; ++k) {
                        colors[0][k] = int((high >> s0[k]) & 15) * 17;
                        colors[1][k] = int((high >> s1[k]) & 15) * 17;
                    }
                }
                const int tables[2] = {int((high >> 5) & 7), int((high >> 2) & 7)};
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 4; ++x) {
                        const int n = x * 4 + y, half = flip ? y / 2 : x / 2;
                        const int selector = int((low >> n) & 1) | int(((low >> (n + 16)) & 1) << 1);
                        uint8_t* p = &out[((ty + uint32_t(by + y)) * w + tx + uint32_t(bx + x)) * 4];
                        for (int k = 0; k < 3; ++k) {
                            p[k] = uint8_t(
                                std::min(255, std::max(0, colors[half][k] + kEtcModifiers[tables[half]][selector])));
                        }
                        p[3] = uint8_t(((alpha >> (4 * n)) & 15) * 17);
                    }
                }
            }
        }
    }
    return out;
}

Bytes decodeRg8(const Bytes& raw, uint32_t w, uint32_t h) {
    Bytes out(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t m = 0;
            for (int k = 0; k < 3; ++k) {
                m |= ((x >> k) & 1) << (2 * k) | ((y >> k) & 1) << (2 * k + 1);
            }
            const size_t q = ((y / 8) * (w / 8) * 64 + (x / 8) * 64 + m) * 2;
            uint8_t* p = &out[(size_t(y) * w + x) * 4];
            p[0] = u8(raw, q + 1);
            p[1] = u8(raw, q);
            p[2] = 0;
            p[3] = 255;
        }
    }
    return out;
}

Bytes decodeTiled(const Bytes& raw, uint32_t w, uint32_t h, int fmt) {
    Bytes out(size_t(w) * h * 4);
    const size_t bpp = size_t(ctrBitsPerPixel(fmt)) / 8;
    size_t o = 0;
    for (uint32_t ty = 0; ty < h; ty += 8) {
        for (uint32_t tx = 0; tx < w; tx += 8) {
            for (int i = 0; i < 64; ++i) {
                int x, y;
                morton(i, x, y);
                pixel(fmt, raw, o, &out[((ty + uint32_t(y)) * w + tx + uint32_t(x)) * 4]);
                o += bpp;
            }
        }
    }
    return out;
}

// PICA 4-bit formats (public PICA200 documentation): L4 (10) and A4 (11), two texels per byte,
// low nibble first, in the usual 8x8 Morton-ordered tiles.
static Bytes decode4bit(const Bytes& raw, uint32_t w, uint32_t h, bool alpha) {
    if (w % 8 || h % 8 || raw.size() < size_t(w) * h / 2) {
        throw ImportError("Unsupported texture size");
    }
    Bytes out(size_t(w) * h * 4);
    size_t texel = 0;
    for (uint32_t ty = 0; ty < h; ty += 8) {
        for (uint32_t tx = 0; tx < w; tx += 8) {
            for (int i = 0; i < 64; ++i, ++texel) {
                int x, y;
                morton(i, x, y);
                const int nibble = (raw[texel / 2] >> ((texel & 1) * 4)) & 15;
                const uint8_t v = uint8_t(nibble * 17);
                uint8_t* px = &out[((ty + uint32_t(y)) * w + tx + uint32_t(x)) * 4];
                px[0] = alpha ? 255 : v;
                px[1] = alpha ? 255 : v;
                px[2] = alpha ? 255 : v;
                px[3] = alpha ? v : 255;
            }
        }
    }
    return out;
}

size_t picaSize(uint32_t w, uint32_t h, uint32_t format) {
    if (format == 10 || format == 11) {
        return size_t(w) * h / 2;
    }
    if (format == 12) {
        return size_t(w) * h / 2;
    }
    if (format == 13) {
        return size_t(w) * h;
    }
    if (format == 6) {
        return size_t(w) * h * 2;
    }
    const int ctr = picaToCtr(format);
    if (ctr < 0) {
        throw ImportError("Unsupported texture format");
    }
    return size_t(w) * h * size_t(ctrBitsPerPixel(ctr)) / 8;
}

Bytes decodePica(const Bytes& raw, uint32_t w, uint32_t h, uint32_t format) {
    if (format == 10 || format == 11) {
        return decode4bit(raw, w, h, format == 11);
    }
    if (format == 12 || format == 13) {
        return decodeEtc(raw, w, h, format);
    }
    if (format == 6) {
        return decodeRg8(raw, w, h);
    }
    const int ctr = picaToCtr(format);
    if (ctr < 0) {
        throw ImportError("Unsupported texture format");
    }
    return decodeTiled(raw, w, h, ctr);
}

Bytes decodeRgba4(const Bytes& raw, uint32_t w, uint32_t h) {
    return decodeTiled(raw, w, h, 8);
}

Flim decodeFlim(const Bytes& raw) {
    if (raw.size() < 0x28) {
        throw ImportError("Not a BFLIM");
    }
    const size_t f = raw.size() - 0x28;
    if (std::memcmp(raw.data() + f, "FLIM", 4) != 0 || std::memcmp(raw.data() + f + 0x14, "imag", 4) != 0) {
        throw ImportError("Not a BFLIM");
    }
    Flim out;
    out.width = u16(raw, f + 0x1c);
    out.height = u16(raw, f + 0x1e);
    out.alignment = u16(raw, f + 0x20);
    out.format = u8(raw, f + 0x22);
    out.flags = u8(raw, f + 0x23);
    out.size = u32(raw, f + 0x24);
    auto p2 = [](uint32_t n) {
        uint32_t m = 8;
        while (m < n) {
            m *= 2;
        }
        return m;
    };
    uint32_t W = p2(out.width), H = p2(out.height);
    if (out.flags & 4) {
        W = p2(out.height);
        H = p2(out.width);
    }   // rotated storage: transposed raster
    out.allocWidth = W;
    out.allocHeight = H;
    if (out.format == 10 || out.format == 11) {
        out.rgba = decodeEtc(raw, W, H, out.format == 10 ? 12 : 13);
        return out;
    }
    const int fmt = int(out.format);
    if (fmt != 12 && fmt != 13) {
        out.rgba = decodeTiled(raw, W, H, fmt);
        return out;
    }
    out.rgba.assign(size_t(W) * H * 4, 0);
    size_t o = 0;
    for (uint32_t ty = 0; ty < H; ty += 8) {
        for (uint32_t tx = 0; tx < W; tx += 8) {
            for (int i = 0; i < 64; ++i) {
                int x, y;
                morton(i, x, y);
                const int v = (u8(raw, o / 2) >> (4 * (o & 1))) & 15;
                ++o;
                uint8_t* p = &out.rgba[((ty + uint32_t(y)) * W + tx + uint32_t(x)) * 4];
                if (fmt == 12) {
                    p[0] = p[1] = p[2] = uint8_t(v * 17);
                    p[3] = 255;
                } else {
                    p[0] = p[1] = p[2] = 255;
                    p[3] = uint8_t(v * 17);
                }
            }
        }
    }
    return out;
}

Font readFont(const Bytes& b) {
    if (b.size() < 0x14 || (std::memcmp(b.data(), "FFNT", 4) != 0 && std::memcmp(b.data(), "FFNU", 4) != 0)) {
        throw ImportError("Not a BFFNT");
    }
    const size_t f = u16(b, 6);
    if (std::memcmp(b.data() + f, "FINF", 4) != 0) {
        throw ImportError("BFFNT lacks FINF");
    }
    Font font;
    json::Value& info = font.info;
    info = json::Value::object();
    info["type"] = u8(b, f + 8);
    info["height"] = u8(b, f + 9);
    info["width"] = u8(b, f + 10);
    info["ascent"] = u8(b, f + 11);
    info["line_feed"] = u16(b, f + 12);
    info["alter_index"] = u16(b, f + 14);
    font.defaultWidth = {int(int8_t(u8(b, f + 16))), u8(b, f + 17), u8(b, f + 18)};
    json::Value dw = json::Value::object();
    dw["left"] = font.defaultWidth.left;
    dw["glyph"] = font.defaultWidth.glyph;
    dw["char"] = font.defaultWidth.chr;
    info["default_width"] = dw;
    info["encoding"] = u8(b, f + 19);
    const size_t t = u32(b, f + 20), pwidth = u32(b, f + 24), pmap = u32(b, f + 28);
    json::Value& s = font.sheet;
    s = json::Value::object();
    s["cell_width"] = u8(b, t);
    s["cell_height"] = u8(b, t + 1);
    s["count"] = u8(b, t + 2);
    s["max_char_width"] = u8(b, t + 3);
    s["size"] = u32(b, t + 4);
    s["baseline"] = u16(b, t + 8);
    s["format"] = u16(b, t + 10);
    s["cells_per_row"] = u16(b, t + 12);
    s["cells_per_column"] = u16(b, t + 14);
    s["width"] = u16(b, t + 16);
    s["height"] = u16(b, t + 18);
    s["offset"] = u32(b, t + 20);
    for (size_t p = pwidth; p; p = u32(b, p + 4)) {
        const int start = u16(b, p), end = u16(b, p + 2);
        for (int i = start; i <= end; ++i) {
            const size_t e = p + 8 + 3 * size_t(i - start);
            font.widths[i] = {int(int8_t(u8(b, e))), u8(b, e + 1), u8(b, e + 2)};
        }
    }
    for (size_t p = pmap; p; p = u32(b, p + 8)) {
        const int first = u16(b, p), last = u16(b, p + 2), method = u16(b, p + 4);
        const size_t q = p + 12;
        if (method == 0) {
            const int off = u16(b, q);
            for (int c = first; c <= last; ++c) {
                font.cmap[c] = off + c - first;
            }
        } else if (method == 1) {
            for (int c = first; c <= last; ++c) {
                const int i = u16(b, q + 2 * size_t(c - first));
                if (i != 0xffff) {
                    font.cmap[c] = i;
                }
            }
        } else if (method == 2) {
            const int n = u16(b, q);
            for (int k = 0; k < n; ++k) {
                font.cmap[u16(b, q + 2 + 4 * size_t(k))] = u16(b, q + 4 + 4 * size_t(k));
            }
        }
    }
    const uint32_t W = uint32_t(s.at("width").integer()), H = uint32_t(s.at("height").integer());
    const size_t size = size_t(s.at("size").integer()), offset = size_t(s.at("offset").integer());
    const int64_t format = s.at("format").integer();
    if (format != 11) {
        // Other sheet formats (PICA numbering): the glyph coverage is the alpha channel, or the
        // luminance of the luminance-only formats (7 L8, 10 L4).
        // RGB formats (0 RGBA8, 1 RGB8, 2 RGBA5551, 3 RGB565, 4 RGBA4) carry colour glyphs: kept as
        // RGBA sheets.
        font.color = format <= 4;
        for (int64_t k = 0; k < s.at("count").integer(); ++k) {
            const Bytes rgba = decodePica(slice(b, offset + size_t(k) * size, size), W, H, uint32_t(format));
            if (font.color) {
                font.sheets.push_back(rgba);
                continue;
            }
            Bytes a(size_t(W) * H);
            for (size_t i = 0; i < a.size(); ++i) {
                a[i] = (format == 7 || format == 10) ? rgba[i * 4] : rgba[i * 4 + 3];
            }
            font.sheets.push_back(std::move(a));
        }
        return font;
    }
    for (int64_t k = 0; k < s.at("count").integer(); ++k) {
        const Bytes data = slice(b, offset + size_t(k) * size, size);
        Bytes a(size_t(W) * H);
        size_t o = 0;
        for (uint32_t ty = 0; ty < H; ty += 8) {
            for (uint32_t tx = 0; tx < W; tx += 8) {
                for (int i = 0; i < 64; ++i) {
                    int x, y;
                    morton(i, x, y);
                    const int v = (u8(data, o / 2) >> (4 * (o & 1))) & 15;
                    ++o;
                    a[(ty + uint32_t(y)) * W + tx + uint32_t(x)] = uint8_t(v * 17);
                }
            }
        }
        font.sheets.push_back(std::move(a));
    }
    return font;
}

}   // namespace captain_toad
