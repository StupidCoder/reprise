#include "video/mobiclip.hpp"
#include <algorithm>
#include <cstring>
#include <utility>

// Routine addresses are ARM9 overlay 1 (loaded at 0x0205a120). The frame
// routine 0x02061870 itself lives in mobiclip_core.cpp (shared with the 3DS
// games); this file holds the MODS container, the table loader, the player's
// buffers and the RGB conversion. Equivalence was checked against the ROM
// routines run in a local research harness (all 27,994 frames of the 23
// movies), and the refactor onto the shared core against per-frame digests
// of the previous code for every movie of the first, second and fourth
// adventures (.analysis/tools/mobiclip_digest.cpp).

namespace picarat::mobiclip {
namespace {

std::uint32_t read32(std::span<const std::uint8_t> d, std::size_t at) {
    if (at > d.size() || d.size() - at < 4) {
        throw DecodeError("truncated MODS file");
    }
    return d[at] | d[at + 1] << 8 | d[at + 2] << 16 | std::uint32_t(d[at + 3]) << 24;
}

std::uint16_t read16(std::span<const std::uint8_t> d, std::size_t at) {
    if (at > d.size() || d.size() - at < 2) {
        throw DecodeError("truncated MODS file");
    }
    return std::uint16_t(d[at] | d[at + 1] << 8);
}

constexpr unsigned margin = 16 * Decoder::stride; // guard rows around each plane

} // namespace

// ---- Container (0x0205a3c8, 0x0205aaf4) ----

Container::Container(std::span<const std::uint8_t> file) : file_(file) {
    if (file.size() < 0x34 || std::memcmp(file.data(), "MODSN", 5) ||
        (file[5] != '2' && file[5] != '3') || read16(file, 6) != 10) {
        throw DecodeError("not a MODS N2/N3 version 10 file");
    }
    header_.variant = char(file[5]);
    header_.frames = read32(file, 8);
    header_.width = read32(file, 12);
    header_.height = read32(file, 16);
    header_.rate = read32(file, 20);
    header_.audioFormat = read16(file, 24);
    header_.audioChannels = read16(file, 26);
    header_.audioRate = read32(file, 28);
    header_.largestPacket = read32(file, 32);
    header_.audioTableOffset = read32(file, 36);
    header_.keyframeTableOffset = read32(file, 40);
    header_.keyframeCount = read32(file, 44);
    if (header_.audioFormat > 4) {
        throw DecodeError("unsupported MODS audio format");
    }
    if (!header_.width || header_.width > 256 || header_.width % 16 || !header_.height ||
        header_.height > 256 || header_.height % 16) {
        throw DecodeError("unsupported MODS dimensions");
    }
    std::size_t at = 0x30;
    if (header_.variant == '3') {
        for (;;) { // tagged chunks up to "HE"
            bool end = file.size() > at + 1 && file[at] == 'H' && file[at + 1] == 'E';
            at += 4 + std::size_t(read16(file, at + 2)) * 4;
            if (end) {
                break;
            }
            if (at >= file.size()) {
                throw DecodeError("MODS header chunks overrun");
            }
        }
    }
    firstPacket_ = at;
    rewind();
}

void Container::rewind() {
    position_ = firstPacket_;
    frame_ = 0;
}

bool Container::next(Packet& packet) {
    if (frame_ >= header_.frames) {
        return false;
    }
    std::uint32_t word = read32(file_, position_);
    std::size_t size = word >> 14, at = position_ + 4;
    if (at > file_.size() || file_.size() - at < size) {
        throw DecodeError("MODS packet overruns file");
    }
    packet.frame = frame_++;
    packet.data = file_.subspan(at, size);
    packet.audioFrames = word & 0x3fff;
    packet.keyframe = size >= 2 && (file_[at + 1] & 0x80);
    position_ = at + size;
    return true;
}

// ---- Tables ----

namespace {

// Reads little-endian values at overlay-1 addresses.
class OverlayReader {
public:
    OverlayReader(std::span<const std::uint8_t> code, std::uint32_t origin)
        : code_(code), load(origin) {}

    OverlayReader(std::span<const std::uint8_t> code, std::span<const Tables::Segment> layout)
        : code_(code), load(0), layout_(layout) {}

    const std::uint8_t* at(std::uint32_t address, std::size_t bytes) const {
        std::uint32_t origin = load;
        for (const auto& s : layout_) {
            if (address >= s.from && address < s.to) {
                origin = s.origin;
            }
        }
        if (address < origin || address - origin + bytes > code_.size()) {
            throw DecodeError("decoder table outside overlay 1");
        }
        return code_.data() + (address - origin);
    }

    std::uint32_t word(std::uint32_t address) const {
        const std::uint8_t* p = at(address, 4);
        return p[0] | p[1] << 8 | p[2] << 16 | std::uint32_t(p[3]) << 24;
    }

    template <std::size_t N>
    void copy(std::uint32_t address, std::array<std::uint8_t, N>& out) const {
        std::memcpy(out.data(), at(address, N), N);
    }

    // The target of "adr rd, label" (add/sub rd, pc, #immediate) at `address`.
    std::uint32_t adrTarget(std::uint32_t address) const {
        const std::uint32_t instruction = word(address);
        const unsigned rotate = (instruction >> 8 & 15) * 2;
        const std::uint32_t immediate = instruction & 0xff;
        const std::uint32_t offset =
            rotate ? (immediate >> rotate | immediate << (32 - rotate)) : immediate;
        const bool subtract = (instruction >> 21 & 15) == 2;
        return address + 8 + (subtract ? -offset : offset);
    }

private:
    std::span<const std::uint8_t> code_;
    std::uint32_t load;
    std::span<const Tables::Segment> layout_;
};

// The partition tree's decoding routines, one per block size; each starts
// with "mov r6, r3, lsr #(32 - peek)", "adr r5, symbols", ..., "adr r6, lengths".
constexpr struct {
    unsigned width, height;
    std::uint32_t routine;
} kPartitionRoutines[16] = {
    {16, 16, 0x02061b1c}, {16, 8, 0x020622f8}, {16, 4, 0x0206241c}, {16, 2, 0x02062528},
    {8, 16, 0x02061d74},  {8, 8, 0x0206264c},  {8, 4, 0x02062770},  {8, 2, 0x0206287c},
    {4, 16, 0x02061fac},  {4, 8, 0x020629ac},  {4, 4, 0x02062aec},  {4, 2, 0x02062c04},
    {2, 16, 0x020621d4},  {2, 8, 0x02062d24},  {2, 4, 0x02062e34},  {2, 2, 0x02062f2c},
};

} // namespace

namespace {
std::shared_ptr<const Tables> readTables(const OverlayReader& rom);
}

std::shared_ptr<const Tables> Tables::fromOverlay(std::span<const std::uint8_t> overlay,
                                                  std::uint32_t origin) {
    return readTables(OverlayReader(overlay, origin));
}

std::shared_ptr<const Tables> Tables::fromOverlay(std::span<const std::uint8_t> overlay,
                                                  std::span<const Segment> layout) {
    return readTables(OverlayReader(overlay, layout));
}

namespace {
std::shared_ptr<const Tables> readTables(const OverlayReader& rom) {
    using Tables = mobiclip::Tables;
    auto t = std::make_shared<Tables>();
    // Each coefficient table: the 4096 lookup halfwords, then the level and
    // run escape offsets at +0x2000 and +0x2080.
    for (auto [table, address] : {std::pair{&t->a, 0x0205f770u}, std::pair{&t->b, 0x0205d670u}}) {
        const std::uint8_t* p = rom.at(address, 0x2100);
        for (std::size_t i = 0; i < table->lookup.size(); ++i) {
            table->lookup[i] = std::uint16_t(p[2 * i] | p[2 * i + 1] << 8);
        }
        rom.copy(address + 0x2000, table->levelOffset);
        rom.copy(address + 0x2080, table->runOffset);
    }
    for (unsigned i = 0; i < 16; ++i) {
        const auto& source = kPartitionRoutines[i];
        auto& node = t->partitions[i];
        node.width = source.width;
        node.height = source.height;
        node.peek = 32 - (rom.word(source.routine) >> 7 & 31);
        if (node.peek < 1 || node.peek > 5) {
            throw DecodeError("partition routine does not start as expected");
        }
        std::memcpy(node.symbols.data(), rom.at(rom.adrTarget(source.routine + 4), 1u << node.peek),
                    1u << node.peek);
        rom.copy(rom.adrTarget(source.routine + 12), node.lengths);
    }
    // Splits continue in the node for the half-size block.
    t->linkPartitions();
    rom.copy(0x02063134, t->intraCodedBlocks);
    rom.copy(0x020632d0, t->interCodedBlocks);
    rom.copy(0x02063664, t->intraCoded4x4);
    rom.copy(0x02063734, t->interCoded4x4);
    rom.copy(0x02065f28, t->dequant8x8);
    rom.copy(0x020660a8, t->scan8x8);
    rom.copy(0x020660e8, t->dequant4x4);
    rom.copy(0x02066148, t->scan4x4);
    rom.copy(0x02066158, t->quantShift);
    rom.copy(0x0206618e, t->quantRow);
    return t;
}
} // namespace

// ---- Decoder (the player around the shared frame routine) ----

Decoder::Decoder(unsigned width, unsigned height, std::shared_ptr<const Tables> tables)
    : FrameDecoder(Generation::DS, width, height, stride, std::move(tables)), width_(width),
      height_(height) {
    if (width > 256 || height > 256) {
        throw DecodeError("unsupported frame size");
    }
    const std::size_t lumaSize = height * stride, chromaSize = (height / 2) * stride;
    std::size_t at = 0;
    for (unsigned i = 0; i < buffers; ++i) {
        lumaAt_[i] = at + margin;
        at += lumaSize + 2 * margin;
        chromaAt_[i] = at + margin;
        at += chromaSize + 2 * margin;
    }
    memory_.assign(at, 0);
    for (unsigned i = 0; i < buffers; ++i) {
        std::memset(memory_.data() + chromaAt_[i], 0x80, chromaSize); // 0x02020a9c(..., 0x80, ...)
    }
    order_ = {0, buffers - 1, buffers - 2, buffers - 3, buffers - 4, buffers - 5};
}

Decoder::Planes Decoder::plane(unsigned index) const {
    auto* base = const_cast<std::uint8_t*>(memory_.data());
    return {base + lumaAt_[index], base + chromaAt_[index]};
}

std::uint32_t Decoder::decode(std::span<const std::uint8_t> packet) {
    // 0x0205aaf4: slot 0 receives the frame, slots 1..5 are the previous frames.
    References refs;
    for (unsigned k = 0; k < 6; ++k) {
        refs.luma[k] = plane(order_[k]).luma;
        refs.chroma[k] = plane(order_[k]).chroma;
    }
    const std::uint32_t consumed = decodeFrame(packet, refs);
    current_ = order_[0];
    for (auto& o : order_) {
        o = (o + 1) % buffers; // 0x0205ad08
    }
    return consumed;
}

// The planes have guard rows; reads beyond them would leave the buffer.
void Decoder::checkMotion(const std::uint8_t* from, unsigned width, unsigned height,
                          const std::uint8_t* plane, bool chroma) const {
    const std::size_t planeSize = std::size_t(chroma ? height_ / 2 : height_) * stride;
    if (from < plane - margin ||
        from + std::size_t(height) * stride + width + 1 > plane + planeSize + margin) {
        throw DecodeError("motion vector outside reference plane");
    }
}

// 0x0205b178. Each 2x2 block shares (Co, Cg); the top-right and bottom-left
// luma samples are lowered by 4 before lookup (ordered dither). The table at
// 0x020684d8 maps clamp(v, 0, 255) >> 3 for v in -256..511; the three entries
// below it (end of the 0x02068358 table) hold 255, reproduced here.
void Decoder::convert(std::uint16_t* rgb555, unsigned pitchPixels) const {
    auto lookup = [](int v) -> std::uint32_t {
        return v < -256 ? 255u : std::uint32_t(std::clamp(v, 0, 255) >> 3);
    };
    auto pixel = [&](int y, int co, int cg) {
        return lookup(y + co - cg) + (lookup(y + cg) << 5) + (lookup(y - co - cg) << 10);
    };
    const std::uint8_t* luma = this->luma();
    const std::uint8_t* chroma = this->chroma();
    for (unsigned y = 0; y < height_; y += 2) {
        for (unsigned x = 0; x < width_; x += 2) {
            int co = chroma[(y / 2) * stride + x / 2] - 128,
                cg = chroma[(y / 2) * stride + x / 2 + 0x80] - 128;
            const std::uint8_t* l = luma + y * stride + x;
            std::uint32_t upper = (pixel(l[0], co, cg) | 0x8000) + (pixel(l[1] - 4, co, cg) << 16);
            std::uint32_t lower =
                (pixel(l[stride] - 4, co, cg) | 0x8000) + (pixel(l[stride + 1], co, cg) << 16);
            upper |= 0x80000000u;
            lower |= 0x80000000u;
            rgb555[y * pitchPixels + x] = std::uint16_t(upper);
            rgb555[y * pitchPixels + x + 1] = std::uint16_t(upper >> 16);
            rgb555[(y + 1) * pitchPixels + x] = std::uint16_t(lower);
            rgb555[(y + 1) * pitchPixels + x + 1] = std::uint16_t(lower >> 16);
        }
    }
}

} // namespace picarat::mobiclip
