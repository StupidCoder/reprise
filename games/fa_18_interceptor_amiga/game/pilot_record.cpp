#include "fa_18_interceptor_amiga/game/pilot_record.hpp"
#include <fstream>
#include <iterator>

namespace interceptor::game {
namespace {

constexpr std::size_t call_sign_at = 0x1E, call_sign_size = 24;

std::uint16_t word(const std::vector<std::uint8_t>& b, std::size_t at) {
    return std::uint16_t(b[at] << 8 | b[at + 1]);
}

void put_word(std::vector<std::uint8_t>& b, std::size_t at, std::uint16_t v) {
    b[at] = std::uint8_t(v >> 8);
    b[at + 1] = std::uint8_t(v);
}

} // namespace

PilotRecord PilotRecord::decode(const std::vector<std::uint8_t>& b) {
    PilotRecord p;
    if (b.size() < size) return p;
    p.qualified = word(b, 0) != 0;
    p.level = word(b, 2);
    p.launches = word(b, 4);
    p.last_mission = b[6];
    p.last_stage = b[7];
    p.flight_seconds = std::uint32_t(word(b, 8)) << 16 | word(b, 10);
    for (std::size_t i = 0; i < p.bytes_c.size(); ++i) p.bytes_c[i] = b[0xC + i];
    for (std::size_t i = 0; i < call_sign_size && b[call_sign_at + i] != 0; ++i)
        p.call_sign.push_back(char(b[call_sign_at + i]));
    std::uint16_t* log[] = {&p.missions_started, &p.missions_completed, &p.cannon_fired, &p.cannon_hits,
                            &p.amraams_fired, &p.amraam_hits, &p.sidewinders_fired, &p.sidewinder_hits,
                            &p.crashes, &p.missile_hits};
    for (std::size_t i = 0; i < std::size(log); ++i) *log[i] = word(b, 0x36 + 2 * i);
    for (std::size_t i = 0; i < p.tail.size(); ++i) p.tail[i] = b[0x4A + i];
    return p;
}

std::vector<std::uint8_t> PilotRecord::encode() const {
    std::vector<std::uint8_t> b(size, 0);
    put_word(b, 0, qualified ? 1 : 0);
    put_word(b, 2, level);
    put_word(b, 4, launches);
    b[6] = last_mission;
    b[7] = last_stage;
    put_word(b, 8, std::uint16_t(flight_seconds >> 16));
    put_word(b, 10, std::uint16_t(flight_seconds));
    for (std::size_t i = 0; i < bytes_c.size(); ++i) b[0xC + i] = bytes_c[i];
    for (std::size_t i = 0; i < call_sign.size() && i < call_sign_size; ++i) b[call_sign_at + i] = std::uint8_t(call_sign[i]);
    const std::uint16_t log[] = {missions_started, missions_completed, cannon_fired, cannon_hits, amraams_fired,
                                 amraam_hits, sidewinders_fired, sidewinder_hits, crashes, missile_hits};
    for (std::size_t i = 0; i < std::size(log); ++i) put_word(b, 0x36 + 2 * i, log[i]);
    for (std::size_t i = 0; i < tail.size(); ++i) b[0x4A + i] = tail[i];
    return b;
}

PilotRecord read_pilot(const std::filesystem::path& content) {
    std::ifstream in(content / "config", std::ios::binary);
    if (!in) return {};
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
    return PilotRecord::decode(bytes);
}

bool write_pilot(const std::filesystem::path& content, const PilotRecord& pilot) {
    std::ofstream out(content / "config", std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const auto bytes = pilot.encode();
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return bool(out);
}

} // namespace interceptor::game
