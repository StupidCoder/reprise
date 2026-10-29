// Reading a byte buffer as a stream of bits, most significant bit first (AAC and SBR syntax).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace doc::media {

class BitReader {
public:
    explicit BitReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    // The next `count` bits (0..32) as an unsigned number; zeros past the end (see overrun()).
    std::uint32_t read(int count) {
        std::uint32_t value = 0;
        for (int i = 0; i < count; ++i) value = (value << 1) | bit();
        return value;
    }
    std::uint32_t bit() {
        const std::size_t byte = position_ >> 3;
        if (byte >= bytes_.size()) {
            overrun_ = true;
            ++position_;
            return 0;
        }
        const std::uint32_t b = (bytes_[byte] >> (7 - (position_ & 7))) & 1u;
        ++position_;
        return b;
    }
    void skip(std::size_t bits) { position_ += bits; }
    void alignToByte() { position_ = (position_ + 7) & ~std::size_t(7); }
    std::size_t position() const { return position_; }
    std::size_t bitsLeft() const { return position_ >= bytes_.size() * 8 ? 0 : bytes_.size() * 8 - position_; }
    // Whether more bits were read than the buffer holds (a damaged frame).
    bool overrun() const { return overrun_ || position_ > bytes_.size() * 8; }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
    bool overrun_ = false;
};

}  // namespace doc::media
