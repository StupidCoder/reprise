// See mobiclip_core.hpp. Routine addresses: DS = the second adventure's
// overlay 1 (loaded at 0x0205a120), 3DS = the fifth adventure's stride-512
// copy of the frame routine (0x11a600); the other copies and games hold the
// same code. Control flow follows the hand-written ARM's dispatch order and
// bit-reader bookkeeping exactly; the size- and alignment-specialised copies
// (motion handlers, sparse transforms, unrolled predictors) are written once.
#include "video/mobiclip_core.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <utility>

namespace picarat::mobiclip {
namespace {

std::uint32_t lsl(std::uint32_t value, unsigned amount) {
    return amount >= 32 ? 0 : value << amount;
} // ARM LSL by register

std::uint32_t lsr(std::uint32_t value, unsigned amount) {
    return amount >= 32 ? 0 : value >> amount;
} // ARM LSR by register

} // namespace

void CodecTables::linkPartitions() {
    auto find = [&](unsigned width, unsigned height) {
        for (unsigned i = 0; i < 16; ++i) {
            if (partitions[i].width == width && partitions[i].height == height) {
                return i;
            }
        }
        return ~0u;
    };
    for (auto& node : partitions) {
        const auto used = std::span(node.symbols).first(1u << node.peek);
        if (std::find(used.begin(), used.end(), 8) != used.end()) {
            node.upper = find(node.width, node.height / 2);
        }
        if (std::find(used.begin(), used.end(), 9) != used.end()) {
            node.left = find(node.width / 2, node.height);
        }
    }
}

// ---- Prediction and transforms (shared by every block size) ----

namespace {

int average2(int a, int b) {
    return (a + b + 1) >> 1;
}

int average3(int a, int center, int c) {
    return (a + 2 * center + c + 2) >> 2;
}

// Four predictor results stored the way the unrolled routines do: OR-packed
// into one little-endian word. Values never leave 0..255 in valid streams.
void storePacked(std::uint8_t* to, int v0, int v1, int v2, int v3) {
    const std::uint32_t word = std::uint32_t(v0) | std::uint32_t(v1) << 8 |
                               std::uint32_t(v2) << 16 | std::uint32_t(v3) << 24;
    to[0] = std::uint8_t(word);
    to[1] = std::uint8_t(word >> 8);
    to[2] = std::uint8_t(word >> 16);
    to[3] = std::uint8_t(word >> 24);
}

// Directional predictors (modes 4-8; the DS formulas, see mobiclip.cpp).
enum class Direction { HorizontalUp, HorizontalDown, VerticalRight, DownRight, VerticalLeft };

template <int N>
void directional(Direction kind, std::uint8_t* b, int stride) {
    auto t = [b, stride](int x) {
        return int(b[x - stride]);
    }; // t(-1) is the corner
    auto l = [b, stride](int y) {
        return int(b[y * stride - 1]);
    };
    int out[N][N];
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            int& p = out[y][x];
            switch (kind) {
            case Direction::HorizontalUp: {
                const int z = x + 2 * y, i = y + (x >> 1);
                if (z < 2 * N - 3) {
                    p = z & 1 ? average3(l(i), l(i + 1), l(i + 2)) : average2(l(i), l(i + 1));
                } else if (z == 2 * N - 3) {
                    p = (l(N - 2) + 3 * l(N - 1) + 2) >> 2;
                } else {
                    p = l(N - 1);
                }
                break;
            }
            case Direction::HorizontalDown: {
                const int z = 2 * y - x, i = y - (x >> 1);
                if (z >= 0) {
                    p = z & 1 ? average3(l(i - 2), l(i - 1), l(i)) : average2(l(i - 1), l(i));
                } else if (z == -1) {
                    p = average3(l(0), t(-1), t(0));
                } else {
                    const int k = x - 2 * y;
                    p = average3(t(k - 1), t(k - 2), t(k - 3));
                }
                break;
            }
            case Direction::VerticalRight: {
                const int z = 2 * x - y, i = x - (y >> 1);
                if (z >= 0) {
                    p = z & 1 ? average3(t(i - 2), t(i - 1), t(i)) : average2(t(i - 1), t(i));
                } else if (z == -1) {
                    p = average3(l(0), t(-1), t(0));
                } else {
                    const int k = y - 2 * x;
                    p = average3(l(k - 1), l(k - 2), l(k - 3));
                }
                break;
            }
            case Direction::DownRight:
                if (x > y) {
                    p = average3(t(x - y - 2), t(x - y - 1), t(x - y));
                } else if (x < y) {
                    p = average3(l(y - x - 2), l(y - x - 1), l(y - x));
                } else {
                    p = average3(t(0), t(-1), l(0));
                }
                break;
            case Direction::VerticalLeft: {
                const int i = x + (y >> 1);
                p = y & 1 ? average3(t(i), t(i + 1), t(i + 2)) : average2(t(i), t(i + 1));
                break;
            }
            }
        }
    }
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            b[y * stride + x] = std::uint8_t(out[y][x]);
        }
    }
}

// DC by edge availability, which depends on the position only: a left
// neighbour unless x is 0, an upper one unless y is 0 (0x11d048 tests the
// offset in the plane against the stride).
template <int N>
void dc(std::uint8_t* b, const std::uint8_t* planeBase, int stride) {
    const std::ptrdiff_t offset = b - planeBase;
    const bool left = offset % stride != 0, above = offset >= stride;
    int sum = 0, value = 0x80;
    if (above) {
        for (int x = 0; x < N; ++x) {
            sum += b[x - stride];
        }
    }
    if (left) {
        for (int y = 0; y < N; ++y) {
            sum += b[y * stride - 1];
        }
    }
    constexpr int shift = N == 8 ? 3 : 2;
    if (left && above) {
        value = (sum + N) >> (shift + 1);
    } else if (left || above) {
        value = (sum + N / 2) >> shift;
    }
    for (int y = 0; y < N; ++y) {
        std::memset(b + y * stride, value, N);
    }
}

// Bilinear surface between the row above, the left column and a
// bottom-right corner corrected by se(v) (8x8 and 4x4 forms).
template <int N>
void planePrediction(std::uint8_t* b, int delta, int stride) {
    constexpr int scale = N * N, shift = N == 8 ? 3 : 2;
    int top[N], left[N];
    for (int i = 0; i < N; ++i) {
        top[i] = b[i - stride];
        left[i] = b[i * stride - 1];
    }
    const int corner = ((left[N - 1] + top[N - 1] + 1) >> 1) + 2 * delta;
    int bottom = left[N - 1] << shift;
    const int bottomStep = corner - left[N - 1];
    int value[N], step[N];
    for (int i = 0; i < N; ++i) {
        bottom += bottomStep;
        value[i] = top[i] * scale;
        step[i] = bottom - (top[i] << shift);
    }
    int right = top[N - 1] << shift;
    const int rightStep = corner - top[N - 1];
    for (int y = 0; y < N; ++y) {
        right += rightStep;
        int horizontal = left[y] * scale;
        const int horizontalStep = right - (left[y] << shift);
        int out[N];
        for (int x = 0; x < N; ++x) {
            value[x] += step[x];
            horizontal += horizontalStep;
            out[x] = (value[x] + horizontal + scale) >> (2 * shift + 1);
        }
        for (int x = 0; x < N; x += 4) {
            storePacked(b + y * stride + x, out[x], out[x + 1], out[x + 2], out[x + 3]);
        }
    }
}

// The 16x16 surface uses half steps with a +1 bias at each stage.
void planePrediction16(std::uint8_t* b, int delta, int stride) {
    int top[16], left[16];
    for (int i = 0; i < 16; ++i) {
        top[i] = b[i - stride];
        left[i] = b[i * stride - 1];
    }
    const int corner = ((left[15] + top[15] + 1) >> 1) + 2 * delta;
    int bottom = left[15] << 3;
    const int bottomStep = corner - left[15] + 1;
    int value[16], step[16];
    for (int i = 0; i < 16; ++i) {
        bottom += bottomStep >> 1;
        value[i] = top[i] << 6;
        step[i] = bottom - (top[i] << 3) + 1;
    }
    int right = top[15] << 3;
    const int rightStep = corner - top[15] + 1;
    for (int y = 0; y < 16; ++y) {
        right += rightStep >> 1;
        int horizontal = left[y] << 6;
        const int horizontalStep = right - (left[y] << 3) + 1;
        int out[16];
        for (int x = 0; x < 16; ++x) {
            value[x] += step[x] >> 1;
            horizontal += horizontalStep >> 1;
            out[x] = (value[x] + horizontal + 64) >> 7;
        }
        for (int x = 0; x < 16; x += 4) {
            storePacked(b + y * stride + x, out[x], out[x + 1], out[x + 2], out[x + 3]);
        }
    }
}

// H.264-style integer transforms: 8x8 (0x11e010) and 4x4.
void inverse8(const std::int32_t* s, std::int32_t* o, unsigned outStride) {
    const std::int32_t a0 = s[0] + s[4], a1 = s[0] - s[4];
    const std::int32_t b = s[2] + (s[6] >> 1), c = (s[2] >> 1) - s[6];
    const std::int32_t e0 = a0 + b, e2 = a1 + c, e4 = a1 - c, e6 = a0 - b;
    const std::int32_t p = s[1] + s[7] - s[3] - (s[3] >> 1);
    const std::int32_t q = s[7] - s[1] + s[5] + (s[5] >> 1);
    const std::int32_t r = s[5] - (s[7] + (s[7] >> 1)) - s[3];
    const std::int32_t t = s[3] + s[5] + s[1] + (s[1] >> 1);
    const std::int32_t f1 = r + (t >> 2), f7 = t - (r >> 2), f3 = p + (q >> 2), f5 = (p >> 2) - q;
    o[0] = e0 + f7;
    o[outStride] = e2 + f5;
    o[2 * outStride] = e4 + f3;
    o[3 * outStride] = e6 + f1;
    o[4 * outStride] = e6 - f1;
    o[5 * outStride] = e4 - f3;
    o[6 * outStride] = e2 - f5;
    o[7 * outStride] = e0 - f7;
}

void inverse4(const std::int32_t* s, std::int32_t* o, unsigned outStride) {
    const std::int32_t e = s[0] + s[2], f = s[0] - s[2], g = (s[1] >> 1) - s[3],
                       h = s[1] + (s[3] >> 1);
    o[0] = e + h;
    o[outStride] = f + g;
    o[2 * outStride] = f - g;
    o[3 * outStride] = e - h;
}

// Half-pel motion copy: every average is (a >> 1) + (b >> 1); the diagonal
// halves both horizontal results.
void interpolate(std::uint8_t* to, const std::uint8_t* from, unsigned width, unsigned height,
                 bool halfX, bool halfY, unsigned s) {
    for (unsigned y = 0; y < height; ++y, to += s, from += s) {
        for (unsigned x = 0; x < width; ++x) {
            const int a = from[x];
            if (!halfX && !halfY) {
                to[x] = std::uint8_t(a);
            } else if (!halfY) {
                to[x] = std::uint8_t((a >> 1) + (from[x + 1] >> 1));
            } else if (!halfX) {
                to[x] = std::uint8_t((a >> 1) + (from[x + s] >> 1));
            } else {
                to[x] = std::uint8_t((((a >> 1) + (from[x + 1] >> 1)) >> 1) +
                                     (((from[x + s] >> 1) + (from[x + s + 1] >> 1)) >> 1));
            }
        }
    }
}

// ue(v) indexes these byte tables without a bound in the ROM; larger codes
// would read neighbouring code bytes, so they are rejected as malformed.
template <std::size_t N>
unsigned codedBlocks(const std::array<std::uint8_t, N>& table, std::uint32_t code) {
    if (code >= N) {
        throw DecodeError("coded-block code outside the ROM table");
    }
    return table[code];
}

constexpr unsigned kModeCells[4] = {9, 11, 25, 27}; // DS ctx +0x55.., 3DS +0x59..
constexpr unsigned kSubCells[4] = {0, 1, 8, 9};

} // namespace

// ---- Bit reader (r1/r2/r3 of the frame routine, as on the DS) ----

std::uint32_t FrameDecoder::Bits::halfword() {
    std::uint32_t value = 0;
    if (next < size) {
        value = data[next];
    }
    if (next + 1 < size) {
        value |= std::uint32_t(data[next + 1]) << 8;
    }
    next += 2;
    return value; // past the frame the ROM reads whatever follows; valid frames never use it
}

void FrameDecoder::Bits::start(std::span<const std::uint8_t> frame) {
    data = frame.data();
    size = frame.size();
    next = 0;
    cache = halfword() << 16;
    count = 0;
}

void FrameDecoder::Bits::refill() { // DS 0x020638d4, 3DS 0x11c668
    const std::uint32_t h = halfword();
    count += 16;
    cache |= lsl(h, unsigned(16 - count));
}

void FrameDecoder::Bits::skip(unsigned n) {
    cache = lsl(cache, n);
    count -= int(n);
    if (count < 0) {
        refill();
    }
}

std::uint32_t FrameDecoder::Bits::unsignedGolomb() { // DS 0x020638a4, 3DS 0x11c638
    const unsigned zeros = unsigned(std::countl_zero(cache));
    cache = lsl(cache, zeros) << 1;
    const std::uint32_t value = lsr(cache, 32 - zeros) + lsl(1, zeros) - 1;
    cache = lsl(cache, zeros);
    count -= int(2 * zeros + 1);
    if (count < 0) {
        refill();
    }
    return value;
}

std::int32_t FrameDecoder::Bits::signedGolomb() { // DS 0x020638e8, 3DS 0x11c67c
    const unsigned zeros = unsigned(std::countl_zero(cache));
    cache = lsl(cache, zeros) << 1;
    std::uint32_t k = lsr(cache, 32 - zeros) + lsl(1, zeros);
    if (k & 1) {
        k = 1 - k;
    }
    cache = lsl(cache, zeros);
    count -= int(2 * zeros + 1);
    if (count < 0) {
        refill();
    }
    return std::int32_t(k) >> 1;
}

// ---- Frame routine ----

FrameDecoder::FrameDecoder(Generation generation, unsigned width, unsigned height, unsigned stride,
                           std::shared_ptr<const CodecTables> tables)
    : generation_(generation), tables_(std::move(tables)), width_(width), height_(height),
      stride_(stride) {
    if (!width || width > stride || width % 16 || !height || height % 16) {
        throw DecodeError("unsupported frame size");
    }
    vectors_.resize(width / 16 + 2);
}

// DS 0x020661c4, 3DS 0x11d970: dequantization entries pack the scan position
// in bits 0-7 and the scaled step above them; the mode cache borders are
// reset to 9. The 3DS clamps the quantizer to 12..52 first; the DS takes it
// as it comes (values the tables do not cover are rejected here, smaller
// ones fail in coefficients() where the step overlaps the position).
void FrameDecoder::setQuantizer(int qp) {
    if (generation_ == Generation::CTR) {
        qp = std::clamp(qp, 12, 52);
    } else if (qp < 0 || qp >= 54) {
        throw DecodeError("quantizer outside the ROM tables");
    }
    qp_ = unsigned(qp);
    unsigned shift = tables_->quantShift[qp_] + 8u;
    const unsigned row = tables_->quantRow[qp_];
    for (unsigned k = 0; k < 16; ++k) {
        entries4_[k] = tables_->scan4x4[k] | std::uint32_t(tables_->dequant4x4[row * 16 + k])
                                                 << shift;
    }
    shift -= 2;
    for (unsigned k = 0; k < 64; ++k) {
        entries8_[k] = tables_->scan8x8[k] | std::uint32_t(tables_->dequant8x8[row * 64 + k])
                                                 << shift;
    }
    for (unsigned i : {1u, 2u, 3u, 4u, 8u, 16u, 24u, 32u}) {
        modes_[i] = 9;
    }
}

// DS 0x02061870, 3DS 0x11bce4.
std::uint32_t FrameDecoder::decodeFrame(std::span<const std::uint8_t> frame,
                                        const References& references) {
    refs_ = references;
    const unsigned s = stride_;
    auto& b = bits_;
    b.start(frame);
    const bool intra = b.cache >> 31;
    b.cache <<= 1;
    if (intra) {       // DS 0x0206308c, 3DS 0x11be00
        b.cache <<= 1; // flag stored in the context (DS +0x48, 3DS +0x4c); never read
        const bool second = b.cache >> 31;
        b.cache <<= 1;
        table_ = second ? &tables_->b : &tables_->a;
        b.count -= 3;
        if (b.count < 0) {
            b.refill();
        }
        const unsigned qp = b.cache >> 26;
        b.cache <<= 6;
        b.count -= 6;
        if (b.count < 0) {
            b.refill();
        }
        if (qp != qp_) { // the raw value against the (on the 3DS clamped) current one
            setQuantizer(int(qp));
        }
        for (unsigned y = 0; y < height_; y += 16) {
            for (unsigned x = 0; x < width_; x += 16) {
                const bool predicted = b.cache >> 31;
                b.cache <<= 1;
                b.count -= 1;
                if (b.count < 0) {
                    b.refill();
                }
                predicted ? intraMacroblockB(y * s + x) : intraMacroblockA(y * s + x);
            }
        }
    } else { // DS 0x02062f7c, 3DS 0x11bcf0
        b.count -= 1;
        if (b.count < 0) {
            b.refill();
        }
        const std::int32_t delta = b.signedGolomb();
        if (generation_ == Generation::CTR && !qp_) {
            setQuantizer(0); // 3DS: no quantizer yet - the delta is ignored (clamped to 12)
        } else if (delta) {
            setQuantizer(int(qp_) + delta);
        }
        table_ = &tables_->a;
        std::fill(vectors_.begin(), vectors_.end(), Vector{});
        for (unsigned y = 0; y < height_; y += 16) {
            for (unsigned x = 0, c = 0; x < width_; x += 16, ++c) {
                // Median of left, above and above-right vectors (row slots c..c+2).
                auto median = [](std::int32_t a, std::int32_t m, std::int32_t z) {
                    if (a > m) {
                        std::swap(a, m);
                    }
                    if (m > z) {
                        std::swap(m, z);
                    }
                    if (a > m) {
                        std::swap(a, m);
                    }
                    return m;
                };
                predicted_ = {median(vectors_[c].x, vectors_[c + 1].x, vectors_[c + 2].x),
                              median(vectors_[c].y, vectors_[c + 1].y, vectors_[c + 2].y)};
                vectors_[c + 1] = {};
                interMacroblock(y * s + x, vectors_[c + 1]);
            }
        }
    }
    return std::uint32_t(b.next - 2);
}

// One 3-bit mode for the whole macroblock; mode 2 is the 16x16 surface.
void FrameDecoder::intraMacroblockA(unsigned offset) {
    const unsigned coded = codedBlocks(tables_->intraCodedBlocks, bits_.unsignedGolomb());
    unsigned mode = bits_.cache >> 29;
    bits_.skip(3);
    std::uint8_t* base = refs_.luma[0];
    const unsigned s = stride_;
    if (mode == 2) {
        mode = 9;
        planePrediction16(base + offset, bits_.signedGolomb(), int(s));
    }
    const std::size_t blocks[4] = {0, 8, 8 * s, 8 * s + 8};
    for (unsigned i = 0; i < 4; ++i) {
        std::uint8_t* block = base + offset + blocks[i];
        if (coded & (1u << i)) {
            intraBlock8x8(mode, block, base);
        } else {
            predict(mode, block, base);
        }
    }
    intraChroma(offset, coded);
}

// Per-8x8 or per-4x4 modes predicted from the cell cache.
void FrameDecoder::intraMacroblockB(unsigned offset) {
    const unsigned coded = codedBlocks(tables_->intraCodedBlocks, bits_.unsignedGolomb());
    std::uint8_t* base = refs_.luma[0];
    const unsigned s = stride_;
    const std::size_t blocks[4] = {0, 8, 8 * s, 8 * s + 8};
    const std::size_t subs[4] = {0, 4, 4 * s, 4 * s + 4};
    for (unsigned i = 0; i < 4; ++i) {
        std::uint8_t* block = base + offset + blocks[i];
        std::uint8_t* cell = modes_.data() + kModeCells[i];
        if (!(coded & (1u << i))) {
            predict(blockMode(cell, false), block, base);
            continue;
        }
        if (bits_.cache >> 31) {
            bits_.cache <<= 1;
            bits_.count -= 1;
            predict(blockMode(cell, false), block, base);
            residual8x8(block);
            continue;
        }
        const unsigned coded4 = codedBlocks(tables_->intraCoded4x4, bits_.unsignedGolomb());
        for (unsigned j = 0; j < 4; ++j) {
            const unsigned mode = blockMode(cell + kSubCells[j], true) + 10;
            predict(mode, block + subs[j], base);
            if (coded4 & (1u << j)) {
                residual4x4(block + subs[j]);
            }
        }
    }
    intraChroma(offset, coded);
}

// One mode for both chroma planes; Co at x, Cg half a stride further.
void FrameDecoder::intraChroma(unsigned offset, unsigned coded) {
    unsigned mode = bits_.cache >> 29;
    bits_.skip(3);
    std::uint8_t* base = refs_.chroma[0];
    std::uint8_t* block = base + offset / 2;
    const unsigned half = stride_ / 2;
    if (mode == 2) {
        mode = 9;
        planePrediction<8>(block, bits_.signedGolomb(), int(stride_));
        planePrediction<8>(block + half, bits_.signedGolomb(), int(stride_));
    }
    if (coded & 0x10) {
        intraBlock8x8(mode, block, base);
    } else {
        predict(mode, block, base);
    }
    if (coded & 0x20) {
        intraBlock8x8(mode, block + half, base + half);
    } else {
        predict(mode, block + half, base + half);
    }
}

// A coded intra 8x8: one transform or four 4x4 transforms.
void FrameDecoder::intraBlock8x8(unsigned mode, std::uint8_t* block,
                                 const std::uint8_t* planeBase) {
    if (bits_.cache >> 31) {
        bits_.cache <<= 1;
        bits_.count -= 1;
        predict(mode, block, planeBase);
        residual8x8(block);
        return;
    }
    // The flag's 0 bit is also the first bit of this ue(v).
    const unsigned coded4 = codedBlocks(tables_->intraCoded4x4, bits_.unsignedGolomb());
    const unsigned s = stride_;
    const std::size_t subs[4] = {0, 4, 4 * s, 4 * s + 4};
    for (unsigned j = 0; j < 4; ++j) {
        predict(mode + 10, block + subs[j], planeBase);
        if (coded4 & (1u << j)) {
            residual4x4(block + subs[j]);
        }
    }
}

// H.264-style most-probable mode with inverted flag polarity; unavailable
// (9) maps to DC (3).
unsigned FrameDecoder::blockMode(std::uint8_t* cell, bool fourByFour) {
    unsigned predicted = std::min(cell[-8], cell[-1]);
    if (predicted == 9) {
        predicted = 3;
    }
    unsigned value = bits_.cache >> 28, used;
    if (value >= predicted) {
        ++value;
    }
    unsigned mode;
    if (value < 9) {
        mode = value;
        used = 4;
    } else {
        mode = predicted;
        used = 1;
    }
    cell[0] = std::uint8_t(mode);
    if (!fourByFour) {
        cell[1] = cell[8] = cell[9] = std::uint8_t(mode);
    }
    bits_.skip(used);
    return mode;
}

// Modes 0-9 for 8x8 blocks, 10-19 for 4x4 blocks.
void FrameDecoder::predict(unsigned mode, std::uint8_t* b, const std::uint8_t* planeBase) {
    const int s = int(stride_);
    switch (mode) {
    case 0:
        for (int y = 0; y < 8; ++y) {
            std::memcpy(b + y * s, b - s, 8);
        }
        break;
    case 1:
        for (int y = 0; y < 8; ++y) {
            std::memset(b + y * s, b[y * s - 1], 8);
        }
        break;
    case 2:
        planePrediction<8>(b, bits_.signedGolomb(), s);
        break;
    case 3:
        dc<8>(b, planeBase, s);
        break;
    case 4:
        directional<8>(Direction::HorizontalUp, b, s);
        break;
    case 5:
        directional<8>(Direction::HorizontalDown, b, s);
        break;
    case 6:
        directional<8>(Direction::VerticalRight, b, s);
        break;
    case 7:
        directional<8>(Direction::DownRight, b, s);
        break;
    case 8:
        directional<8>(Direction::VerticalLeft, b, s);
        break;
    case 9:
    case 19:
        break; // already predicted (16x16 or chroma surface)
    case 10:
        for (int y = 0; y < 4; ++y) {
            std::memcpy(b + y * s, b - s, 4);
        }
        break;
    case 11:
        for (int y = 0; y < 4; ++y) {
            std::memset(b + y * s, b[y * s - 1], 4);
        }
        break;
    case 12:
        planePrediction<4>(b, bits_.signedGolomb(), s);
        break;
    case 13:
        dc<4>(b, planeBase, s);
        break;
    case 14:
        directional<4>(Direction::HorizontalUp, b, s);
        break;
    case 15:
        directional<4>(Direction::HorizontalDown, b, s);
        break;
    case 16:
        directional<4>(Direction::VerticalRight, b, s);
        break;
    case 17:
        directional<4>(Direction::DownRight, b, s);
        break;
    case 18:
        directional<4>(Direction::VerticalLeft, b, s);
        break;
    default:
        throw DecodeError("intra mode outside the ROM jump table");
    }
}

// The macroblock partition tree; intra symbols skip the inter residual.
void FrameDecoder::interMacroblock(unsigned offset, Vector& slot) {
    const auto& node = tables_->partitions[0];
    const unsigned symbol = node.symbols[bits_.cache >> (32 - node.peek)];
    if (symbol == 6 || symbol == 7) {
        bits_.skip(node.lengths[symbol]);
        symbol == 6 ? intraMacroblockA(offset) : intraMacroblockB(offset);
        return;
    }
    partition(0, offset, slot);
    interResidual(offset);
}

void FrameDecoder::partition(unsigned index, unsigned offset, Vector& slot) {
    const auto& node = tables_->partitions[index];
    const unsigned symbol = node.symbols[bits_.cache >> (32 - node.peek)];
    if (!node.lengths[symbol]) {
        throw DecodeError("partition symbol outside the ROM jump table");
    }
    bits_.skip(node.lengths[symbol]);
    if (symbol == 0) {
        motion(node.width, node.height, offset, 1, predicted_, slot);
    } else if (symbol <= 5) {
        Vector vector = predicted_;
        vector.x += bits_.signedGolomb();
        vector.y += bits_.signedGolomb();
        motion(node.width, node.height, offset, symbol, vector, slot);
    } else if (symbol == 8) {
        partition(node.upper, offset, slot);
        partition(node.upper, offset + node.height / 2 * stride_, slot);
    } else if (symbol == 9) {
        partition(node.left, offset, slot);
        partition(node.left, offset + node.width / 2, slot);
    } else {
        throw DecodeError("intra partition below macroblock level");
    }
}

// Store the vector, copy luma at half-pel and chroma at quarter-pel
// (half-pel on the half-resolution planes).
void FrameDecoder::motion(unsigned width, unsigned height, unsigned offset, unsigned reference,
                          Vector v, Vector& slot) {
    slot = v;
    const std::ptrdiff_t s = stride_;
    const std::uint8_t* lumaFrom = refs_.luma[reference] + offset + (v.y >> 1) * s + (v.x >> 1);
    checkMotion(lumaFrom, width, height, refs_.luma[reference], false);
    interpolate(refs_.luma[0] + offset, lumaFrom, width, height, v.x & 1, v.y & 1, stride_);
    const std::uint8_t* chromaFrom =
        refs_.chroma[reference] + offset / 2 + (v.y >> 2) * s + (v.x >> 2);
    // One read covers both chroma planes: the second sits half a stride on.
    checkMotion(chromaFrom, width / 2 + stride_ / 2, height / 2, refs_.chroma[reference], true);
    for (unsigned half = 0; half < 2; ++half) {
        interpolate(refs_.chroma[0] + offset / 2 + half * (stride_ / 2),
                    chromaFrom + half * (stride_ / 2), width / 2, height / 2, (v.x >> 1) & 1,
                    (v.y >> 1) & 1, stride_);
    }
}

// Coded-block pattern, then 8x8 or 4x4 residuals added to the prediction.
void FrameDecoder::interResidual(unsigned offset) {
    const unsigned coded = codedBlocks(tables_->interCodedBlocks, bits_.unsignedGolomb());
    std::uint8_t* lumaBlock = refs_.luma[0] + offset;
    const unsigned s = stride_;
    const std::size_t blocks[4] = {0, 8, 8 * s, 8 * s + 8};
    for (unsigned i = 0; i < 4; ++i) {
        if (coded & (1u << i)) {
            interBlock8x8(lumaBlock + blocks[i]);
        }
    }
    std::uint8_t* chromaBlock = refs_.chroma[0] + offset / 2;
    if (coded & 0x10) {
        interBlock8x8(chromaBlock);
    }
    if (coded & 0x20) {
        interBlock8x8(chromaBlock + stride_ / 2);
    }
}

void FrameDecoder::interBlock8x8(std::uint8_t* block) {
    if (bits_.cache >> 31) {
        bits_.cache <<= 1;
        bits_.count -= 1;
        residual8x8(block);
        return;
    }
    const unsigned coded4 = codedBlocks(tables_->interCoded4x4, bits_.unsignedGolomb());
    const unsigned s = stride_;
    const std::size_t subs[4] = {0, 4, 4 * s, 4 * s + 4};
    for (unsigned j = 0; j < 4; ++j) {
        if (coded4 & (1u << j)) {
            residual4x4(block + subs[j]);
        }
    }
}

// DS 0x0206580c, 3DS 0x11dfa0: run/level/last codes with three escape forms; each level is
// multiplied by its scan entry's step.
void FrameDecoder::coefficients(const std::uint32_t* entries, unsigned limit) {
    const auto& table = *table_;
    auto& b = bits_;
    unsigned index = 0;
    for (bool last = false; !last;) {
        std::uint32_t entry = 0, run;
        std::int32_t level;
        auto lookup = [&] {
            entry = table.lookup[b.cache >> 20];
        };
        auto signedLevel = [&](std::int32_t magnitude) { // the last code bit is the sign
            const unsigned length = entry & 15;
            const bool negative = (b.cache >> (32 - length)) & 1;
            b.cache = lsl(b.cache, length);
            b.count -= int(length);
            if (b.count < 0) {
                b.refill();
            }
            return negative ? -magnitude : magnitude;
        };
        if ((b.cache >> 25) != 3) {
            lookup();
            level = signedLevel((entry >> 4) & 31);
            run = (entry >> 9) & 63;
            last = entry >> 15;
        } else {
            const bool second = (b.cache >> 24) & 1;
            b.cache <<= 8;
            if (!second) { // level escape: add the largest level coded for this run
                b.count -= 8;
                if (b.count < 0) {
                    b.refill();
                }
                lookup();
                level =
                    signedLevel(std::int32_t((entry >> 4) & 31) + table.levelOffset[entry >> 9]);
                run = (entry >> 9) & 63;
                last = entry >> 15;
            } else if (!(b.cache >> 31)) { // run escape: add the longest run for this level + 1
                b.cache <<= 1;
                b.count -= 9;
                if (b.count < 0) {
                    b.refill();
                }
                lookup();
                const std::uint32_t magnitude = (entry >> 4) & 31;
                run = ((entry >> 9) & 63) + table.runOffset[(entry >> 15) * 64 + magnitude];
                last = entry >> 15;
                level = signedLevel(std::int32_t(magnitude));
            } else { // fixed length: last(1) run(6) level(12, signed)
                b.cache <<= 1;
                b.count -= 9;
                if (b.count < 0) {
                    b.refill();
                }
                last = b.cache >> 31;
                b.cache <<= 1;
                run = b.cache >> 26;
                b.cache <<= 6;
                b.count -= 7;
                if (b.count < 0) {
                    b.refill();
                }
                level = std::int32_t(b.cache) >> 20;
                b.cache <<= 12;
                b.count -= 12;
                if (b.count < 0) {
                    b.refill();
                }
            }
        }
        index += run;
        if (index >= limit) {
            throw DecodeError("coefficient run beyond the block");
        }
        const std::uint32_t scan = entries[index++];
        const unsigned position = scan & 0xff;
        if (position >= 64) { // DS quantizers below 12
            throw DecodeError("dequantization entry overlaps the scan position");
        }
        // The DS multiplies the low halfwords (smulbb, 0x02065864); the 3DS
        // the full step (mul, 0x117ff8 in the stride-256 copy).
        coeffs_[position] =
            generation_ == Generation::DS
                ? std::int32_t(std::int16_t(level)) * std::int32_t(std::int16_t(scan >> 8))
                : std::int32_t(std::uint32_t(level) * (scan >> 8));
    }
}

void FrameDecoder::residual8x8(std::uint8_t* block) {
    coeffs_.fill(0);
    coefficients(entries8_.data(), 64);
    coeffs_[0] += 32; // rounding for the final >> 6
    for (unsigned r = 0; r < 8; ++r) {
        inverse8(&coeffs_[r * 8], &transform_[r], 8);
    }
    std::int32_t rows[64];
    for (unsigned k = 0; k < 8; ++k) {
        inverse8(&transform_[k * 8], &rows[k * 8], 1);
    }
    addResidual(block, rows, 8);
}

void FrameDecoder::residual4x4(std::uint8_t* block) {
    std::fill_n(coeffs_.begin(), 16, 0);
    coefficients(entries4_.data(), 16);
    coeffs_[0] += 32;
    for (unsigned r = 0; r < 4; ++r) {
        inverse4(&coeffs_[r * 4], &transform_[r], 4);
    }
    std::int32_t rows[16];
    for (unsigned k = 0; k < 4; ++k) {
        inverse4(&transform_[k * 4], &rows[k * 4], 1);
    }
    addResidual(block, rows, 4);
}

// The sum goes through a 0x180-byte clamp table in the code (DS 0x02068358,
// 3DS coefficients + 0x4200; index -64..319: 0 below, identity, 255 above).
void FrameDecoder::addResidual(std::uint8_t* block, const std::int32_t* rows, unsigned size) {
    const unsigned s = stride_;
    for (unsigned y = 0; y < size; ++y) {
        for (unsigned x = 0; x < size; ++x) {
            const int value = block[y * s + x] + (rows[y * size + x] >> 6);
            if (value < -64 || value > 319) {
                throw DecodeError("reconstruction outside the ROM clamp table");
            }
            block[y * s + x] = std::uint8_t(std::clamp(value, 0, 255));
        }
    }
}

} // namespace picarat::mobiclip
