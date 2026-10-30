#include "ui/shelf.hpp"
#include "platform/type.hpp"
#include "platform/resources.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>

namespace ui::shelf {
namespace {
const platform::TypesetLine& line_for(std::string_view text, int size, bool bold) {
    using Key = std::tuple<std::string, int, bool>;
    static std::map<Key, platform::TypesetLine> cache;
    Key key{std::string(text), size, bold};
    if (auto found = cache.find(key); found != cache.end()) return found->second;
    // Bound the cache when imported filenames or transient messages change.
    if (cache.size() >= 1024) cache.clear();
    return cache.emplace(std::move(key), platform::typeset(text, size, bold)).first->second;
}
const Image& artwork(std::string_view id) {
    static std::map<std::string, Image> cache;
    const std::string name(id);
    if (auto it = cache.find(name); it != cache.end()) return it->second;
    const auto bytes = platform::resource(name);
    auto* source = SDL_LoadPNG_IO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true);
    if (!source) throw std::runtime_error("Cannot decode Reprise artwork: " + name);
    auto* rgba = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(source);
    if (!rgba) throw std::runtime_error(SDL_GetError());
    Image result(rgba->w, rgba->h);
    for (int y = 0; y < rgba->h; ++y)
        std::memcpy(result.pixels.data() + y * rgba->w,
                    static_cast<const char*>(rgba->pixels) + y * rgba->pitch, rgba->w * 4);
    SDL_DestroySurface(rgba);
    return cache.emplace(name, std::move(result)).first->second;
}
}
int width(std::string_view value, int size, bool bold) {
    return line_for(value, size, bold).advance;
}
int text(Image& im, std::string_view value, int x, int y, int size, Color c, bool bold) {
    const auto& line = line_for(value, size, bold);
    const auto& mask = line.coverage;
    for (int yy = 0; yy < mask.height; ++yy) for (int xx = 0; xx < mask.width; ++xx) {
        if (x + xx < 0 || x + xx >= im.width || y + yy < 0 || y + yy >= im.height) continue;
        const int a = mask.pixels[yy * mask.width + xx].a;
        if (!a) continue;
        auto& dst = im.pixels[(y + yy) * im.width + x + xx];
        const int keep = dst.a * (255 - a) / 255, total = a + keep;
        auto blend = [&](int src, int d) { return std::uint8_t((src * a + d * keep) / total); };
        dst = {blend(c.r, dst.r), blend(c.g, dst.g), blend(c.b, dst.b), std::uint8_t(total)};
    }
    return x + line.advance;
}
void right(Image& im, std::string_view s, int x, int y, int size, Color c, bool bold) {
    text(im, s, x - width(s, size, bold), y, size, c, bold);
}
void clipped(Image& im, std::string_view s, int x, int y, int w, int size, Color c, bool bold) {
    if (width(s, size, bold) <= w) { text(im, s, x, y, size, c, bold); return; }
    std::string shortened(s);
    while (!shortened.empty() && width(shortened + "...", size, bold) > w) {
        std::size_t end = shortened.size() - 1;
        while (end > 0 && (static_cast<unsigned char>(shortened[end]) & 0xc0) == 0x80) --end;
        shortened.resize(end);
    }
    text(im, shortened + "...", x, y, size, c, bold);
}
int wrap(Image& im, std::string_view s, int x, int y, int w, int lines, int size, Color c) {
    int row = 0;
    while (!s.empty() && row < lines) {
        if (row == lines - 1) { clipped(im, s, x, y + row * (size + 5), w, size, c); return row + 1; }
        std::size_t take = s.size();
        while (take > 0 && width(s.substr(0, take), size) > w) {
            const auto split = s.rfind(' ', take - 1);
            take = split == std::string_view::npos || !split ? take - 1 : split;
        }
        take = std::max<std::size_t>(1, take);
        text(im, s.substr(0, take), x, y + row * (size + 5), size, c);
        s.remove_prefix(take);
        while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
        ++row;
    }
    return row;
}
int prose(Image& im, std::string_view content, int x, int y, int w, int size, Color color) {
    // Cache line breaks, not a tall bitmap. Scrolling only draws visible lines.
    struct Layout { std::vector<std::pair<int, std::string>> lines; int height{}; };
    using Key = std::tuple<std::string, int, int>;
    static std::map<Key, Layout> layouts;
    const Key key{std::string(content), w, size};
    auto found = layouts.find(key);
    if (found == layouts.end()) {
        if (layouts.size() >= 32) layouts.clear();
        Layout layout;
        auto remaining = content;
        while (!remaining.empty()) {
            const auto end = remaining.find("\n\n");
            auto paragraph = remaining.substr(0, end);
            std::string current;
            auto flush = [&] {
                if (current.empty()) return;
                layout.lines.emplace_back(layout.height, current);
                layout.height += size + 5;
                current.clear();
            };
            while (!paragraph.empty()) {
                const auto start = paragraph.find_first_not_of(" \n\r\t");
                if (start == std::string_view::npos) break;
                paragraph.remove_prefix(start);
                const auto space = paragraph.find_first_of(" \n\r\t");
                const std::string word(paragraph.substr(0, space));
                const std::string candidate = current.empty() ? word : current + " " + word;
                if (!current.empty() && width(candidate, size) > w) flush();
                if (!current.empty()) current += ' ';
                current += word;
                if (space == std::string_view::npos) break;
                paragraph.remove_prefix(space);
            }
            flush();
            layout.height += 20;
            if (end == std::string_view::npos) break;
            remaining.remove_prefix(end + 2);
        }
        found = layouts.emplace(key, std::move(layout)).first;
    }
    for (const auto& [offset, line] : found->second.lines)
        if (y + offset + size + 5 > 0 && y + offset < im.height)
            clipped(im, line, x, y + offset, w, size, color);
    return found->second.height;
}
void button(Image& im, std::string_view s, int x, int y, int w, int h, bool primary) {
    ui::rounded(im, x, y, w, h, 7, primary ? cobalt : line);
    if (!primary) ui::rounded(im, x + 1, y + 1, w - 2, h - 2, 6, card);
    text(im, s, x + (w - width(s, 15, true)) / 2, y + (h - 22) / 2, 15, primary ? white : ink, true);
}
void danger_button(Image& im, std::string_view s, int x, int y, int w, int h, bool filled) {
    ui::rounded(im, x, y, w, h, 7, red);
    if (!filled) ui::rounded(im, x + 1, y + 1, w - 2, h - 2, 6, card);
    text(im, s, x + (w - width(s, 15, true)) / 2, y + (h - 22) / 2, 15, filled ? white : red, true);
}
static void draw_art(Image& im, std::string_view id, int x, int y, int w, int h) {
    using Key = std::tuple<std::string, int, int>;
    static std::map<Key, Image> thumbnails;
    const Key key{std::string(id), w, h};
    auto cached = thumbnails.find(key);
    if (cached == thumbnails.end()) {
        if (thumbnails.size() >= 48) thumbnails.clear();
        const Image* source = &artwork(id);
        std::vector<Image> levels;
        // Box-filter each reduction before the final bilinear crop. This keeps
        // the screenprint grain from aliasing when a full cover becomes a tile.
        while (source->width / 2 >= w && source->height / 2 >= h) {
            Image half(source->width / 2, source->height / 2);
            for (int yy = 0; yy < half.height; ++yy) for (int xx = 0; xx < half.width; ++xx) {
                const int pos = yy * 2 * source->width + xx * 2;
                const auto a = ui::mix(source->pixels[pos], source->pixels[pos + 1], 128);
                const auto b = ui::mix(source->pixels[pos + source->width], source->pixels[pos + source->width + 1], 128);
                half.pixels[yy * half.width + xx] = ui::mix(a, b, 128);
            }
            levels.push_back(std::move(half));
            source = &levels.back();
        }
        const auto& src = *source;
        Image thumbnail(w, h);
        const double scale = std::max(double(w) / src.width, double(h) / src.height);
        const double ox = (src.width - w / scale) / 2, oy = (src.height - h / scale) / 2;
        for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
            const double sx = std::clamp(ox + (xx + .5) / scale - .5, 0.0, double(src.width - 1));
            const double sy = std::clamp(oy + (yy + .5) / scale - .5, 0.0, double(src.height - 1));
            const int ix = int(sx), iy = int(sy), jx = std::min(ix + 1, src.width - 1), jy = std::min(iy + 1, src.height - 1);
            const auto a = ui::mix(src.pixels[iy * src.width + ix], src.pixels[iy * src.width + jx], int((sx - ix) * 255));
            const auto b = ui::mix(src.pixels[jy * src.width + ix], src.pixels[jy * src.width + jx], int((sx - ix) * 255));
            thumbnail.put(xx, yy, ui::mix(a, b, int((sy - iy) * 255)));
        }
        cached = thumbnails.emplace(key, std::move(thumbnail)).first;
    }
    const auto& thumbnail = cached->second;
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx)
        im.put(x + xx, y + yy, thumbnail.pixels[yy * w + xx]);
}
void cover(Image& im, std::string_view id, int x, int y, int w, int h) {
    draw_art(im, "covers/" + std::string(id) + ".png", x, y, w, h);
}
void backdrop(Image& im, std::string_view collection) {
    draw_art(im, "themes/" + std::string(collection) + ".png", 0, 0, im.width, im.height);
}
void reprise(Image& im, int x, int y) {
    // Reconstruct the original mock-up mark as geometry: overlapping rounded
    // tiles, a small blue branch, and three circular connected nodes. Coverage
    // sampling makes curved edges smooth even on the 42-pixel logical mark.
    static const Image mark = [] {
        Image out(42, 42);
        constexpr int samples = 8;
        auto rounded = [](double x, double y, double left, double top, double w, double h, double radius) {
            const double dx = std::max(std::abs(x - left - w / 2) - (w / 2 - radius), 0.0);
            const double dy = std::max(std::abs(y - top - h / 2) - (h / 2 - radius), 0.0);
            return dx * dx + dy * dy <= radius * radius;
        };
        auto segment = [](double x, double y, double ax, double ay, double bx, double by, double radius) {
            const double t = std::clamp(((x-ax)*(bx-ax)+(y-ay)*(by-ay))/((bx-ax)*(bx-ax)+(by-ay)*(by-ay)), 0.0, 1.0);
            return std::hypot(x-ax-t*(bx-ax), y-ay-t*(by-ay)) <= radius;
        };
        for (int yy = 0; yy < 42; ++yy) for (int xx = 0; xx < 42; ++xx) {
            int red{}, green{}, blue{}, count{};
            for (int sy = 0; sy < samples; ++sy) for (int sx = 0; sx < samples; ++sx) {
                const double px = xx + (sx + .5) / samples, py = yy + (sy + .5) / samples;
                Color c{0,0,0,0};
                if (rounded(px, py, 8, 8, 33, 33, 5)) c = {28,76,225};
                if (rounded(px, py, 0, 0, 33, 34, 5)) {
                    c = ui::mix({14,74,100}, {5,48,68}, int(py * 255 / 34));
                    if (segment(px,py,10,11,17,6,2.3)) c = {18,143,209};
                    if (segment(px,py,9,11,17,26,.65) || segment(px,py,17,26,25,16,.65) || segment(px,py,9,11,25,16,.65)) c = {105,165,183};
                    if (std::hypot(px-9,py-11)<3.6 || std::hypot(px-17,py-26)<3.6) c = {250,248,232};
                    if (std::hypot(px-25,py-16)<3.5) c = {36,157,235};
                }
                if (c.a) { red += c.r; green += c.g; blue += c.b; ++count; }
            }
            out.pixels[yy * 42 + xx] = count ? Color{std::uint8_t(red/count), std::uint8_t(green/count), std::uint8_t(blue/count), std::uint8_t(count*255/(samples*samples))} : Color{0,0,0,0};
        }
        return out;
    }();
    for (int yy = 0; yy < mark.height; ++yy) for (int xx = 0; xx < mark.width; ++xx) {
        if (x+xx < 0 || y+yy < 0 || x+xx >= im.width || y+yy >= im.height) continue;
        auto& dst = im.pixels[(y+yy)*im.width+x+xx];
        const auto c = mark.pixels[yy*mark.width+xx];
        const auto alpha = dst.a;
        dst = ui::mix(dst, c, c.a);
        dst.a = alpha;
    }
}
void port(Image& im, int x, int y, int scale) {
    // DE-9, shared by the C64 and Amiga: FIVE contacts above FOUR staggered
    // contacts. No screw holes or decorative dots that could read as extra pins.
    // Supersampling keeps the sloped D-sub shell and round contacts clear at
    // small sizes, using only the same portable pixel buffer as the rest of UI.
    struct Point { double x, y; };
    constexpr std::array<Point, 8> outer{{{5, 1}, {41, 1}, {45, 5}, {41, 21},
                                         {37, 25}, {9, 25}, {5, 21}, {1, 5}}};
    constexpr std::array<Point, 8> inner{{{6, 3}, {40, 3}, {42, 6}, {38, 21},
                                         {35, 23}, {11, 23}, {8, 21}, {4, 6}}};
    constexpr std::array<Point, 9> pins{{{9, 8.5}, {16, 8.5}, {23, 8.5}, {30, 8.5}, {37, 8.5},
                                        {12.5, 17}, {19.5, 17}, {26.5, 17}, {33.5, 17}}};
    auto inside = [](double px, double py, const auto& polygon) {
        bool result = false;
        for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
            const auto a = polygon[i], b = polygon[j];
            if ((a.y > py) != (b.y > py) && px < (b.x - a.x) * (py - a.y) / (b.y - a.y) + a.x)
                result = !result;
        }
        return result;
    };
    for (int yy = 0; yy < 26 * scale; ++yy) for (int xx = 0; xx < 46 * scale; ++xx) {
        if (x + xx < 0 || x + xx >= im.width || y + yy < 0 || y + yy >= im.height) continue;
        const auto background = im.pixels[(y + yy) * im.width + x + xx];
        int r = 0, g = 0, b = 0;
        for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) {
            const double px = (xx + (sx + .5) / 4) / scale, py = (yy + (sy + .5) / 4) / scale;
            Color c = background;
            if (inside(px, py, outer)) c = muted;
            if (inside(px, py, inner)) c = ink;
            for (const auto pin : pins)
                if ((px - pin.x) * (px - pin.x) + (py - pin.y) * (py - pin.y) < 1.85 * 1.85) c = card;
            r += c.r; g += c.g; b += c.b;
        }
        im.put(x + xx, y + yy, {std::uint8_t(r / 16), std::uint8_t(g / 16), std::uint8_t(b / 16)});
    }
}
}
