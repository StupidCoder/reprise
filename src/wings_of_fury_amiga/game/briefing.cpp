#include "wings_of_fury_amiga/game/briefing.hpp"

namespace wings::game {
namespace {

constexpr std::uint32_t a4 = 0x11AF52;
// $108590: where the panel and the texts go (Move() before each text).
constexpr int panel_centre_x = 0x140, panel_centre_y = 0x65;
constexpr int rank_x = 0x128, rank_y = 0x3D;
constexpr int values_x = 0x154, mission_y = 0x49, islands_y = 0x77, ships_y = 0x83;
// (The text's colour: all three planes, measured on the original's screen.)
constexpr std::uint8_t text_colour = 7;

std::string string_at(const amiga::LoadedImage& wings, std::uint32_t at) {
    std::string s;
    for (char c; (c = char(wings.u8(at))) != 0; ++at) s += c;
    return s;
}

} // namespace

BriefingTables read_briefing_tables(const amiga::LoadedImage& wings) {
    BriefingTables t;
    for (std::size_t i = 0; i < t.palette.size(); ++i) t.palette[i] = wings.u16(a4 - 0x56D2 + std::uint32_t(i * 2));
    for (std::uint32_t i = 0; i < 7; ++i) t.rank_names.push_back(string_at(wings, wings.u32(a4 - 0x56EE + i * 4)));
    return t;
}

int draw_text(IndexedCanvas& canvas, const TickerFont& font, const std::string& text, int x, int y, std::uint8_t colour) {
    int at = x;
    for (const unsigned char c : text) {
        const int index = int(c) - font.first;
        if (index < 0 || index >= int(font.widths.size())) continue; // (outside the font: skipped)
        const int w = font.widths[std::size_t(index)];
        if (w == 0) {
            at += 0xA + 1;
            continue;
        }
        const int words = (w + 15) >> 4;
        const auto& glyph = font.glyphs[std::size_t(index)];
        for (int row = 0; row < font.height; ++row) {
            for (int k = 0; k < words; ++k) {
                const std::uint16_t bits = glyph[std::size_t(row * words + k)];
                for (int b = 0; b < 16; ++b) {
                    if ((bits >> (15 - b) & 1) == 0) continue;
                    const int px = at + k * 16 + b, py = y + row;
                    if (px < canvas.clip.left || px >= canvas.clip.right || py < canvas.clip.top || py >= canvas.clip.bottom) continue;
                    canvas.pixels[std::size_t(py * canvas.width + px)] |= colour;
                }
            }
        }
        at += w + 1;
    }
    return at - x;
}

IndexedCanvas draw_briefing(const data::Assets& assets, const TickerFont& font, const BriefingTables& t, int rank,
                            int mission, int islands, int ships) {
    IndexedCanvas canvas(BriefingScreen::width, BriefingScreen::height);
    for (const auto& library : assets.libraries) {
        if (library.name != "world.shp") continue;
        if (const auto* panel = library.library.find("rank")) {
            canvas.draw(*panel, panel_centre_x - panel->bytes_per_row * 4, panel_centre_y - panel->height / 2);
        }
    }
    const std::string name = rank >= 0 && std::size_t(rank) < t.rank_names.size() ? t.rank_names[std::size_t(rank)] : "";
    draw_text(canvas, font, name, rank_x, rank_y, text_colour);
    draw_text(canvas, font, std::to_string(mission), values_x, mission_y, text_colour);
    draw_text(canvas, font, std::to_string(islands), values_x, islands_y, text_colour);
    draw_text(canvas, font, std::to_string(ships), values_x, ships_y, text_colour);
    return canvas;
}

} // namespace wings::game
