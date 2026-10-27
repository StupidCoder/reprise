#include "wings_of_fury_amiga/data/packed.hpp"
#include "amiga/adf.hpp"
#include <stdexcept>

namespace wings::data {

bool is_run_packed(std::span<const std::uint8_t> file) {
    return file.size() >= 8 && file[0] == 'R' && file[1] == 'p' && file[2] == 'c' && file[3] == 'k';
}

std::vector<std::uint8_t> unpack_file(std::span<const std::uint8_t> file) {
    if (!is_run_packed(file)) {
        return {file.begin(), file.end()};
    }
    const std::size_t size = amiga::read_be32(file, 4);
    std::vector<std::uint8_t> out;
    out.reserve(size + 128);
    std::size_t at = 8;
    while (at < file.size()) {
        const int control = std::int8_t(file[at++]);
        if (control < 0) {
            const auto count = std::size_t(-control);
            if (at + count > file.size()) {
                throw std::runtime_error("A packed file ends inside a run.");
            }
            out.insert(out.end(), file.begin() + std::ptrdiff_t(at), file.begin() + std::ptrdiff_t(at + count));
            at += count;
        } else {
            if (at >= file.size()) {
                throw std::runtime_error("A packed file ends inside a run.");
            }
            out.insert(out.end(), std::size_t(control) + 1, file[at++]);
        }
    }
    if (out.size() < size) {
        throw std::runtime_error("A packed file is shorter than its stated size.");
    }
    out.resize(size);
    return out;
}

} // namespace wings::data
