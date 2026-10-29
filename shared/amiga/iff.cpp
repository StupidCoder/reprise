#include "amiga/iff.hpp"
#include "amiga/adf.hpp"
#include "amiga/byte_run1.hpp"
#include <stdexcept>
#include <string_view>

namespace amiga {
namespace {

[[noreturn]] void malformed(const char* what) {
    throw std::runtime_error(std::string("A picture on the disk is malformed: ") + what + ".");
}

std::string_view tag_at(std::span<const std::uint8_t> bytes, std::size_t at) {
    return {reinterpret_cast<const char*>(bytes.data() + at), 4};
}

} // namespace

IlbmPicture read_ilbm(std::span<const std::uint8_t> file) {
    if (file.size() < 12 || tag_at(file, 0) != "FORM" || tag_at(file, 8) != "ILBM") {
        malformed("not an IFF ILBM");
    }
    const std::size_t end = std::min<std::size_t>(file.size(), 8 + std::size_t(read_be32(file, 4)));
    IlbmPicture picture;
    int masking = 0, compression = 0;
    bool have_header = false;
    std::span<const std::uint8_t> body;
    for (std::size_t at = 12; at + 8 <= end;) {
        const auto tag = tag_at(file, at);
        const std::size_t length = read_be32(file, at + 4);
        if (at + 8 + length > end) {
            malformed("a chunk runs past the end");
        }
        const auto chunk = file.subspan(at + 8, length);
        if (tag == "BMHD" && length >= 20) {
            picture.width = read_be16(chunk, 0);
            picture.height = read_be16(chunk, 2);
            picture.planes = chunk[8];
            masking = chunk[9];
            compression = chunk[10];
            have_header = true;
        } else if (tag == "CMAP") {
            for (std::size_t i = 0; i + 2 < length; i += 3) {
                picture.palette.push_back(Rgb4((chunk[i] >> 4) << 8 | (chunk[i + 1] >> 4) << 4 | chunk[i + 2] >> 4));
            }
        } else if (tag == "CRNG" && length >= 8) {
            const auto flags = read_be16(chunk, 4);
            picture.ranges.push_back({.rate = read_be16(chunk, 2),
                                      .active = (flags & 1) != 0,
                                      .reverse = (flags & 2) != 0,
                                      .low = chunk[6],
                                      .high = chunk[7]});
        } else if (tag == "BODY") {
            body = chunk;
        }
        at += 8 + length + (length & 1);
    }
    if (!have_header || body.empty() || picture.planes < 1 || picture.planes > 8 || compression > 1) {
        malformed("unsupported header or no picture");
    }
    const std::size_t row_bytes = std::size_t(picture.width + 15) / 16 * 2;
    const int stored_planes = picture.planes + (masking == 1 ? 1 : 0);
    const std::size_t total = row_bytes * std::size_t(stored_planes) * std::size_t(picture.height);
    std::vector<std::uint8_t> planar;
    if (compression == 1) {
        planar = unpack_byte_run1(body, total);
    } else {
        planar.assign(body.begin(), body.begin() + std::ptrdiff_t(std::min(total, body.size())));
    }
    if (planar.size() < total) {
        malformed("the picture data is short");
    }
    picture.pixels.assign(std::size_t(picture.width) * std::size_t(picture.height), 0);
    for (int y = 0; y < picture.height; ++y) {
        for (int plane = 0; plane < picture.planes; ++plane) {
            const auto* row = planar.data() + (std::size_t(y) * std::size_t(stored_planes) + std::size_t(plane)) * row_bytes;
            for (int x = 0; x < picture.width; ++x) {
                if (row[x / 8] >> (7 - x % 8) & 1) {
                    picture.pixels[std::size_t(y) * std::size_t(picture.width) + std::size_t(x)] |= std::uint8_t(1 << plane);
                }
            }
        }
    }
    return picture;
}

} // namespace amiga
