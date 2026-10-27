#include "mindwalker_amiga/screen/gadget_bar.hpp"

namespace mindwalker::screen {
namespace {

std::string c_string(const amiga::LoadedImage& program, std::uint32_t at) {
    std::string text;
    for (char c; (c = char(program.u8(at))) != 0; ++at) text += c;
    return text;
}

} // namespace

GadgetBar::GadgetBar(const amiga::LoadedImage& program, std::uint32_t new_window,
                     std::map<std::uint32_t, std::vector<std::uint8_t>> image_data) {
    // NewWindow +18: FirstGadget. Gadget: +0 next, +4 left, +6 top, +8 width,
    // +10 height, +12 flags, +16 type, +18 render, +26 text, +34 special
    // info, +38 id. Image: +0 left, +2 top, +4 width, +6 height, +8 depth,
    // +10 data, +14 PlanePick, +15 PlaneOnOff, +16 next. IntuiText: +0 front
    // pen, +1 back pen, +2 mode, +4 left, +6 top, +12 text, +16 next.
    for (std::uint32_t g = program.u32(new_window + 18); g != 0; g = program.u32(g)) {
        Gadget gadget;
        gadget.left = program.s16(g + 4);
        gadget.top = program.s16(g + 6);
        gadget.width = program.s16(g + 8);
        gadget.height = program.s16(g + 10);
        const auto flags = program.u16(g + 12);
        gadget.relative_right = (flags & 0x0010) != 0;
        gadget.enabled = (flags & 0x0100) == 0;
        gadget.proportional = (program.u16(g + 16) & 0x0F) == 3;
        gadget.id = program.u16(g + 38);
        if (gadget.proportional) {
            gadget.horizontal_pot = program.u16(program.u32(g + 34) + 2); // PropInfo +2 HorizPot
            gadget.free_horizontal = (program.u16(program.u32(g + 34)) & 0x0002) != 0; // +0 Flags: FREEHORIZ
        }
        for (std::uint32_t i = program.u32(g + 18); i != 0; i = program.u32(i + 16)) {
            gadget.images.push_back({program.s16(i), program.s16(i + 2), program.s16(i + 4), program.s16(i + 6),
                                     program.s16(i + 8), i + 10, program.u8(i + 14), program.u8(i + 15)});
            if (!image_data.contains(i + 10)) gadget.images.back().data = 0;
        }
        for (std::uint32_t t = program.u32(g + 26); t != 0; t = program.u32(t + 16)) {
            gadget.texts.push_back({program.u8(t), program.u8(t + 1), program.u8(t + 2), program.s16(t + 4),
                                    program.s16(t + 6), c_string(program, program.u32(t + 12))});
        }
        gadgets_.push_back(std::move(gadget));
    }
    data_ = std::move(image_data);
}

Gadget* GadgetBar::find(int id) {
    for (auto& gadget : gadgets_) {
        if (gadget.id == id) return &gadget;
    }
    return nullptr;
}

void GadgetBar::draw_image(Canvas& canvas, const GadgetImage& image, int x0, int y0) const {
    const std::vector<std::uint8_t>* bytes = image.data ? &data_.at(image.data) : nullptr;
    const int row_bytes = (image.width + 15) / 16 * 2, plane_bytes = row_bytes * image.height;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            int pen = 0, plane_of_image = 0;
            for (int p = 0; p < 6; ++p) {
                int bit;
                if ((image.plane_pick >> p & 1) && plane_of_image < image.depth) {
                    const std::size_t at = std::size_t(plane_of_image * plane_bytes + y * row_bytes + x / 8);
                    bit = bytes && at < bytes->size() ? (*bytes)[at] >> (7 - x % 8) & 1 : 0;
                    ++plane_of_image;
                } else {
                    bit = image.plane_on_off >> p & 1;
                }
                pen |= bit << p;
            }
            canvas.plot(x0 + x, y0 + y, pen);
        }
    }
}

void GadgetBar::draw(Canvas& canvas, int background) const {
    canvas.fill(0, 0, Canvas::width - 1, 9, background);
    canvas.fill(0, 10, Canvas::width - 1, 11, 0);
    for (const auto& gadget : gadgets_) {
        const int x = gadget.x(), y = gadget.top;
        if (gadget.proportional) {
            canvas.fill(x + 2, y + 1, x + gadget.width - 3, y + gadget.height - 2, 0);
            // Knob travel: without FREEHORIZ the knob stays at the left
            // whatever HorizPot holds (P at progress 7: dump psy0); the
            // travel for a free knob is a hypothesis.
            for (const auto& knob : gadget.images) {
                const int travel = gadget.width - 8 - knob.width;
                const int offset = gadget.free_horizontal ? travel * gadget.horizontal_pot / 0xFFFF : 0;
                draw_image(canvas, knob, x + 4 + offset + knob.left, y + 2 + knob.top);
            }
        } else {
            for (const auto& image : gadget.images) draw_image(canvas, image, x + image.left, y + image.top);
        }
        for (const auto& text : gadget.texts) {
            canvas.text(x + text.left, y + text.top + 6, text.text, text.front, TextFont::topaz8, plain, text.mode == 1,
                        text.back);
        }
        if (!gadget.enabled) {
            for (int gy = y; gy < y + gadget.height; ++gy) {
                for (int gx = x; gx < x + gadget.width; ++gx) {
                    if ((gx & 3) == ((gy & 1) ? 3 : 1)) canvas.plot(gx, gy, background);
                }
            }
        }
    }
}

} // namespace mindwalker::screen
