#include "cannon_fodder_amiga/game/objects.hpp"
#include "amiga/adf.hpp"
#include <stdexcept>

namespace fodder::game {
namespace {

constexpr std::size_t file_offset(std::uint32_t address) {
    return address - 0x80000 + 0x1a; // program body starts 26 bytes into the file
}

constexpr std::uint32_t frame_table = 0x8dcda;
constexpr int frame_groups = 232; // the group entries follow the table directly
constexpr std::uint32_t army_sheet = 0x57f60, vehicle_sheet = 0x3294a;
constexpr int army_plane = 40 * 336, vehicle_plane = 40 * 337;

constexpr int kind_soldier = 0, kind_enemy = 5, kind_enemy_24 = 0x24;

} // namespace

MissionObjects spawn_objects(const std::vector<data::Placement>& placements) {
    MissionObjects mission;
    int soldiers = 0;
    int delay = 10;
    std::size_t slot = 0;
    for (const auto& placement : placements) {
        if (slot == mission.slots.size()) {
            break; // the original would run on into the end marker (not seen in any map)
        }
        auto& object = mission.slots[slot++];
        object.used = true;
        object.graphic = 0x7c;
        object.squad = soldiers / 8;
        object.x = std::int32_t(std::uint32_t(std::uint16_t(placement.words[2] + 16)) << 16);
        object.y = std::int32_t(std::uint32_t(placement.words[3]) << 16);
        object.home_x = std::int16_t(placement.words[2] + 16);
        object.home_y = std::int16_t(placement.words[3]);
        object.kind = std::int16_t(placement.words[4]);
        if (object.kind == 0x6a || object.kind == 0x48) {
            ++mission.kind_6a_48;
        }
        if (object.kind == kind_soldier) {
            ++soldiers;
            object.start_delay = 0;
            continue;
        }
        if (object.kind == kind_enemy_24) {
            object.side = 1;
            ++mission.enemies;
        } else if (object.kind == kind_enemy) {
            ++mission.enemies;
        }
        delay += 10; // a fresh slot's +$62 is 0, so the reset at $8A62C never applies here
        object.start_delay = delay;
    }
    return mission;
}

FrameTables::FrameTables(std::span<const std::uint8_t> program) : program_(program.begin(), program.end()) {
    if (program_.size() < file_offset(frame_table) + 4 * frame_groups) {
        throw std::runtime_error("The game program is shorter than expected.");
    }
    for (int i = 0; i < frame_groups; ++i) {
        groups_.push_back(amiga::read_be32(program_, file_offset(frame_table) + 4 * std::size_t(i)));
    }
}

std::optional<FrameInfo> FrameTables::frame(int graphic, int frame) const {
    if (graphic < 0 || graphic >= int(groups_.size()) || frame < 0) {
        return std::nullopt;
    }
    // The table's group pointers are relocated by the program's own loader:
    // in the file they are offsets from the program's start.
    const auto entry = file_offset(0x80000 + groups_[std::size_t(graphic)] + std::uint32_t(frame) * 16);
    if (entry + 16 > program_.size()) {
        return std::nullopt;
    }
    const auto address = amiga::read_be32(program_, entry);
    const int plane = amiga::read_be16(program_, entry + 12);
    FrameInfo info;
    std::uint32_t base = 0;
    if (plane == army_plane) {
        info.sheet = 0;
        base = army_sheet;
    } else if (plane == vehicle_plane) {
        info.sheet = 1;
        base = vehicle_sheet;
    } else {
        return std::nullopt;
    }
    if (address < base || address >= base + std::uint32_t(plane)) {
        return std::nullopt;
    }
    const auto offset = address - base;
    info.sheet_x = int(offset % 40) * 8;
    info.sheet_y = int(offset / 40);
    info.width_words = amiga::read_be16(program_, entry + 8);
    info.height = amiga::read_be16(program_, entry + 10);
    info.offset_x = std::int8_t(program_[entry + 14]);
    info.offset_y = std::int8_t(program_[entry + 15]);
    return info;
}

} // namespace fodder::game
