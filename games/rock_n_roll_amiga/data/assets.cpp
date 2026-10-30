#include "rock_n_roll_amiga/data/assets.hpp"
#include "amiga/adf.hpp"
#include "platform/files.hpp"
#include "rock_n_roll_amiga/import/disk_import.hpp"
#include <algorithm>
#include <stdexcept>

namespace rocknroll::data {
namespace {

// Program tables (addresses in the running program; game.md "Program
// tables").
constexpr std::uint32_t world_table = 0x70E94;   // 8 worlds x 4 longs: graphics, extra, songs, samples
constexpr int world_count = 8;
constexpr std::uint32_t level_table = 0x7855E;   // per level: long file, long world ($6C6A8)
constexpr std::uint32_t level_names = 0x6AEFC;   // 16 bytes each: 14 characters, $FF, $00
constexpr std::uint32_t level_name_size = 16;

// Level file layout ($73B88 = file +$3C8 rows, $73B8A columns, cells $73BA8).
constexpr std::size_t level_rows_at = 0x3C8;
constexpr std::size_t level_columns_at = 0x3CA;

// The graphics bank: world file rows 0-336, program graphics rows 344-615
// ($29CC0-$37140; rows 616-621 hold bytes that do not look like pictures and
// row 630 on is the TFMX player, so the bank stops at 616 — extent observed,
// not yet read from a routine).
constexpr int bank_rows = 616;

// The file that holds the ending picture ("YOU MADE IT"), loaded like a
// world's graphics to $18F46 (load table $78136).
constexpr int ending_file = 0x41;
constexpr int name_box_file = 0x31;
constexpr int name_box_rows = 60;

std::vector<std::uint8_t> read(const std::filesystem::path& content, int file) {
    const auto path = content / import::file_name(file);
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("The content folder lacks file " + import::file_name(file) + "; import the disk again.");
    }
    return platform::read_file(path);
}

// Offset of the ILBM's BODY data in the file.
std::size_t body_offset(std::span<const std::uint8_t> file) {
    for (std::size_t at = 12; at + 8 <= file.size();) {
        const std::uint32_t length = amiga::read_be32(file, at + 4);
        if (file[at] == 'B' && file[at + 1] == 'O' && file[at + 2] == 'D' && file[at + 3] == 'Y') {
            return at + 8;
        }
        at += 8 + length + (length & 1);
    }
    throw std::runtime_error("A world's graphics file has no picture body.");
}

// Plane-interleaved rows (5 planes of 40 bytes) as colour indices.
void decode_rows(std::span<const std::uint8_t> source, int rows, int first_row, GraphicsBank& bank) {
    for (int y = 0; y < rows; ++y) {
        const auto row = source.subspan(std::size_t(y) * GraphicsBank::row_bytes, GraphicsBank::row_bytes);
        for (int x = 0; x < GraphicsBank::width; ++x) {
            std::uint8_t index = 0;
            for (int plane = 0; plane < 5; ++plane) {
                if (row[std::size_t(plane * 40 + x / 8)] & (0x80 >> (x & 7))) {
                    index |= std::uint8_t(1 << plane);
                }
            }
            bank.pixels[std::size_t(first_row + y) * GraphicsBank::width + std::size_t(x)] = index;
        }
    }
}

GraphicsBank build_bank(std::span<const std::uint8_t> world_file, const Program& program) {
    // The bank's bytes are the program's, with the world file laid over them
    // so that its BODY starts at row 0 (it fills rows 0-335 and 198 bytes of
    // row 336). Below $1A578 the program has no bytes; the world covers them.
    const std::size_t size = std::size_t(bank_rows) * GraphicsBank::row_bytes;
    std::vector<std::uint8_t> bytes(size, 0);
    for (std::size_t i = 0; i < size; ++i) {
        const std::uint32_t address = GraphicsBank::address + std::uint32_t(i);
        if (program.contains(address)) bytes[i] = program.u8(address);
    }
    const auto from_body = world_file.subspan(body_offset(world_file));
    std::copy_n(from_body.begin(), std::min(from_body.size(), size), bytes.begin());

    GraphicsBank bank;
    bank.rows = bank_rows;
    bank.pixels.assign(std::size_t(bank_rows) * GraphicsBank::width, 0);
    decode_rows(bytes, bank_rows, 0, bank);
    return bank;
}

Level load_level(const std::filesystem::path& content, const Program& program, int number) {
    const std::uint32_t entry = level_table + std::uint32_t(number - 1) * 8;
    Level level;
    level.number = number;
    level.file = int(program.u32(entry));
    level.world = int(program.u32(entry + 4));
    level.name = program.text(level_names + std::uint32_t(number - 1) * level_name_size, 14);
    const auto bytes = read(content, level.file);
    if (bytes.size() < level_cells_at) {
        throw std::runtime_error("A level file is cut short.");
    }
    level.rows = amiga::read_be16(bytes, level_rows_at);
    level.columns = amiga::read_be16(bytes, level_columns_at);
    const std::size_t cells = std::size_t(level.rows) * std::size_t(level.columns);
    if (level.rows == 0 || level.columns == 0 || level_cells_at > bytes.size()) {
        throw std::runtime_error("A level file is malformed.");
    }
    if (level.world < 1 || level.world > 7) {
        throw std::runtime_error("The program's level table names an unknown world.");
    }
    level.header.assign(bytes.begin(), bytes.begin() + std::ptrdiff_t(level_cells_at));
    level.stored_cells = std::min(cells, bytes.size() - level_cells_at);
    level.cells.assign(bytes.begin() + std::ptrdiff_t(level_cells_at),
                       bytes.begin() + std::ptrdiff_t(level_cells_at + level.stored_cells));
    level.padding.assign(bytes.begin() + std::ptrdiff_t(level_cells_at + level.stored_cells), bytes.end());
    // A map can be larger than its file (level 28: 60 x 100 cells in 6,144
    // bytes). The game reads on into whatever lies behind the file in memory:
    // at start-up the program's own image there (all abyss), later the tail of
    // a larger level loaded before.
    const auto behind = program.bytes(level_load_address + std::uint32_t(level_cells_at + level.stored_cells),
                                      std::uint32_t(cells - level.stored_cells));
    level.cells.insert(level.cells.end(), behind.begin(), behind.end());
    return level;
}

} // namespace

Program::Program(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

std::uint8_t Program::u8(std::uint32_t address) const {
    if (!contains(address)) throw std::runtime_error("The program has no byte at a table address.");
    return bytes_[address - base];
}

std::uint16_t Program::u16(std::uint32_t address) const {
    return std::uint16_t(u8(address) << 8 | u8(address + 1));
}

std::uint32_t Program::u32(std::uint32_t address) const {
    return std::uint32_t(u16(address)) << 16 | u16(address + 2);
}

std::span<const std::uint8_t> Program::bytes(std::uint32_t address, std::uint32_t length) const {
    if (!contains(address, length)) throw std::runtime_error("The program is shorter than a table it should hold.");
    return std::span<const std::uint8_t>(bytes_).subspan(address - base, length);
}

std::uint16_t Memory::u16(std::uint32_t address) const {
    const auto b = bytes(address, 2);
    return std::uint16_t(b[0] << 8 | b[1]);
}

std::uint32_t Memory::u32(std::uint32_t address) const {
    return std::uint32_t(u16(address)) << 16 | u16(address + 2);
}

std::span<const std::uint8_t> Memory::bytes(std::uint32_t address, std::uint32_t length) const {
    if (!contains(address, length)) throw std::runtime_error("The intro programs are shorter than a table they should hold.");
    return std::span<const std::uint8_t>(bytes_).subspan(address - base_, length);
}

std::string Program::text(std::uint32_t address, std::uint32_t limit, std::uint8_t terminator) const {
    std::string out;
    for (std::uint32_t i = 0; i < limit; ++i) {
        const auto c = u8(address + i);
        if (c == terminator) break;
        out += char(c);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

bool is_abyss(std::uint8_t cell) {
    return cell == 0x14 || cell == 0x21; // $6C9AA re-points both table entries
}

ScoreTable::Time ScoreTable::record(int level_index) const {
    const auto at = std::size_t(level_index) * 128 + 12; // $6F1A6: $762B8 + level × 128 + $C
    if (at + 4 > bytes.size()) return {0xFFFF, 0xFF};
    const std::uint32_t v = amiga::read_be32(bytes, at);
    return {int((v >> 8) & 0xFFFF), int(v & 0xFF)};
}

ScoreTable::Entry ScoreTable::entry(int table, int place) const {
    const auto at = std::size_t(table) * 128 + std::size_t(place - 1) * 16;
    if (at + 16 > bytes.size()) return {std::string(12, '('), 0};
    return {std::string(bytes.begin() + std::ptrdiff_t(at + 1), bytes.begin() + std::ptrdiff_t(at + 13)),
            amiga::read_be32(bytes, at + 12) & 0xFFFFFF};
}

int ScoreTable::insert(int table, std::string_view name, std::uint32_t value) {
    const bool scores = table == hall_of_fame;
    const int places = scores ? score_places : time_places;
    const auto base = std::size_t(table) * 128;
    if (base + std::size_t(places) * 16 > bytes.size()) return 0;
    value &= 0xFFFFFF;
    int place = 0;
    for (int p = 1; p <= places && place == 0; ++p) {
        const std::uint32_t held = entry(table, p).value;
        if (scores ? value > held : held > value) place = p;
    }
    if (place == 0) return 0;
    for (int p = places - 1; p >= place; --p) { // 15 bytes (name and value) down one place
        const auto from = base + std::size_t(p - 1) * 16 + 1;
        std::copy_n(bytes.begin() + std::ptrdiff_t(from), 15, bytes.begin() + std::ptrdiff_t(from + 16));
    }
    const auto at = base + std::size_t(place - 1) * 16 + 1;
    for (std::size_t i = 0; i < 12; ++i) bytes[at + i] = std::uint8_t(i < name.size() ? name[i] : '(');
    bytes[at + 12] = std::uint8_t(value >> 16);
    bytes[at + 13] = std::uint8_t(value >> 8);
    bytes[at + 14] = std::uint8_t(value);
    return place;
}

int ScoreTable::high_score() const {
    constexpr std::size_t at = 0x772C4 - 0x762B8;
    return at + 4 <= bytes.size() ? int(amiga::read_be32(bytes, at) & 0xFFFFFF) : 0;
}

Assets load_assets(const std::filesystem::path& content) {
    Assets assets;
    const auto main_path = content / "main";
    if (!std::filesystem::exists(main_path)) {
        throw std::runtime_error("The content folder has no program; import the disk again.");
    }
    assets.program = Program(platform::read_file(main_path));
    for (const char* name : {"logo", "intro"}) {
        if (!std::filesystem::exists(content / name)) {
            throw std::runtime_error("This copy of Rock 'n Roll was imported from a disk Reprise no longer supports. "
                                     "Import the 1991 Rainbow Arts 5th Anniversary compilation disk again.");
        }
    }
    assets.logo = Memory(platform::read_file(content / "logo"), logo_base);
    assets.intro = Memory(platform::read_file(content / "intro"), intro_base);
    const auto& program = assets.program;
    assets.scores.bytes = read(content, ScoreTable::file);
    {
        const auto box = read(content, name_box_file);
        if (box.size() < std::size_t(name_box_rows) * GraphicsBank::row_bytes) throw std::runtime_error("The name box file is cut short.");
        assets.name_box.rows = name_box_rows;
        assets.name_box.pixels.assign(std::size_t(name_box_rows) * GraphicsBank::width, 0);
        decode_rows(box, name_box_rows, 0, assets.name_box);
    }

    for (int number = 1; number <= world_count; ++number) {
        const std::uint32_t entry = world_table + std::uint32_t(number - 1) * 16;
        World world;
        world.number = number;
        world.files = {int(program.u32(entry)), int(program.u32(entry + 4)), int(program.u32(entry + 8)),
                       int(program.u32(entry + 12))};
        const auto graphics = read(content, world.files.graphics);
        world.tiles = amiga::read_ilbm(graphics);
        world.bank = build_bank(graphics, program);
        assets.worlds.push_back(std::move(world));
    }

    // The name list holds one 16-byte record per level, each ending $FF $00;
    // the first record that does not is the list's end.
    for (int number = 1;; ++number) {
        const std::uint32_t name = level_names + std::uint32_t(number - 1) * level_name_size;
        if (program.u8(name + 14) != 0xFF || program.u8(name + 15) != 0x00) break;
        assets.levels.push_back(load_level(content, program, number));
    }

    assets.pictures.push_back({"Title", assets.world(8).tiles});
    assets.pictures.push_back({"Ending", amiga::read_ilbm(read(content, ending_file))});

    std::vector<int> music_files;
    for (const auto& world : assets.worlds) {
        for (const int file : {world.files.songs, world.files.samples}) {
            if (std::find(music_files.begin(), music_files.end(), file) == music_files.end()) music_files.push_back(file);
        }
    }
    music_files.push_back(0x40); // title and jukebox songs, copied into place by $70E40
    for (const int file : music_files) {
        const auto bytes = read(content, file);
        const bool song = bytes.size() > 4 && bytes[0] == 'T' && bytes[1] == 'F' && bytes[2] == 'M' && bytes[3] == 'X';
        assets.music.push_back({file, song, bytes.size(), bytes});
    }
    return assets;
}

} // namespace rocknroll::data
