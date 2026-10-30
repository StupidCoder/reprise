#include "rock_n_roll_amiga/import/packers.hpp"
#include "amiga/adf.hpp"
#include <stdexcept>

namespace rocknroll::import {
namespace {

[[noreturn]] void damaged() {
    throw std::runtime_error("The disk's packed data is damaged.");
}

// Reads a buffer from its end towards its start.
class Backwards {
public:
    explicit Backwards(std::span<const std::uint8_t> bytes) : bytes_(bytes), at_(bytes.size()) {}
    std::uint8_t byte() {
        if (at_ == 0) damaged();
        return bytes_[--at_];
    }
    std::uint32_t long_word() {
        if (at_ < 4) damaged();
        at_ -= 4;
        return amiga::read_be32(bytes_, at_);
    }
    bool done() const { return at_ == 0; }
    bool has_long() const { return at_ >= 4; }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t at_;
};

// Writes a buffer from its end towards its start; copies take bytes already
// written above the write position.
class Output {
public:
    explicit Output(std::size_t size) : bytes_(size), at_(size) {}
    void put(std::uint8_t b) {
        if (at_ == 0) damaged();
        bytes_[--at_] = b;
    }
    void copy(std::size_t offset, int count) {
        for (int i = 0; i < count; ++i) {
            if (at_ == 0 || at_ - 1 + offset >= bytes_.size()) damaged();
            const std::uint8_t b = bytes_[at_ - 1 + offset];
            bytes_[--at_] = b;
        }
    }
    bool full() const { return at_ == 0; }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
    std::size_t at_;
};

// The LZ unpackers' bits: longs taken from the end, each shifted out from its
// low end; a long is used up when only its marker bit is left, and the next
// one is shifted in behind a new marker. Fields are read high bit first.
class LzBits {
public:
    LzBits(Backwards& source, std::uint32_t first) : source_(source), bits_(first) {}
    int bit() {
        int b = int(bits_ & 1);
        bits_ >>= 1;
        if (bits_ == 0) {
            const std::uint32_t v = source_.long_word();
            b = int(v & 1);
            bits_ = v >> 1 | 0x80000000u;
        }
        return b;
    }
    int bits(int count) {
        int value = 0;
        for (int i = 0; i < count; ++i) value = value << 1 | bit();
        return value;
    }

private:
    Backwards& source_;
    std::uint32_t bits_;
};

} // namespace

std::vector<std::uint8_t> unpack_boot(std::span<const std::uint8_t> header, std::span<const std::uint8_t> stream) {
    if (header.size() < 0x24) damaged();
    const std::size_t unpacked = amiga::read_be32(header, 0x00);
    const std::size_t after_pass1 = amiga::read_be32(header, 0x04);
    const std::size_t coded = amiga::read_be32(header, 0x0C);
    const std::size_t tree_size = amiga::read_be32(header, 0x14);
    const auto tree = header.subspan(0x24);
    if (unpacked > (1u << 20) || after_pass1 > unpacked || coded > after_pass1 || tree_size / 2 + 2 > tree.size()) damaged();

    // Huffman ($50074-$500BE): bits from the low end of each long, longs from
    // the end; the next long is fetched as soon as the current one is begun.
    std::vector<std::uint8_t> symbols;
    {
        Backwards source(stream);
        std::uint32_t current = source.long_word();
        std::uint32_t next = source.long_word();
        int left = 32;
        Output out(coded);
        while (!out.full()) {
            std::size_t node = tree_size / 2;
            for (;;) {
                const int bit = int(current & 1);
                current >>= 1;
                if (--left == 0) {
                    left = 32;
                    current = next;
                    next = source.has_long() ? source.long_word() : 0; // read ahead, unused at the end
                }
                node += std::size_t(bit ^ 1) * 2;
                if (node + 1 >= tree.size()) damaged();
                const std::uint16_t value = amiga::read_be16(tree, node);
                if (value < 0x400) {
                    node = value;
                    continue;
                }
                out.put(std::uint8_t(value));
                break;
            }
        }
        symbols = out.take();
    }

    // Pass 1 ($500C0-$5013C): an escape byte, then its argument; an argument
    // of zero stands for the escape byte itself.
    std::vector<std::uint8_t> pass1;
    {
        Backwards in(symbols);
        Output out(after_pass1);
        while (!in.done()) {
            const std::uint8_t b = in.byte();
            if (b == header[0x22]) { // two bytes, twice
                const std::uint8_t second = in.byte();
                if (second == 0) { out.put(b); continue; }
                const std::uint8_t first = in.byte();
                out.put(first), out.put(second), out.put(first), out.put(second);
            } else if (b == header[0x21] || b == header[0x23]) { // 3 or 4 bytes from 8-bit offset
                const std::uint8_t offset = in.byte();
                if (offset == 0) { out.put(b); continue; }
                out.copy(offset, b == header[0x21] ? 3 : 4);
            } else if (b == header[0x20] || b == header[0x0B]) { // 4 or n+1 bytes from 16-bit offset
                int count = 4;
                if (b == header[0x0B]) {
                    count = in.byte() + 1;
                    if (count == 1) { out.put(b); continue; }
                }
                const std::uint8_t low = in.byte();
                if (b == header[0x20] && low == 0) { out.put(b); continue; }
                const std::uint8_t high = in.byte();
                out.copy(std::size_t(high) << 8 | low, count);
            } else {
                out.put(b);
            }
        }
        if (!out.full()) damaged();
        pass1 = out.take();
    }

    // Pass 2 ($5013E-$50194): runs. The unpacker reads one byte ahead.
    Backwards in(pass1);
    Output out(unpacked);
    auto read = [&] { return in.done() ? std::uint8_t(0) : in.byte(); };
    std::uint8_t b = in.byte();
    while (!out.full()) {
        if (b != header[0x08] && b != header[0x09]) {
            const std::uint8_t literal = b;
            b = read();
            out.put(literal);
            continue;
        }
        const int count = in.byte() + 1;
        std::uint8_t fill;
        if (count < 4) { // the escape byte itself, 1-3 times
            fill = b;
            b = read();
        } else if (b == header[0x09]) {
            b = read();
            fill = header[0x0A];
        } else {
            fill = in.byte();
            b = read();
        }
        for (int i = 0; i < count && !out.full(); ++i) out.put(fill);
    }
    return out.take();
}

std::vector<std::uint8_t> unpack_loader(std::span<const std::uint8_t> stream) {
    Backwards source(stream);
    const std::size_t size = source.long_word();
    if (size > (1u << 20)) damaged();
    LzBits in(source, source.long_word());
    Output out(size);
    for (;;) {
        int literals = in.bits(3);
        if (literals == 7) {
            if (in.bit()) {
                literals = in.bits(10);
                if (literals == 0) literals = in.bits(18);
            } else {
                literals = in.bits(4) + 7;
            }
        }
        for (int i = 0; i < literals; ++i) out.put(std::uint8_t(in.bits(8)));
        if (out.full()) break;

        int count = 2, width = 8;
        const auto long_offset = [&] { return !in.bit() ? 16 : in.bit() ? 8 : 12; };
        switch (in.bits(2)) {
        case 0: break;
        case 1: count = 3, width = in.bit() ? 8 : 14; break;
        case 2: count = 4, width = long_offset(); break;
        default: {
            const int kind = in.bits(2);
            count = kind == 3 ? in.bits(8) : kind == 2 ? in.bits(2) + 7 : kind + 5;
            width = long_offset();
        }
        }
        out.copy(std::size_t(in.bits(width)), count);
    }
    return out.take();
}

std::vector<std::uint8_t> unpack_defjam(std::span<const std::uint8_t> stream, std::span<const std::uint8_t> literals) {
    if (literals.size() < 264) damaged();
    Backwards source(stream);
    const std::size_t size = source.long_word();
    if (size > (1u << 20)) damaged();
    LzBits in(source, source.long_word());
    Output lz(size);
    for (;;) {
        int run = in.bits(3);
        if (run == 7) {
            if (in.bit()) {
                run = in.bits(8);
                if (run != 0) {
                    run += 10;
                } else {
                    run = in.bits(12);
                    run = run != 0 ? run + 0x109 : in.bits(15) + 0x110B;
                }
            } else {
                run = in.bits(2) + 7;
            }
        }
        for (int i = 0; i < run; ++i) {
            // Four classes of table entries: 0-7, 8-71, 72-135, 136-263.
            int base, width;
            if (!in.bit()) base = 8, width = 6;
            else if (!in.bit()) base = 0, width = 3;
            else if (!in.bit()) base = 0x48, width = 6;
            else base = 0x88, width = 7;
            lz.put(literals[std::size_t(base + in.bits(width))]);
        }
        if (lz.full()) break;

        int count; // bytes copied minus one
        if (!in.bit()) {
            count = in.bit() ? 1 : 2;
        } else {
            switch (in.bits(2)) {
            case 0: count = 3; break;
            case 1: count = 4 + in.bits(1); break;
            case 2: count = 6 + in.bits(3); break;
            default: count = in.bits(8) + 14;
            }
        }
        lz.copy(std::size_t(in.bits(9)), count + 1);
    }

    // The run-length pass ($7F000-$7F02A), forwards.
    const auto packed = lz.take();
    std::vector<std::uint8_t> out;
    out.reserve(packed.size() * 3 / 2);
    for (std::size_t i = 0; i < packed.size();) {
        std::uint8_t b = packed[i++];
        if (b == 0x95) {
            if (i >= packed.size()) damaged();
            const int n = packed[i++];
            if (n != 0) {
                if (i >= packed.size()) damaged();
                b = packed[i++];
                out.insert(out.end(), std::size_t(n) + 2, b);
            }
        }
        out.push_back(b);
    }
    return out;
}

} // namespace rocknroll::import
