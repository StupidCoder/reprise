// See moflex.hpp. Routine addresses are the fifth adventure's .code
// (loaded at 0x00100000); decoder routines are named by their place in the
// stride-512 copy at 0x11a600 unless noted.
#include "video/moflex.hpp"
#include <algorithm>
#include <cstring>
#include <utility>

namespace picarat::moflex {
namespace {

// ---- Container helpers ----

// MSB-first bit reader over bytes (0x267d98); bytes are taken only as bits
// are needed, so the payload starts at the first byte not touched.
class ByteBits {
public:
    ByteBits(std::span<const std::uint8_t> data, std::size_t at) : data_(data), next_(at) {}

    std::uint64_t read(unsigned n) {
        while (count_ < n) {
            if (next_ >= data_.size()) {
                throw DecodeError("moflex packet header runs past the file");
            }
            cache_ = cache_ << 8 | data_[next_++];
            count_ += 8;
        }
        count_ -= n;
        const std::uint64_t value = n ? (cache_ >> count_) & (~0ull >> (64 - n)) : 0;
        cache_ &= count_ ? ~0ull >> (64 - count_) : 0;
        return value;
    }

    std::size_t next() const { return next_; }

private:
    std::span<const std::uint8_t> data_;
    std::size_t next_;
    std::uint64_t cache_ = 0;
    unsigned count_ = 0;
};

// 0x327a1c: up to four bytes, seven bits each with a continuation bit; the
// fourth byte gives all eight.
std::uint32_t readVarint(std::span<const std::uint8_t> d, std::size_t& at) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (at >= d.size()) {
            throw DecodeError("moflex stream descriptor runs past the file");
        }
        const std::uint8_t byte = d[at++];
        if (i == 3) {
            return value << 8 | byte;
        }
        value = value << 7 | (byte & 0x7f);
        if (!(byte & 0x80)) {
            return value;
        }
    }
    return value;
}

std::uint32_t be32(std::span<const std::uint8_t> d, std::size_t at) {
    return std::uint32_t(d[at]) << 24 | d[at + 1] << 16 | d[at + 2] << 8 | d[at + 3];
}

std::uint16_t be16(std::span<const std::uint8_t> d, std::size_t at) {
    return std::uint16_t(d[at] << 8 | d[at + 1]);
}

// 0x12d1b8: a sync header is "L2", a checksum, a 63-bit time (the top bit is
// a flag) and the block size - 1, all big-endian. The checksum folds the
// time's halfwords (top bit cleared) with 0xaaaa.
struct SyncHeader {
    std::int64_t time = 0;
    std::size_t blockSize = 0;
};

bool syncHeader(std::span<const std::uint8_t> d, std::size_t at, SyncHeader& out) {
    if (at > d.size() || d.size() - at < 14 || d[at] != 'L' || d[at + 1] != '2') {
        return false;
    }
    const std::uint32_t high = be32(d, at + 4) & 0x7fffffff, low = be32(d, at + 8);
    const unsigned sum = (high >> 16) ^ 0xaaaa ^ (high & 0xffff) ^ (low >> 16) ^ (low & 0xffff);
    if (sum != be16(d, at + 2)) {
        return false;
    }
    out.time = std::int64_t(std::uint64_t(high) << 32 | low);
    out.blockSize = std::size_t(be16(d, at + 12)) + 1;
    return true;
}

} // namespace

// ---- Demuxer (0x327c38 and its helpers) ----

Demuxer::Demuxer(std::span<const std::uint8_t> file) : file_(file) {
    SyncHeader header;
    if (!syncHeader(file_, 0, header)) {
        throw DecodeError("not a moflex file (no sync header at the start)");
    }
    rewind();
    // The stream table is complete after the first sync header.
    readSync(0);
    rewind();
}

void Demuxer::rewind() {
    position_ = 0;
    blockSize_ = 0;
    time_ = step_ = 0;
    counter_ = 0x40;
    synced_ = false;
    partial_.clear();
    ready_.clear();
    readyNext_ = 0;
}

const Stream* Demuxer::stream(unsigned index) const {
    for (const auto& s : streams_) {
        if (s.index == index) {
            return &s;
        }
    }
    return nullptr;
}

bool Demuxer::next(Frame& frame) {
    while (readyNext_ >= ready_.size()) {
        ready_.clear();
        readyNext_ = 0;
        if (!readBlock()) {
            return false;
        }
    }
    frame = std::move(ready_[readyNext_++]);
    return true;
}

// 0x3288b0: the stream descriptors after a sync header, each a varint type
// and a varint size; type 0 ends the list. The descriptor classes come from
// the factory 0x268174, which also fixes each type's size.
std::size_t Demuxer::readSync(std::size_t at) {
    SyncHeader header;
    syncHeader(file_, at, header);
    // 0x327c38: a new time starts the clock; the difference to the previous
    // one becomes the block step the counter below multiplies.
    if (header.time) {
        if (time_ && !step_) {
            step_ = header.time - time_;
        }
        time_ = header.time;
        counter_ = 0x40;
    }
    blockSize_ = header.blockSize;
    std::size_t p = at + 14;
    for (;;) {
        const std::uint32_t type = readVarint(file_, p), size = readVarint(file_, p);
        if (p > file_.size() || file_.size() - p < size) {
            throw DecodeError("moflex stream descriptor runs past the file");
        }
        const auto d = file_.subspan(p, size);
        p += size;
        if (!type) {
            return p;
        }
        Stream s;
        s.type = type;
        static constexpr unsigned kSizes[5] = {0, 12, 6, 13, 2};
        if (type > 4 || size != kSizes[type]) {
            throw DecodeError("unsupported moflex stream descriptor");
        }
        s.index = d[0];
        s.codec = d[1];
        if (type == 1 || type == 3) { // 0x3278e8 / 0x327b04
            s.kind = Stream::Kind::Video;
            s.rateNumerator = be16(d, 2);
            s.rateDenominator = be16(d, 4);
            s.width = be16(d, 6);
            s.height = be16(d, 8);
            s.aspectX = d[10];
            s.aspectY = d[11];
            if (type == 3) {
                s.layout = d[12] & 15;
                s.layoutHigh = d[12] >> 4;
            }
        } else if (type == 2) { // 0x327840: rate - 1 (24 bits), channels - 1
            s.kind = Stream::Kind::Audio;
            s.sampleRate = (std::uint32_t(d[2]) << 16 | d[3] << 8 | d[4]) + 1;
            s.channels = d[5] + 1u;
        } else { // 0x3279c4
            s.kind = Stream::Kind::Timeline;
        }
        auto old = std::find_if(streams_.begin(), streams_.end(),
                                [&](const Stream& x) { return x.index == s.index; });
        if (old == streams_.end()) {
            streams_.push_back(s);
        } else {
            *old = s;
        }
    }
}

bool Demuxer::readBlock() {
    if (position_ >= file_.size()) {
        return false;
    }
    SyncHeader header;
    std::size_t at = position_;
    if (!synced_) { // 0x327c38 looks for a sync header byte by byte
        while (at + 14 <= file_.size() && !syncHeader(file_, at, header)) {
            ++at;
        }
        if (at + 14 > file_.size()) {
            position_ = file_.size();
            return false;
        }
        synced_ = true;
    }
    const std::size_t start = at;
    std::size_t p = start;
    if (syncHeader(file_, start, header)) {
        p = readSync(start);
    }
    if (!blockSize_) {
        throw DecodeError("moflex block before any sync header");
    }
    const std::size_t end = std::min(file_.size(), start + blockSize_);
    if (p >= end) {
        throw DecodeError("moflex block header missing");
    }
    // 0x3286e0: bit 0 - the block ends at its zero byte, bit 1 - a 16-bit
    // sequence number follows, bits 2-7 - a counter; a change of the counter
    // advances the clock by the step for each count.
    const std::uint8_t flags = file_[p++];
    const bool variable = flags & 1;
    if (flags & 2) {
        p += 2;
    }
    const unsigned counter = flags >> 2;
    if (counter_ != 0x40 && counter_ != counter) {
        if (!step_) {
            throw DecodeError("moflex block counter changed before the clock step is known");
        }
        // The ROM multiplies the unsigned 32-bit difference (0x3286e0).
        time_ +=
            std::int64_t(std::uint64_t(std::uint32_t(counter - counter_)) * std::uint64_t(step_));
    }
    counter_ = counter;
    const std::size_t after = readPackets(p, end);
    position_ = variable ? after : end;
    return true;
}

// 0x329cbc: packets until a zero byte or the end of the block. Each starts
// with a bit-packed header: the stream number (n zero bits, a one, n + 1
// bits), a time-stamp flag, for a stamp a tag (the same code) and a signed
// time offset (sign bit, then 28 + 2k bits after k zero bits and a one),
// and the payload size - 1 in 13 bits. The payload follows from the next
// byte. A packet with a time stamp completes the stream's frame.
std::size_t Demuxer::readPackets(std::size_t at, std::size_t end) {
    while (at < end) {
        if (!file_[at]) {
            return at + 1;
        }
        ByteBits bits(file_.first(end), at);
        unsigned n = 1;
        while (!bits.read(1)) {
            ++n;
        }
        const auto index = unsigned(bits.read(n));
        if (!stream(index)) {
            throw DecodeError("moflex packet for an unknown stream");
        }
        const bool stamped = bits.read(1);
        unsigned tag = 0;
        std::int64_t offset = 0;
        if (stamped) {
            n = 1;
            while (!bits.read(1)) {
                ++n;
            }
            tag = unsigned(bits.read(n));
            const bool negative = bits.read(1);
            n = 0x1c;
            while (!bits.read(1)) {
                n += 2;
            }
            if (n > 0x40) {
                throw DecodeError("moflex time stamp too long");
            }
            offset = std::int64_t(bits.read(n));
            if (negative) {
                offset = -offset;
            }
        }
        const std::size_t size = std::size_t(bits.read(13)) + 1;
        at = bits.next();
        if (end - at < size) {
            throw DecodeError("moflex packet goes beyond the end of its block");
        }
        auto part = std::find_if(partial_.begin(), partial_.end(),
                                 [&](const Assembly& a) { return a.stream == index; });
        if (part == partial_.end()) {
            partial_.push_back({index, {}});
            part = partial_.end() - 1;
        }
        part->data.insert(part->data.end(), file_.begin() + std::ptrdiff_t(at),
                          file_.begin() + std::ptrdiff_t(at + size));
        at += size;
        if (stamped) {
            Frame frame;
            frame.stream = index;
            frame.time = time_ + offset;
            frame.tag = tag;
            frame.data = std::move(part->data);
            part->data.clear();
            ready_.push_back(std::move(frame));
        }
    }
    return end;
}

// ---- Decoder tables ----

namespace {

// Reads little-endian values at .code addresses.
class CodeReader {
public:
    explicit CodeReader(std::span<const std::uint8_t> code) : code_(code) {}

    const std::uint8_t* at(std::uint32_t address, std::size_t bytes) const {
        if (address < VideoTables::kCodeBase ||
            address - VideoTables::kCodeBase + bytes > code_.size()) {
            throw DecodeError("decoder table outside the code");
        }
        return code_.data() + (address - VideoTables::kCodeBase);
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
        const unsigned opcode = instruction >> 21 & 15;
        if ((instruction & 0x0e0f0000) != 0x020f0000 || (opcode != 2 && opcode != 4)) {
            throw DecodeError("decoder table reference is not an adr instruction");
        }
        return address + 8 + (opcode == 2 ? -offset : offset);
    }

private:
    std::span<const std::uint8_t> code_;
};

// The partition tree's routines, one per block size, as offsets into a copy
// of the frame routine. Each starts with "mov r6, r3, lsr #(32 - peek)",
// "adr r5, symbols", ..., "adr r6, lengths"; their jump tables give every
// node the same symbol meanings as on the DS (checked for all sixteen).
constexpr struct {
    unsigned width, height;
    std::uint32_t offset;
} kPartitionRoutines[16] = {
    {16, 16, 0x02d0}, {16, 8, 0x0ab4}, {16, 4, 0x0bd8}, {16, 2, 0x0ce4},
    {8, 16, 0x0538},  {8, 8, 0x0e08},  {8, 4, 0x0f3c},  {8, 2, 0x1048},
    {4, 16, 0x0770},  {4, 8, 0x117c},  {4, 4, 0x12a0},  {4, 2, 0x13ac},
    {2, 16, 0x0980},  {2, 8, 0x14b8},  {2, 4, 0x15c4},  {2, 2, 0x16b0},
};

// Small tables inside each copy of the frame routine (offsets from its
// first byte; the same bytes as the DS decoder's tables).
constexpr std::uint32_t kIntraCodedBlocks = 0x18c4, kInterCodedBlocks = 0x1a60,
                        kIntraCoded4x4 = 0x1df4, kInterCoded4x4 = 0x1ec4, kDequant8x8 = 0x46d4,
                        kScan8x8 = 0x4854, kDequant4x4 = 0x4894, kScan4x4 = 0x48f4,
                        kQuantShift = 0x4904, kQuantRow = 0x493a;

} // namespace

std::shared_ptr<const VideoTables> VideoTables::fromCode(std::span<const std::uint8_t> code,
                                                         const CodeLayout& layout) {
    const CodeReader rom(code);
    auto t = std::make_shared<VideoTables>();
    for (auto [table, address] :
         {std::pair{&t->a, layout.coefficients}, std::pair{&t->b, layout.coefficients + 0x2100}}) {
        const std::uint8_t* p = rom.at(address, 0x2100);
        for (std::size_t i = 0; i < table->lookup.size(); ++i) {
            table->lookup[i] = std::uint16_t(p[2 * i] | p[2 * i + 1] << 8);
        }
        rom.copy(address + 0x2000, table->levelOffset);
        rom.copy(address + 0x2080, table->runOffset);
    }
    // The clamp table the transforms add through (+0x4200, used with +0x40):
    // 0 for -64..-1, identity, 255 up to 319 - checked, the decoder computes it.
    const std::uint8_t* clamp = rom.at(layout.coefficients + 0x4200, 0x180);
    for (int i = 0; i < 0x180; ++i) {
        if (clamp[i] != std::clamp(i - 64, 0, 255)) {
            throw DecodeError("clamp table differs from 0..255 saturation");
        }
    }
    for (unsigned i = 0; i < 16; ++i) {
        const auto& source = kPartitionRoutines[i];
        const std::uint32_t routine = layout.decoder + source.offset;
        auto& node = t->partitions[i];
        node.width = source.width;
        node.height = source.height;
        const std::uint32_t first = rom.word(routine);
        if ((first & 0xfffff07f) != 0xe1a06023) { // mov r6, r3, lsr #n
            throw DecodeError("partition routine does not start as expected");
        }
        node.peek = 32 - (first >> 7 & 31);
        if (node.peek < 1 || node.peek > 6) {
            throw DecodeError("partition routine peeks an unexpected bit count");
        }
        std::memcpy(node.symbols.data(), rom.at(rom.adrTarget(routine + 4), 1u << node.peek),
                    1u << node.peek);
        rom.copy(rom.adrTarget(routine + 12), node.lengths);
    }
    t->linkPartitions();
    rom.copy(layout.decoder + kIntraCodedBlocks, t->intraCodedBlocks);
    rom.copy(layout.decoder + kInterCodedBlocks, t->interCodedBlocks);
    rom.copy(layout.decoder + kIntraCoded4x4, t->intraCoded4x4);
    rom.copy(layout.decoder + kInterCoded4x4, t->interCoded4x4);
    rom.copy(layout.decoder + kDequant8x8, t->dequant8x8);
    rom.copy(layout.decoder + kScan8x8, t->scan8x8);
    rom.copy(layout.decoder + kDequant4x4, t->dequant4x4);
    rom.copy(layout.decoder + kScan4x4, t->scan4x4);
    rom.copy(layout.decoder + kQuantShift, t->quantShift);
    rom.copy(layout.decoder + kQuantRow, t->quantRow);
    rom.copy(layout.deblocking + 12, t->alpha);
    rom.copy(layout.deblocking + 12 + 0x25, t->beta);
    return t;
}

// ---- Player (0x12467c, 0x12b878) around the shared frame routine ----

VideoDecoder::VideoDecoder(unsigned width, unsigned height,
                           std::shared_ptr<const VideoTables> tables)
    // 0x12467c: the plane stride is the next of 256, 512 and 1024; the
    // matching copy of the frame routine runs (0x12b878).
    : FrameDecoder(mobiclip::Generation::CTR, width, height,
                   width <= 256   ? 256
                   : width <= 512 ? 512
                                  : 1024,
                   std::move(tables)),
      width_(width), height_(height), stride_(width <= 256   ? 256
                                              : width <= 512 ? 512
                                                             : 1024) {
    if (width > 1024 || height > 1024) {
        throw DecodeError("unsupported frame size");
    }
    // Each buffer holds luma, then Co | Cg rows of half height, then one row.
    bufferSize_ = std::size_t(stride_) * (height + height / 2 + 1);
    memory_.assign(bufferSize_ * kBuffers, 0);
    next_ = kBuffers - 1; // player +0x0c starts at the last buffer
}

Planes VideoDecoder::planes() const {
    const std::uint8_t* base = memory_.data() + current_ * bufferSize_;
    const std::uint8_t* c = base + std::size_t(stride_) * height_;
    return {base, c, c + stride_ / 2, stride_, width_, height_};
}

std::uint32_t VideoDecoder::decode(std::span<const std::uint8_t> frame) {
    // 0x12b878: reference slot k is the buffer k frames back; slot 0 takes
    // the new frame.
    References refs;
    for (unsigned k = 0; k < 6; ++k) {
        std::uint8_t* base = memory_.data() + ((next_ + kBuffers - k) % kBuffers) * bufferSize_;
        refs.luma[k] = base;
        refs.chroma[k] = base + std::size_t(stride_) * height_;
    }
    current_ = next_;
    next_ = (next_ + 1) % kBuffers;
    return decodeFrame(frame, refs);
}

// The ROM has no guard rows; every read must stay inside the reference
// picture's buffer (one more row and column for the half-pel neighbours).
void VideoDecoder::checkMotion(const std::uint8_t* from, unsigned width, unsigned height,
                               const std::uint8_t* plane, bool chroma) const {
    const std::uint8_t* start = chroma ? plane - std::size_t(stride_) * height_ : plane;
    if (from < start || from + std::size_t(height) * stride_ + width + 1 > start + bufferSize_) {
        throw DecodeError("motion vector outside the reference picture");
    }
}

// ---- Post filter (0x165f88 and the stride-512 routines it calls) ----

namespace {

std::uint8_t saturate(int value) {
    return std::uint8_t(std::clamp(value, 0, 255));
}

// One edge position across the six pixels p2 p1 p0 | q0 q1 q2 (the SIMD
// routines filter four rows or columns per instruction; this is one lane).
// The new p0/q0 move the edge's average by d = ((p1 - q1 + 255) / 2 + 1) / 2
// + 1) / 2 - 32 (three halvings, 0x109f44 uhadd8 chain), saturated at each
// step. Luma also smooths p1/q1 towards the average where the next pixel
// out is close; chroma keeps them. The gates compare against alpha/beta;
// the chroma routines compare signed bytes, so a difference of 128 or more
// counts as small there.
struct Edge {
    std::uint8_t p2, p1, p0, q0, q1, q2;
};

void filterEdge(Edge& e, unsigned alpha, unsigned beta, bool luma) {
    auto below = [luma](unsigned difference, unsigned limit) {
        return luma ? difference < limit
                    : int(std::int8_t(std::uint8_t(difference))) < int(std::int8_t(limit));
    };
    auto distance = [](int a, int b) {
        return unsigned(a > b ? a - b : b - a);
    };
    if (!below(distance(e.p1, e.p0), beta) || !below(distance(e.q1, e.q0), beta) ||
        !below(distance(e.p0, e.q0), alpha)) {
        return;
    }
    const int average = (e.p0 + e.q0) >> 1;
    int d = ((255 - e.q1) + e.p1) >> 1;
    d = (d + 1) >> 1;
    d = (d + 1) >> 1;
    const int plus = std::max(d - 32, 0), minus = std::max(32 - d, 0);
    const std::uint8_t p0 = saturate(saturate(average + plus) - minus);
    const std::uint8_t q0 = saturate(saturate(average - plus) + minus);
    if (luma) {
        if (distance(e.p2, e.p0) < beta) {
            e.p1 = std::uint8_t((average + e.p2) >> 1);
        }
        if (distance(e.q2, e.q0) < beta) {
            e.q1 = std::uint8_t((average + e.q2) >> 1);
        }
    }
    e.p0 = p0;
    e.q0 = q0;
}

// Horizontal edges every eight rows (0x10ae04 luma, 0x10bc58 chroma) read
// the unfiltered source; vertical edges every eight columns (0x109f44,
// 0x108c58) then filter the result in place.
void deblockPlane(std::uint8_t* plane, unsigned width, unsigned height, unsigned alpha,
                  unsigned beta, bool luma) {
    const std::vector<std::uint8_t> source(plane, plane + std::size_t(width) * height);
    auto at = [&](unsigned x, unsigned y) {
        return std::size_t(y) * width + x;
    };
    for (unsigned y = 8; y < height; y += 8) {
        for (unsigned x = 0; x < width; ++x) {
            Edge e{source[at(x, y - 3)], source[at(x, y - 2)], source[at(x, y - 1)],
                   source[at(x, y)],     source[at(x, y + 1)], source[at(x, y + 2)]};
            filterEdge(e, alpha, beta, luma);
            plane[at(x, y - 2)] = e.p1;
            plane[at(x, y - 1)] = e.p0;
            plane[at(x, y)] = e.q0;
            plane[at(x, y + 1)] = e.q1;
        }
    }
    for (unsigned x = 8; x < width; x += 8) {
        for (unsigned y = 0; y < height; ++y) {
            std::uint8_t* row = plane + at(0, y);
            Edge e{row[x - 3], row[x - 2], row[x - 1], row[x], row[x + 1], row[x + 2]};
            filterEdge(e, alpha, beta, luma);
            row[x - 2] = e.p1;
            row[x - 1] = e.p0;
            row[x] = e.q0;
            row[x + 1] = e.q1;
        }
    }
}

} // namespace

Picture deblock(const Planes& planes, unsigned quantizer, const VideoTables& tables) {
    Picture picture;
    picture.width = planes.width;
    picture.height = planes.height;
    const unsigned cw = planes.width / 2, ch = planes.height / 2;
    picture.luma.resize(std::size_t(planes.width) * planes.height);
    picture.u.resize(std::size_t(cw) * ch);
    picture.v.resize(std::size_t(cw) * ch);
    for (unsigned y = 0; y < planes.height; ++y) {
        std::memcpy(&picture.luma[std::size_t(y) * planes.width],
                    planes.luma + std::size_t(y) * planes.stride, planes.width);
    }
    for (unsigned y = 0; y < ch; ++y) {
        std::memcpy(&picture.u[std::size_t(y) * cw], planes.u + std::size_t(y) * planes.stride, cw);
        std::memcpy(&picture.v[std::size_t(y) * cw], planes.v + std::size_t(y) * planes.stride, cw);
    }
    // 0x165f88 refuses quantizers outside 12..48; the movie view then keeps
    // the unfiltered planes of this frame (open item: the ROM's caller
    // reuses a stale descriptor there; no movie of either pack reaches it).
    if (quantizer < 12 || quantizer > 48) {
        return picture;
    }
    const unsigned alpha = tables.alpha[quantizer - 12], beta = tables.beta[quantizer - 12];
    if (!alpha || !beta) {
        return picture;
    }
    deblockPlane(picture.luma.data(), planes.width, planes.height, alpha, beta, true);
    deblockPlane(picture.u.data(), cw, ch, alpha, beta, false);
    deblockPlane(picture.v.data(), cw, ch, alpha, beta, false);
    return picture;
}

// ---- Colour conversion (the Y2R hardware, programmed by 0x27bb30) ----

// 0x27bb30 sets input format 1 (YUV 4:2:0, separate 8-bit planes), output
// format 1 (24-bit RGB), standard coefficient set 2 and alpha 0xff. The
// coefficient set and the hardware's arithmetic are system data, not part
// of the game: taken from Azahar 219d743 (core/hle/service/cam/y2r_u.cpp,
// "ITU_Rec601_Scaling"; core/hw/y2r.cpp, "bit-exact with hardware, as far
// as could be tested").
void toRgba(const Picture& picture, std::uint8_t* rgba, std::size_t pitchBytes) {
    constexpr int c[8] = {0x12a, 0x198, 0xd0, 0x64, 0x204, -0x1bde, 0x10f2, -0x229b};
    constexpr int rounding = 0x18;
    const unsigned cw = picture.width / 2;
    for (unsigned y = 0; y < picture.height; ++y) {
        std::uint8_t* out = rgba + std::size_t(y) * pitchBytes;
        for (unsigned x = 0; x < picture.width; ++x) {
            const int luma = picture.luma[std::size_t(y) * picture.width + x];
            const int u = picture.u[std::size_t(y / 2) * cw + x / 2];
            const int v = picture.v[std::size_t(y / 2) * cw + x / 2];
            const int base = c[0] * luma;
            const int r = ((base + c[1] * v) >> 3) + c[5] + rounding;
            const int g = ((base - c[2] * v - c[3] * u) >> 3) + c[6] + rounding;
            const int b = ((base + c[4] * u) >> 3) + c[7] + rounding;
            out[4 * x] = saturate(r >> 5);
            out[4 * x + 1] = saturate(g >> 5);
            out[4 * x + 2] = saturate(b >> 5);
            out[4 * x + 3] = 0xff;
        }
    }
}

// ---- FastAudio (0x10c86c header, 0x10ce18 pulses, 0x10cff4 synthesis) ----

std::shared_ptr<const FastAudioTables> FastAudioTables::fromCode(std::span<const std::uint8_t> code,
                                                                 std::uint32_t address) {
    const CodeReader rom(code);
    auto t = std::make_shared<FastAudioTables>();
    auto words = [&](std::uint32_t at, std::int32_t* out, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = std::int32_t(rom.word(at + 4 * std::uint32_t(i)));
        }
    };
    words(address, t->c01.data(), 64);
    words(address + 0x100, t->c2.data(), 32);
    words(address + 0x180, t->c3.data(), 32);
    words(address + 0x200, t->c4.data(), 16);
    words(address + 0x240, t->c6.data(), 8);
    words(address + 0x260, t->c7.data(), 8);
    words(address + 0x7ec, t->c5.data(), 16);
    for (unsigned row = 0; row < 64; ++row) {
        words(address + 0xba8 + row * 32, t->pulses[row].data(), 8);
    }
    return t;
}

FastAudioDecoder::FastAudioDecoder(unsigned channels, std::shared_ptr<const FastAudioTables> tables)
    : tables_(std::move(tables)), channels_(channels) {
    if (!channels) {
        throw DecodeError("audio stream without channels");
    }
}

void FastAudioDecoder::decode(std::span<const std::uint8_t> frames, std::int16_t* out) {
    if (frames.size() < kFrameBytes * channels_.size()) {
        throw DecodeError("short FastAudio frame");
    }
    for (std::size_t c = 0; c < channels_.size(); ++c) {
        decodeChannel(frames.data() + c * kFrameBytes, out + c, channels_[c]);
    }
}

void FastAudioDecoder::decodeChannel(const std::uint8_t* frame, std::int16_t* out, Channel& state) {
    const auto& t = *tables_;
    const std::size_t step = channels_.size();
    std::uint32_t w[10];
    for (unsigned i = 0; i < 10; ++i) { // ten little-endian words (two halfwords each)
        w[i] = frame[4 * i] | frame[4 * i + 1] << 8 | frame[4 * i + 2] << 16 |
               std::uint32_t(frame[4 * i + 3]) << 24;
    }
    // Word 0: the eight filter coefficients (coefficient 5 is assembled from
    // the low bits of words 3, 5, 7 and 9).
    const std::int32_t coefficient[8] = {
        t.c01[w[0] >> 26],
        t.c01[(w[0] >> 20) & 63],
        t.c2[(w[0] >> 15) & 31],
        t.c3[(w[0] >> 10) & 31],
        t.c4[(w[0] >> 6) & 15],
        t.c5[(w[3] & 1) << 3 | (w[5] & 1) << 2 | (w[7] & 1) << 1 | (w[9] & 1)],
        t.c6[(w[0] >> 3) & 7],
        t.c7[w[0] & 7],
    };
    // Word 1: per 64-sample subframe a pulse-row number and a start offset.
    // Words 2..9: per subframe 21 three-bit pulse codes (ten from each word,
    // the 21st from the first word's low bits and the second's bit 1).
    for (unsigned sub = 0; sub < 4; ++sub) {
        const auto& row = t.pulses[(w[1] >> (8 + 6 * sub)) & 63];
        const unsigned offset = (w[1] >> (2 * sub)) & 3;
        const std::uint32_t a = w[2 + 2 * sub], b = w[3 + 2 * sub];
        unsigned codes[21];
        for (unsigned k = 0; k < 10; ++k) {
            codes[k] = (a >> (29 - 3 * k)) & 7;
            codes[10 + k] = (b >> (29 - 3 * k)) & 7;
        }
        codes[20] = (a & 3) << 1 | ((b >> 1) & 1);
        std::int16_t* o = out + std::size_t(sub) * 64 * step;
        unsigned n = 0;
        auto put = [&](std::int32_t v) {
            o[std::size_t(n++) * step] = std::int16_t(v);
        }; // strh
        for (unsigned k = 0; k < offset; ++k) {
            put(0);
        }
        for (unsigned k = 0; k < 21; ++k) {
            put(row[codes[k]]);
            if (k < 20) {
                put(0);
                put(0);
            }
        }
        for (unsigned k = offset; k < 3; ++k) {
            put(0);
        }
    }
    // Lattice synthesis in place (0x10d078): Q15 products rounded with
    // 0x4000, then de-emphasis by 0x6e14 / 32768 and a doubled, saturated
    // output. 32-bit wrap-around as in the ARM multiplies.
    auto q15 = [](std::int32_t a, std::int32_t b) {
        return std::int32_t(std::uint32_t(a) * std::uint32_t(b) + 0x4000u) >> 15;
    };
    auto& s = state.lattice;
    for (unsigned i = 0; i < kSamples; ++i) {
        std::int16_t& sample = out[std::size_t(i) * step];
        std::int32_t x = sample;
        for (int k = 7; k >= 0; --k) {
            x = std::int32_t(std::uint32_t(x) - std::uint32_t(q15(coefficient[k], s[k])));
            s[k + 1] = std::int32_t(std::uint32_t(s[k]) + std::uint32_t(q15(coefficient[k], x)));
        }
        s[0] = x;
        state.emphasis =
            std::int32_t(std::uint32_t(x) + std::uint32_t(q15(state.emphasis, 0x6e14)));
        const std::int32_t doubled = std::int32_t(std::uint32_t(state.emphasis) * 2);
        sample = std::int16_t(std::clamp(doubled, -32768, 32767));
    }
}

// ---- ADPCM (codec 1) ----

std::shared_ptr<const AdpcmTables> AdpcmTables::fromCode(std::span<const std::uint8_t> code,
                                                         std::uint32_t address) {
    const CodeReader rom(code);
    auto t = std::make_shared<AdpcmTables>();
    const std::uint8_t* p = rom.at(address, 0xba);
    for (unsigned i = 0; i < 89; ++i) {
        t->steps[i] = std::int16_t(p[2 * i] | p[2 * i + 1] << 8);
    }
    for (unsigned i = 0; i < 8; ++i) {
        t->indexChange[i] = std::int8_t(p[0xb2 + i]);
    }
    return t;
}

AudioDecoder::AudioDecoder(const Stream& stream, std::shared_ptr<const FastAudioTables> fastAudio,
                           std::shared_ptr<const AdpcmTables> adpcm)
    : codec_(stream.codec), channels_(stream.channels), adpcm_(std::move(adpcm)) {
    // 0x2dc788 makes a decoder for codec 0 or 1 only.
    if (stream.kind != Stream::Kind::Audio || codec_ > 1 || !channels_ || channels_ > 2) {
        throw DecodeError("unsupported moflex audio stream");
    }
    if (codec_ == 0) {
        fastAudio_ = std::make_unique<FastAudioDecoder>(channels_, std::move(fastAudio));
    }
}

void AudioDecoder::decode(std::span<const std::uint8_t> packet,
                          std::vector<std::int16_t>& samples) {
    const std::size_t first = samples.size();
    if (codec_ == 0) {
        // 0x36c574: (size / 40) * 512 output bytes, shifted by the channel
        // count and by 8 for the frame count (exact for one and two channels).
        const std::size_t frames = (packet.size() / 40 * 512 >> channels_) >> 8;
        samples.resize(first + frames * FastAudioDecoder::kSamples * channels_);
        for (std::size_t f = 0; f < frames; ++f) {
            fastAudio_->decode(packet.subspan(f * 40 * channels_),
                               &samples[first + f * FastAudioDecoder::kSamples * channels_]);
        }
        return;
    }
    // 0x36ca58 / 0x120d7c / 0x120c08.
    if (packet.size() < 4 * channels_) {
        throw DecodeError("short ADPCM packet");
    }

    struct State {
        int predictor, index;
    } state[2];

    for (unsigned c = 0; c < channels_; ++c) {
        state[c].index = packet[4 * c];
        state[c].predictor = std::int16_t(packet[4 * c + 2] | packet[4 * c + 3] << 8);
    }
    const auto body = packet.subspan(4 * channels_);
    const std::size_t blocks = (body.size() * 4 >> channels_) >> 8;
    samples.resize(first + blocks * 256 * channels_);
    const auto& t = *adpcm_;
    auto nibble = [&](State& s, unsigned code) {
        const int step = t.steps[std::size_t(s.index)];
        s.index = std::clamp(s.index + t.indexChange[code & 7], 0, 88);
        int difference = step >> 3;
        if (code & 4) {
            difference += step;
        }
        if (code & 2) {
            difference += step >> 1;
        }
        if (code & 1) {
            difference += step >> 2;
        }
        if (code & 8) {
            difference = -difference;
        }
        s.predictor = std::clamp(s.predictor + difference, -0x8000, 0x7fff);
        return std::int16_t(s.predictor);
    };
    for (std::size_t b = 0; b < blocks; ++b) {
        for (unsigned c = 0; c < channels_; ++c) {
            const std::uint8_t* in = &body[(b * channels_ + c) * 0x80];
            std::int16_t* out = &samples[first + b * 256 * channels_ + c];
            for (unsigned i = 0; i < 0x80; ++i) {
                out[(2 * i) * channels_] = nibble(state[c], in[i] & 15);
                out[(2 * i + 1) * channels_] = nibble(state[c], in[i] >> 4);
            }
        }
    }
}

// ---- Movie ----

Tables Tables::fromCode(std::span<const std::uint8_t> code, Game game) {
    const bool sixth = game == Game::Adventure6;
    return {
        VideoTables::fromCode(code, sixth ? VideoTables::kAdventure6 : VideoTables::kAdventure5),
        FastAudioTables::fromCode(code, sixth ? FastAudioTables::kAdventure6
                                              : FastAudioTables::kAdventure5),
        AdpcmTables::fromCode(code, sixth ? AdpcmTables::kAdventure6 : AdpcmTables::kAdventure5)};
}

Movie::Movie(std::vector<std::uint8_t> file, Tables tables)
    : file_(std::move(file)), tables_(std::move(tables)) {
    demuxer_ = std::make_unique<Demuxer>(file_);
    // 0x2dc788 takes the first stream of each kind.
    bool hasVideo = false;
    for (const auto& s : demuxer_->streams()) {
        if (s.kind == Stream::Kind::Video && !hasVideo) {
            video_ = s;
            hasVideo = true;
        } else if (s.kind == Stream::Kind::Audio && !hasAudio_) {
            audio_ = s;
            hasAudio_ = true;
        }
    }
    if (!hasVideo) {
        throw DecodeError("moflex file without a video stream");
    }
    // Count the pictures once (headers only).
    Frame frame;
    unsigned pictures = 0;
    while (demuxer_->next(frame)) {
        if (frame.stream == video_.index) {
            ++pictures;
        }
    }
    frames_ = stereo() ? pictures / 2 : pictures;
    rewind();
}

double Movie::rate() const {
    const double perSecond = double(video_.rateNumerator) / double(video_.rateDenominator);
    return stereo() ? perSecond / 2 : perSecond;
}

void Movie::rewind() {
    demuxer_->rewind();
    decoder_ = std::make_unique<VideoDecoder>(video_.width, video_.height, tables_.video);
    if (hasAudio_) {
        audioDecoder_ = std::make_unique<AudioDecoder>(audio_, tables_.fastAudio, tables_.adpcm);
    }
    samples_.clear();
    inPair_ = 0;
}

bool Movie::next(std::vector<std::uint8_t>& rgba, unsigned& eye, std::int64_t& time) {
    Frame frame;
    for (;;) {
        if (!demuxer_->next(frame)) {
            return false;
        }
        if (frame.stream == video_.index) {
            break;
        }
        if (hasAudio_ && frame.stream == audio_.index) {
            audioDecoder_->decode(frame.data, samples_);
        }
    }
    decoder_->decode(frame.data);
    const Picture picture = deblock(decoder_->planes(), decoder_->quantizer(), *tables_.video);
    rgba.resize(std::size_t(picture.width) * picture.height * 4);
    toRgba(picture, rgba.data(), std::size_t(picture.width) * 4);
    // Layout 0 sends the left eye first, layout 1 the right eye (0x2dd584).
    eye = stereo() ? (inPair_ ^ (video_.layout == 1 ? 1u : 0u)) : 0;
    inPair_ = stereo() ? inPair_ ^ 1 : 0;
    time = frame.time;
    return true;
}

void Movie::takeAudio(std::vector<std::int16_t>& samples) {
    samples.insert(samples.end(), samples_.begin(), samples_.end());
    samples_.clear();
}

} // namespace picarat::moflex
