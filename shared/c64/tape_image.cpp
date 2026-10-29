#include "c64/tape_image.hpp"
#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace c64 {
namespace {
constexpr std::size_t header_size = 20;
constexpr std::string_view signatures[] = {"C64-TAPE-RAW", "C16-TAPE-RAW"};
} // namespace

bool is_tap(std::span<const std::uint8_t> file) {
    if (file.size() < header_size) {
        return false;
    }
    const std::string_view start(reinterpret_cast<const char*>(file.data()), 12);
    return std::ranges::find(signatures, start) != std::end(signatures);
}

TapeImage read_tap(std::span<const std::uint8_t> file) {
    if (!is_tap(file)) {
        throw std::runtime_error("This is not a TAP tape image.");
    }
    TapeImage tape;
    tape.version = file[12];
    if (tape.version > 1) {
        throw std::runtime_error("This TAP image uses an unknown format version (" +
                                 std::to_string(tape.version) + ").");
    }
    const std::uint32_t length = std::uint32_t(file[16]) | std::uint32_t(file[17]) << 8 |
                                 std::uint32_t(file[18]) << 16 | std::uint32_t(file[19]) << 24;
    const auto data = file.subspan(header_size);
    if (length != data.size()) {
        throw std::runtime_error("This TAP image is incomplete or damaged (its length does not "
                                 "match its header).");
    }
    tape.pulses.reserve(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (data[i] != 0) {
            tape.pulses.push_back({std::uint32_t(data[i]) * 8, false});
        } else if (tape.version == 0) {
            tape.pulses.push_back({256 * 8, true});
        } else {
            if (i + 3 >= data.size()) {
                throw std::runtime_error("This TAP image ends in the middle of a pause.");
            }
            const std::uint32_t cycles = std::uint32_t(data[i + 1]) |
                                         std::uint32_t(data[i + 2]) << 8 |
                                         std::uint32_t(data[i + 3]) << 16;
            tape.pulses.push_back({cycles, true});
            i += 3;
        }
    }
    return tape;
}

} // namespace c64
