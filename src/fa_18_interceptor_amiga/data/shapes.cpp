#include "fa_18_interceptor_amiga/data/shapes.hpp"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace interceptor::data {
namespace {

constexpr std::uint32_t type_table = 0x115F4C; // 20-byte records: routine, base, list, alternate list, extra
constexpr std::uint32_t type_size = 20;
constexpr int vehicle_types = 16;
constexpr int point_size = 6;                  // x, y, z words; offsets in the lists are bytes

using Points = std::map<int, ShapePoint>;

ShapePoint operator+(ShapePoint a, ShapePoint b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
ShapePoint operator-(ShapePoint a, ShapePoint b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
ShapePoint operator*(double k, ShapePoint a) { return {k * a.x, k * a.y, k * a.z}; }

// The vertex builders: per-model routines that complete points from others
// (parallelograms, midpoints) instead of storing them. Equations recovered
// from the routines (tools/builders.py); `p` is offset by the operand's
// point index where the opcode has one. The original computes in 16-bit
// words with arithmetic shifts, so its halves and quarters round down at
// each step; this decoder does not reproduce that rounding (viewer only).
struct Builder {
    int opcode;
    std::uint32_t routine;
    int operand_bytes;
    void (*build)(const std::function<ShapePoint&(int)>& p);
};

const Builder builders[] = {
    {0x98, 0x11558C, 0, [](const auto& p) { // buffer only
         p(11) = p(0) + p(1) - p(2);
         p(12) = p(0) + p(1) - p(4);
         p(13) = p(0) + p(1) - p(3);
         p(14) = p(0) - p(1) + p(6);
         p(15) = p(2) - p(1) + p(6);
         p(16) = p(0) - p(2) + p(6);
         p(17) = p(3) - p(4) + p(9);
         p(18) = p(0) + p(1) - 2 * p(4) + p(9);
         p(19) = p(0) + p(1) - p(3) - p(4) + p(9);
         p(20) = 3 * p(5) - 2 * p(7);
     }},
    {0xA8, 0x11540A, 2, [](const auto& p) { // $11545A twice, then $11542C
         p(17) = p(3) + p(4) - p(8);
         p(18) = p(2) - p(1) + p(6);
         p(19) = 0.5 * (p(6) - p(1)) + p(7);
         p(20) = 0.5 * (p(6) - p(1)) + p(16);
         p(21) = 2.5 * (p(6) - p(1)) + 0.5 * (p(3) + p(4));
         p(22) = 0.5 * p(0) + 1.25 * (p(6) - p(1)) + 0.25 * (p(3) + p(4));
         p(23) = p(4) + p(5) - p(8);
         p(24) = 0.5 * (p(8) + p(11));
         p(25) = p(4) + p(5) - p(3);
         p(26) = p(6) - p(1) + p(9);
         p(27) = 2 * p(6) - p(1);
         p(28) = 2 * (p(6) - p(1)) + p(2);
         p(29) = p(0) + 0.5 * (p(6) - p(1));
         p(100) = 0.5 * p(10) + 0.25 * (p(13) + p(14));
     }},
    {0xAC, 0x1152BC, 2, [](const auto& p) { // $1152DA twice
         p(10) = p(3) + p(4) - p(6);
         p(11) = 0.25 * p(3) + 0.5 * p(4) + 0.25 * p(5);
         p(12) = 0.5 * p(0) + 0.125 * p(3) + 0.25 * p(4) + 0.125 * p(5);
         p(13) = 0.625 * p(7) + 0.375 * p(9);
         p(14) = p(4) + p(5) - p(3);
         p(15) = p(3) + p(4) - p(7);
     }},
    {0xE0, 0x115D54, 0, [](const auto& p) { // mirror images through points 3 and 4
         for (int i = 0; i < 4; ++i) p(15 + i) = 2 * p(4) - p(7 + i);
         for (int i = 0; i < 4; ++i) p(19 + i) = 2 * p(3) - p(11 + i);
     }},
    {0xEC, 0x115B64, 2, [](const auto& p) { // $115BB4 twice, then $115B86
         const auto step = p(6) - p(7);
         p(21) = step + p(10);
         p(22) = 0.5 * (p(3) + p(4));
         p(23) = p(3) + p(4) - p(9);
         p(24) = p(3) + p(4) - p(8);
         p(25) = p(4) + p(5) - p(3);
         p(26) = 0.25 * (p(4) - p(3)) + p(5);
         p(27) = 0.75 * (p(4) - p(3)) + p(5);
         p(28) = p(4) + p(5) - 0.5 * (p(8) + p(9));
         p(29) = 0.5 * (p(1) + p(2));
         p(30) = step + p(12);
         p(31) = step + p(14);
         p(32) = p(1) + 1.5 * step;
         p(33) = p(2) + 1.5 * step;
         p(34) = 2.5 * step + p(11);
         p(35) = 2.5 * step + p(13);
         p(36) = p(1) + 2.5 * step;
         p(37) = p(2) + 2.5 * step;
         p(38) = p(4) + p(5) - p(9);
         p(39) = p(4) + p(5) - p(8);
         p(110) = 0.5 * p(16) + 0.25 * (p(18) + p(19));
     }},
    {0xFC, 0x115728, 0, [](const auto& p) { // five parallelograms
         p(16) = p(0) - p(1) + p(2);
         p(17) = p(3) - p(4) + p(5);
         p(18) = p(7) - p(8) + p(9);
         p(19) = p(10) - p(11) + p(12);
         p(20) = p(13) - p(14) + p(15);
     }},
    {0x100, 0x115E4C, 0, [](const auto& p) {
         p(23) = p(1) + p(2) - p(0);
         p(24) = 0.5 * (p(1) + p(2));
         p(25) = 1.5 * p(0) - 0.25 * (p(1) + p(2));
         p(26) = 1.5 * p(0) - 0.5 * p(2);
         p(27) = 1.25 * p(1) - 0.25 * p(2);
         p(28) = 0.5 * p(0) + p(1) - 0.5 * p(2);
     }},
    {0x104, 0x115EF4, 0, [](const auto& p) {
         p(23) = p(1) + p(2) - p(0);
         p(24) = p(2) + p(5) - p(0);
     }},
};

class Decoder {
public:
    Decoder(const amiga::LoadedImage& program, Shape& shape, std::uint32_t base, std::uint32_t model)
        : m_(program), shape_(shape), base_(base), model_(model) {}

    // The draw list's facing-test tree ($112650): every branch, polygon
    // lists in the order first reached.
    void walk_tree(std::uint32_t at, int depth = 0) {
        if (depth > 64) return;
        while (visited_nodes_.insert(at).second) {
            const auto word = m_.u16(at);
            if (word & 0x8000) { // a leaf
                if (word == 0xFFFF) return;
                if (word & 0x2000) { // one polygon list, the last thing drawn
                    add_list(word & 0x1000 ? m_.u32(at + 2) : base_ + (word & 0xFFF));
                    return;
                }
                // a list of polygon-list references, bit 15 on the last
                std::uint32_t refs = word & 0x1000 ? m_.u32(at + 2) : base_ + (word & 0xFFF);
                at += word & 0x1000 ? 6 : 2;
                for (int guard = 0; guard < 64; ++guard) {
                    const auto ref = m_.u16(refs);
                    if (ref & 0x1000) { add_list(m_.u32(refs + 2)); refs += 6; }
                    else { add_list(base_ + (ref & 0xFFF)); refs += 2; }
                    if (ref & 0x8000) break;
                }
                if (word & 0x4000) return; // drawing stops after this leaf
                continue;
            }
            const auto test = m_.u16(at + 2);
            if ((test & 0xFC00) == 0xFC00) { at = base_ + m_.u16(at + 4); continue; } // a jump
            // A facing test: three points ($112AD4), an explicit plane
            // (bit 13, six words) or an axis comparison (bits 10-11); then
            // the offsets of the two branches.
            const std::uint32_t branches = test & 0x0C00 ? at + 4 : test & 0x2000 ? at + 14 : at + 8;
            walk_tree(base_ + m_.u16(branches), depth + 1);
            walk_tree(base_ + m_.u16(branches + 2), depth + 1);
            return;
        }
    }

    void run_lists() {
        for (const auto list : lists_) run_list(list);
    }

private:
    void add_list(std::uint32_t list) {
        if (std::find(lists_.begin(), lists_.end(), list) == lists_.end()) lists_.push_back(list);
    }

    ShapePoint stored(int index) const {
        const auto at = model_ + 0xA + std::uint32_t(index * point_size);
        return {double(m_.s16(at)), double(m_.s16(at + 2)), double(m_.s16(at + 4))};
    }
    ShapePoint& point(int index) {
        const auto found = points_.find(index);
        if (found != points_.end()) return found->second;
        return points_[index] = stored(index);
    }
    ShapePoint& at_offset(std::uint16_t offset) { return point((offset & 0x7FFF) / point_size); }

    void problem(const char* what, std::uint32_t at, int value) {
        char text[64];
        std::snprintf(text, sizeof text, "%s %X at $%06X", what, value, at);
        shape_.problems.push_back(text);
    }

    // A face: point offsets up to one with bit 15, then a colour word;
    // bit 14 of a non-negative colour word marks a two-sided face whose
    // front colour follows ($1137E4, $113836).
    std::uint32_t face(std::uint32_t a) {
        std::vector<ShapePoint> corners;
        for (int guard = 0; guard < 32; ++guard) {
            const auto v = m_.u16(a); a += 2;
            corners.push_back(at_offset(v));
            if (v & 0x8000) break;
        }
        return face_colour(a, std::move(corners));
    }
    std::uint32_t face_colour(std::uint32_t a, std::vector<ShapePoint> corners) {
        auto colour = m_.u16(a); a += 2;
        bool two_sided = false;
        if (!(colour & 0x8000) && (colour & 0x4000)) { two_sided = true; colour = m_.u16(a); a += 2; }
        shape_.faces.push_back({std::move(corners), colour & 0x1F, two_sided});
        return a;
    }

    void line(std::uint16_t from, std::uint16_t to, std::uint16_t colour) {
        shape_.lines.push_back({at_offset(from), at_offset(to), colour & 0x1F});
    }

    // A polygon list as $11284E runs it at distance 0.
    void run_list(std::uint32_t a) {
        for (int guard = 0; guard < 400; ++guard) {
            auto word = m_.u16(a); a += 2;
            if (!(word & 0x8000)) {
                if (word != 0x7FFF) {
                    // a distance band: T then $FFFF (nearer than T: go on)
                    // or T2 (only between T and T2: not at distance 0)
                    if (!(m_.u16(a) & 0x8000)) return;
                    a += 2;
                }
                const auto count = m_.s16(a); a += 2;
                if (count > 0) { // more stored points, loaded at their own offset
                    const int first = m_.u16(a) / point_size; a += 2;
                    for (int i = 0; i < count; ++i) points_[first + i] = stored(first + i);
                }
                word = m_.u16(a); a += 2; // the opcode word, whatever its sign
            }
            if (word == 0xFFFF) { problem("end word", a - 2, word); return; }
            const int op = word & 0x3FFF;
            if (!step(op, a)) { problem("opcode", a - 2, op); return; }
            if (word & 0x4000) return; // the last opcode of the list
        }
    }

    // One opcode; advances `a` past its operands. False for one this
    // decoder does not know.
    bool step(int op, std::uint32_t& a) {
        switch (op) {
        case 0x00: return true;                                   // nothing
        case 0x04: a += 4; return true;                           // a point (dot), not shown
        case 0x08: case 0xC0: case 0x128:                         // a line (thick, thin, near only)
            line(m_.u16(a), m_.u16(a + 2), m_.u16(a + 4)); a += 6; return true;
        case 0x0C: a = face(a); return true;                      // a face
        case 0x114:                                               // a triangle ($11378A)
            a = face_colour(a + 6, {at_offset(m_.u16(a)), at_offset(m_.u16(a + 2)), at_offset(m_.u16(a + 4))});
            return true;
        case 0x10: {                                              // the ground shadow ($11392E)
            a += 2;
            for (int guard = 0; guard < 64; ++guard) {
                a += 4 + 2 * m_.u16(a);
                const auto next = m_.s16(a); a += 2;
                if (next < 0) break;
            }
            return true;
        }
        case 0x34: {                                              // lines sharing a colour ($114A34)
            const auto colour = m_.u16(a); a += 2;
            for (int guard = 0; guard < 64; ++guard) {
                const auto from = m_.u16(a), to = m_.u16(a + 2); a += 4;
                line(from, to, colour);
                if (to & 0x8000) break;
            }
            return true;
        }
        case 0xDC: {                                              // faces kept elsewhere ($113888)
            const auto table = m_.u32(a); a += 4;
            for (int guard = 0; guard < 64; ++guard) {
                const auto offset = m_.s16(a); a += 2;
                if (offset < 0) break;
                face(table + std::uint32_t(offset));
            }
            return true;
        }
        case 0x58: {                                              // rows of parallel lines ($1144F0)
            const auto colour = m_.u16(a);
            const int first = m_.u16(a + 2) / point_size;
            const int rows = m_.u16(a + 4);
            a += 6;
            const auto step = point(first) - point(first + 2), across = point(first + 1) - point(first + 2);
            for (int row = 0; row < rows; ++row) {
                const int count = m_.u16(a); a += 2;
                const auto start = point(first + 2 + row);
                for (int i = 0; i < count; ++i) {
                    const auto from = start + (0.5 * i) * step;
                    shape_.lines.push_back({from, from + across, colour & 0x1F});
                }
            }
            return true;
        }
        case 0xF8: a += 6; return true;                           // a disc (point, colour, radius), not shown
        case 0xC4: a += 8; return true;                           // a light seen from one side, not shown
        case 0x80: a += 6; return true;                           // evenly spaced lines across a quad, not shown yet
        case 0x50: a += 4; return true;                           // the same in colour $D
        case 0x10C: a += 8; return true;                          // skip 18 bytes when facing away: both drawn here
        case 0xCC: a += 2; return true;                           // skip unless an object flag: both drawn here
        case 0x130: a += 2; return true;                          // skip one word
        case 0x84: case 0xC8: case 0xD8: case 0x108: return true; // state flags
        case 0x110: case 0x120: case 0x134: return true;          // animation frames follow: the first is drawn
        default: break;
        }
        for (const auto& builder : builders) {
            if (builder.opcode != op) continue;
            const int shift = builder.operand_bytes ? m_.u16(a) / point_size : 0;
            a += std::uint32_t(builder.operand_bytes);
            builder.build([&](int i) -> ShapePoint& { return point(i + shift); });
            return true;
        }
        return false;
    }

    const amiga::LoadedImage& m_;
    Shape& shape_;
    std::uint32_t base_, model_;
    std::set<std::uint32_t> visited_nodes_;
    std::vector<std::uint32_t> lists_;
    Points points_;
};

} // namespace

Shape read_shape(const amiga::LoadedImage& program, int type) {
    const auto record = type_table + std::uint32_t(type) * type_size;
    const auto base = program.u32(record + 4);
    std::uint32_t list = program.u32(record + 8);
    if (program.u32(record) == 0) throw std::runtime_error("Object type " + std::to_string(type) + " has no shape.");
    Shape shape;
    shape.type = type;
    // Detail levels ($111D8C): threshold (bit 14: a 6-byte entry without a
    // link), link to the next entry, model word, then the draw list. At
    // distance 0 the first entry is taken.
    const auto first = program.u16(list);
    for (std::uint32_t at = list, guard = 0; guard < 8; ++guard) {
        const auto t = program.u16(at);
        if (t & 0x8000) break;
        ++shape.detail_levels;
        if (t & 0x4000) { at += 6; continue; }
        const auto link = program.u16(at + 2);
        if (link & 0x8000) break;
        at = base + link;
    }
    const std::uint32_t model_word_at = first & 0x4000 ? list + 2 : list + 4;
    const auto model_word = program.u16(model_word_at);
    shape.model = base + (model_word & 0xFFF);
    Decoder decoder(program, shape, base, shape.model);
    decoder.walk_tree(model_word_at + 2);
    decoder.run_lists();
    return shape;
}

std::vector<Shape> read_vehicle_shapes(const amiga::LoadedImage& program) {
    std::vector<Shape> shapes;
    std::set<std::uint32_t> seen;
    for (int type = 0; type < vehicle_types; ++type) {
        const auto record = type_table + std::uint32_t(type) * type_size;
        if (program.u32(record) == 0 || !seen.insert(program.u32(record + 8)).second) continue;
        shapes.push_back(read_shape(program, type));
    }
    return shapes;
}

} // namespace interceptor::data
