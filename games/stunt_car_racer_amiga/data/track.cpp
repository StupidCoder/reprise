#include "stunt_car_racer_amiga/data/track.hpp"
#include <algorithm>
#include <stdexcept>

namespace stuntcar::data {
namespace {

// The track bank: shapes, height profiles and the eight track definitions.
// It lies at bank_address in the program; its own pointers (little-endian
// words) count from bank_origin.
constexpr std::uint32_t bank_address = 0x1EF82;
constexpr std::uint16_t bank_origin = 0xB100;
constexpr std::uint16_t bank_end = 0xC000;
constexpr int shape_table = 0x000, shape_slots = 16;
constexpr int profile_table = 0x020, profile_slots = 128;
constexpr int track_table = 0x120, track_slots = 8;

// Track names, 16 characters each, space padded, in the bank's order.
constexpr std::uint32_t track_names = 0x1EDAA;
// Segment kinds $C and $D are straights with a fixed pair of profiles,
// read from two small tables in the program ($5B2B8 + kind, $5B2BA + kind).
constexpr std::uint32_t fixed_left_profiles = 0x5B2B8;
constexpr std::uint32_t fixed_right_profiles = 0x5B2BA;
constexpr int first_fixed_kind = 0xC, last_fixed_kind = 0xD;

struct Bank {
    const Program& program;
    std::uint8_t u8(std::uint16_t at) const { return program.u8(bank_address + at - bank_origin); }
    std::uint16_t le16(std::uint16_t at) const { return std::uint16_t(u8(at) | u8(std::uint16_t(at + 1)) << 8); }
    std::int16_t sle16(std::uint16_t at) const { return std::int16_t(le16(at)); }
    std::uint16_t entry(int table, int slot) const { return le16(std::uint16_t(bank_origin + table + slot * 2)); }
    static bool inside(std::uint16_t at) { return at >= bank_origin && at < bank_end; }
};

// Shape data: byte 0 is the offset of its description; that holds the
// point count, five bytes the shape code uses elsewhere, then the points
// (x, z as signed little-endian words), left and right edge in turn.
Shape decode_shape(const Bank& bank, std::uint16_t at) {
    const auto info = std::uint16_t(at + bank.u8(at));
    const int points = bank.u8(info);
    if (points < 4 || points % 2) throw std::runtime_error("A track shape has an odd point count.");
    Shape shape;
    auto p = std::uint16_t(info + 7);
    for (int i = 0; i < points / 2; ++i, p = std::uint16_t(p + 8)) {
        shape.steps.push_back({PlanPoint{bank.sle16(p), bank.sle16(std::uint16_t(p + 2))},
                               PlanPoint{bank.sle16(std::uint16_t(p + 4)), bank.sle16(std::uint16_t(p + 6))}});
    }
    return shape;
}

std::string track_name(const Program& program, int track) {
    std::string raw;
    for (const auto c : program.at(track_names + std::uint32_t(track) * 16, 16)) raw += char(c);
    raw.erase(raw.find_last_not_of(' ') + 1);
    // Shown in capitals by the game ("The LITTLE RAMP"); written in words here.
    bool word_start = true;
    for (auto& c : raw) {
        if (!word_start && c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        word_start = c == ' ';
    }
    return raw;
}

// Reads one track definition (the unpacker at $5AE46 does the same).
Track decode_track(const Bank& bank, const Circuits& circuits, int index) {
    auto at = bank.entry(track_table, index);
    const auto next = [&] {
        if (!Bank::inside(at)) throw std::runtime_error("A track runs past the end of the track bank.");
        const auto byte = bank.u8(at);
        at = std::uint16_t(at + 1);
        return byte;
    };
    Track track;
    track.name = track_name(bank.program, index);
    const int count = next();
    // Three header bytes: the car's start segment, the start line's segment
    // (the race view compares segments with it) and one not yet understood.
    track.car_start_segment = next();
    track.start_line_segment = next();
    track.header_third = next();
    const int low = next();
    track.start_height = next() << 8 | low;

    int height[2] = {track.start_height, track.start_height};
    int repeats = 0;
    std::uint8_t code = 0;
    int cell_x = 0, cell_z = 0;
    bool kerb_phase = false;
    for (int n = 0; n < count; ++n) {
        if (repeats == 0) {
            std::uint8_t read = next();
            // A code with low nibble $F repeats the previous segment's code in
            // the next cells along its heading, (high nibble) times.
            while ((read & 0x0F) == 0x0F && (read >> 4) == 0) read = next();
            if ((read & 0x0F) == 0x0F) {
                if (track.segments.empty()) throw std::runtime_error("A track starts with a repeat.");
                repeats = read >> 4;
            } else {
                code = read;
                const auto cell = next();
                cell_x = cell & 0x0F;
                cell_z = cell >> 4;
            }
        }
        Segment segment;
        segment.heading = code >> 6;
        segment.reversed = code & 0x10;
        if (repeats > 0) {
            --repeats;
            static constexpr int dx[4] = {0, 1, 0, -1}, dz[4] = {1, 0, -1, 0};
            // A reversed code steps along heading ^ 3 (the unpacker flips both
            // heading bits), as in the original.
            const int travel = segment.reversed ? segment.heading ^ 3 : segment.heading;
            cell_x = (cell_x + dx[travel]) & 0x0F;
            cell_z = (cell_z + dz[travel]) & 0x0F;
        }
        segment.cell_x = cell_x;
        segment.cell_z = cell_z;
        int kind = code & 0x0F;
        int left = 0, right = 0;
        if (kind >= first_fixed_kind) {
            if (kind > last_fixed_kind) throw std::runtime_error("A track uses an unknown segment kind.");
            left = bank.program.u8(fixed_left_profiles + std::uint32_t(kind));
            right = bank.program.u8(fixed_right_profiles + std::uint32_t(kind));
            kind = 0;
        } else {
            left = next();
            right = code & 0x20 ? left : next(); // bit 5: both edges share one profile
        }
        if (circuits.shapes.at(std::size_t(kind)).steps.empty()) throw std::runtime_error("A track uses a missing shape.");
        segment.shape = kind;
        segment.steps = int(circuits.shapes[std::size_t(kind)].steps.size()) - 1;
        segment.left_profile = left & 0x7F;
        segment.right_profile = right & 0x7F;
        segment.wide_profiles = left & 0x80;
        segment.kerb_phase = kerb_phase;
        kerb_phase ^= segment.steps & 1;

        // Each edge carries on from where the previous segment left it.
        const int profile[2] = {segment.left_profile, segment.right_profile};
        int base[2];
        for (int e = 0; e < 2; ++e) {
            const auto& p = circuits.profiles.at(std::size_t(profile[e]));
            base[e] = (height[e] - p.offset(0, segment.wide_profiles)) & 0xFFFF;
            height[e] = (base[e] + p.offset(segment.steps, segment.wide_profiles)) & 0xFFFF;
        }
        segment.left_base = base[0];
        segment.right_base = base[1];
        track.segments.push_back(segment);
    }
    if (track.car_start_segment >= count || track.start_line_segment >= count)
        throw std::runtime_error("A track starts outside its segments.");
    for (auto& b : track.trailer) b = next();
    const int marks = next(), listed = next();
    for (int i = 0; i < marks; ++i) {
        const auto segment = next();
        track.segment_marks.emplace_back(segment, next());
    }
    for (int i = 0; i < listed; ++i) track.segment_list.push_back(next());
    return track;
}

} // namespace

int Profile::offset(int step, bool wide) const {
    if (step < 0) throw std::runtime_error("A height profile was read before its start.");
    if (wide) {
        const auto i = std::size_t(step) * 2;
        if (i + 1 >= bytes.size()) throw std::runtime_error("A height profile is too short.");
        return (bytes[i] & 0x7F) << 8 | bytes[i + 1];
    }
    if (std::size_t(step) >= bytes.size()) throw std::runtime_error("A height profile is too short.");
    const auto b = bytes[std::size_t(step)];
    return (b & 0x0F) << 8 | (b << 1 & 0xE0);
}

bool Profile::marked(int step, bool wide) const {
    const auto i = std::size_t(step) * (wide ? 2 : 1);
    if (step < 0 || i >= bytes.size()) throw std::runtime_error("A height profile is too short.");
    return bytes[i] & 0x80;
}

Circuits load_circuits(const Program& program) {
    const Bank bank{program};
    Circuits circuits;
    // A profile runs to the next thing the bank points at.
    std::vector<std::uint16_t> starts;
    for (int i = 0; i < shape_slots; ++i) starts.push_back(bank.entry(shape_table, i));
    for (int i = 0; i < profile_slots; ++i) starts.push_back(bank.entry(profile_table, i));
    for (int i = 0; i < track_slots; ++i) starts.push_back(bank.entry(track_table, i));
    std::sort(starts.begin(), starts.end());

    for (int i = 0; i < shape_slots; ++i) {
        const auto at = bank.entry(shape_table, i);
        circuits.shapes.push_back(Bank::inside(at) ? decode_shape(bank, at) : Shape{});
    }
    for (int i = 0; i < profile_slots; ++i) {
        const auto at = bank.entry(profile_table, i);
        Profile profile;
        if (Bank::inside(at)) {
            const auto end = *std::upper_bound(starts.begin(), starts.end(), at);
            for (auto p = at; p < end && p < bank_end; ++p) profile.bytes.push_back(bank.u8(p));
        }
        circuits.profiles.push_back(std::move(profile));
    }
    for (int i = 0; i < track_slots; ++i) circuits.tracks.push_back(decode_track(bank, circuits, i));
    return circuits;
}

std::vector<CrossSection> road(const Circuits& circuits, const Track& track) {
    std::vector<CrossSection> sections;
    for (std::size_t n = 0; n < track.segments.size(); ++n) {
        const auto& s = track.segments[n];
        auto steps = circuits.shapes.at(std::size_t(s.shape)).steps;
        if (s.reversed) {
            std::reverse(steps.begin(), steps.end());
            for (auto& edges : steps) std::swap(edges[0], edges[1]);
        }
        const int x0 = s.cell_x * cell_size, z0 = s.cell_z * cell_size;
        const auto place = [&](PlanPoint p) {
            switch (s.heading) {
            case 0: return PlanPoint{x0 + p.x, z0 + p.z};
            case 1: return PlanPoint{x0 + p.z, z0 + cell_size - p.x};
            case 2: return PlanPoint{x0 + cell_size - p.x, z0 + cell_size - p.z};
            default: return PlanPoint{x0 + cell_size - p.z, z0 + p.x};
            }
        };
        const auto& left = circuits.profiles.at(std::size_t(s.left_profile));
        const auto& right = circuits.profiles.at(std::size_t(s.right_profile));
        for (int i = 0; i < int(steps.size()); ++i) {
            sections.push_back({place(steps[std::size_t(i)][0]), place(steps[std::size_t(i)][1]),
                                s.left_base + left.offset(i, s.wide_profiles),
                                s.right_base + right.offset(i, s.wide_profiles), int(n),
                                left.marked(i, s.wide_profiles)});
        }
    }
    return sections;
}

} // namespace stuntcar::data
