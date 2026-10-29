#include "cannon_fodder_amiga/game/tables.hpp"
#include "amiga/adf.hpp"
#include <algorithm>
#include <stdexcept>

namespace fodder::game {
namespace {

constexpr std::size_t file_offset(std::uint32_t address) {
    return address - 0x80000 + 0x1a; // program body starts 26 bytes into the file
}

void need(std::span<const std::uint8_t> program, std::uint32_t end) {
    if (program.size() < file_offset(end)) throw std::runtime_error("The game program is shorter than expected.");
}

int word(std::span<const std::uint8_t> program, std::uint32_t address) {
    return std::int16_t(amiga::read_be16(program, file_offset(address)));
}

} // namespace

SoldierTables::SoldierTables(std::span<const std::uint8_t> program) : program_(program.begin(), program.end()) {
    need(program, 0x9c4f8 + 16);
    need(program, 0x955c0 + 16);
    for (std::size_t i = 0; i < 16; ++i) turn_[i] = std::int8_t(program[file_offset(0x955c0) + i]);
    for (std::size_t i = 0; i < 8; ++i) firing_speed_[i] = word(program, 0x9c4f8 + std::uint32_t(i) * 2);
    need(program, 0xa399c + 8);
    for (std::size_t i = 0; i < 8; ++i) cries_[i] = program[file_offset(0xa399c) + i];
    for (std::size_t i = 0; i < 8; ++i) rotor_[i] = word(program, 0x955d0 + std::uint32_t(i) * 2);
}

int SoldierTables::picture(int side, int offset) const {
    // The pointer table holds unrelocated offsets from the program's start.
    const auto pointer = amiga::read_be32(program_, file_offset(0x8dbb2 + std::uint32_t(side) * 4));
    return amiga::read_be16(program_, file_offset(0x80000 + pointer + std::uint32_t(offset)));
}

HitTables::HitTables(std::span<const std::uint8_t> program) : program_(program.begin(), program.end()) {
    need(program, 0x80456 + 2 * 111);
}

int HitTables::word(std::uint32_t table, int kind) const {
    if (kind < 0 || kind >= 111) return 0;
    return game::word(program_, table + std::uint32_t(kind) * 2);
}

FireTables::FireTables(std::span<const std::uint8_t> program) {
    need(program, 0x9e826 + 16 * 10);
    for (std::uint32_t row = 0; row < 16; ++row) {
        const std::uint32_t at = 0x9e826 + row * 10;
        weapons_[row] = {word(program, at), word(program, at + 2), word(program, at + 4), word(program, at + 6)};
    }
    for (std::uint32_t n = 0; n < 8; ++n) {
        // Unrelocated pointers; the lists for 7 and 8 members run on into
        // the next list (the original reads them as one).
        std::uint32_t at = 0x80000 + amiga::read_be32(program, file_offset(0x9e748 + n * 4));
        for (int guard = 0; guard < 64; ++guard, at += 2) {
            const int v = word(program, at);
            orders_[n].push_back(v);
            if (v < 0) break;
        }
    }
}

WeaponStats FireTables::weapons(int rank) const {
    // $9E92A / $9E97E: rank + 8 as a signed byte, at most 15.
    const int row = std::min<int>(std::int8_t(rank + 8), 15);
    return weapons_[std::size_t(std::max(row, 0))];
}

const std::vector<int>& FireTables::firing_order(int members) const {
    return orders_[std::size_t(std::clamp(members, 1, 8) - 1)];
}

BlastTables::BlastTables(std::span<const std::uint8_t> program) {
    need(program, 0xaaf48 + 128);
    for (std::uint32_t i = 0; i < 14; ++i) steps_[i] = {word(program, 0x974ae + i * 4), word(program, 0x974ae + i * 4 + 2)};
    for (std::uint32_t at = 0xaaf48; at < 0xaaf48 + 128 && (word(program, at) & 0xffff) != 0x8000; at += 4) {
        points_.push_back({word(program, at), word(program, at + 2)});
    }
    for (std::uint32_t n = 0; n < 5; ++n) { // unrelocated pointers
        std::uint32_t at = 0x80000 + amiga::read_be32(program, file_offset(0xa4b8c + n * 4));
        for (int guard = 0; guard < 64; ++guard, at += 2) {
            const int v = word(program, at);
            if (v < 0) break;
            immune_[n].push_back(v);
        }
    }
}

bool BlastTables::immune(int landscape, int tile) const {
    if (landscape < 0 || landscape >= 5) return false;
    const auto& list = immune_[std::size_t(landscape)];
    return std::find(list.begin(), list.end(), tile) != list.end();
}

MissionTables::MissionTables(std::span<const std::uint8_t> program) : program_(program.begin(), program.end()) {
    need(program, 0xa4f70 + phases * 4);
    need(program, 0xa4d8e + phases * 4);
    for (std::size_t i = 0; i < 16; ++i) avoid_[i] = program[file_offset(0xa22a8) + i] != 0;
    for (std::size_t i = 0; i < 16; ++i) shelter_[i] = program[file_offset(0xa22b8) + i] != 0;
    need(program, 0xa4c1e + 2 * 24);
    for (std::size_t i = 0; i < mission_phases_.size(); ++i) {
        mission_phases_[i] = amiga::read_be16(program, file_offset(0xa4c1e) + 2 * i);
    }
}

bool MissionTables::last_of_mission(int phase) const {
    int end = 0;
    for (const int n : mission_phases_) {
        end += n;
        if (phase < end) return phase == end - 1;
    }
    return false;
}

std::vector<int> MissionTables::objectives(int phase) const {
    std::vector<int> list;
    if (phase < 0 || phase >= phases) return list;
    std::size_t at = file_offset(0x80000 + amiga::read_be32(program_, file_offset(0xa4d8e + std::uint32_t(phase) * 4)));
    for (; at < program_.size() && program_[at] < 0x80 && list.size() < 16; ++at) list.push_back(program_[at]);
    return list;
}

std::array<int, 2> MissionTables::skill_range(int phase) const {
    if (phase < 0 || phase >= phases) return {0, 0};
    const std::uint32_t at = 0xa4f70 + std::uint32_t(phase) * 4;
    return {word(program_, at), word(program_, at + 2)};
}

} // namespace fodder::game
