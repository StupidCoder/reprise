#pragma once
#include <cstdint>
#include <span>
#include <vector>

// IFF ILBM pictures (FORM ILBM with BMHD, CMAP, CRNG and BODY chunks).
namespace amiga {

// An Amiga colour as the hardware takes it: $0RGB, four bits per gun.
using Rgb4 = std::uint16_t;

struct ColourRange { // CRNG: colours low..high cycle at `rate` (16384 = 60 steps/s)
    std::uint16_t rate{};
    bool active{};
    bool reverse{};
    std::uint8_t low{}, high{};
};

struct IlbmPicture {
    int width{}, height{}, planes{};
    std::vector<std::uint8_t> pixels; // colour index per pixel, row by row
    std::vector<Rgb4> palette;        // from CMAP, reduced to 4 bits per gun
    std::vector<ColourRange> ranges;
};

// Decodes an ILBM; throws std::runtime_error when it is malformed.
IlbmPicture read_ilbm(std::span<const std::uint8_t> file);

} // namespace amiga
