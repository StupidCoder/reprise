// The AAC and SBR code tables (aac_tables.cpp; derived from Apache-2.0 work, see there).
#pragma once

#include <cstdint>

namespace doc::media {

// One Huffman codeword: the symbol it stands for, its length in bits and its bits.
struct HuffmanCode {
    std::uint16_t index;
    std::uint8_t length;
    std::uint32_t code;
};
struct HuffmanBook {
    const HuffmanCode* codes;
    int count;
};

extern const HuffmanCode kScalefactorCodes[121];  // index 60 = no change
extern const HuffmanBook kSpectralBooks[12];      // books 1..11 (0 unused)

// Scalefactor band end offsets (the first band starts at 0), long and short windows, by rate.
extern const std::int16_t k_sfb_96_1024[41];
extern const std::int16_t k_sfb_64_1024[47];
extern const std::int16_t k_sfb_64_128[12];
extern const std::int16_t k_sfb_48_1024[49];
extern const std::int16_t k_sfb_48_128[14];
extern const std::int16_t k_sfb_32_1024[51];
extern const std::int16_t k_sfb_24_1024[47];
extern const std::int16_t k_sfb_24_128[15];
extern const std::int16_t k_sfb_16_1024[43];
extern const std::int16_t k_sfb_16_128[15];
extern const std::int16_t k_sfb_8_1024[40];
extern const std::int16_t k_sfb_8_128[15];

// SBR Huffman trees: start at node 0, follow [node][bit]; a negative entry is a leaf whose
// value is entry + 64. "10" = 1.5 dB steps, "11" = 3.0 dB steps; T time, F frequency direction.
extern const std::int8_t k_bookSbrEnvLevel10T[120][2];
extern const std::int8_t k_bookSbrEnvLevel10F[120][2];
extern const std::int8_t k_bookSbrEnvBalance10T[48][2];
extern const std::int8_t k_bookSbrEnvBalance10F[48][2];
extern const std::int8_t k_bookSbrEnvLevel11T[62][2];
extern const std::int8_t k_bookSbrEnvLevel11F[62][2];
extern const std::int8_t k_bookSbrEnvBalance11T[24][2];
extern const std::int8_t k_bookSbrEnvBalance11F[24][2];
extern const std::int8_t k_bookSbrNoiseLevel11T[62][2];
extern const std::int8_t k_bookSbrNoiseBalance11T[24][2];

}  // namespace doc::media
