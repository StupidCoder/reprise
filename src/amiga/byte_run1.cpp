#include "amiga/byte_run1.hpp"
#include <stdexcept>

namespace amiga {

std::vector<std::uint8_t> unpack_byte_run1(std::span<const std::uint8_t> packed, std::size_t limit,
                                           std::size_t* consumed) {
    std::vector<std::uint8_t> out;
    out.reserve(limit);
    std::size_t at = 0;
    while (at < packed.size() && out.size() < limit) {
        const auto control = std::int8_t(packed[at++]);
        if (control >= 0) {
            const std::size_t count = std::size_t(control) + 1;
            if (at + count > packed.size() || out.size() + count > limit) {
                throw std::runtime_error("A packed file on the disk is cut short.");
            }
            out.insert(out.end(), packed.begin() + std::ptrdiff_t(at), packed.begin() + std::ptrdiff_t(at + count));
            at += count;
        } else if (control != -128) {
            const std::size_t count = std::size_t(1 - control);
            if (at >= packed.size() || out.size() + count > limit) {
                throw std::runtime_error("A packed file on the disk is cut short.");
            }
            out.insert(out.end(), count, packed[at++]);
        }
    }
    if (consumed != nullptr) {
        *consumed = at;
    }
    return out;
}

} // namespace amiga
