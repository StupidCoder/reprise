#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "platform/files.hpp"
#include <stdexcept>

namespace interceptor::data {
namespace {

// Tables in the program (addresses in the $100000 layout).
constexpr std::uint32_t palettes_at = 0x119FCC;  // hunk 21: ten palettes of 32 colours
constexpr int palette_count = 10;
constexpr std::uint32_t small_font_at = 0x1328B8; // $127402 draws 5 rows (d7 = $142 >> 6)
constexpr std::uint32_t text_font_at = 0x132A24;  // $127B58 draws 7 rows (d7 = $1C2 >> 6)
constexpr std::uint32_t text_columns_at = 0x13635A; // $1278A0 / $127920
constexpr int text_column_count = 46;                // up to the 320-pixel line's end
constexpr std::uint32_t text_table = 0x133FFE;    // $127926
constexpr int text_table_entries = 110;           // up to the next table (hypothesis, see game.md)
constexpr std::uint32_t messages_at = 0x1321C8;   // $127140: 28-byte records
constexpr int message_size = 28;
constexpr int message_count = 54;

struct PictureFile { const char* file; const char* title; };
constexpr PictureFile picture_files[] = {
    {"pix/splsh", "Title picture"},
    {"pix/inst5", "Instrument panel"},
    {"pix/frnt5", "Strip (frnt5)"},
};

// Where the loader puts each sample ($1099CA, $1096BA, $109660) and the
// period it starts with: $145784 gives $166, $109660 sets $B3 for text201.
struct SoundFile { const char* file; const char* title; int slot; int period; };
constexpr SoundFile sound_files[] = {
    {"text/textcpt", "textcpt", 0, 0x166},
    {"text/textegn", "textegn", 2, 0x166},
    {"text/textegn2", "textegn2 (loaded when textegn is missing)", 2, 0x166},
    {"text/texttre", "texttre", 5, 0x166},
    {"text/textwnd", "textwnd", 8, 0x166},
    {"text/textger", "textger", 11, 0x166},
    {"text/texti0a", "Music: texti0a", 13, 0x166},
    {"text/texti0b", "Music: texti0b", 15, 0x166},
    {"text/texti1", "Music: texti1", 17, 0x166},
    {"text/texti2", "Music: texti2", 19, 0x166},
    {"text/texti3", "Music: texti3", 25, 0x166},
    {"text/texti4", "Music: texti4", 27, 0x166},
    {"text/texti5", "Music: texti5", 33, 0x166},
    {"text/text201", "text201", 35, 0xB3},
};

bool printable(const std::string& text) {
    for (char c : text) {
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

} // namespace

amiga::LoadedImage load_program(std::span<const std::uint8_t> load_file) {
    return amiga::LoadedImage(amiga::read_load_file(load_file), program_base);
}

Font read_font(const amiga::LoadedImage& program, std::uint32_t table, int rows) {
    Font font;
    font.rows = rows;
    for (std::uint32_t i = 0; i < 64; ++i) {
        const auto glyph = table + std::uint32_t(program.s16(table + 2 * i));
        for (int r = 0; r < rows; ++r) font.glyphs[i].push_back(program.u8(glyph + std::uint32_t(r)));
    }
    return font;
}

// Block layout ($127920): row, byte column, then lines of
//   flags, colour << 4 | speed, text, 0, delay
// where a flags byte with bit 7 set ends the block ($127BCA).
std::vector<TextBlock> read_text_blocks(const amiga::LoadedImage& program) {
    std::vector<TextBlock> blocks;
    for (int n = 1; n <= text_table_entries; ++n) {
        const auto offset = program.u16(text_table + std::uint32_t(2 * (n - 1)));
        if (offset == 0) continue;
        TextBlock block;
        block.number = n;
        block.address = text_table + offset;
        std::uint32_t at = block.address;
        block.row = program.u8(at++);
        block.column = program.u8(at++);
        bool sane = block.row < 200 && block.column < 40;
        while (sane) {
            const int flags = program.u8(at++);
            if (flags & 0x80) break;
            TextLine line;
            line.flags = flags;
            const int colour = program.u8(at++);
            line.colour = colour >> 4;
            line.speed = colour & 15;
            for (int c; (c = program.u8(at++)) != 0;) line.text.push_back(char(c));
            line.delay = program.u8(at++);
            sane = printable(line.text) && block.lines.size() < 40;
            block.lines.push_back(std::move(line));
        }
        // A few entries point at bytes that are not a block start (they are
        // reached as continuations, hypothesis); they are left out.
        if (sane && !block.lines.empty()) blocks.push_back(std::move(block));
    }
    return blocks;
}

Assets load_assets(const std::filesystem::path& content) {
    Assets assets;
    assets.program = load_program(platform::read_file(content / "program"));
    for (const auto& file : picture_files) {
        assets.pictures.push_back({file.file, file.title, amiga::read_ilbm(platform::read_file(content / file.file))});
    }
    for (const auto& file : sound_files) {
        const auto bytes = platform::read_file(content / file.file);
        Sound sound{file.file, file.title, file.slot, file.period, {}};
        sound.samples.assign(reinterpret_cast<const std::int8_t*>(bytes.data()),
                             reinterpret_cast<const std::int8_t*>(bytes.data()) + bytes.size());
        assets.sounds.push_back(std::move(sound));
    }
    const auto& p = assets.program;
    for (int i = 0; i < palette_count; ++i) {
        Palette palette{};
        for (std::uint32_t c = 0; c < 32; ++c) palette[c] = p.u16(palettes_at + std::uint32_t(i) * 64 + 2 * c);
        assets.palettes.push_back(palette);
    }
    assets.small_font = read_font(p, small_font_at, 5);
    assets.text_font = read_font(p, text_font_at, 7);
    assets.texts = read_text_blocks(p);
    for (int i = 0; i < text_column_count; ++i) {
        const auto at = text_columns_at + std::uint32_t(4 * i);
        assets.text_columns.push_back(p.s16(at) * 8 + (p.u16(at + 2) >> 12));
    }
    for (int i = 0; i < message_count; ++i) {
        // An attribute byte, then 26 characters and a zero (copied as 27 bytes)
        std::string text;
        for (std::uint32_t c = 1; c < message_size - 1; ++c) {
            text.push_back(char(p.u8(messages_at + std::uint32_t(i * message_size) + c)));
        }
        assets.messages.push_back(text);
    }
    assets.config = platform::read_file(content / "config");
    assets.demo_controls = platform::read_file(content / "text/textply");
    assets.demo_keys = platform::read_file(content / "text/textctl");
    return assets;
}

} // namespace interceptor::data
