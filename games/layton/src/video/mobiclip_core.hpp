#pragma once
// The Mobiclip frame decoder shared by the DS games' movies (mobiclip.hpp,
// the hand-written ARM frame routine of the second adventure's overlay 1 at
// 0x02061870) and the 3DS games' (moflex.hpp, the same routine in three
// stride copies of the fifth adventure's code, 0x10da00 / 0x114000 /
// 0x11a600). One frame goes in, the picture's luma and chroma planes come
// out in place: intra and predicted macroblocks, H.264-like intra
// predictors, integer 8x8 / 4x4 transforms, half-pel motion from up to five
// earlier pictures.
//
// The two generations differ in a few rules, kept explicit here
// (Generation): the 3DS clamps the quantizer to 12..52 and sets it on a
// predicted frame that arrives before any quantizer, and multiplies levels
// by the full 32-bit step where the DS multiplies 16 x 16 bits. Everything
// else - tables apart - is the same code. Callers own the picture buffers
// and say which pictures the reference slots are.
//
// Self-contained (standard library only).
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace picarat::mobiclip {

struct DecodeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// The frame routine's constant tables; data in each game's code, so they are
// read from the user's ROM by the game-specific loaders (mobiclip.hpp,
// moflex.hpp).
struct CodecTables {
    // Coefficient codes: the next 12 bits index `lookup` (run, level, last
    // and code length); the escape forms add the largest coded level for a
    // run, or the largest coded run (+1) for a level.
    struct Coefficients {
        std::array<std::uint16_t, 4096> lookup{};
        std::array<std::uint8_t, 128> levelOffset{}; // [last * 64 + run]
        std::array<std::uint8_t, 128> runOffset{};   // [last * 64 + level]
    };

    // A node of the inter partition tree: the next `peek` bits (up to five on
    // the DS, six on the 3DS) index a symbol, whose code length is then
    // consumed. Symbols: 0 predicted motion, 1..5 motion delta with
    // reference n, 6/7 intra macroblock, 8 split into upper/lower halves
    // (node `upper`), 9 split into left/right halves (node `left`).
    struct Partition {
        unsigned width = 0, height = 0, peek = 0, upper = ~0u, left = ~0u;
        std::array<std::uint8_t, 64> symbols{};
        std::array<std::uint8_t, 10> lengths{};
    };

    Coefficients a;                       // P frames, and I frames with header bit 2 clear
    Coefficients b;                       // I frames with header bit 2 set
    std::array<Partition, 16> partitions; // [0] is the 16x16 macroblock
    std::array<std::uint8_t, 64> intraCodedBlocks{}; // ue(v) -> coded 8x8 blocks, intra
    std::array<std::uint8_t, 64> interCodedBlocks{}; // ue(v) -> coded 8x8 blocks, inter
    std::array<std::uint8_t, 17> intraCoded4x4{};    // ue(v) -> coded 4x4 blocks, intra 8x8
    std::array<std::uint8_t, 16> interCoded4x4{};    // ue(v) -> coded 4x4 blocks, inter 8x8
    std::array<std::uint8_t, 384> dequant8x8{};      // six QP%6 rows in scan order
    std::array<std::uint8_t, 64> scan8x8{};          // zigzag scan (raster positions)
    std::array<std::uint8_t, 96> dequant4x4{};
    std::array<std::uint8_t, 16> scan4x4{};
    std::array<std::uint8_t, 54> quantShift{}; // QP / 6
    std::array<std::uint8_t, 54> quantRow{};   // QP % 6

    // Links each node that splits to the node of the half-size block.
    void linkPartitions();
};

enum class Generation { DS, CTR };

class FrameDecoder {
public:
    // Reference slot k (0..5) is the picture k frames back; slot 0 receives
    // the frame being decoded. Chroma rows hold the first plane in bytes
    // 0..width/2-1 and the second half a stride further.
    struct References {
        std::array<std::uint8_t*, 6> luma{}, chroma{};
    };

    FrameDecoder(Generation generation, unsigned width, unsigned height, unsigned stride,
                 std::shared_ptr<const CodecTables> tables);
    virtual ~FrameDecoder() = default;

    // Decodes one frame; returns the byte count the ROM routine reports.
    std::uint32_t decodeFrame(std::span<const std::uint8_t> frame, const References& references);

    unsigned quantizer() const { return qp_; }

protected:
    // A motion copy is about to read `height` rows of `width` + 1 bytes (plus
    // one row for vertical half-pels) from `from` in the reference plane
    // starting at `plane`. The ROM reads whatever lies there; callers reject
    // reads their buffers do not cover.
    virtual void checkMotion(const std::uint8_t* from, unsigned width, unsigned height,
                             const std::uint8_t* plane, bool chroma) const = 0;

private:
    // ROM bit reader: r1 (halfword pointer), r3 (MSB-first cache), r2 (count).
    struct Bits {
        const std::uint8_t* data = nullptr;
        std::size_t size = 0, next = 0;
        std::uint32_t cache = 0;
        int count = 0;
        std::uint32_t halfword();
        void start(std::span<const std::uint8_t> frame);
        void refill();
        void skip(unsigned n);
        std::uint32_t unsignedGolomb();
        std::int32_t signedGolomb();
    };

    struct Vector {
        std::int32_t x = 0, y = 0;
    };

    void setQuantizer(int qp);
    void intraMacroblockA(unsigned offset);
    void intraMacroblockB(unsigned offset);
    void intraChroma(unsigned offset, unsigned coded);
    void interMacroblock(unsigned offset, Vector& slot);
    void partition(unsigned node, unsigned offset, Vector& slot);
    void motion(unsigned width, unsigned height, unsigned offset, unsigned reference, Vector vector,
                Vector& slot);
    void interResidual(unsigned offset);
    unsigned blockMode(std::uint8_t* cell, bool fourByFour);
    void predict(unsigned mode, std::uint8_t* block, const std::uint8_t* planeBase);
    void residual8x8(std::uint8_t* block);
    void residual4x4(std::uint8_t* block);
    void interBlock8x8(std::uint8_t* block);
    void intraBlock8x8(unsigned mode, std::uint8_t* block, const std::uint8_t* planeBase);
    void coefficients(const std::uint32_t* entries, unsigned limit);
    void addResidual(std::uint8_t* block, const std::int32_t* rows, unsigned size);

    Generation generation_;
    std::shared_ptr<const CodecTables> tables_;
    unsigned width_, height_, stride_;
    References refs_;

    Bits bits_;
    const CodecTables::Coefficients* table_ = nullptr; // ctx +0x3b8 (DS +0x3b8 too)
    std::array<std::uint8_t, 0x28> modes_{};           // intra mode cells, 8 per row
    std::array<std::uint32_t, 64> entries8_{};         // scan position | step << 8
    std::array<std::uint32_t, 16> entries4_{};
    std::array<std::int32_t, 64> coeffs_{};
    std::array<std::int32_t, 64> transform_{};
    unsigned qp_ = 0;
    Vector predicted_;
    std::vector<Vector> vectors_; // one macroblock row + 2
};

} // namespace picarat::mobiclip
