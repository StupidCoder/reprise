#include "wings_of_fury_amiga/data/shapes.hpp"
#include "amiga/adf.hpp"
#include "wings_of_fury_amiga/data/packed.hpp"
#include <algorithm>
#include <stdexcept>

namespace wings::data {
namespace {

constexpr std::size_t header_size = 20;
constexpr std::size_t max_planes = 6;     // bytes 14-19
constexpr std::size_t max_mask_planes = 5; // $110CE2 ORs at most five planes

int bit(const std::vector<std::uint8_t>& bits, int bytes_per_row, int x, int y) {
    return bits[std::size_t(y * bytes_per_row + x / 8)] >> (7 - x % 8) & 1;
}

Shape read_shape(std::span<const std::uint8_t> bytes, std::string name) {
    if (bytes.size() < header_size) {
        throw std::runtime_error("A shape in a shape library is cut short.");
    }
    Shape shape;
    shape.name = std::move(name);
    shape.bytes_per_row = amiga::read_be16(bytes, 0);
    shape.height = amiga::read_be16(bytes, 2);
    shape.offset_x = std::int16_t(amiga::read_be16(bytes, 4));
    shape.offset_y = std::int16_t(amiga::read_be16(bytes, 6));
    shape.sheet_x = amiga::read_be16(bytes, 8);
    shape.sheet_y = amiga::read_be16(bytes, 10);
    shape.cleared_planes = bytes[12];
    shape.set_planes = bytes[13];
    const std::size_t plane_size = std::size_t(shape.bytes_per_row) * std::size_t(shape.height);
    std::size_t at = header_size;
    for (std::size_t p = 0; p < max_planes && bytes[14 + p] != 0; ++p) {
        if (at + plane_size > bytes.size()) {
            throw std::runtime_error("A shape in a shape library is cut short.");
        }
        shape.planes.push_back({bytes[14 + p], {bytes.begin() + std::ptrdiff_t(at),
                                                bytes.begin() + std::ptrdiff_t(at + plane_size)}});
        at += plane_size;
    }
    return shape;
}

} // namespace

bool Shape::covers(int x, int y) const {
    if (x < 0 || y < 0 || x >= width() || y >= height) {
        return false;
    }
    if (planes.empty()) {
        return true;
    }
    const auto count = std::min(planes.size(), max_mask_planes);
    for (std::size_t p = 0; p < count; ++p) {
        if (bit(planes[p].bits, bytes_per_row, x, y)) {
            return true;
        }
    }
    return false;
}

int Shape::colour(int x, int y, int under) const {
    int value = (under & ~cleared_planes) | set_planes;
    for (const auto& plane : planes) {
        value = (value & ~plane.bitplanes) | (bit(plane.bits, bytes_per_row, x, y) ? plane.bitplanes : 0);
    }
    return value;
}

const Shape* ShapeLibrary::find(std::string_view name) const {
    const auto found = std::find_if(shapes.begin(), shapes.end(), [&](const Shape& s) { return s.name == name; });
    return found == shapes.end() ? nullptr : &*found;
}

ShapeLibrary read_shape_library(std::span<const std::uint8_t> file) {
    const auto bytes = unpack_file(file);
    const std::span<const std::uint8_t> data(bytes);
    if (data.size() < 6 || data[0] != 'P' || data[1] != 'P' || data[2] != 'k' || data[3] != 'c') {
        throw std::runtime_error("A shape library does not start with PPkc.");
    }
    const std::size_t count = amiga::read_be16(data, 4);
    const std::size_t base = 6 + 8 * count;
    if (base > data.size()) {
        throw std::runtime_error("A shape library's index is cut short.");
    }
    ShapeLibrary library;
    for (std::size_t i = 0; i < count; ++i) {
        std::string name(reinterpret_cast<const char*>(&data[6 + 4 * i]), 4);
        const std::size_t start = base + amiga::read_be32(data, 6 + 4 * count + 4 * i);
        const std::size_t end = i + 1 < count ? base + amiga::read_be32(data, 6 + 4 * count + 4 * (i + 1)) : data.size();
        if (start > end || end > data.size()) {
            throw std::runtime_error("A shape library's index points outside the file.");
        }
        library.shapes.push_back(read_shape(data.subspan(start, end - start), std::move(name)));
    }
    return library;
}

} // namespace wings::data
