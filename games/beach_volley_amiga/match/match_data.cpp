#include "beach_volley_amiga/match/match_data.hpp"
#include "platform/files.hpp"
#include <stdexcept>
#include <string>

namespace beach_volley::match {
namespace {

constexpr std::uint32_t object_table = 0x30CC4; // 20-byte records
constexpr std::uint32_t court_palettes = 0x29190;
constexpr std::uint32_t arc_end = 0x2AF38;

std::vector<std::uint8_t> read_installed(const std::filesystem::path& content, const std::string& name) {
    try {
        return platform::read_file(content / name);
    } catch (const std::exception&) {
        throw std::runtime_error("The imported file " + name + " is missing; import the disk again.");
    }
}

class Reader {
public:
    explicit Reader(const data::CodeFile& code) : code_(code) {}

    int word(std::uint32_t at) const { return std::int16_t(code_.u16(at)); }
    Image image(std::uint32_t picture, int width, int height) const {
        if (picture < sprite_block_address) throw std::runtime_error("volley.code names a picture outside its block.");
        return {picture - sprite_block_address, width, height};
    }
    // 8-byte frames {picture, width, height}; `zero_ends`: a zero long ends
    // the run, otherwise $FFFF in the picture's low word does.
    Frames frames(std::uint32_t at, bool zero_ends) const {
        Frames out;
        for (;; at += 8) {
            const std::uint32_t picture = code_.u32(at);
            if (zero_ends ? picture == 0 : (picture & 0xFFFF) == 0xFFFF) return out;
            out.push_back(image(picture, word(at + 4), word(at + 6)));
        }
    }
    Offsets words(std::uint32_t at, int end) const {
        Offsets out;
        for (; word(at) != end; at += 2) out.push_back(word(at));
        return out;
    }
    std::vector<int> bytes(std::uint32_t at, std::size_t count) const {
        std::vector<int> out;
        for (const auto b : code_.at(at, count)) out.push_back(std::int8_t(b));
        return out;
    }

private:
    const data::CodeFile& code_;
};

} // namespace

MatchData load_match_data(const std::filesystem::path& content, int venue) {
    if (venue < 1 || venue > data::court_count) throw std::runtime_error("There is no such venue.");
    const data::CodeFile code(read_installed(content, "volley.code"), data::volley_base);
    const Reader r(code);
    MatchData m;
    m.venue = venue;
    m.sprites = read_installed(content, "animation1");
    const auto second = read_installed(content, "animation2");
    m.sprites.insert(m.sprites.end(), second.begin(), second.end());

    m.court.number = venue;
    m.court.picture = data::planar_picture(read_installed(content, "terrain" + std::to_string(venue)),
                                           data::court_width, data::court_height, 4);
    for (int i = 0; i < 16; ++i)
        m.court.palette[std::size_t(i)] = code.u16(court_palettes + std::uint32_t(venue - 1) * 32 + std::uint32_t(i) * 2);

    for (int n = 0; n < object_count; ++n) {
        const std::uint32_t at = object_table + std::uint32_t(n) * 20;
        auto& o = m.objects[std::size_t(n)];
        o.x = r.word(at);
        o.y = r.word(at + 2);
        o.image = r.image(code.u32(at + 4), r.word(at + 16), r.word(at + 18));
    }

    for (int n = 0; n < 8; ++n) m.stance[std::size_t(n)] = r.image(code.u32(0x2B5C6 + std::uint32_t(n) * 4), 48, 59);
    const std::uint32_t walks[6] = {0x2F5B0, 0x2F5D4, 0x2F5F8, 0x2F61C, 0x2F640, 0x2F664};
    for (int n = 0; n < 6; ++n) m.walk[std::size_t(n)] = r.frames(walks[n], false);
    for (std::uint32_t at = 0x2AEC0; code.u32(at) != 0xFFFFFFFF; at += 4)
        m.ball_spin.push_back(r.image(code.u32(at), 32, 16));
    m.serve_swing_a = r.frames(0x2716C, true);
    m.serve_swing_b = r.frames(0x28014, true);
    m.jump_a = r.frames(0x2F498, false);
    m.jump_b = r.frames(0x2F4CC, false);
    m.recover_left = r.frames(0x2F53C, false);
    m.recover_right = r.frames(0x2F500, false);

    m.toss = r.words(0x2719A, 0xFF);
    m.jump = r.words(0x30B7A, 0xFF);
    m.hop = r.words(0x30BBA, 0xFF);
    m.serve_heights_b = r.words(0x27FF4, -1);
    for (std::uint32_t n = 0; n < 24; ++n) m.serve_toss_b.push_back(r.word(0x27FB0 + n * 2));
    m.setter_targets = r.words(0x2FC12, 0);
    m.touch_order = r.words(0x2A95A, 0);
    m.dive_rise = r.bytes(0x2D21E, 20);
    m.dig_rise = r.bytes(0x2D232, 22);
    m.dive_shift = r.bytes(0x2D248, 22);
    m.fall_x = r.words(0x2C5CC, 0xFF);
    m.fall_y = r.words(0x2C600, 0xFF);

    for (std::uint32_t at = 0x2AADC; r.word(at) != 0xFF; at += 2) m.ease_thresholds.push_back(r.word(at));
    // Travels up to 209 pick runs 1-21 (run 0 is never picked).
    for (std::uint32_t n = 0; n < 22; ++n) m.ease.push_back(r.words(code.u32(0x2AB12 + n * 4), 0xFF));
    // The arc: the walk starts up to 128 bytes below $2AF38 and returns.
    for (std::uint32_t at = arc_end - 128; at <= arc_end; at += 2) m.arc.push_back(r.word(at));

    for (std::uint32_t n = 0; n < 8; ++n) m.board_digits[n] = r.image(code.u32(0x26286 + n * 4), 16, 10);
    for (std::uint32_t n = 0; n < 8; ++n) {
        for (std::uint32_t at = code.u32(0x30640 + n * 4); r.word(at) != -1; at += 4)
            m.big_digits[n].push_back({r.word(at), r.word(at + 2)});
    }
    for (std::uint32_t n = 0; r.word(0x31EE2 + n * 2) != 0; ++n)
        m.clock_positions.push_back({r.word(0x31EE2 + n * 2), r.word(0x31EFC + n * 2)});
    const std::uint32_t chances[8] = {0x2869A, 0x28708, 0x28776, 0x287E4, 0x28852, 0x288C0, 0x2892E, 0x2899C};
    for (std::uint32_t at = chances[venue - 1]; r.word(at) >= 0; at += 2) m.chance.push_back(r.word(at));

    m.instruments = read_installed(content, "instruments");
    m.player_tables = sound::read_player_tables(code, 0x312A2, 0x312DE, m.instruments.size());
    // $3133C loads the venue's song to $7BD1C and music8 to $796C4; the
    // names are the file names in its loads ("df0:music12.cru").
    const auto file_name = [&](std::uint32_t at) {
        std::string name;
        for (auto c = code.at(at, 1)[0]; c != 0; c = code.at(++at, 1)[0]) name.push_back(char(c));
        if (name.rfind("df0:", 0) != 0 || name.size() < 9 || name.substr(name.size() - 4) != ".cru")
            throw std::runtime_error("volley.code's song names are not as expected.");
        return name.substr(4, name.size() - 8);
    };
    const std::uint32_t venue_songs[8] = {0x31AE9, 0x31AF9, 0x31B08, 0x31B18, 0x31B27, 0x31B37, 0x31B46, 0x31B55};
    m.venue_song = sound::Song::parse(read_installed(content, file_name(venue_songs[venue - 1])));
    m.hurry_song = sound::Song::parse(read_installed(content, file_name(0x31ACA)));
    const auto embedded = [&](std::uint32_t address) {
        const std::size_t at = address - sprite_block_address;
        if (at >= m.sprites.size()) throw std::runtime_error("The sprite block is cut short.");
        return sound::Song::parse(std::span<const std::uint8_t>(m.sprites).subspan(at));
    };
    m.a_won_song = embedded(0x78A14);
    m.b_won_song = embedded(0x7906C);
    return m;
}

} // namespace beach_volley::match
