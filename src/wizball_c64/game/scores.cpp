#include "wizball_c64/game/scores.hpp"

namespace wizball::game {
namespace {
constexpr std::uint16_t records_at = 0xBDB9, top_at = 0xBFEB, top_name_at = 0xB6AA, block_rows_at = 0xBEDF;
} // namespace

Scores::Scores(const data::GameImage& image) : image_(image) {
    for (int r = 0; r < 18; ++r)
        for (int i = 0; i < 14; ++i) records_[std::size_t(r)][std::size_t(i)] = image.byte(std::uint16_t(records_at + 14 * r + i));
    for (int i = 0; i < 6; ++i) top_[std::size_t(i)] = image.byte(std::uint16_t(top_at + i));
    for (int i = 0; i < 10; ++i) top_name_[std::size_t(i)] = image.byte(std::uint16_t(top_name_at + i));
    for (int c = 0; c < 2; ++c)
        for (int row = 0; row < 6; ++row)
            for (int i = 0; i < 14; ++i)
                block_[std::size_t(c)][std::size_t(row)][std::size_t(i)] =
                    image.byte(std::uint16_t(block_rows_at + 40 * row + 20 * c + i));
    best_table_ = image.byte(0xBDA5);
}

void Scores::sort(int first) {
    // $BC23: a bubble pass from the first record, starting over after
    // each swap; digits compared as text (a larger digit first).
    const int end = first == 0 ? 8 : 0x11;
    int k = first;
    while (k != end) {
        const auto& a = record(k);
        const auto& b = record(k + 1);
        int order = 0;
        for (int i = 0; i < 6 && order == 0; ++i) {
            if (a[std::size_t(i)] == b[std::size_t(i)]) continue;
            order = std::int8_t(std::uint8_t(a[std::size_t(i)] - b[std::size_t(i)])) < 0 ? -1 : 1;
        }
        if (order < 0) {
            std::swap(record(k), record(k + 1));
            k = first;
            continue;
        }
        ++k;
    }
}

std::array<std::uint8_t, 2> Scores::enter(const Entry& first, const Entry* second) {
    // $BBA0: each player's score into the spare record of the table, its
    // name the player's number for now.
    auto put = [&](const Entry& e, std::uint8_t marker) {
        auto& r = record(e.one_player_table ? 8 : 17);
        for (int i = 0; i < 6; ++i) r[std::size_t(i)] = std::uint8_t(e.digits[std::size_t(i)] + 0x30);
        r[7] = marker;
        sort(0);
        sort(9);
    };
    put(first, 0x31);
    if (second) put(*second, 0x32);
    // $BC05: which table holds the best score (compared as text).
    best_table_ = 2;
    for (int i = 0; i < 14; ++i) {
        const auto a = record(0)[std::size_t(i)], b = record(9)[std::size_t(i)];
        if (a == b) continue;
        best_table_ = std::int8_t(std::uint8_t(a - b)) < 0 ? 2 : 1;
        break;
    }
    // $BC90: the places of the marked records.
    std::array<std::uint8_t, 2> ranks{};
    for (int y = 0; y < 8; ++y) {
        for (int table = 0; table < 2; ++table) {
            const auto marker = record(9 * table + y)[7];
            if (marker != 0x31 && marker != 0x32) continue;
            auto& rank = ranks[std::size_t(marker - 0x31)];
            const int best = table + 1;
            if (y == 0 && best_table_ == best) rank = std::uint8_t(1 + 2 * table);
            else if (y != 7 || best_table_ == best) rank = std::uint8_t(2 + 2 * table);
        }
    }
    return ranks;
}

void Scores::name(std::uint8_t marker, const std::array<std::uint8_t, 7>& name) {
    // $BD32
    for (auto& r : records_) {
        if (r[7] != marker) continue;
        for (int i = 0; i < 7; ++i) r[std::size_t(7 + i)] = name[std::size_t(i)];
        break;
    }
    rebuild_block();
}

void Scores::rebuild_block() {
    // $BD52: the best table's first record is the top line; each column
    // shows the next six records of its table.
    for (int c = 0; c < 2; ++c) {
        int first = 9 * c;
        if (best_table_ == c + 1) {
            for (int i = 0; i < 6; ++i) top_[std::size_t(i)] = record(first)[std::size_t(i)]; // $BD96
            ++first;
        }
        for (int row = 0; row < 6; ++row) block_[std::size_t(c)][std::size_t(row)] = record(first + row);
    }
}

std::uint8_t Scores::block_byte(std::uint16_t address) const {
    for (int c = 0; c < 2; ++c)
        for (int row = 0; row < 6; ++row) {
            const int at = block_rows_at + 40 * row + 20 * c;
            if (address >= at && address < at + 14) return block_[std::size_t(c)][std::size_t(row)][std::size_t(address - at)];
        }
    if (address >= top_at && address < top_at + 6) return top_[std::size_t(address - top_at)];
    return image_.byte(address);
}

std::uint8_t Scores::top_name_byte(std::uint16_t address) const {
    if (address >= top_name_at && address < top_name_at + 10) return top_name_[std::size_t(address - top_name_at)];
    return image_.byte(address);
}

std::array<std::uint8_t, 6> Scores::top_digits() const {
    std::array<std::uint8_t, 6> d{};
    for (int i = 0; i < 6; ++i) d[std::size_t(i)] = std::uint8_t(top_[std::size_t(i)] - 0x30);
    return d;
}

} // namespace wizball::game
