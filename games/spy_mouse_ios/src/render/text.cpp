#include "render/text.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H
#include FT_DRIVER_H
#include FT_MODULE_H

#include <algorithm>
#include <cmath>

namespace doc::render {
namespace {

// ManagerBackendFT::loadFont @0x282da4 with hinting mode 2 (App::loadFonts): default loading.
constexpr FT_Int32 kLoadFlags = FT_LOAD_DEFAULT;

FT_Library library() {
    static FT_Library lib = [] {
        FT_Library l = nullptr;
        FT_Init_FreeType(&l);
        // The game embeds FreeType 2.4.4 (FT_New_Library @0x46485c stores 2, 4, 4), whose
        // TrueType hinting is the v35 interpreter; current FreeType defaults to v40. (The game's
        // font has no bytecode, so this changes nothing measurable; what differs by a pixel
        // here and there is the rasteriser of the two versions.)
        FT_UInt version = TT_INTERPRETER_VERSION_35;
        FT_Property_Set(l, "truetype", "interpreter-version", &version);
        return l;
    }();
    return lib;
}

std::u32string decodeUtf8(const std::string& s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = c;
        int extra = 0;
        if (c >= 0xF0) cp = c & 0x07, extra = 3;
        else if (c >= 0xE0) cp = c & 0x0F, extra = 2;
        else if (c >= 0xC0) cp = c & 0x1F, extra = 1;
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        out.push_back(cp);
    }
    return out;
}

}  // namespace

struct Font::Impl {
    FT_Face face = nullptr;
};

Font::~Font() {
    if (impl_ && impl_->face) FT_Done_Face(impl_->face);
}

std::unique_ptr<Font> Font::load(std::vector<std::uint8_t> ttf, int pixelSize, std::string& error) {
    std::unique_ptr<Font> font(new Font);
    font->data_ = std::move(ttf);
    font->impl_ = std::make_unique<Impl>();
    if (FT_New_Memory_Face(library(), font->data_.data(), FT_Long(font->data_.size()), 0, &font->impl_->face)) {
        error = "font could not be read";
        return nullptr;
    }
    // ManagerBackendFT::loadFont @0x282da4: FT_Set_Pixel_Sizes(face, 0, size).
    FT_Set_Pixel_Sizes(font->impl_->face, 0, FT_UInt(pixelSize));
    font->pixelSize_ = pixelSize;
    const FT_Size_Metrics& m = font->impl_->face->size->metrics;
    font->lineHeight_ = float(m.height) / 64.0f;
    font->ascent_ = float(m.ascender) / 64.0f;
    return font;
}

const Font::Shape& Font::shape(char32_t c) {
    auto it = shapes_.find(c);
    if (it != shapes_.end()) return it->second;
    Shape s;
    s.index = FT_Get_Char_Index(impl_->face, FT_ULong(c));
    if (FT_Load_Glyph(impl_->face, s.index, kLoadFlags) == 0) {
        s.advance = float(impl_->face->glyph->advance.x) / 64.0f;
        if (FT_Render_Glyph(impl_->face->glyph, FT_RENDER_MODE_NORMAL) == 0) {
            s.inkLeft = float(impl_->face->glyph->bitmap_left);
            s.inkWidth = float(impl_->face->glyph->bitmap.width);
        }
    }
    return shapes_[c] = s;
}

// ManagerBackendFT::loadBitmap @0x282f70: the glyph, stroked with radius width x 64 (round caps
// and joins) for outline shapes, rendered anti-aliased.
Font::Glyph* Font::glyph(platform::Host& host, unsigned index, Outline outline, float width) {
    const auto key = std::make_tuple(index, int(outline), int(width * 64.0f));
    auto it = glyphs_.find(key);
    if (it != glyphs_.end()) return it->second.get();
    auto g = std::make_unique<Glyph>();
    FT_Glyph ftGlyph = nullptr;
    if (FT_Load_Glyph(impl_->face, index, kLoadFlags) == 0 && FT_Get_Glyph(impl_->face->glyph, &ftGlyph) == 0) {
        if (outline != Outline::None) {
            FT_Stroker stroker = nullptr;
            if (FT_Stroker_New(library(), &stroker) == 0) {
                FT_Stroker_Set(stroker, FT_Fixed(int(width * 64.0f)), FT_STROKER_LINECAP_ROUND,
                               FT_STROKER_LINEJOIN_ROUND, 0);
                if (outline == Outline::Stroke) FT_Glyph_Stroke(&ftGlyph, stroker, 1);
                else if (outline == Outline::Inside) FT_Glyph_StrokeBorder(&ftGlyph, stroker, 1, 1);
                else FT_Glyph_StrokeBorder(&ftGlyph, stroker, 0, 1);
                FT_Stroker_Done(stroker);
            }
        }
        if (FT_Glyph_To_Bitmap(&ftGlyph, FT_RENDER_MODE_NORMAL, nullptr, 1) == 0) {
            auto* bmp = reinterpret_cast<FT_BitmapGlyph>(ftGlyph);
            g->left = float(bmp->left);
            g->top = float(bmp->top);
            g->width = int(bmp->bitmap.width);
            g->height = int(bmp->bitmap.rows);
            if (g->width > 0 && g->height > 0) {
                platform::Image image;
                image.width = g->width;
                image.height = g->height;
                image.rgba.resize(std::size_t(g->width) * std::size_t(g->height) * 4);
                for (int y = 0; y < g->height; ++y)
                    for (int x = 0; x < g->width; ++x) {
                        const std::uint8_t a = bmp->bitmap.buffer[y * bmp->bitmap.pitch + x];
                        std::uint8_t* p = &image.rgba[(std::size_t(y) * std::size_t(g->width) + std::size_t(x)) * 4];
                        p[0] = p[1] = p[2] = 255;
                        p[3] = a;
                    }
                g->texture = host.createTexture(image);
            }
        }
        FT_Done_Glyph(ftGlyph);
    }
    Glyph* result = g.get();
    glyphs_[key] = std::move(g);
    return result;
}

// ManagerFontFT::layoutStr @0x21e154: pens advance by the glyph advance plus kerning; the
// first glyph's own offset is not compensated.
std::vector<std::pair<unsigned, float>> Font::layout(const std::string& text, float& total) {
    std::vector<std::pair<unsigned, float>> pens;
    float pen = 0.0f;
    unsigned previous = 0;
    const bool kerning = FT_HAS_KERNING(impl_->face);
    for (char32_t c : decodeUtf8(text)) {
        const Shape& s = shape(c);
        if (previous && kerning) {
            FT_Vector k;
            if (FT_Get_Kerning(impl_->face, previous, s.index, FT_KERNING_DEFAULT, &k) == 0) pen += float(k.x) / 64.0f;
        }
        pens.emplace_back(s.index, pen);
        pen += s.advance;
        previous = s.index;
    }
    total = pen;
    return pens;
}

float Font::width(const std::string& text, float scale) {
    float total = 0.0f;
    layout(text, total);
    return total * scale;
}

float Font::inkWidth(const std::string& text, float scale) {
    float total = 0.0f;
    const auto pens = layout(text, total);
    if (pens.empty()) return 0.0f;
    const std::u32string chars = decodeUtf8(text);
    const Shape& first = shape(chars.front());
    const Shape& last = shape(chars.back());
    return ((last.inkLeft + last.inkWidth) * scale + pens.back().second * scale) - (first.inkLeft * scale);
}

// ManagerFontFT::layoutStrWrap @0x21dbe8: words are added while the line still fits.
std::vector<std::string> Font::wrapLines(const std::string& text, float wrapWidth, float scale) {
    std::vector<std::string> lines;
    std::string paragraph;
    auto flush = [&](const std::string& para) {
        if (wrapWidth <= 0.0f) {
            lines.push_back(para);
            return;
        }
        std::string line, word;
        std::size_t i = 0;
        while (i <= para.size()) {
            const std::size_t space = para.find(' ', i);
            const std::size_t end = space == std::string::npos ? para.size() : space;
            word = para.substr(i, end - i);
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && inkWidth(candidate, scale) > wrapWidth) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
            if (space == std::string::npos) break;
            i = space + 1;
        }
        lines.push_back(line);
    };
    std::size_t start = 0;
    while (true) {
        const std::size_t nl = text.find('\n', start);
        flush(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

// ManagerFontFT::loadFont @0x21ef64 keeps the top of 'A' and the bottom of 'y' as every line's
// vertical extent; text is aligned by those, not by its own ink.
void Font::lineExtent(platform::Host& host, float& top, float& bottom) {
    top = 10.0f;
    bottom = -3.0f;
    if (Glyph* a = glyph(host, shape(U'A').index, Outline::None, 0.0f)) top = a->top;
    if (Glyph* y = glyph(host, shape(U'y').index, Outline::None, 0.0f)) bottom = y->top - float(y->height);
}

// ManagerFontFT::getWrapHeight @0x21e0d0: from the first line's top to the last line's bottom.
float Font::wrapHeight(platform::Host& host, const std::string& text, float wrapWidth, float scale) {
    float top, bottom;
    lineExtent(host, top, bottom);
    const float lines = float(wrapLines(text, wrapWidth, scale).size());
    return (top - bottom + (lines - 1.0f) * lineHeight_) * scale;
}

void Font::drawFlags(platform::Host& host, const std::string& text, float x, float y, int flags, float scale,
                     float wrapWidth, const TextStyle& style, const Colour& current, const Affine& xf) {
    struct Line {
        std::vector<std::pair<unsigned, float>> pens;
        float inkLeft = 0, inkRight = 0;
    };
    std::vector<Line> lines;
    const std::vector<std::string> texts = wrapLines(text, wrapWidth, scale);
    for (std::size_t n = 0; n < texts.size(); ++n) {
        Line line;
        float total = 0.0f;
        line.pens = layout(texts[n], total);
        float left = 1e9f, right = -1e9f;
        for (const auto& [index, pen] : line.pens) {
            Glyph* g = glyph(host, index, Outline::None, 0.0f);
            if (!g || g->width <= 0) continue;
            left = std::min(left, pen + g->left);
            right = std::max(right, pen + g->left + float(g->width));
        }
        line.inkLeft = left > right ? 0.0f : left;
        line.inkRight = left > right ? 0.0f : right;
        lines.push_back(std::move(line));
    }
    if (lines.empty()) return;
    float capTop, descent;
    lineExtent(host, capTop, descent);
    // Extent relative to the first baseline, y down.
    const float inkTop = -capTop;
    const float inkBottom = float(lines.size() - 1) * lineHeight_ - descent;
    float baseline = y;
    if (flags & 0x20) baseline = y - (inkTop + inkBottom) * 0.5f * scale;
    else if (flags & 0x40) baseline = y - inkBottom * scale;
    else if (flags & 0x10) baseline = y - inkTop * scale;
    // renderStr rounds the origin to whole pixels.
    baseline = std::floor(baseline + 0.5f);
    const float originX = std::floor(x + 0.5f);
    auto pass = [&](float dx, float dy, Outline outline, float width, const Colour& c) {
        for (std::size_t n = 0; n < lines.size(); ++n) {
            const Line& line = lines[n];
            float lx = originX;
            if (flags & 1) lx -= line.inkLeft * scale;
            else if (flags & 2) lx -= (line.inkLeft + line.inkRight) * 0.5f * scale;
            else if (flags & 4) lx -= line.inkRight * scale;
            const float by = baseline + float(n) * lineHeight_ * scale;
            for (const auto& [index, pen] : line.pens) {
                Glyph* g = glyph(host, index, outline, width);
                if (!g || !g->texture) continue;
                platform::DrawStyle ds;
                ds.r = c.r;
                ds.g = c.g;
                ds.b = c.b;
                ds.a = c.a;
                const float gx = lx + dx + (pen + g->left) * scale, gy = by + dy - g->top * scale;
                host.draw(g->texture, 0, 0, float(g->width), float(g->height), xf.x(gx), xf.y(gy),
                          float(g->width) * scale * xf.ax, float(g->height) * scale * xf.ay, ds);
            }
        }
    };
    if (style.shadow) {
        Outline o = style.outline;
        if (o == Outline::Stroke && style.fill) o = Outline::Grown;
        pass(style.shadowX, style.shadowY, o, style.outline == Outline::None ? 0.0f : style.outlineWidth,
             style.shadowColour);
    }
    if (style.outline != Outline::None) pass(0, 0, style.outline, style.outlineWidth, style.outlineColour);
    if (style.fill) pass(0, 0, Outline::None, 0.0f, style.fillColourSet ? style.fillColour : current);
}

}  // namespace doc::render
