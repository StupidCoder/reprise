#pragma once
// Mobiclip MODS video decoder reconstructed from the movie player in ARM9
// overlay 1 of the user's ROM (Actimagine "Mobiclip SDK V1.1.1" identification
// string in the resident ARM9 image). Addresses below are overlay-1 runtime
// addresses.
//
// Self-contained (standard library only) so that the engine can later be
// hosted elsewhere. The frame routine itself is shared with the 3DS games'
// movies (mobiclip_core.hpp, Generation::DS here). Decoding is bit-exact with
// the ROM routines for all 23 movies of this ROM; behavior outside that tested
// input is documented in the notes rather than assumed.
#include "video/mobiclip_core.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace picarat::mobiclip {

// MODS container header (0x30 bytes) as read by the open routine 0x0205a3c8.
struct Header {
    char variant = 0; // '2' or '3' after "MODSN"
    std::uint32_t frames = 0, width = 0, height = 0;
    std::uint32_t rate = 0; // frames per second, 8.24 fixed point
    std::uint16_t audioFormat = 0, audioChannels = 0;
    std::uint32_t audioRate = 0, largestPacket = 0;
    std::uint32_t audioTableOffset = 0, keyframeTableOffset = 0, keyframeCount = 0;
};

struct Packet {
    std::uint32_t frame = 0;
    std::span<const std::uint8_t> data; // packet bytes; video starts at offset 0
    std::uint32_t audioFrames = 0;      // low 14 bits of the packet word
    bool keyframe = false;              // bit 15 of the first video halfword (0x0205ad08)
};

// Sequential packet walk of 0x0205a3c8/0x0205aaf4: each packet is preceded by a
// 32-bit word whose upper 18 bits give the packet size in bytes.
class Container {
public:
    explicit Container(std::span<const std::uint8_t> file);

    const Header& header() const { return header_; }

    bool next(Packet& packet); // false after the declared frame count
    void rewind();

private:
    std::span<const std::uint8_t> file_;
    Header header_;
    std::size_t firstPacket_ = 0, position_ = 0;
    std::uint32_t frame_ = 0;
};

// The decoder's constant tables (mobiclip_core.hpp). They are data in the
// movie player's code (ARM9 overlay 1), so they come from the user's own ROM:
// the importer saves the expanded overlay in the pack (code/overlay1.bin) and
// fromOverlay() reads them from it. Table A sits at 0x0205f770, table B at
// 0x0205d670.
struct Tables : CodecTables {
    // `overlay` is the expanded decoder overlay (the second adventure's
    // overlay 1, loaded at 0x0205a120); `origin` is the address its first
    // byte would have in that overlay's layout. The third adventure's
    // overlay 2 holds the same code with the same tables, its first byte
    // standing for 0x0205a240 (every table found 0x2a320 above the second
    // game's addresses at its load address 0x02084560).
    static constexpr std::uint32_t kAdventure2Origin = 0x0205a120;
    static constexpr std::uint32_t kAdventure3Origin = 0x0205a240;
    static std::shared_ptr<const Tables> fromOverlay(std::span<const std::uint8_t> overlay,
                                                     std::uint32_t origin = kAdventure2Origin);

    // Overlays whose parts moved: each range of the second adventure's
    // addresses [from, to) is read with its own origin.
    struct Segment {
        std::uint32_t from, to, origin;
    };

    static std::shared_ptr<const Tables> fromOverlay(std::span<const std::uint8_t> overlay,
                                                     std::span<const Segment> layout);
    // The fourth adventure's overlay 1 (loaded at 0x020505c0): the code
    // block from the frame routine on sits 0x5d94 lower than in the second
    // adventure's layout, and the two coefficient tables were laid out
    // apart from it (found by their bytes; every frame of the opening film
    // decodes).
    static constexpr Segment kAdventure4Layout[] = {
        {0x0205d670, 0x0205f770, 0x020599dc}, // coefficient table B
        {0x0205f770, 0x02061870, 0x0205dbdc}, // coefficient table A
        {0x02061870, 0x02068600, 0x0205badc}, // the frame routine and its tables
    };
};

// The movie player around the shared frame routine: six picture buffers with
// guard rows, rotated as 0x0205aaf4 / 0x0205ad08 do, and the RGB555
// conversion.
class Decoder : FrameDecoder {
public:
    static constexpr unsigned stride = 256; // plane row pitch used by every routine
    static constexpr unsigned buffers = 6;  // the movie scene opens movies with six frame buffers

    Decoder(unsigned width, unsigned height, std::shared_ptr<const Tables> tables);

    // 0x0205ad08 + frame routine 0x02061870: decodes the video frame at the
    // start of a packet. Returns the byte count the ROM routine reports.
    std::uint32_t decode(std::span<const std::uint8_t> packet);

    // Planes of the most recently decoded frame (stride 256). Chroma rows hold
    // Co in bytes 0..width/2-1 and Cg in bytes 128..128+width/2-1.
    const std::uint8_t* luma() const { return plane(current_).luma; }

    const std::uint8_t* chroma() const { return plane(current_).chroma; }

    using FrameDecoder::quantizer;

    // 0x0205b178: YCoCg to DS RGB555 with the routine's 2x2 ordered dither.
    void convert(std::uint16_t* rgb555, unsigned pitchPixels) const;

private:
    struct Planes {
        std::uint8_t* luma;
        std::uint8_t* chroma;
    };

    Planes plane(unsigned index) const;
    void checkMotion(const std::uint8_t* from, unsigned width, unsigned height,
                     const std::uint8_t* plane, bool chroma) const override;

    unsigned width_, height_;
    std::vector<std::uint8_t> memory_; // six luma + six chroma planes with guard margins
    std::array<std::size_t, buffers> lumaAt_{}, chromaAt_{};
    std::array<unsigned, 6> order_{}; // player +0xac: buffer per reference slot
    unsigned current_ = 0;
};

} // namespace picarat::mobiclip
