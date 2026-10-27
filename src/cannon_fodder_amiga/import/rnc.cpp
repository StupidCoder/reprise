#include "cannon_fodder_amiga/import/rnc.hpp"
#include "amiga/adf.hpp"
#include <array>
#include <stdexcept>

namespace fodder::import {
namespace {

constexpr std::size_t header_size = 18;

[[noreturn]] void damaged() {
    throw std::runtime_error("A packed file on the disk is damaged.");
}

// The bit reader keeps at least 16 bits ahead. Bits come from little-endian
// 16-bit words, lowest bit first. Literal bytes are taken from the byte
// position the reader stands at, after which the buffer is refilled there.
class BitReader {
public:
    BitReader(std::span<const std::uint8_t> data, std::size_t at) : data_(data), at_(at) {
        buffer_ = word32(at_);
        count_ = 16;
    }

    unsigned peek(int bits) const { return unsigned(buffer_ & ((1ull << bits) - 1)); }

    void skip(int bits) {
        buffer_ >>= bits;
        count_ -= bits;
        if (count_ < 16) {
            at_ += 2;
            buffer_ |= std::uint64_t(word32(at_)) << count_;
            count_ += 16;
        }
    }

    unsigned read(int bits) {
        const auto value = peek(bits);
        skip(bits);
        return value;
    }

    // Copies `count` literal bytes from the stream and re-reads the bits that
    // follow them.
    void copy_bytes(std::size_t count, std::vector<std::uint8_t>& out) {
        if (at_ + count > data_.size()) {
            damaged();
        }
        out.insert(out.end(), data_.begin() + std::ptrdiff_t(at_), data_.begin() + std::ptrdiff_t(at_ + count));
        at_ += count;
        count_ -= 16;
        buffer_ &= (1ull << count_) - 1;
        buffer_ |= std::uint64_t(word32(at_)) << count_;
        count_ += 16;
    }

private:
    std::uint32_t word32(std::size_t at) const {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            if (at + std::size_t(i) < data_.size()) {
                value |= std::uint32_t(data_[at + std::size_t(i)]) << (8 * i);
            }
        }
        return value;
    }

    std::span<const std::uint8_t> data_;
    std::size_t at_;
    std::uint64_t buffer_{};
    int count_{};
};

// A canonical Huffman table. Codes are assigned in order of length, then of
// symbol, and stored bit-reversed because the stream is read lowest bit first.
// A decoded symbol n >= 2 stands for a value with n - 1 further bits:
// (1 << (n - 1)) | bits.
class HuffmanTable {
public:
    explicit HuffmanTable(BitReader& bits) {
        const auto symbols = bits.read(5);
        std::array<unsigned, 32> lengths{};
        for (unsigned i = 0; i < symbols; ++i) {
            lengths[i] = bits.read(4);
        }
        unsigned code = 0;
        for (unsigned length = 1; length <= 16; ++length) {
            for (unsigned symbol = 0; symbol < symbols; ++symbol) {
                if (lengths[symbol] == length) {
                    unsigned reversed = 0;
                    for (unsigned b = 0; b < length; ++b) {
                        reversed |= (code >> b & 1) << (length - 1 - b);
                    }
                    entries_.push_back({reversed, int(length), symbol});
                    ++code;
                }
            }
            code <<= 1;
        }
    }

    unsigned decode(BitReader& bits) const {
        for (const auto& entry : entries_) {
            if (bits.peek(entry.length) == entry.code) {
                bits.skip(entry.length);
                if (entry.symbol < 2) {
                    return entry.symbol;
                }
                const int extra = int(entry.symbol) - 1;
                return (1u << extra) | bits.read(extra);
            }
        }
        damaged();
    }

private:
    struct Entry {
        unsigned code;
        int length;
        unsigned symbol;
    };
    std::vector<Entry> entries_;
};

} // namespace

bool is_rnc(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= header_size && bytes[0] == 'R' && bytes[1] == 'N' && bytes[2] == 'C' && bytes[3] == 1;
}

std::uint16_t rnc_crc(std::span<const std::uint8_t> bytes) {
    std::uint16_t crc = 0;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 1) ? std::uint16_t(crc >> 1 ^ 0xa001) : std::uint16_t(crc >> 1);
        }
    }
    return crc;
}

std::vector<std::uint8_t> unpack_rnc(std::span<const std::uint8_t> packed) {
    if (!is_rnc(packed)) {
        damaged();
    }
    const auto unpacked_size = amiga::read_be32(packed, 4);
    const auto packed_size = amiga::read_be32(packed, 8);
    const auto unpacked_crc = amiga::read_be16(packed, 12);
    const auto packed_crc = amiga::read_be16(packed, 14);
    if (header_size + packed_size > packed.size() || unpacked_size > (16u << 20)) {
        damaged();
    }
    const auto body = packed.subspan(0, header_size + packed_size);
    if (rnc_crc(body.subspan(header_size)) != packed_crc) {
        damaged();
    }
    std::vector<std::uint8_t> out;
    out.reserve(unpacked_size);
    BitReader bits(body, header_size);
    bits.skip(2); // two flag bits (lock, key), unused by these files
    while (out.size() < unpacked_size) {
        const auto before = out.size();
        const HuffmanTable literals(bits), distances(bits), lengths(bits);
        int runs = int(bits.read(16));
        for (;;) {
            if (const auto count = literals.decode(bits); count > 0) {
                bits.copy_bytes(count, out);
            }
            if (--runs <= 0) {
                break;
            }
            const std::size_t distance = distances.decode(bits) + 1;
            const std::size_t length = lengths.decode(bits) + 2;
            if (distance > out.size()) {
                damaged();
            }
            for (std::size_t i = 0; i < length; ++i) {
                out.push_back(out[out.size() - distance]);
            }
        }
        if (out.size() == before || out.size() > unpacked_size + 0x10000) {
            damaged();
        }
    }
    out.resize(unpacked_size);
    if (rnc_crc(out) != unpacked_crc) {
        damaged();
    }
    return out;
}

} // namespace fodder::import
