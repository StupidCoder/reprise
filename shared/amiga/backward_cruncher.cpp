#include "amiga/backward_cruncher.hpp"
#include "amiga/adf.hpp"
#include <stdexcept>

namespace amiga {
namespace {

class BitReader {
public:
    BitReader(std::span<const std::uint8_t> stream, std::uint32_t checksum)
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
        for (int i = 0; i < count; ++i) value = value << 1 | bit();
        return value;
    }

    bool checksum_ok() const { return check_ == 0; }

private:
    std::uint32_t load() {
        if (next_ < 4) throw std::runtime_error("A crunched file ends early.");
        next_ -= 4;
        const std::uint32_t v = read_be32(stream_, next_);
        check_ ^= v;
        return v;
    }

    std::span<const std::uint8_t> stream_;
    std::size_t next_;
    std::uint32_t bits_{};
    std::uint32_t check_;
};

} // namespace

std::vector<std::uint8_t> uncrunch_backward(std::span<const std::uint8_t> stream, std::size_t unpacked_length,
                                            std::uint32_t checksum) {
    if (stream.size() % 4 != 0 || stream.size() < 4) throw std::runtime_error("A crunched file is malformed.");
    std::vector<std::uint8_t> out(unpacked_length);
    BitReader in(stream, checksum);
    std::size_t at = out.size();
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
            if (in.bit() == 0) literal(in.bits(3) + 1);
            else copy(2, 8);
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
    if (!in.checksum_ok()) throw std::runtime_error("A crunched file fails its checksum (the disk may be damaged).");
    return out;
}

} // namespace amiga
