#include "rock_n_roll_amiga/import/cruncher.hpp"
#include "amiga/adf.hpp"
#include <stdexcept>

namespace rocknroll::import {
namespace {

constexpr std::size_t header_size = 12;

// The bit stream, consumed from its end (Main's unpacker, hunk +$1A..+$E6).
class BitReader {
public:
    explicit BitReader(std::span<const std::uint8_t> stream, std::uint32_t checksum)
        : stream_(stream), next_(stream.size()), check_(checksum) {
        bits_ = load(); // the first long carries its own end marker
    }

    int bit() {
        int b = int(bits_ & 1);
        bits_ >>= 1;
        if (bits_ == 0) {
            const std::uint32_t v = load();
            b = int(v & 1);
            bits_ = v >> 1 | 0x80000000u; // the marker that ends this long
        }
        return b;
    }

    int bits(int count) {
        int value = 0;
        for (int i = 0; i < count; ++i) {
            value = value << 1 | bit();
        }
        return value;
    }

    bool checksum_ok() const { return check_ == 0; }

private:
    std::uint32_t load() {
        if (next_ < 4) {
            throw std::runtime_error("A crunched file ends early.");
        }
        next_ -= 4;
        const std::uint32_t v = amiga::read_be32(stream_, next_);
        check_ ^= v;
        return v;
    }

    std::span<const std::uint8_t> stream_;
    std::size_t next_;
    std::uint32_t bits_{};
    std::uint32_t check_;
};

} // namespace

bool is_crunched(std::span<const std::uint8_t> file) {
    if (file.size() < header_size + 4) {
        return false;
    }
    const std::uint32_t packed = amiga::read_be32(file, 0);
    const std::uint32_t unpacked = amiga::read_be32(file, 4);
    return packed % 4 == 0 && packed + header_size <= file.size() && file.size() - packed - header_size < 4 &&
           unpacked >= packed && unpacked < 0x100000;
}

std::vector<std::uint8_t> uncrunch(std::span<const std::uint8_t> file) {
    if (!is_crunched(file)) {
        throw std::runtime_error("A file is not in the game's crunched format.");
    }
    const std::uint32_t packed = amiga::read_be32(file, 0);
    std::vector<std::uint8_t> out(amiga::read_be32(file, 4));
    BitReader in(file.subspan(header_size, packed), amiga::read_be32(file, 8));
    std::size_t at = out.size(); // written from the end down
    const auto literal = [&](int count) {
        for (int i = 0; i < count; ++i) {
            if (at == 0) throw std::runtime_error("A crunched file unpacks to more bytes than it says.");
            out[--at] = std::uint8_t(in.bits(8));
        }
    };
    const auto copy = [&](int count, int offset_bits) {
        const std::size_t offset = std::size_t(in.bits(offset_bits));
        for (int i = 0; i < count; ++i) {
            if (at == 0 || at - 1 + offset >= out.size()) {
                throw std::runtime_error("A crunched file refers outside its own data.");
            }
            --at;
            out[at] = out[at + offset];
        }
    };
    while (at > 0) {
        if (in.bit() == 0) {
            if (in.bit() == 0) {
                literal(in.bits(3) + 1);
            } else {
                copy(2, 8);
            }
            continue;
        }
        switch (in.bits(2)) {
        case 0: copy(3, 9); break;
        case 1: copy(4, 10); break;
        case 2: {
            const int count = in.bits(8) + 1;
            copy(count, 12);
            break;
        }
        default: literal(in.bits(8) + 9); break;
        }
    }
    if (!in.checksum_ok()) {
        throw std::runtime_error("A crunched file fails its checksum (the disk may be damaged).");
    }
    return out;
}

} // namespace rocknroll::import
