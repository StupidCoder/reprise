#include "c64/kernal_tape.hpp"
#include <optional>
#include <stdexcept>

namespace c64 {
namespace {
enum class PulseKind { short_pulse, medium, long_pulse, other };

// Generous windows around the nominal lengths; anything outside (a fast
// loader's pulses, noise, a pause) is `other`.
PulseKind classify(const TapePulse& pulse) {
    if (pulse.pause) {
        return PulseKind::other;
    }
    if (pulse.cycles >= 288 && pulse.cycles < 456) {
        return PulseKind::short_pulse;
    }
    if (pulse.cycles >= 456 && pulse.cycles < 608) {
        return PulseKind::medium;
    }
    if (pulse.cycles >= 608 && pulse.cycles < 800) {
        return PulseKind::long_pulse;
    }
    return PulseKind::other;
}

struct Block {
    bool repeat{};                     // the second recording ($09..$01 countdown)
    std::vector<std::uint8_t> payload; // without countdown and checksum
    bool checksum_ok{};
    std::size_t end_pulse{};
};

class BlockReader {
public:
    explicit BlockReader(const std::vector<TapePulse>& pulses) : pulses_(pulses) {}

    // Every block on the tape; stretches that do not decode are skipped.
    std::vector<Block> all_blocks() {
        std::vector<Block> blocks;
        std::size_t i = 0;
        while (i < pulses_.size()) {
            if (kind(i) != PulseKind::long_pulse) {
                ++i;
                continue;
            }
            if (auto block = read_block(i)) {
                i = block->end_pulse;
                blocks.push_back(std::move(*block));
            } else {
                ++i;
            }
        }
        return blocks;
    }

private:
    enum class Frame { byte, end_of_block, bad };

    PulseKind kind(std::size_t i) const {
        return i < pulses_.size() ? classify(pulses_[i]) : PulseKind::other;
    }

    // One byte frame starting at pulse i; advances i past it.
    Frame read_byte(std::size_t& i, std::uint8_t& value) const {
        if (kind(i) != PulseKind::long_pulse) {
            return Frame::bad;
        }
        if (kind(i + 1) == PulseKind::short_pulse) {
            i += 2;
            return Frame::end_of_block;
        }
        if (kind(i + 1) != PulseKind::medium) {
            return Frame::bad;
        }
        i += 2;
        value = 0;
        int parity = 1; // odd parity
        for (int bit = 0; bit < 9; ++bit) {
            const auto first = kind(i), second = kind(i + 1);
            i += 2;
            int d;
            if (first == PulseKind::short_pulse && second == PulseKind::medium) {
                d = 0;
            } else if (first == PulseKind::medium && second == PulseKind::short_pulse) {
                d = 1;
            } else {
                return Frame::bad;
            }
            if (bit < 8) {
                value |= std::uint8_t(d << bit);
                parity ^= d;
            } else if (d != parity) {
                return Frame::bad;
            }
        }
        return Frame::byte;
    }

    // A block whose first byte marker is at pulse i, if a countdown follows.
    std::optional<Block> read_block(std::size_t i) const {
        std::vector<std::uint8_t> bytes;
        std::uint8_t value{};
        while (read_byte(i, value) == Frame::byte) {
            bytes.push_back(value);
            if (bytes.size() <= 9) {
                const auto expected = std::uint8_t(10 - bytes.size()); // $09, $08, ...
                if (value != expected && value != (expected | 0x80)) {
                    return std::nullopt;
                }
            }
        }
        if (bytes.size() < 11) { // countdown, at least one payload byte, checksum
            return std::nullopt;
        }
        Block block;
        block.repeat = bytes[0] == 0x09;
        block.payload.assign(bytes.begin() + 9, bytes.end() - 1);
        std::uint8_t sum = 0;
        for (auto b : block.payload) {
            sum ^= b;
        }
        block.checksum_ok = sum == bytes.back();
        block.end_pulse = i;
        return block;
    }

    const std::vector<TapePulse>& pulses_;
};

// The two recordings of one block.
struct Recordings {
    const Block* first{};
    const Block* repeat{};

    const Block& good(const char* what) const {
        if (first && first->checksum_ok) {
            return *first;
        }
        if (repeat && repeat->checksum_ok) {
            return *repeat;
        }
        throw std::runtime_error(std::string("The tape's ") + what +
                                 " block is damaged in both recordings.");
    }
};

bool looks_like_header(const Block& block) {
    return block.payload.size() == 192 && block.payload[0] >= 1 && block.payload[0] <= 5;
}
} // namespace

std::vector<KernalFile> read_kernal_files(const TapeImage& tape) {
    const auto blocks = BlockReader(tape.pulses).all_blocks();
    std::vector<KernalFile> files;

    Recordings header, data;
    std::size_t end_pulse = 0;
    auto finish = [&] {
        if (!header.first && !header.repeat) {
            return;
        }
        const auto& h = header.good("header").payload;
        const auto type = h[0];
        if ((data.first || data.repeat) && (type == 1 || type == 3 || type == 4)) {
            KernalFile file;
            file.type = KernalFile::Type(type);
            file.start = std::uint16_t(h[1] | h[2] << 8);
            file.end = std::uint16_t(h[3] | h[4] << 8);
            file.name.assign(h.begin() + 5, h.begin() + 21);
            file.name.erase(file.name.find_last_not_of(' ') + 1);
            file.header_tail.assign(h.begin() + 21, h.end());
            file.data = data.good("data").payload;
            file.end_pulse = end_pulse;
            files.push_back(std::move(file));
        }
        header = {};
        data = {};
    };

    for (const auto& block : blocks) {
        // After a header, the next blocks are its data, whatever they contain.
        const bool has_header = header.first || header.repeat;
        const bool expecting_data = has_header && !data.first && !data.repeat;
        if (expecting_data && block.repeat && looks_like_header(block) && !header.repeat) {
            header.repeat = &block;
        } else if (has_header && block.repeat && data.first && !data.repeat) {
            data.repeat = &block;
        } else if (!expecting_data && looks_like_header(block)) {
            finish();
            (block.repeat ? header.repeat : header.first) = &block;
        } else if (has_header) {
            (block.repeat ? data.repeat : data.first) = &block;
        } else {
            continue; // a data block without its header
        }
        end_pulse = block.end_pulse;
    }
    finish();
    return files;
}

} // namespace c64
