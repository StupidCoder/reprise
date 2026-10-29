#include "tower_of_babel_amiga/design/tower_design.hpp"
#include <algorithm>
#include <functional>

namespace babel::design {
namespace {

// The record's 3-bit cell codes and the designer's kinds ($13712 / $1371A).
constexpr std::array<int, 8> code_kinds = {empty, floor_section, block, lift_up, lift_down, lift_top, lift_bottom, empty};
int kind_code(int kind) {
    switch (kind) {
    case floor_section: return 1;
    case block: return 2;
    case lift_up: return 3;
    case lift_down: return 4;
    case lift_top: return 5;
    case lift_bottom: return 6;
    default: return 0;
    }
}

// Most significant bit first, through 32-bit words ($1390A / $138DC).
class Bits {
public:
    explicit Bits(std::span<std::uint8_t> bytes) : bytes_(bytes) {}
    int get(int n) {
        int v = 0;
        for (int i = 0; i < n; ++i, ++at_) v = v << 1 | (bytes_[std::size_t(at_ >> 3)] >> (7 - (at_ & 7)) & 1);
        return v;
    }
    void put(int n, long value) {
        for (int i = n - 1; i >= 0; --i, ++at_) {
            auto& byte = bytes_[std::size_t(at_ >> 3)];
            const auto mask = std::uint8_t(0x80 >> (at_ & 7));
            byte = std::uint8_t((value >> i & 1) ? (byte | mask) : (byte & ~mask));
        }
    }

private:
    std::span<std::uint8_t> bytes_;
    int at_{};
};

// $136FE: printable characters, anything else as 0.
template <std::size_t N> void get_text(Bits& in, std::array<char, N>& a) {
    for (auto& c : a) {
        const int v = in.get(7);
        c = v >= 0x20 && v < 0x7F ? char(v) : 0;
    }
}
template <std::size_t N> void put_text(Bits& out, const std::array<char, N>& a) {
    for (const char c : a) out.put(7, std::uint8_t(c) & 0x7F);
}

} // namespace

TowerDesign TowerDesign::from_record(std::span<const std::uint8_t> record) {
    TowerDesign t;
    std::copy_n(record.begin(), std::min(record.size(), t.base.size()), t.base.begin());
    Bits in{std::span<std::uint8_t>(t.base)};
    get_text(in, t.name);
    get_text(in, t.author);
    get_text(in, t.password);
    t.time_limit = in.get(14);
    for (auto& f : t.flags) f = in.get(1) != 0;
    t.bomb_speed = in.get(2);
    t.klondikes = in.get(3);
    t.destroy = in.get(3);
    t.stars = in.get(2);
    t.pattern = in.get(3);
    t.sky_type = in.get(2);
    t.moon_heading = in.get(3);
    t.moon_type = in.get(3);
    t.moon_x = in.get(9);
    t.moon_y = in.get(7);
    t.moon_rotation = in.get(2);
    for (auto& c : t.cells) c = std::int8_t(code_kinds[std::size_t(in.get(3))]);
    t.objects.fill(-1);
    for (int i = 0; i < 20; ++i) {
        const int kind = in.get(6), place = in.get(8);
        if (kind >= 4 && kind < 0x28) t.objects[std::size_t(place)] = std::int8_t(kind);
    }
    for (auto& c : t.colours) {
        const int r = in.get(4), g = in.get(4), b = in.get(4);
        c = data::Colour(r << 8 | g << 4 | b);
    }
    t.background = in.get(2);
    t.no_footsteps = in.get(1) != 0;
    t.no_effects = in.get(1) != 0;
    return t;
}

bool TowerDesign::to_record(std::array<std::uint8_t, data::tower_record_size>& record) const {
    record = base;
    Bits out{std::span<std::uint8_t>(record)};
    put_text(out, name);
    put_text(out, author);
    put_text(out, password);
    out.put(14, time_limit);
    for (const bool f : flags) out.put(1, f);
    out.put(2, bomb_speed);
    out.put(3, klondikes);
    out.put(3, destroy);
    out.put(2, stars);
    out.put(3, pattern);
    out.put(2, sky_type);
    out.put(3, moon_heading);
    out.put(3, moon_type);
    out.put(9, moon_x);
    out.put(7, moon_y);
    out.put(2, moon_rotation);
    for (const auto c : cells) out.put(3, kind_code(c));
    // Objects in cell order; unused entries all ones (kind 63, skipped).
    int written = 0;
    for (int i = 0; i < 256; ++i) {
        const int k = objects[std::size_t(i)];
        if (k < 4 || k >= 0x28) continue;
        if (++written > 20) return false;
        out.put(6, k);
        out.put(8, i);
    }
    for (; written < 20; ++written) out.put(14, 0x3FFF);
    for (const auto c : colours) {
        out.put(4, c >> 8 & 15);
        out.put(4, c >> 4 & 15);
        out.put(4, c & 15);
    }
    out.put(2, background);
    out.put(1, no_footsteps);
    out.put(1, no_effects);
    return true;
}

Message TowerDesign::place(int cell, int kind) {
    auto& c = cells[std::size_t(cell)];
    auto& o = objects[std::size_t(cell)];
    const int floor = cell / 64;
    const auto at = [&](int offset) -> std::int8_t& {
        static std::int8_t outside = -1;
        const int i = cell + offset;
        return i >= 0 && i < 256 ? cells[std::size_t(i)] : outside = -1;
    };
    const auto object_at = [&](int offset) -> std::int8_t& {
        static std::int8_t outside = -1;
        const int i = cell + offset;
        return i >= 0 && i < 256 ? objects[std::size_t(i)] : outside = -1;
    };
    if (kind < 4) {
        if (c == kind) return ok;
        if (c == lift_up) at(64) = -1;
        else if (c == lift_down) at(-64) = -1;
        else if (c == lift_top || c == lift_bottom) return cannot_remove_lift;
        switch (kind) {
        case floor_section:
            c = std::int8_t(kind);
            return ok;
        case block:
            if (floor == 3) return cannot_place_support;
            c = std::int8_t(kind);
            o = -1;
            if (at(64) < 0) at(64) = floor_section;
            return ok;
        case lift_up:
            if (floor == 3) return cannot_place_lift;
            c = std::int8_t(kind);
            at(64) = lift_top;
            object_at(64) = -1;
            return ok;
        default: // lift down
            if (floor == 0) return cannot_place_lift;
            if (at(-64) == lift_top || at(-64) == lift_bottom) return cannot_remove_lift;
            c = std::int8_t(kind);
            at(-64) = lift_bottom;
            object_at(-64) = -1;
            return ok;
        }
    }
    if (c < 0) return no_floor;
    if (o == kind) return ok;
    if (kind >= 7) {
        // At most 17 objects besides the spiders (the one being replaced counts).
        const auto n = std::count_if(objects.begin(), objects.end(), [](std::int8_t k) { return k >= 7; });
        if (n >= 17) return too_many_objects;
    }
    if (c >= 4 || c == block) return no_floor;
    if (kind < 7 && !flags[std::size_t(kind - 4)]) return spider_placed;
    o = std::int8_t(kind);
    return ok;
}

Message TowerDesign::remove(int cell) {
    auto& c = cells[std::size_t(cell)];
    auto& o = objects[std::size_t(cell)];
    if (o >= 0) {
        o = -1;
        return ok;
    }
    const int k = c;
    if (k == lift_top || k == lift_bottom) return cannot_remove_lift;
    if (cell >= 64 && cells[std::size_t(cell - 64)] == block) return supported_block;
    c = -1;
    if (k == lift_up) cells[std::size_t(cell + 64)] = -1;
    if (k == lift_down) cells[std::size_t(cell - 64)] = -1;
    return ok;
}

void TowerDesign::update_spiders() {
    flags[0] = flags[1] = flags[2] = true;
    for (const auto k : objects)
        if (k >= 4 && k < 7) flags[std::size_t(k - 4)] = false;
}

void TowerDesign::repair() {
    int spiders = 0; // the spiders seen so far (bits 4-6)
    const auto check_object = [&](int i) {
        auto& o = objects[std::size_t(i)];
        if (o == -1) return;
        if (o < 4 || o >= 0x28) {
            o = -1;
        } else if (o < 7) {
            if (spiders >> o & 1) o = -1;
            spiders |= 1 << o;
        }
    };
    for (int i = 0; i < 256; ++i) {
        const int floor = i / 64;
        auto& c = cells[std::size_t(i)];
        const int above = i + 64 < 256 ? cells[std::size_t(i + 64)] : -1;
        const int below = i >= 64 ? cells[std::size_t(i - 64)] : -1;
        switch (c) {
        case floor_section:
            check_object(i);
            break;
        case block:
            if (floor == 3 || above < 0 || above >= 3) {
                c = floor_section;
                check_object(i);
            } else {
                objects[std::size_t(i)] = -1; // a block carrying something holds nothing itself
            }
            break;
        case lift_up:
            if (floor == 3 || above != lift_top) c = floor_section;
            check_object(i);
            break;
        case lift_down:
            if (floor == 0 || below != lift_bottom) c = floor_section;
            check_object(i);
            break;
        case lift_top:
            if (floor == 0) {
                c = -1;
                objects[std::size_t(i)] = -1;
            } else {
                if (below != lift_up) c = floor_section;
                check_object(i);
            }
            break;
        case lift_bottom:
            if (floor == 3 || above != lift_down) c = floor_section;
            check_object(i);
            break;
        default:
            c = -1;
            objects[std::size_t(i)] = -1;
            break;
        }
    }
    // Objects past the 17th (besides spiders) go, the first ones found.
    int extra = 0;
    for (int i = 0; i < 256; ++i)
        if (cells[std::size_t(i)] >= 0 && objects[std::size_t(i)] >= 7) ++extra;
    extra -= 17;
    for (int i = 0; i < 256 && extra > 0; ++i)
        if (cells[std::size_t(i)] >= 0 && objects[std::size_t(i)] >= 7) {
            objects[std::size_t(i)] = -1;
            --extra;
        }
}

void TowerDesign::scroll(int level, int dx, int dz) {
    for (int f = 0; f < 4; ++f) {
        if (level >= 0 && f != level) continue;
        const auto old_cells = cells, old_objects = objects;
        for (int z = 0; z < 8; ++z)
            for (int x = 0; x < 8; ++x) {
                const int from = index(f, (x - dx + 8) & 7, (z - dz + 8) & 7);
                cells[std::size_t(index(f, x, z))] = old_cells[std::size_t(from)];
                objects[std::size_t(index(f, x, z))] = old_objects[std::size_t(from)];
            }
    }
    repair();
}

void TowerDesign::rotate(int level) {
    // $FEAE: the kinds that face a way, each range turned one step.
    static constexpr std::array<std::pair<int, int>, 6> turning = {
        {{0x07, 0x0B}, {0x0B, 0x0F}, {0x11, 0x13}, {0x19, 0x1D}, {0x1D, 0x1F}, {0x21, 0x23}}};
    for (int f = 0; f < 4; ++f) {
        if (level >= 0 && f != level) continue;
        const auto old_cells = cells, old_objects = objects;
        for (int z = 0; z < 8; ++z)
            for (int x = 0; x < 8; ++x) {
                const int from = index(f, z, 7 - x);
                int k = old_objects[std::size_t(from)];
                for (const auto& [a, b] : turning)
                    if (k >= a && k < b) {
                        if (k == a) k = b;
                        --k;
                        break;
                    }
                cells[std::size_t(index(f, x, z))] = old_cells[std::size_t(from)];
                objects[std::size_t(index(f, x, z))] = std::int8_t(k);
            }
    }
    repair();
}

void TowerDesign::copy_level(int from, int to) {
    for (int i = 0; i < 64; ++i) {
        cells[std::size_t(to * 64 + i)] = cells[std::size_t(from * 64 + i)];
        objects[std::size_t(to * 64 + i)] = objects[std::size_t(from * 64 + i)];
    }
    repair();
}

void TowerDesign::swap_levels(int a, int b) {
    for (int i = 0; i < 64; ++i) {
        std::swap(cells[std::size_t(a * 64 + i)], cells[std::size_t(b * 64 + i)]);
        std::swap(objects[std::size_t(a * 64 + i)], objects[std::size_t(b * 64 + i)]);
    }
    repair();
}

void TowerDesign::clear_level(int level) {
    for (int i = level * 64; i < level * 64 + 64; ++i) {
        objects[std::size_t(i)] = -1;
        auto& c = cells[std::size_t(i)];
        if (c == floor_section) {
            if (!(level > 0 && cells[std::size_t(i - 64)] == block)) c = -1; // a floor on a block stays
        } else if (c == lift_up) {
            c = -1;
            cells[std::size_t(i + 64)] = -1;
        } else if (c == lift_down) {
            c = -1;
            cells[std::size_t(i - 64)] = -1;
        }
    }
}

void TowerDesign::clear_all() {
    cells.fill(-1);
    objects.fill(-1);
    name[0] = author[0] = password[0] = 0;
    flags[3] = true; // unlocked
}

TowerDesign::Statistics TowerDesign::statistics() const {
    Statistics s;
    int used = 0;
    for (int i = 0; i < 256; ++i) {
        if (cells[std::size_t(i)] < 0) continue;
        ++used;
        const int k = objects[std::size_t(i)];
        if (k < 7) continue;
        if (k == 0x0A || k < 0x0B) ++s.zappers;          // the four Zappers ($103CE)
        else if (k == 0x10 || k < 0x0F) ++s.pushers;     // Pushers and the rotating Pusher
        else if (k == klondike) ++s.klondikes;
        else ++s.others;
    }
    s.percent_used = used * 100 / 256;
    s.total = s.zappers + s.pushers + s.klondikes + s.others;
    return s;
}

bool TowerDesign::support_check(std::vector<int>& reached) const {
    std::array<bool, 256> seen{};
    const std::function<void(int, int, int)> visit = [&](int x, int z, int f) {
        const int i = index(f, x, z);
        if (cells[std::size_t(i)] < 0 || seen[std::size_t(i)]) return;
        seen[std::size_t(i)] = true;
        reached.push_back(i);
        if (x > 0) visit(x - 1, z, f);
        if (x < 7) visit(x + 1, z, f);
        if (z > 0) visit(x, z - 1, f);
        if (z < 7) visit(x, z + 1, f);
        if (cells[std::size_t(i)] == block && f < 3) visit(x, z, f + 1);
    };
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x) visit(x, z, 0);
    for (int i = 0; i < 256; ++i)
        if (cells[std::size_t(i)] >= 0 && !seen[std::size_t(i)]) return false;
    return true;
}

bool TowerDesign::path_check(int spider, std::vector<int>& reached, bool& placed) const {
    int start = -1;
    for (int i = 0; i < 256 && start < 0; ++i)
        if (objects[std::size_t(i)] == spider + 4) start = i;
    placed = start >= 0;
    if (!placed) return false;
    std::array<bool, 256> seen{};
    // $106F2 / $10700 (the second entry: a lift's other end, walked on).
    const std::function<void(int, bool)> visit = [&](int i, bool lift_end) {
        const int c = cells[std::size_t(i)];
        if (!lift_end && (c < 0 || c >= 4)) return;
        if (seen[std::size_t(i)]) return;
        seen[std::size_t(i)] = true;
        if (c == block) return;
        reached.push_back(i);
        const int x = i & 7, z = i >> 3 & 7, f = i >> 6;
        if (x > 0) visit(i - 1, false);
        if (x < 7) visit(i + 1, false);
        if (z > 0) visit(i - 8, false);
        if (z < 7) visit(i + 8, false);
        if (c == lift_up && f < 3) visit(i + 64, true);
        if (c == lift_down && f > 0) visit(i - 64, true);
    };
    visit(start, false);
    for (int i = 0; i < 256; ++i)
        if (cells[std::size_t(i)] >= 0 && cells[std::size_t(i)] != block && !seen[std::size_t(i)]) return false;
    return true;
}

} // namespace babel::design
