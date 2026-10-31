#include "stunt_car_racer_amiga/league/records.hpp"

namespace stuntcar::league {
namespace {

int from_bcd(std::uint8_t v) { return (v >> 4) * 10 + (v & 15); }
std::uint8_t to_bcd(int v) { return std::uint8_t((v / 10) << 4 | (v % 10)); }

bool offer(Record& r, const std::string& holder, race::Time t) {
    if (r.time && !faster(t, *r.time)) return false;
    r.holder = holder;
    r.time = t;
    return true;
}

} // namespace

race::Time add_times(race::Time a, race::Time b) {
    int hundredths = from_bcd(a.hundredths) + from_bcd(b.hundredths);
    int seconds = from_bcd(a.seconds) + from_bcd(b.seconds) + hundredths / 100;
    const int minutes = from_bcd(a.minutes) + from_bcd(b.minutes) + seconds / 60;
    hundredths %= 100, seconds %= 60;
    return {to_bcd(minutes % 100), to_bcd(seconds), to_bcd(hundredths)};
}

bool faster(race::Time a, race::Time b) {
    if (a.minutes != b.minutes) return a.minutes < b.minutes;
    if (a.seconds != b.seconds) return a.seconds < b.seconds;
    return a.hundredths < b.hundredths;
}

bool Records::offer_lap(bool super, int track, const std::string& holder, race::Time t) {
    return offer(laps_[super][std::size_t(track)], holder, t);
}

bool Records::offer_race(bool super, int track, const std::string& holder, race::Time t) {
    return offer(races_[super][std::size_t(track)], holder, t);
}

} // namespace stuntcar::league
