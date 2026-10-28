#include "spindizzy_worlds_amiga/world/level_builder.hpp"
#include <algorithm>
#include <stdexcept>

namespace spindizzy::world {
namespace {

constexpr std::size_t max_objects = 48; // $12790: the table holds $180 bytes, 8 per object
constexpr int max_macro_depth = 64;     // ours: the original recurses without a limit

// Command kinds: the top three bits of the first word ($125F6 jump table).
enum class Command { none0, box, block, none3, macro, conditional_macro, none6, object };

// Where a macro's contents land: the running offset, rotation and the
// block-type variant ($2488-$248C).
struct Frame {
    int x{}, y{}, z{};      // in cells; the original keeps x, y in eighths and wraps bytes
    int turns{};            // quarter turns (low two bits of $248B)
    std::uint16_t variant{}; // $248C: sum of macro words, bits 14-15 kept
};

class Builder {
public:
    Builder(const data::World& world, const std::array<std::uint8_t, 256>& attributes, const Switches& switches)
        : world_(world), attributes_(attributes), switches_(switches) {}

    Level build(int level) {
        // $125B0 starts from the level's rotation $248B, which $12028 clears:
        // no offset, no turn. (Its offset table $125A8 only matters for
        // turned levels, which no traced path builds.)
        run(world_.commands(level), Frame{}, 0);
        return std::move(level_);
    }

private:
    // $12624: a command's cell position, turned by the frame's rotation.
    static void position(std::uint16_t word, int turns, int& x, int& y) {
        x = word >> 8 & 31;
        y = word >> 3 & 31;
        for (int i = 0; i < (turns & 3); ++i) {
            const int old_x = x;
            x = y;
            y = (32 - old_x) & 31; // neg.b on eighths
        }
    }

    // $12472: one block, with the column below it filled.
    void place(int x, int y, int z, std::uint16_t word, const Frame& frame) {
        auto type = std::uint8_t(word);
        if (!(attributes_[type] & 0x80)) { // $1251C: turnable types turn with the frame
            type = std::uint8_t((type & ~3) | ((type + frame.turns) & 3));
        }
        type = std::uint8_t(type + (frame.variant >> 8)); // add.b $248C(a5): the variant's high byte
        x = (x + frame.x) & 31;
        y = (y + frame.y) & 31;
        z = (z + frame.z) & 7;
        CellLook look;
        const std::uint8_t attribute = attributes_[type];
        if (type & 0x3F) {
            look.in_front = attribute & 4;
            if (attribute & 1) { // $124CE: no column under these
                look.lowered = attribute & 2;
                level_.grid.set(x, y, z, type, look);
                return;
            }
        }
        level_.grid.set(x, y, z, type, look);
        // The column below: support cells (or empty ones) counting their
        // distance from the block; the lowest cell gets the base flag
        // (for an empty type only when the block word has upper bits).
        const std::uint8_t below = (type & 0x3F) ? std::uint8_t((type & 0xC0) + 2) : 0;
        const bool base = (type & 0x3F) || (word & 0xFF00);
        const int depth = std::min(word >> 8 & 7, z);
        int lowest = z;
        for (int k = 1; k <= depth; ++k) {
            lowest = z - k;
            CellLook column;
            column.depth = (k - 1) & 3; // the counter's low two bits are what the drawing reads
            level_.grid.set(x, y, lowest, below, column);
        }
        if (base) level_.grid.look(x, y, lowest).base = true;
    }

    void run(std::span<const std::uint8_t> list, const Frame& frame, int depth) {
        if (depth > max_macro_depth) throw std::runtime_error("A world's macros call each other without end.");
        std::size_t at = 0;
        const auto word = [&]() -> std::uint16_t {
            if (at + 2 > list.size()) throw std::runtime_error("A level's command list runs past its record.");
            const auto value = std::uint16_t(list[at] << 8 | list[at + 1]);
            at += 2;
            return value;
        };
        const auto byte = [&]() -> std::uint8_t {
            if (at >= list.size()) throw std::runtime_error("A level's command list runs past its record.");
            return list[at++];
        };
        for (;;) {
            const std::uint16_t first = word();
            if (first == 0) return;
            const std::uint16_t second = word();
            int x{}, y{};
            position(first, frame.turns, x, y);
            const int z = first & 7;
            switch (Command(first >> 13)) {
            case Command::block:
                place(x, y, z, second, frame);
                break;
            case Command::box: { // $12542: counts and slopes in two bytes
                std::uint8_t across = byte(), along = byte();
                if (frame.turns & 1) std::swap(across, along);
                constexpr int step_x[4] = {1, 1, -1, -1}; // $1253A
                constexpr int step_y[4] = {1, -1, -1, 1}; // $1253E
                const int slope_x = std::int8_t(across) >> 5, slope_y = std::int8_t(along) >> 5;
                for (int i = 0; i <= (across & 31); ++i) {
                    for (int j = 0; j <= (along & 31); ++j) {
                        place(x + i * step_x[frame.turns & 3], y + j * step_y[frame.turns & 3],
                              z + i * slope_x + j * slope_y, second, frame);
                    }
                }
                break;
            }
            case Command::conditional_macro: { // $1269E: a third word, the condition
                const std::uint16_t condition = word();
                level_.conditions.push_back(condition);
                if (!switches_.holds(condition)) break;
                call(x, y, z, second, frame, depth);
                break;
            }
            case Command::macro:
                call(x, y, z, second, frame, depth);
                break;
            case Command::object: { // $1278C: parameters follow, their count in the low byte
                if (level_.objects.size() == max_objects) break; // the original then reads them as commands
                const std::size_t count = second & 0xFF;
                if (at + count > list.size()) throw std::runtime_error("An object's parameters run past its record.");
                level_.objects.push_back({(x + frame.x) & 31, (y + frame.y) & 31, (z + frame.z) & 7, second >> 8,
                                          {list.begin() + std::ptrdiff_t(at), list.begin() + std::ptrdiff_t(at + count)}});
                at += count;
                break;
            }
            default: // kinds 0, 3 and 6 do nothing
                break;
            }
        }
    }

    // $126B0: a macro runs in a frame moved, turned and varied by the call.
    void call(int x, int y, int z, std::uint16_t call_word, const Frame& frame, int depth) {
        const int id = int(std::int16_t(std::uint16_t((call_word >> 2) | 0xFF00)));
        if (!world_.macros.contains(id)) return; // $126DE: unknown macros are skipped
        Frame inner = frame;
        inner.x = (frame.x + x) & 31;
        inner.y = (frame.y + y) & 31;
        inner.z = (frame.z + z) & 7;
        inner.turns = (frame.turns + call_word) & 3;
        inner.variant = std::uint16_t((frame.variant + call_word) & 0xC000);
        run(world_.commands(id), inner, depth + 1);
    }

    const data::World& world_;
    const std::array<std::uint8_t, 256>& attributes_;
    const Switches& switches_;
    Level level_;
};

} // namespace

int LevelGrid::top(int x, int y) const {
    for (int z = level_height - 1; z >= 0; --z) {
        if (at(x, y, z) != 0) return z;
    }
    return -1;
}

bool Switches::holds(std::uint16_t condition) const {
    if (!conditions_enabled) return true;
    if (condition & 0x8000) { // jewels: all gone, or (bit 11) some left
        const bool left = (condition & 0x0800) ? !jewels_left : jewels_left;
        if (left) return false;
    }
    if (condition & 0x2000) { // a push button's value; bit 10 picks the primary one
        int value = condition >> 8 & 7;
        bool differs;
        if (value & 4) {
            value = (value & 3) + 1;
            differs = value != primary;
        } else {
            differs = value != secondary;
        }
        if (condition & 0x1000) differs = !differs; // "NOT pushed"
        if (differs) return false;
    }
    if (condition & 0x4000) { // binary switches: (state & mask) == value
        const int mask = condition & 15, value = condition >> 4 & 15;
        if ((binary & mask) != value) return false;
    }
    return true;
}

Level build_level(const data::World& world, int level, const std::array<std::uint8_t, 256>& block_attributes,
                  const Switches& switches) {
    return Builder(world, block_attributes, switches).build(level);
}

} // namespace spindizzy::world
