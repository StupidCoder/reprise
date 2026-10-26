#include "bruce_lee_c64/data/assets.hpp"
#include "c64/character_rom.hpp"
#include "platform/files.hpp"
#include <algorithm>
#include <stdexcept>

namespace bruce_lee::data {
namespace {

// Tables in the game file (addresses as the game reads them).
constexpr std::uint16_t charset_sources = 0x9000;  // 4 x 1 KiB, expanded by $487F
constexpr std::uint16_t play_colour_table = 0x4788; // $D020-$D023, set at $07C0
constexpr std::uint16_t room_colours = 0x478D;     // colour RAM fill per room ($09CE)
constexpr std::uint16_t room_headers = 0x4AB5;     // word per room -> 20 bytes ($0A79)
constexpr std::uint16_t room_maps_lo = 0x4B41;     // map pointer low bytes ($0AA1)
constexpr std::uint16_t room_maps_hi = 0x4B56;     // map pointer high bytes ($0AA6)
constexpr std::uint16_t room_band_colours = 0x47F8; // word per room -> 12 colours ($48F7)
constexpr std::uint16_t room_band_lines = 0x4820;   // word per room -> raster lines, $FD ends ($4901)
constexpr std::uint16_t room_lanterns = 0x4CEF;     // word per room -> 4-byte records ($335F)
constexpr std::uint16_t string_table = 0x43AB;      // word per string ($1803)
constexpr std::uint8_t last_split = 0xFD;           // the bottom interrupt's line ($4978)
constexpr std::uint16_t pose_lo = 0x59BD, pose_hi = 0x5A8F; // shape address per pose ($2706)
constexpr std::uint16_t pose_rows = 0x5B61, pose_colour = 0x5C33, pose_wide = 0x5CBF;
constexpr std::uint16_t mask_hi = 0x1251;           // non-zero: the pose has its own mask ($5148)
constexpr std::uint16_t move_delay_table = 0x4A99;  // per move code ($1FA7); ends at the header table
constexpr std::uint16_t pose_base_table = 0x59A5;   // per character kind ($29A0)
constexpr std::uint16_t exit_rooms = 0x4B6B, exit_x = 0x4C0B, exit_y = 0x4C1F; // type 2 ($2F30)
constexpr std::uint16_t lantern_counts = 0x4CDB;    // $10BC
constexpr std::uint16_t lift_pattern_table = 0x42DE; // $2C7F
constexpr std::uint16_t title_charset_address = 0x8000; // $D018 = $30
constexpr std::uint16_t trademark_table = 0x0E34;      // $05D1
constexpr std::uint16_t player_label_table = 0x4A6A;   // $0EE4
constexpr std::uint16_t player_digit_table = 0x4A8A;   // $0ED0
constexpr std::uint16_t game_over_table = 0x42BA;      // $190B
constexpr std::uint16_t version_line_table = 0x46F4;   // $1B8C
constexpr std::uint16_t left_hand_table = 0x4A92;      // $16D2
constexpr std::uint16_t effect_priority_table = 0x255F; // $2507
constexpr std::uint16_t lift_speed_table = 0x4CBF;   // $2C75
constexpr std::uint16_t face_threshold_table = 0x4A96; // $3D33
constexpr std::uint16_t fireball_mask_table = 0x430F;  // $4127
constexpr std::uint16_t yamo_wait_table = 0x1373, ninja_wait_table = 0x1337; // $0D14, $0DD8
constexpr std::uint16_t start_x_table = 0x4BBB, start_y_table = 0x4BCF;     // $0BD9
constexpr std::uint16_t other_x_table = 0x4BE3, other_y_table = 0x4BF7;     // $0C10
constexpr std::uint16_t yamo_place_table = 0x1387, ninja_place_table = 0x134B; // $0BF6, $0C1E
constexpr std::uint16_t quiet_table = 0x4CAB;       // $0A33, $1C30
constexpr std::uint16_t ninja_entry_table = 0x1323, yamo_entry_table = 0x135F; // $0A40
constexpr std::uint16_t first_extra_life_table = 0x4378; // $0FC2, three bytes high first
constexpr std::uint16_t demo_start = 0xF300;        // $0574/$0745: $1D/$1E = $F300
constexpr std::uint16_t sprite_block_table = 0x2750; // $267A, then $275A first sprites
constexpr std::uint16_t sprite_first = 0x59A8, sprite_second = 0x59AB, sprite_slots = 0x59AE; // $0B8C, $0E19

// $487F: per 8 stored bytes, rows 0-3 doubled make character k, rows 4-7
// doubled make character k + 128.
Charset expand_charset(const GameImage& image, std::uint16_t source) {
    Charset charset{};
    for (int k = 0; k < 128; ++k) {
        const auto stored = image.bytes(std::uint16_t(source + 8 * k), 8);
        for (int r = 0; r < 4; ++r) {
            auto& top = charset[std::size_t(k)];
            auto& bottom = charset[std::size_t(k + 128)];
            top[std::size_t(2 * r)] = top[std::size_t(2 * r + 1)] = stored[std::size_t(r)];
            bottom[std::size_t(2 * r)] = bottom[std::size_t(2 * r + 1)] = stored[std::size_t(4 + r)];
        }
    }
    return charset;
}

// $0443-$0477: ROM $D100 (screen codes $20-$3F) to $8800 and its inverse to
// $8A00, ROM $D000 (codes $00-$1F) to $8900 and its inverse to $8B00; then
// $8900-$8907 = $FF, $8A10-$8A17 = $03, $8AB0-$8AB7 = $C0.
TextCharset build_text_charset() {
    TextCharset text{};
    for (int code = 0; code < 64; ++code) {
        const auto& glyph = c64::character_rom_upper[std::size_t((code + 0x20) & 0x3F)];
        for (int row = 0; row < 8; ++row) {
            text[std::size_t(code)][std::size_t(row)] = glyph[std::size_t(row)];
            text[std::size_t(code + 64)][std::size_t(row)] = std::uint8_t(~glyph[std::size_t(row)]);
        }
    }
    text[0x20].fill(0xFF);
    text[0x42].fill(0x03);
    text[0x56].fill(0xC0);
    return text;
}

// $0A8D: rooms 0-9 use the charset at $A000, room 18 the one at $B000, the
// others the one at $A800.
int room_charset(int room) {
    return room < 10 ? 0 : room == 18 ? 2 : 1;
}

// $0AC7: the map stream. A byte with bit 7 set starts a literal run of
// (byte & $7F) cells ($80 ends the room); a byte with bit 7 clear repeats
// the next byte that many times (0 = 256). Cells run 40 per row.
// $48F3-$491C and $4970-$49C7: three colours for the top part, then per
// raster line of the list one more set of three.
void decode_bands(const GameImage& image, int room, Room& result) {
    const auto colours = image.word(std::uint16_t(room_band_colours + 2 * room),
                                    std::uint16_t(room_band_colours + 2 * room + 1));
    const auto lines = image.word(std::uint16_t(room_band_lines + 2 * room), std::uint16_t(room_band_lines + 2 * room + 1));
    for (int i = 0; i < 3; ++i) {
        result.top_colours[std::size_t(i)] = image.byte(std::uint16_t(colours + i));
    }
    for (int band = 0;; ++band) {
        const int line = image.byte(std::uint16_t(lines + band));
        if (line == last_split) {
            break;
        }
        if (band == 3) {
            throw std::runtime_error("A room's colour splits in the game file do not end.");
        }
        ColourBand b{line, {}};
        for (int i = 0; i < 3; ++i) {
            b.colours[std::size_t(i)] = image.byte(std::uint16_t(colours + 3 + 3 * band + i));
        }
        result.bands.push_back(b);
    }
}

// $335B: records of state, character, column, row until a byte with bit 7;
// the row is a row-table index (screen row 2 * index - 1, $085B).
void decode_lanterns(const GameImage& image, int room, Room& result) {
    auto at = image.word(std::uint16_t(room_lanterns + 2 * room), std::uint16_t(room_lanterns + 2 * room + 1));
    for (int guard = 0; !(image.byte(at) & 0x80); ++guard, at = std::uint16_t(at + 4)) {
        if (guard == 64) {
            throw std::runtime_error("A room's lantern list in the game file does not end.");
        }
        const auto record = image.bytes(at, 4);
        if (record[1] == 0) {
            continue; // rooms without lanterns hold one empty record
        }
        result.lanterns.push_back({record[2], 2 * record[3] - 1, std::uint8_t(record[1] & 0x7F), at});
    }
}

// $5208: four bits of a sprite byte, lowest first, each doubled into a
// pixel pair; the first bit ends up rightmost.
std::uint8_t double_bits(unsigned bits) {
    std::uint8_t out = 0;
    for (int i = 3; i >= 0; --i) {
        out = std::uint8_t(out << 2 | (bits >> i & 1 ? 3 : 0));
    }
    return out;
}

// $5187: a pose's shape as a collision mask: the row's first byte, low
// nibble to column 1, high nibble to column 0. For wide rows the code loads
// the second byte for column 2 but doubles the bits of the first byte's
// already emptied copy ($51F4-$51FA: $5208 works on $1F, not on A), so
// column 2 stays empty and wide poses collide only with their left 16
// pixels. Kept: it decides what the actors touch.
data::CollisionMask pose_mask(const Pose& pose) {
    CollisionMask mask{};
    for (int row = 0; row <= pose.row_count && row < 32 && !pose.rows.empty(); ++row) {
        const auto first = pose.rows[std::size_t(pose.wide ? 2 * row : row)];
        mask[std::size_t(32 + row)] = double_bits(first & 15);
        mask[std::size_t(row)] = double_bits(first >> 4);
    }
    return mask;
}

Room decode_room(const GameImage& image, int room) {
    Room result;
    result.map_address = image.word(std::uint16_t(room_maps_lo + room), std::uint16_t(room_maps_hi + room));
    result.header_address = image.word(std::uint16_t(room_headers + 2 * room), std::uint16_t(room_headers + 2 * room + 1));
    const auto header = image.bytes(result.header_address, result.header.size());
    std::copy(header.begin(), header.end(), result.header.begin());
    decode_bands(image, room, result);
    // $2F29-$2F78: each exit type's room table is 20 bytes after the last,
    // its x and y tables 40 bytes.
    for (int type = 0; type < 4; ++type) {
        result.exits[std::size_t(type)] = {image.byte(std::uint16_t(exit_rooms + 20 * type + room)),
                                           image.byte(std::uint16_t(exit_x + 40 * type + room)),
                                           image.byte(std::uint16_t(exit_y + 40 * type + room))};
    }
    decode_lanterns(image, room, result);
    result.charset = room_charset(room);
    result.colour = image.byte(std::uint16_t(room_colours + room));

    std::size_t written = 0;
    auto put = [&](std::uint8_t cell) {
        if (written < result.cells.size()) {
            result.cells[written] = cell;
        } else {
            ++result.spill;
        }
        ++written;
    };
    std::uint16_t at = result.map_address;
    while (true) {
        const std::uint8_t code = image.byte(at++);
        if (code & 0x80) {
            const int count = code & 0x7F;
            if (count == 0) {
                break;
            }
            for (int i = 0; i < count; ++i) {
                put(image.byte(at++));
            }
        } else {
            const std::uint8_t cell = image.byte(at++);
            for (int i = 0; i < (code ? code : 256); ++i) {
                put(cell);
            }
        }
        if (written > 4096) {
            throw std::runtime_error("A room map in the game file does not end.");
        }
    }
    if (written < result.cells.size()) {
        throw std::runtime_error("A room map in the game file is too short.");
    }
    return result;
}

c64::Program read_program(const std::filesystem::path& file, std::uint16_t load, std::size_t size) {
    auto program = c64::Program::from_prg(platform::read_file(file));
    if (program.load_address != load || program.bytes.size() != size) {
        throw std::runtime_error("The installed file " + file.filename().string() +
                                 " is not the expected one; please import the disk again.");
    }
    return program;
}

} // namespace

GameImage::GameImage(c64::Program game, c64::Program high) : game_(std::move(game)), high_(std::move(high)) {}

std::uint8_t GameImage::byte(std::uint16_t address) const {
    return bytes(address, 1)[0];
}

std::span<const std::uint8_t> GameImage::bytes(std::uint16_t address, std::size_t count) const {
    for (const auto* program : {&game_, &high_}) {
        if (program->contains(address) && address - program->load_address + count <= program->bytes.size()) {
            return std::span(program->bytes).subspan(address - program->load_address, count);
        }
    }
    throw std::out_of_range("Bruce Lee data read outside the game files");
}

Assets decode_assets(const GameImage& image, std::span<const std::uint8_t> loading_bitmap) {
    Assets assets;
    for (int set = 0; set < 4; ++set) {
        assets.charsets[std::size_t(set)] = expand_charset(image, std::uint16_t(charset_sources + 0x400 * set));
    }
    assets.text = build_text_charset();
    // The header table runs up to the first header: one entry per room.
    const int room_count = (image.word(room_headers, room_headers + 1) - room_headers) / 2;
    for (int room = 0; room < room_count; ++room) {
        assets.rooms.push_back(decode_room(image, room));
    }
    // The string table runs up to the first string.
    std::uint16_t first_string = 0xFFFF;
    for (std::uint16_t at = string_table; at < first_string; at = std::uint16_t(at + 2)) {
        const auto address = image.word(at, std::uint16_t(at + 1));
        first_string = std::min(first_string, address);
        Text text;
        for (std::uint16_t c = address; image.byte(c) != 0xA0; ++c) {
            text.push_back(std::uint8_t(image.byte(c) - 0x20));
            if (text.size() > 80) {
                throw std::runtime_error("A string in the game file does not end.");
            }
        }
        assets.strings.push_back(std::move(text));
    }
    // Poses: the shape table runs up to the high-byte table.
    for (int p = 0; p < pose_hi - pose_lo; ++p) {
        Pose pose;
        pose.row_count = image.byte(std::uint16_t(pose_rows + p));
        pose.wide = image.byte(std::uint16_t(pose_wide + p)) != 0;
        pose.colour = image.byte(std::uint16_t(pose_colour + p));
        const auto address = image.word(std::uint16_t(pose_lo + p), std::uint16_t(pose_hi + p));
        if (address != 0) { // the Ninja's poses have no second layer (address 0)
            const auto bytes = image.bytes(address, std::size_t(pose.row_count + 1) * (pose.wide ? 2 : 1));
            pose.rows.assign(bytes.begin(), bytes.end());
        }
        assets.poses.push_back(std::move(pose));
    }
    // $5144: from the last pose down in pairs; an odd pose with a mask of its
    // own is ORed into its even partner's.
    for (std::size_t p = 0; p < assets.poses.size(); ++p) {
        assets.masks.push_back(pose_mask(assets.poses[p]));
    }
    for (std::size_t p = 0; p + 1 < assets.poses.size(); p += 2) {
        if (image.byte(std::uint16_t(mask_hi + p + 1)) != 0) {
            for (std::size_t i = 0; i < 96; ++i) {
                assets.masks[p][i] |= assets.masks[p + 1][i];
            }
        }
    }
    for (int r = 0; r < room_count; ++r) {
        const auto at = [&](std::uint16_t table) { return image.byte(std::uint16_t(table + r)); };
        assets.lanterns_per_room.push_back(at(lantern_counts));
        assets.yamo_waits.push_back(at(yamo_wait_table));
        assets.ninja_waits.push_back(at(ninja_wait_table));
        assets.start_x.push_back(at(start_x_table));
        assets.start_y.push_back(at(start_y_table));
        assets.other_x.push_back(at(other_x_table));
        assets.other_y.push_back(at(other_y_table));
        assets.yamo_places.push_back(at(yamo_place_table));
        assets.ninja_places.push_back(at(ninja_place_table));
        assets.quiet_rooms.push_back(at(quiet_table) != 0);
        assets.ninja_entry_waits.push_back(at(ninja_entry_table));
        assets.yamo_entry_waits.push_back(at(yamo_entry_table));
    }
    // The recording runs to the end of memory (the pointer is not checked;
    // the attract mode ends long before).
    const auto demo = image.bytes(demo_start, 0x10000 - demo_start);
    assets.demo_input.assign(demo.begin(), demo.end());
    const auto three_bytes = [&](std::uint16_t at) {
        return std::uint32_t(image.byte(at)) << 16 | std::uint32_t(image.byte(std::uint16_t(at + 1))) << 8 |
               image.byte(std::uint16_t(at + 2));
    };
    assets.first_extra_life = three_bytes(first_extra_life_table);
    assets.extra_life_step = three_bytes(first_extra_life_table + 3);
    for (int k = 0; k < 3; ++k) {
        assets.actor_sprites[std::size_t(k)] = {image.byte(std::uint16_t(sprite_first + k)),
                                                image.byte(std::uint16_t(sprite_second + k))};
    }
    for (int i = 0; i < 5; ++i) {
        assets.sprite_slot_of_code[std::size_t(i)] = image.byte(std::uint16_t(sprite_slots + i));
    }
    for (std::size_t i = 0; i < 10; ++i) {
        assets.sprite_blocks[i] = image.byte(std::uint16_t(sprite_block_table + i));
    }
    for (std::size_t i = 0; i < 3; ++i) {
        assets.first_sprite[i] = image.byte(std::uint16_t(sprite_block_table + 10 + i));
    }
    for (std::uint16_t a = move_delay_table; a < room_headers; ++a) {
        assets.move_delays.push_back(image.byte(a));
    }
    for (int i = 0; i < 4; ++i) {
        assets.pose_base[std::size_t(i)] = image.byte(std::uint16_t(pose_base_table + i));
    }
    for (int i = 0; i < 4; ++i) {
        assets.play_colours[std::size_t(i)] = image.byte(std::uint16_t(play_colour_table + i));
    }
    for (std::size_t set = 0; set < 2; ++set) {
        for (std::size_t i = 0; i < 24; ++i) {
            assets.lift_patterns[set][i] = image.byte(std::uint16_t(lift_pattern_table + 24 * set + i));
        }
    }
    for (std::size_t i = 0; i < 8; ++i) {
        assets.lift_speeds[i] = image.byte(std::uint16_t(lift_speed_table + i));
    }
    for (std::size_t i = 0; i < 3; ++i) {
        assets.face_thresholds[i] = image.byte(std::uint16_t(face_threshold_table + i));
    }
    for (std::size_t i = 0; i < 4; ++i) {
        assets.fireball_masks[i] = image.byte(std::uint16_t(fireball_mask_table + i));
    }
    for (std::size_t g = 0; g < 256; ++g) {
        for (std::size_t r = 0; r < 8; ++r) {
            assets.title_charset[g][r] = image.byte(std::uint16_t(title_charset_address + 8 * g + r));
        }
    }
    const std::array<std::uint16_t, 4> title_row_tables{0x4A1A, 0x49CA, 0x49F2, 0x4A42};
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 40; ++c) {
            assets.title_rows[r][c] = image.byte(std::uint16_t(title_row_tables[r] + c));
        }
    }
    for (std::size_t i = 0; i < 12; ++i) {
        assets.trademark[i] = image.byte(std::uint16_t(trademark_table + i));
    }
    for (std::size_t i = 0; i < 16; ++i) {
        assets.player_label[0][i] = image.byte(std::uint16_t(player_label_table + i));
        assets.player_label[1][i] = image.byte(std::uint16_t(player_label_table + 16 + i));
    }
    for (std::size_t p = 0; p < 2; ++p) {
        for (std::size_t i = 0; i < 2; ++i) {
            assets.player_digit[p][i] = image.byte(std::uint16_t(player_digit_table + 2 * p + i));
            assets.player_digit[p][2 + i] = image.byte(std::uint16_t(player_digit_table + 4 + 2 * p + i));
        }
    }
    for (std::size_t i = 0; i < 18; ++i) {
        assets.game_over_label[0][i] = image.byte(std::uint16_t(game_over_table + i));
        assets.game_over_label[1][i] = image.byte(std::uint16_t(game_over_table + 18 + i));
    }
    for (std::size_t i = 0; i < 40; ++i) {
        assets.version_line[i] = image.byte(std::uint16_t(version_line_table + i));
    }
    for (std::size_t i = 0; i < 4; ++i) {
        assets.left_hand_bits[i] = image.byte(std::uint16_t(left_hand_table + i));
    }
    for (std::size_t i = 0; i < 16; ++i) {
        assets.effect_priorities[i] = image.byte(std::uint16_t(effect_priority_table + i));
    }
    const auto sound = image.bytes(0xE000, 0x2000);
    assets.sound_block.assign(sound.begin(), sound.end());
    if (loading_bitmap.size() < assets.loading.bitmap.size()) {
        throw std::runtime_error("The loading picture is too short.");
    }
    std::copy_n(loading_bitmap.begin(), assets.loading.bitmap.size(), assets.loading.bitmap.begin());
    return assets;
}

GameImage load_game_image(const std::filesystem::path& content) {
    return {read_program(content / "game.prg", 0x0400, 0xA000), read_program(content / "high.prg", 0xE000, 0x2000)};
}

Assets load_assets(const std::filesystem::path& content) {
    const auto loading = read_program(content / "loading.prg", 0x6000, 0x2000);
    return decode_assets(load_game_image(content), loading.bytes);
}

} // namespace bruce_lee::data
