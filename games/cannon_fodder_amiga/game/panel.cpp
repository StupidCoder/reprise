#include "cannon_fodder_amiga/game/panel.hpp"
#include "amiga/adf.hpp"
#include <algorithm>
#include <stdexcept>

namespace fodder::game {
namespace {

constexpr std::size_t file_offset(std::uint32_t address) {
    return address - 0x80000 + 0x1a; // program body starts 26 bytes into the file
}

constexpr std::uint32_t piece_table = 0x89658;
constexpr std::uint32_t recruit_names = 0x917e0;  // 9 bytes per recruit: 6 letters, 3 more bytes
constexpr std::uint32_t squad_bars = 0x89020;      // opaque bar piece per squad
constexpr std::uint32_t icons_selected = 0x8919a;  // squad icon pieces, selected squad
constexpr std::uint32_t icons_other = 0x891b0;     // squad icon pieces, other squads
constexpr std::uint32_t name_fonts = 0x8a0dc;      // first letter piece per name colour
constexpr std::uint32_t name_length = 0x85b0e;     // last letter index looked at

class Canvas {
public:
    Canvas(const PanelArt& art) : art_(art), pixels_(std::size_t(panel_width * panel_height)) {}

    // $895BC/$894F0 with $9C65A: whole 16-pixel words, every pixel.
    void opaque(int number, int x, int y) {
        const auto p = art_.piece(number);
        const int source_x = p.x & ~15;
        for (int word = 0; word * 16 < p.width; ++word) {
            const int column = std::min(x / 16 + word, 2);
            for (int line = 0; line < p.lines; ++line) {
                for (int i = 0; i < 16; ++i) {
                    put(column * 16 + i, y + line, source(source_x + word * 16 + i, p.y + line));
                }
            }
        }
    }

    // $8A0E0/$8A1D0: each source word shifted right by x & 15, OR'd in
    // where the source colour is not 0.
    void masked(int number, int x, int y) {
        const auto p = art_.piece(number);
        const int source_x = p.x & ~15;
        for (int word = 0; word * 16 < p.width; ++word) {
            const int left = (x / 16 + word) * 16 + (x & 15);
            for (int line = 0; line < p.lines; ++line) {
                for (int i = 0; i < 16; ++i) {
                    const int colour = source(source_x + word * 16 + i, p.y + line);
                    if (colour != 0) {
                        put(left + i, y + line, colour);
                    }
                }
            }
        }
    }

    void fill(int colour) { std::fill(pixels_.begin(), pixels_.end(), std::uint8_t(colour)); }

    std::vector<std::uint8_t> take() { return std::move(pixels_); }

private:
    int source(int x, int y) const {
        const auto& picture = art_.pstuff();
        if (x < 0 || y < 0 || x >= picture.width || y >= picture.height) {
            return 0;
        }
        return picture.pixels[std::size_t(y * picture.width + x)] & 15;
    }
    void put(int x, int y, int colour) {
        if (x >= 0 && y >= 0 && x < panel_width && y < panel_height) {
            pixels_[std::size_t(y * panel_width + x)] = std::uint8_t(colour);
        }
    }

    const PanelArt& art_;
    std::vector<std::uint8_t> pixels_;
};

// A name centred in the panel, 4 pixels per letter ($8A050).
void draw_name(Canvas& canvas, const PanelArt& art, const std::string& name, int font, int y) {
    const int last = art.word(name_length);
    int length = 0;
    while (length <= last) {
        const char c = length < int(name.size()) ? name[std::size_t(length)] : '\0';
        if ((last == 5 && c == ' ') || c == '\0') {
            break;
        }
        ++length;
    }
    const int left = int(unsigned(48 - length * 4) >> 1);
    for (int i = 0; i <= last && i < int(name.size()); ++i) {
        const char c = name[std::size_t(i)];
        if (c == '\0') {
            break;
        }
        if (c != ' ') {
            canvas.masked(font + (c - 'A'), left + i * 4, y);
        }
    }
}

} // namespace

PanelArt::PanelArt(std::span<const std::uint8_t> program, amiga::IlbmPicture pstuff)
    : program_(program.begin(), program.end()), pstuff_(std::move(pstuff)) {
    if (program_.size() < file_offset(0x917e0) + 9 * 64) {
        throw std::runtime_error("The game program is shorter than expected.");
    }
}

int PanelArt::word(std::uint32_t address) const {
    return std::int16_t(amiga::read_be16(program_, file_offset(address)));
}

int PanelArt::byte(std::uint32_t address) const {
    return program_[file_offset(address)];
}

PanelArt::Piece PanelArt::piece(int number) const {
    const auto at = piece_table + std::uint32_t(number) * 8;
    return {word(at), word(at + 2) & 0xff, word(at + 4), word(at + 6) + 1};
}

std::string PanelArt::recruit_name(int recruit) const {
    std::string name;
    for (int i = 0; i < 6; ++i) {
        name += char(byte(recruit_names + std::uint32_t(recruit) * 9 + std::uint32_t(i)));
    }
    return name;
}

std::vector<std::uint8_t> compose_panel(const PanelArt& art, const PanelState& state) {
    Canvas canvas(art);
    canvas.fill(15); // $88566 → $8936C: every line of all six sprite columns set to $FFFF
    int end = 0; // $821B6: the line after the last squad's last bar
    for (int squad = 0; squad < 3; ++squad) {
        // $88566: squad 0 at line 0, later squads 8 lines below `end`
        // (unchanged by squads without soldiers).
        const int top = squad == 0 || end == 0 ? 0 : end + 8;
        const int count = state.soldiers[std::size_t(squad)];
        if (count == 0) {
            continue;
        }
        const bool selected = squad == state.selected_squad;
        // $89000: the squad's bar.
        canvas.opaque(art.word(squad_bars + std::uint32_t(squad) * 2), 0, top);
        // $89026 / $890BC: grenade and rocket boxes (lit while selected and
        // armed; the counts themselves are open).
        canvas.masked(selected && state.grenades[std::size_t(squad)] > 0 ? 0xc3 : 0xc4, 0, top + 14);
        canvas.masked(selected && state.rockets[std::size_t(squad)] > 0 ? 0xc5 : 0xc6, 0x20, top + 14);
        // $892FC: the counts, centred in 13 pixels under the boxes (digits
        // $AF…, lit $B9… for the weapon the squad has chosen).
        const auto draw_count = [&](int value, int left, bool lit) {
            if (value <= 0) return;
            const std::string digits = std::to_string(std::min(value, 99));
            int width = 0;
            for (const char c : digits) width += art.byte(0xa9c46 + std::uint32_t(c));
            int x = left + ((0xd - width) >> 1);
            for (const char c : digits) {
                canvas.masked((lit ? 0xb9 : 0xaf) + (c - '0'), x, top + 0x19);
                x += art.byte(0xa9c46 + std::uint32_t(c));
            }
        };
        const int w = state.weapon[std::size_t(squad)];
        draw_count(state.grenades[std::size_t(squad)], 0, w == 1 && !state.marking);
        draw_count(state.rockets[std::size_t(squad)], 0x24, w == 3 && !state.marking);
        // $89156: the squad icon.
        const auto icons = selected ? icons_selected : icons_other;
        canvas.masked(art.word(icons + std::uint32_t(state.icon[std::size_t(squad)]) * 2), 12, top + 14);
        // $891C8: a bar per soldier, then rank chevrons and names.
        for (int k = 0; k < count; ++k) {
            canvas.opaque(selected ? 0xa9 : 0xab, 0, top + 0x22 + 12 * k);
        }
        end = top + 0x22 + 12 * count;
        int line = top + 0x23;
        for (const auto& soldier : state.roster) {
            if (soldier.squad != squad || !soldier.shown) {
                continue;
            }
            canvas.masked(soldier.rank + 9, 0, line);
            canvas.masked(soldier.rank + 9, 0x23, line);
            const int colour = selected ? (soldier.name_colour & 1) : 2;
            draw_name(canvas, art, art.recruit_name(soldier.recruit), art.byte(name_fonts + std::uint32_t(colour)),
                      line + 2);
            line += 12;
        }
    }
    if (state.globe) {
        canvas.opaque(0xd0, 0, 0xd5); // $88472
    }
    return canvas.take();
}

std::vector<std::uint8_t> compose_hill_column(const PanelArt& art, const HillColumn& column) {
    Canvas canvas(art);
    canvas.fill(15);
    const auto number = [&](int value, int y) { // $A65A0: digits $43… 4 apart, right to 48
        const std::string digits = std::to_string(std::clamp(value, 0, 999));
        int x = 0x30 - 4 * int(digits.size());
        for (const char c : digits) {
            canvas.masked(0x13 + c, x, y);
            x += 4;
        }
    };
    // $A63BA: the HEROES label and five bars (lit where the hall of fame has
    // someone), the SQUAD label and eight bars (lit for the recruits going).
    canvas.opaque(0xad, 0, 0);
    const int famous = int(column.fame.size());
    for (int k = 0, y = 0xe; y < 0x4a; ++k, y += 0xc) canvas.opaque(k < famous ? 0xa9 : 0xab, 0, y);
    canvas.opaque(0xae, 0, 0x4a);
    const int going = int(column.squad.size());
    for (int k = 0, y = 0x58; y < 0xb8; ++k, y += 0xc) canvas.opaque(k < going ? 0xa9 : 0xab, 0, y);
    // $89D90 / $9DEF6 with $81FE2 = 15: each recruit's rank, name and kills.
    int y = 0x58;
    for (const auto& r : column.squad) {
        canvas.masked(r.rank + 9, 0, y);
        draw_name(canvas, art, art.recruit_name(r.recruit), art.byte(name_fonts), y + 2);
        number(r.kills, y + 2);
        y += 0xc;
    }
    // $A64A8 with $81FE2 = -59: the hall of fame's names and kills.
    y = 0xe;
    for (const auto& f : column.fame) {
        canvas.masked(f.rank + 9, 0, y);
        draw_name(canvas, art, art.recruit_name(f.recruit), art.byte(name_fonts), y + 2);
        number(f.kills, y + 2);
        y += 0xc;
    }
    return canvas.take();
}

} // namespace fodder::game

