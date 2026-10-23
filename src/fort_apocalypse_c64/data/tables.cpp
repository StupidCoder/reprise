#include "fort_apocalypse_c64/data/tables.hpp"
#include "fort_apocalypse_c64/data/game_file.hpp"
#include <algorithm>
#include <stdexcept>

namespace fort_apocalypse::data {
namespace {
template <std::size_t N>
std::array<std::uint8_t, N> bytes(const GameFile& file, std::uint16_t address) {
    std::array<std::uint8_t, N> out{};
    std::ranges::copy(file.bytes(address, N), out.begin());
    return out;
}

Text text(const GameFile& file, std::uint16_t address) {
    Text result;
    for (std::uint8_t b; (b = file.byte(address++)) != 0xFF;) {
        result.push_back(b);
        if (result.size() > 40) {
            throw std::runtime_error("A Fort Apocalypse text has no end mark.");
        }
    }
    return result;
}
} // namespace

GameTables decode_tables(const c64::Program& program) {
    const GameFile file(program);
    GameTables t;
    auto& d = t.difficulty;
    d.gravity_mask = bytes<3>(file, 0x8DFC);
    d.enemy_mask = bytes<3>(file, 0x8DFE);
    d.lives = bytes<3>(file, 0x8E01);
    d.barrier_step = bytes<3>(file, 0x8E04);
    d.last_mine = bytes<3>(file, 0x8E07);
    d.tank_period = bytes<3>(file, 0x8E0A);
    d.missile_period = bytes<3>(file, 0x8E0B);
    d.sweep_period = bytes<3>(file, 0x8E0E);
    const auto adjust = bytes<3>(file, 0x916B);
    for (int i = 0; i < 3; ++i) {
        d.rank_adjust[std::size_t(i)] = std::int8_t(adjust[std::size_t(i)]);
    }

    for (int l = 0; l < 3; ++l) {
        auto& s = t.levels[std::size_t(l)];
        s.multicolour = file.byte(std::uint16_t(0x9107 + l));
        s.sprite_x = file.byte(std::uint16_t(0x910A + 2 * l));
        s.sprite_y = file.byte(std::uint16_t(0x910B + 2 * l));
        s.camera_column = file.byte(std::uint16_t(0x9110 + 2 * l));
        s.camera_row = file.byte(std::uint16_t(0x9111 + 2 * l));
        // $8F57: level 1 has its own tank homes, levels 0 and 2 share.
        const std::uint16_t columns = l == 1 ? 0x9128 : 0x911C, rows = l == 1 ? 0x912E : 0x9122;
        s.tank_column = bytes<6>(file, columns);
        s.tank_row = bytes<6>(file, rows);
        s.enemy_colour = file.byte(std::uint16_t(0x9C4F + l));
    }
    t.enemy_column = bytes<16>(file, 0x9CBA);
    t.enemy_row = bytes<16>(file, 0x9CCA);
    for (int i = 0; i < 4; ++i) {
        auto& p = t.drop_points[std::size_t(i)];
        p.fine_x = file.byte(std::uint16_t(0x98DA + i));
        p.fine_y = file.byte(std::uint16_t(0x98DE + i));
        p.camera_column = file.byte(std::uint16_t(0x98E2 + i));
        p.camera_row = file.byte(std::uint16_t(0x98E6 + i));
        p.sprite_x = file.byte(std::uint16_t(0x98EA + i));
        p.sprite_y = file.byte(std::uint16_t(0x98EE + i));
    }

    t.barrier_pattern = bytes<32>(file, 0x8907);
    t.barrier_cap_a = bytes<8>(file, 0x891F);
    t.barrier_cap_b = bytes<8>(file, 0x8917);
    t.static_pattern = bytes<8>(file, 0xA927);
    t.shimmer = bytes<32>(file, 0xAF80);

    t.mine_characters = bytes<8>(file, 0x963C);
    t.tank_characters = bytes<3>(file, 0xA469);
    t.obstacles = bytes<22>(file, 0xA45D);
    t.prisoner_characters = bytes<8>(file, 0xAC12);
    t.landing_characters = bytes<5>(file, 0xAC16);
    t.teleport_barriers = bytes<4>(file, 0xB264);

    const auto dx = bytes<6>(file, 0xA472), dy = bytes<6>(file, 0xA477);
    for (int i = 0; i < 6; ++i) {
        t.bullet_dx[std::size_t(i)] = std::int8_t(dx[std::size_t(i)]);
        t.bullet_dy[std::size_t(i)] = std::int8_t(dy[std::size_t(i)]);
    }
    t.radar_mask = bytes<8>(file, 0xADB5);
    t.attract_joystick = bytes<108>(file, 0xAA4F);

    for (int i = 0; i < 4; ++i) {
        t.fort_explosion[std::size_t(i)] = bytes<6>(file, file.word(std::uint16_t(0x988A + 2 * i)));
    }
    t.depot = bytes<54>(file, 0xACF8);

    t.entering = text(file, 0x9134);
    t.vaults = text(file, 0x913D);
    t.crystalline = text(file, 0x9152);
    t.get_ready = text(file, 0x8EBE);
    t.pilots_left = text(file, 0x8ED0);
    t.men_to_rescue = text(file, 0xAC1B);
    t.low_on_fuel = text(file, 0xAC79);
    t.high_score = text(file, 0x92D6);
    t.mission = text(file, 0x9285);
    t.aborted = text(file, 0x928D);
    t.completed = text(file, 0x9295);
    t.your_rank = text(file, 0x929F);
    t.class_text = text(file, 0x92AE);
    for (int i = 0; i < 4; ++i) {
        t.birds[std::size_t(i)] = text(file, file.word(std::uint16_t(0x92B4 + 2 * i)));
    }
    return t;
}

} // namespace fort_apocalypse::data
