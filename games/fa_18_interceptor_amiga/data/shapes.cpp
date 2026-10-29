#include "fa_18_interceptor_amiga/data/shapes.hpp"
#include <algorithm>
#include <cmath>
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
constexpr int object_types = 134;               // records up to $116A2C
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
        : m_(program), shape_(shape), base_(base), model_(model), flat_((program.u8(model + 7) & 2) != 0) {
        const int flags = program.u8(model + 7);
        if ((flags & 8) && !(flags & 1)) generate_points();
        if (flat_ && (flags & 4)) strip_points();
    }

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
            // (bit 13, six words: a point and a normal, $1126B8) or an axis
            // comparison (bits 10-11); then
            // the offsets of the two branches.
            const std::uint32_t branches = test & 0x0C00 ? at + 4 : test & 0x2000 ? at + 16 : at + 8;
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

    // Point i of the model: x, y, z words, or x, z on the object's level
    // for flag 2 ($112292); sub-point blocks follow at the same stride.
    ShapePoint stored(int index) const {
        if (flat_) {
            const auto at = model_ + 0xA + std::uint32_t(index * 4);
            return {double(m_.s16(at)), 0, double(m_.s16(at + 2))};
        }
        const auto at = model_ + 0xA + std::uint32_t(index * point_size);
        return {double(m_.s16(at)), double(m_.s16(at + 2)), double(m_.s16(at + 4))};
    }

    // Flag 4 with flat points ($112C36, the runways): after the main points
    // blocks `n` (negative: the end), then n points each followed by
    // itself plus the vector between the two points before the block
    // (strips as wide as the first), then a word (negative: the end) and
    // two points as they are, then the next block.
    void strip_points() {
        const int count = m_.u8(model_ + 8);
        std::uint32_t a = model_ + 0xA + std::uint32_t(count * 4);
        int slot = count;
        const auto read = [&] {
            const ShapePoint p{double(m_.s16(a)), 0, double(m_.s16(a + 2))};
            a += 4;
            return p;
        };
        for (int guard = 0; guard < 32; ++guard) {
            const int n = m_.s16(a);
            a += 2;
            if (n < 0 || slot < 2) return;
            const auto vector = point(slot - 1) - point(slot - 2);
            for (int i = 0; i < n; ++i) {
                const auto p = read();
                points_[slot++] = p;
                points_[slot++] = p + vector;
            }
            const int more = m_.s16(a);
            a += 2;
            if (more < 0) return;
            points_[slot++] = read();
            points_[slot++] = read();
        }
    }

    // Flag 8 ($1124CC): chains of segments after the main points, each
    // subdivided into equal steps; the first chain fills every other slot,
    // a second one runs back through the gaps from the last point, so
    // neighbouring slots pair two edges (cables, decks). The original
    // interpolates transformed points; that is the same in model space.
    void generate_points() {
        const int count = m_.u8(model_ + 8);
        std::uint32_t a = model_ + 0xA + std::uint32_t(count * (flat_ ? 4 : point_size));
        const auto vector = [&](std::uint32_t at) {
            return ShapePoint{double(m_.s16(at)), double(m_.s16(at + 2)), double(m_.s16(at + 4))};
        };
        int slot = count;
        for (int pair = 0; pair < 16; ++pair) {
            int segments = m_.s16(a);
            a += 2;
            if (segments < 0) return;
            int stride = 2;
            auto from = vector(a);
            a += 6;
            points_[slot] = from;
            slot += 2;
            int end = slot;
            for (int chain = 0; chain < 2; ++chain) {
                for (int k = 0; k < segments; ++k) {
                    const auto to = vector(a);
                    const int steps = m_.s16(a + 6);
                    a += 8;
                    for (int j = 1; j < steps; ++j) {
                        points_[slot] = from + (double(j) / steps) * (to - from);
                        slot += stride;
                    }
                    points_[slot] = to;
                    slot += stride;
                    from = to;
                }
                if (chain == 1) break;
                end = slot;
                const auto back = m_.u16(a);
                a += 2;
                if (back == 0) break;
                slot -= back & 0x8000 ? 1 : 3;
                segments = back & 0x7FFF;
                stride = -2;
            }
            if (m_.s16(a) <= 0) return;
            slot = end;
        }
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
        const auto word = m_.u16(a); a += 2;
        ShapeFace face{std::move(corners), word & 0x1F};
        if (!(word & 0x8000)) {
            face.facing = Facing::one_sided;
            if (word & 0x4000) {
                face.facing = Facing::two_sided;
                face.two_sided = true;
                face.back_colour = word & 0x1F;
                face.colour = m_.u16(a) & 0x1F;
                a += 2;
            }
        }
        shape_.faces.push_back(std::move(face));
        return a;
    }

    void add_face(std::vector<ShapePoint> corners, int colour) {
        shape_.faces.push_back({std::move(corners), colour & 0x1F, false});
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
                    const int first = m_.u16(a) / (flat_ ? 4 : point_size); a += 2;
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
        case 0x04:                                                // a dot ($1135AC)
            shape_.dots.push_back({at_offset(m_.u16(a)), m_.u16(a + 2) & 0x1F});
            a += 4;
            return true;
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
        case 0x10C: a += 8; return true;                          // skip 18 bytes when facing away: both drawn here
        case 0xCC: a += 2; return true;                           // skip unless an object flag: both drawn here
        case 0x130: a += 2; return true;                          // skip one word
        case 0x84: case 0xC8: case 0xD8: case 0x108: return true; // state flags
        case 0x124: return true;                                  // skips a word for type 87 only
        case 0x7C: a += 2; return true;                           // orientation set-up, draws nothing
        // Scenery (game.md "Scenery shapes").
        case 0x24: case 0x78: case 0x6C: {                        // parallelogram P0 P1 P2 ($114C14, $1145EA, $1145C8)
            const int colour = m_.u16(a), v = m_.u16(a + 2) / point_size;
            a += 4;
            add_face({point(v), point(v + 1), point(v + 2), point(v + 2) - (point(v + 1) - point(v))}, colour);
            return true;
        }
        case 0x28: {                                              // parallelogram at P3 ($114C84)
            const int colour = m_.u16(a), v = m_.u16(a + 2) / point_size;
            a += 4;
            const auto u = point(v + 1) - point(v), w = point(v + 2) - point(v + 1);
            add_face({point(v + 3), point(v + 3) + u, point(v + 3) + u + w, point(v + 3) + w}, colour);
            return true;
        }
        case 0x44: case 0x48:                                     // quads, colour 12 dithered ($1147E4)
            for (int guard = 0; guard < 64; ++guard) {
                const auto first = m_.s16(a);
                a += 2;
                if (first < 0) break;
                add_face({point(first / point_size), at_offset(m_.u16(a)), at_offset(m_.u16(a + 2)),
                          at_offset(m_.u16(a + 4))}, 12);
                a += 6;
            }
            return true;
        case 0x4C: {                                              // 2n lines across a quad ($11408C)
            const int colour = m_.u16(a), v = m_.u16(a + 2) / point_size, n = m_.u16(a + 4);
            a += 6;
            const auto across = point(v) - point(v + 2), step = point(v + 1) - point(v + 2);
            const auto back = point(v + 2) - 0.5 * step + across;
            for (int j = 0; j < n; ++j) {
                const auto s1 = point(v + 2) + (0.5 * j) * step, s2 = back - (0.5 * j) * step;
                shape_.lines.push_back({s1, s1 + across, colour & 0x1F});
                shape_.lines.push_back({s2, s2 - across, colour & 0x1F});
            }
            return true;
        }
        case 0x50: case 0x80: {                                   // 2n filled stripes ($1141DA, $1141C8)
            int colour = 0xD;
            if (op == 0x80) { colour = m_.u16(a); a += 2; }
            const int v = m_.u16(a) / point_size, n = m_.u16(a + 2);
            a += 4;
            const auto across = point(v) - point(v + 2), step = point(v + 1) - point(v + 2);
            const auto back1 = point(v + 2) - 0.5 * step + across, back2 = point(v + 3) - 0.5 * step + across;
            for (int j = 0; j < n; ++j) {
                const auto s1 = point(v + 2) + (0.5 * j) * step, t1 = point(v + 3) + (0.5 * j) * step;
                add_face({s1, s1 + across, t1 + across, t1}, colour);
                const auto s2 = back1 - (0.5 * j) * step, t2 = back2 - (0.5 * j) * step;
                add_face({s2, s2 - across, t2 - across, t2}, colour);
            }
            return true;
        }
        case 0x54: case 0xE4: {                                   // rows of stripes ($1143C0, $1143AA)
            int colour = 0xD;
            if (op == 0xE4) { colour = m_.u16(a); a += 2; }
            const int v = m_.u16(a) / point_size, rows = m_.u16(a + 2);
            a += 4;
            const auto across = point(v) - point(v + 3), step = point(v + 1) - point(v + 3),
                       side = point(v + 2) - point(v + 3);
            for (int row = 0; row < rows; ++row) {
                const int n = m_.u16(a);
                a += 2;
                const auto start = point(v + 3 + row);
                for (int j = 0; j < n; ++j) {
                    const auto s1 = start + (0.5 * j) * step, t1 = start + side + (0.5 * j) * step;
                    add_face({s1, s1 + across, t1 + across, t1}, colour);
                }
            }
            return true;
        }
        case 0x2C: case 0x30: {                                   // lines between point pairs ($114960, $1149AE)
            const int word = m_.u16(a);
            a += 2;
            ShapePoint shift{};
            if (op == 0x30) {
                const int ref = m_.u16(a) / point_size;
                a += 2;
                shift = point(ref + 1) - point(ref);
            }
            const int v = m_.u16(a) / point_size;
            a += 2;
            for (int i = 0; i < (word >> 8); ++i) {
                shape_.lines.push_back({point(v + 2 * i) - shift, point(v + 2 * i + 1) - shift, word & 0x1F});
            }
            return true;
        }
        case 0x38: {                                              // moved lines ($114AA0)
            const int colour = m_.u16(a), ref = m_.u16(a + 2) / point_size;
            a += 4;
            const auto shift = point(ref + 1) - point(ref);
            for (int guard = 0; guard < 64; ++guard) {
                const auto from = m_.u16(a), to = m_.u16(a + 2);
                a += 4;
                shape_.lines.push_back({at_offset(from) - shift, at_offset(to) - shift, colour & 0x1F});
                if (to & 0x8000) break;
            }
            return true;
        }
        case 0x3C: {                                              // quad with a moved corner ($114B22)
            const int colour = m_.u16(a), ref = m_.u16(a + 2) / point_size;
            const auto p1 = at_offset(m_.u16(a + 4)), p2 = at_offset(m_.u16(a + 6)), p3 = at_offset(m_.u16(a + 8));
            a += 10;
            add_face({p1, p2, p3, p3 - (point(ref + 1) - point(ref))}, colour);
            return true;
        }
        case 0x40: {                                              // quad with a moved edge ($114B96)
            const int colour = m_.u16(a);
            const auto p1 = at_offset(m_.u16(a + 2)), p2 = at_offset(m_.u16(a + 6));
            const int ref = m_.u16(a + 4) / point_size;
            a += 8;
            const auto shift = point(ref + 1) - point(ref);
            add_face({p1, p1 - shift, p2 - shift, p2}, colour);
            return true;
        }
        case 0x1C: case 0x20: {                                   // one of two triangles by the camera's side ($114D22, $114E0E)
            const int colour = m_.u16(a);
            a += 2;
            ShapePoint shift{};
            if (op == 0x20) {
                const int ref = m_.u16(a) / point_size;
                a += 2;
                shift = point(ref + 1) - point(ref);
            }
            a += 4; // two model points: the side test
            const int apex = m_.u16(a) / point_size, v = m_.u16(a + 2) / point_size;
            a += 4;
            if (op == 0x1C) {
                add_face({point(apex), point(v), point(v + 2)}, colour);
            } else {
                const auto u = point(v + 1) - point(v), w = point(v + 2) - point(v + 1);
                add_face({point(apex) - shift, point(v + 3), point(v + 3) + u + w}, colour);
            }
            return true;
        }
        case 0x88: case 0x8C: case 0x90: case 0x94: case 0xA4: {  // more builders ($11464C, $114698, $114700, $11474C, $114F6E)
            const int v = m_.u16(a) / point_size;
            a += 2;
            int shift = 0;
            if (op == 0x88 || op == 0x90 || op == 0xA4) { shift = m_.s16(a); a += 2; }
            const auto scaled = [&](ShapePoint d) { return std::ldexp(1.0, shift) * d; };
            const auto P = [&](int i) -> ShapePoint& { return point(v + i); };
            if (op == 0xA4) {
                P(10) = P(3) - scaled(P(2) - P(3));
                P(9) = P(3) + P(2) - P(1);
                P(11) = P(7) + P(5) - P(6);
                P(12) = P(11) + P(8) - P(6);
                const auto e = P(4) - P(6);
                P(13) = P(5) + e; P(14) = P(8) + e; P(15) = P(7) + e;
                P(16) = P(11) + e; P(17) = P(12) + e;
                return true;
            }
            if (op == 0x88 || op == 0x8C) {
                if (op == 0x88) { P(8) = P(3) - scaled(P(2) - P(3)); P(9) = P(8) - (P(1) - P(2)); }
                const auto e = P(0) - P(1);
                P(4) = P(2) + e; P(5) = P(3) + e; P(6) = P(3) + P(2) - P(1); P(7) = P(6) + e;
                return true;
            }
            if (op == 0x90) { P(12) = P(4) - scaled(P(3) - P(4)); P(13) = P(12) - (P(2) - P(3)); }
            const auto e = P(0) - P(1);
            for (int i = 0; i < 4; ++i) P(6 + i) = P(2 + i) + e;
            P(10) = P(5) + P(2) - P(1); P(11) = P(10) + e;
            return true;
        }
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
    bool flat_;
    std::set<std::uint32_t> visited_nodes_;
    std::vector<std::uint32_t> lists_;
    Points points_;
};

} // namespace

namespace {

Shape decode_model(const amiga::LoadedImage& program, int type, std::uint32_t base, std::uint32_t model_word_at) {
    Shape shape;
    shape.type = type;
    shape.detail_levels = 1;
    const auto model_word = program.u16(model_word_at);
    if (model_word == 0xFFFF) return shape;
    shape.model = base + (model_word & 0xFFF);
    shape.coarse_pass = model_word & 0x2000;
    shape.scale = std::ldexp(1.0, -(program.u8(shape.model + 6) & 15));
    Decoder decoder(program, shape, base, shape.model);
    decoder.walk_tree(model_word_at + 2);
    decoder.run_lists();
    return shape;
}

} // namespace

std::vector<DetailLevel> read_detail_levels(const amiga::LoadedImage& program, int type) {
    const auto record = type_table + std::uint32_t(type) * type_size;
    const auto base = program.u32(record + 4);
    std::vector<DetailLevel> levels;
    if (program.u32(record) == 0) return levels;
    for (std::uint32_t at = program.u32(record + 8), guard = 0; guard < 8; ++guard) {
        const auto word = program.u16(at);
        if (word & 0x8000) { // the model word itself: no further limit
            DetailLevel level{1e30, std::nullopt};
            if (word != 0xFFFF) level.shape = decode_model(program, type, base, at);
            levels.push_back(std::move(level));
            break;
        }
        const bool inline_entry = word & 0x4000;
        const std::uint32_t model_at = inline_entry ? at + 2 : at + 4;
        DetailLevel level{double(word & 0x3FFF) * 16, std::nullopt}; // thresholds count 16 world units
        if (program.u16(model_at) != 0xFFFF) level.shape = decode_model(program, type, base, model_at);
        levels.push_back(std::move(level));
        if (inline_entry) {
            at += 6;
        } else {
            const auto link = program.u16(at + 2);
            if (link & 0x8000) break;
            at = base + link;
        }
    }
    return levels;
}

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
    // A negative first word is itself the model word, without a distance
    // entry ($111D9C -> $111DD2); $FFFF draws nothing.
    const std::uint32_t model_word_at = first & 0x8000 ? list : first & 0x4000 ? list + 2 : list + 4;
    const auto model_word = program.u16(model_word_at);
    if (model_word == 0xFFFF) return shape; // nothing drawn (type 88)
    shape.model = base + (model_word & 0xFFF);
    shape.coarse_pass = model_word & 0x2000;
    shape.scale = std::ldexp(1.0, -(program.u8(shape.model + 6) & 15));
    Decoder decoder(program, shape, base, shape.model);
    decoder.walk_tree(model_word_at + 2);
    decoder.run_lists();
    return shape;
}

// The flat ground shapes drawn by $112D76: `threshold, next list (long),
// point block (long)`, then a polygon list. The point block is a count, 4
// bytes, then x, z words from +6 ($112E44); sub-point blocks sit at +6 +
// offset, buffer index offset / 4. Distance bands as in the other lists,
// except that a positive second value skips one more word ($112F18).
// Opcodes $60 (n, n points, colour), $64 (the first n points, colour),
// $68 (n points from an offset, colour 7).
namespace {

Shape read_ground_list(const amiga::LoadedImage& m, int type, std::uint32_t list) {
    Shape shape;
    shape.type = type;
    shape.detail_levels = 1;
    if (m.u16(list) & 0x8000) return shape;
    const auto model = m.u32(list + 6);
    shape.model = model;
    std::map<int, ShapePoint> points;
    const int count = m.u16(model);
    const auto stored = [&](int i) {
        return ShapePoint{double(m.s16(model + 6 + std::uint32_t(4 * i))), 0,
                          double(m.s16(model + 8 + std::uint32_t(4 * i)))};
    };
    for (int i = 0; i < count; ++i) points[i] = stored(i);
    const auto point = [&](int i) { return points.count(i) ? points[i] : ShapePoint{}; };
    std::uint32_t a = list + 10;
    for (int guard = 0; guard < 200; ++guard) {
        auto word = m.u16(a);
        a += 2;
        if (!(word & 0x8000)) {
            if (word != 0x7FFF) {
                const auto second = m.u16(a);
                a += 2;
                if (!(second & 0x8000)) a += 2;
            }
            const auto subpoints = m.s16(a);
            a += 2;
            if (subpoints > 0) {
                const int offset = m.u16(a);
                a += 2;
                for (int i = 0; i < subpoints; ++i) points[offset / 4 + i] = stored(offset / 4 + i);
            }
            word = m.u16(a);
            a += 2;
        }
        if (word == 0xFFFF) break;
        const int op = word & 0x3FFF;
        ShapeFace face;
        if (op == 0x68) {
            const int n = m.u16(a), first = m.u16(a + 2) / point_size;
            a += 4;
            for (int i = 0; i < n; ++i) face.corners.push_back(point(first + i));
            face.colour = 7;
        } else if (op == 0x64) {
            const int n = m.u16(a);
            face.colour = m.u16(a + 2) & 0x1F;
            a += 4;
            for (int i = 0; i < n; ++i) face.corners.push_back(point(i));
        } else if (op == 0x60) {
            const int n = m.u16(a);
            a += 2;
            for (int i = 0; i < n; ++i) face.corners.push_back(point(m.u16(a + std::uint32_t(2 * i)) / point_size));
            a += std::uint32_t(2 * n);
            face.colour = m.u16(a) & 0x1F;
            a += 2;
        } else if (op == 0x24 || op == 0x78) {
            // A parallelogram P0 P1 P2 ($114C14, $1145D6; $24 only with
            // the camera low, `$13AD68` >= -$80: drawn always here).
            face.colour = m.u16(a) & 0x1F;
            const int v = m.u16(a + 2) / point_size;
            a += 4;
            face.corners = {point(v), point(v + 1), point(v + 2), point(v + 2) - (point(v + 1) - point(v))};
        } else if (op == 0x34 || op == 0x12C) {
            // Lines sharing a colour: pairs of point offsets, the last
            // with bit 15 ($114A34; $12C only with the camera above -$C0
            // of `$13AD68`: drawn always here).
            const int colour = m.u16(a) & 0x1F;
            a += 2;
            for (int guard = 0; guard < 64; ++guard) {
                const int from = m.u16(a), to = m.u16(a + 2);
                a += 4;
                shape.lines.push_back({point((from & 0x7FFF) / point_size), point((to & 0x7FFF) / point_size), colour});
                if (to & 0x8000) break;
            }
        } else {
            char text[48];
            std::snprintf(text, sizeof text, "ground opcode %X", op);
            shape.problems.push_back(text);
            break;
        }
        if (face.corners.size() >= 3) shape.faces.push_back(std::move(face));
        if (word & 0x4000) break;
    }
    return shape;
}

} // namespace

Shape read_ground_shape(const amiga::LoadedImage& m, int type) {
    return read_ground_list(m, type, m.u32(type_table + std::uint32_t(type) * type_size + 8));
}

// The ground lists' chain: each list's first word is how near (x 16 world
// units) the object must be for it ($7FFF: any distance), the long after it
// the next, simpler list for farther away (negative: nothing farther).
std::vector<DetailLevel> read_ground_levels(const amiga::LoadedImage& m, int type) {
    std::vector<DetailLevel> levels;
    std::uint32_t list = m.u32(type_table + std::uint32_t(type) * type_size + 8);
    std::vector<std::uint32_t> seen;
    while (m.contains(list, 10) && levels.size() < 8) {
        if (std::find(seen.begin(), seen.end(), list) != seen.end()) break; // (a list naming itself)
        seen.push_back(list);
        const int threshold = m.u16(list);
        if (threshold & 0x8000) break;
        DetailLevel level;
        level.nearer_than = threshold == 0x7FFF ? 1e30 : threshold * 16.0;
        level.shape = read_ground_list(m, type, list);
        levels.push_back(std::move(level));
        if (threshold == 0x7FFF) break;
        list = m.u32(list + 2);
    }
    return levels;
}

ShapeFamily shape_family(const amiga::LoadedImage& program, int type) {
    const auto routine = program.u32(type_table + std::uint32_t(type) * type_size);
    switch (routine) {
    case 0x111C94: case 0x1169C4: case 0x116A1E: case 0x116ABE: return ShapeFamily::vehicle;
    case 0x111C90: case 0x111D5C: case 0x111C84: return ShapeFamily::scenery;
    case 0x112D5C: case 0x112D6A: return ShapeFamily::ground;
    default: return ShapeFamily::none;
    }
}

double ground_ceiling(const amiga::LoadedImage& program, int type) {
    const auto routine = program.u32(type_table + std::uint32_t(type) * type_size);
    return routine == 0x112D5C ? 0x380000 / 256.0 : 0x800000 / 256.0;
}

std::vector<Shape> read_shapes(const amiga::LoadedImage& program, ShapeFamily family) {
    std::vector<Shape> shapes;
    std::set<std::uint32_t> seen;
    for (int type = 0; type < object_types; ++type) {
        if (shape_family(program, type) != family) continue;
        const auto record = type_table + std::uint32_t(type) * type_size;
        if (!seen.insert(program.u32(record + 8)).second) continue;
        auto shape = family == ShapeFamily::ground ? read_ground_shape(program, type) : read_shape(program, type);
        if (!shape.faces.empty() || !shape.lines.empty()) shapes.push_back(std::move(shape));
    }
    return shapes;
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
