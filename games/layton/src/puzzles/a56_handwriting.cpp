// The 3DS adventures' handwriting recogniser. See a56_handwriting.hpp.
//
// Function comments cite the fifth adventure's routine addresses. Integer
// widths (16-bit indices and lengths wrapping like the original's halfword
// stores) and the order of operations follow the code.
#include "puzzles/a56_handwriting.hpp"
#include <algorithm>
#include <cstring>

namespace picarat::adventure56::hwr {
namespace {

using u8 = std::uint8_t;
using s8 = std::int8_t;
using u16 = std::uint16_t;
using s16 = std::int16_t;
using u32 = std::uint32_t;
using s32 = std::int32_t;

constexpr u16 kNone = 0xffff;
constexpr std::size_t kMaxStrokes = 32;
constexpr std::size_t kMaxPoints = 0x140;
constexpr std::size_t kMaxSegments = 0x140;
constexpr u32 kCodeBase = 0x00100000;

// 0x23e5f4: the direction from (x0, y0) to (x1, y1), one of 16 (0 right,
// 4 up, 8 left, 12 down; screen y grows downwards), and its octagonal
// length (the longer axis plus half the shorter). A zero step is
// direction 12 with length 0.
int direction(int x0, int y0, int x1, int y1, s16& length) {
    const s32 dx = s16(x1 - x0), dy = s16(y1 - y0);
    if (dx == 0 && dy == 0) {
        length = 0;
        return 12;
    }
    const s32 ndy = -dy;
    if (dx >= 0) {
        if (dy > 0) {
            if (dy > dx) {
                length = s16(dy + (dx >> 1));
                return dy > dx * 2 ? 12 : 13;
            }
            length = s16(dx + (dy >> 1));
            return dx > dy * 2 ? 15 : 14;
        }
        if (ndy < dx) {
            length = s16(dx + (ndy >> 1));
            return dx > ndy * 2 ? 0 : 1;
        }
        length = s16((dx >> 1) - dy);
        if (ndy <= dx * 2) {
            return 2;
        }
        return dx > 0 ? 3 : 4;
    }
    const s32 ndx = -dx;
    if (dy >= 0) {
        if (dy < ndx) {
            length = s16((dy >> 1) - dx);
            return ndx > dy * 2 ? 8 : 9;
        }
        length = s16(dy + (ndx >> 1));
        return dy <= ndx * 2 ? 10 : 11;
    }
    if (ndy <= ndx) {
        length = s16((ndy >> 1) - dx);
        return ndx > ndy * 2 ? 7 : 6;
    }
    length = s16((ndx >> 1) - dy);
    return ndy <= ndx * 2 ? 5 : 4;
}

// 0x240ef4: the octagonal distance between two points, plus one.
u32 distance(int x0, int y0, int x1, int y1) {
    u32 dx = u32(x1 - x0), dy = u32(y1 - y0);
    if (s32(dx) < 1) {
        dx = u32(x0 - x1);
    }
    if (s32(dy) < 1) {
        dy = u32(y0 - y1);
    }
    dx &= 0xffff;
    dy &= 0xffff;
    const u32 d = dx < dy ? dy + (dx >> 1) : dx + (dy >> 1);
    return (d + 1) & 0xffff;
}

} // namespace

struct Recognizer::Engine {
    // A segment (0xe bytes at +0x28a of the work structure).
    struct Segment {
        u16 first = 0, last = 0; // points
        u16 prev = kNone, next = kNone;
        u16 length = 0;
        s8 dir = 0;
        u8 level = 0;    // 0..7 (0x3cdb14)
        u8 inStroke = 0; // 0: the pen-up link to the next stroke
        u8 pad = 0;
    };

    struct Point {
        u8 x = 0, y = 0;
    };

    // The work structure (*0x4235d0).
    struct Work {
        u16 strokes = 0;     // +0
        u16 segments = 0;    // +2
        u16 nextSegment = 0; // +4
        u16 points = 0;      // +6
        u16 length = 0;      // +8: all segments' lengths
        std::array<Point, kMaxPoints> point{};
        std::array<Segment, kMaxSegments> seg{};
        std::array<u16, kMaxStrokes> firstPoint{};                  // +0x140a
        std::array<u16, kMaxStrokes> firstSegment{};                // +0x144a
        std::array<u16, kMaxStrokes> segmentCount{};                // +0x148a
        std::array<u16, kMaxStrokes> strokeLength{};                // +0x14ca
        std::array<u16, kMaxStrokes> longest{};                     // +0x150a
        std::array<u8, kMaxStrokes> minX{}, minY{}, maxX{}, maxY{}; // +0x154a..
        u8 boxMinX = 0, boxMinY = 0, boxMaxX = 0, boxMaxY = 0;      // +0x15ca
        u16 width = 0, height = 0, size = 0;                        // +0x15ce
        std::array<u16, kMaxStrokes> group{}; // +0x15d4: stroke groups (crossings)
    };

    // A stroke as 0x3c1f4c hands it on: its points (at most 0x100).
    struct Stroke {
        std::vector<Point> p;
    };

    std::vector<u8> db;
    std::span<const u8> code;
    int width = 0, height = 0;
    unsigned classMask = 0;
    Work w;
    // Raw points of all strokes for the histogram (0x3c4a48: at most 0x100
    // bytes; `rawUsed` keeps growing past it to mark an overflow).
    std::array<u8, 0x100> raw{};
    u16 rawUsed = 0;

    // The fifth adventure's table addresses (0x3fa060..0x3fa1e4) and where
    // this code segment keeps them: the sixth adventure's are 0x3a688 lower
    // (Recognizer finds the turn table by its contents).
    s32 codeShift = 0;

    s8 codeS8(u32 address) const {
        const u32 o = address + u32(codeShift) - kCodeBase;
        return o < code.size() ? s8(code[o]) : 0;
    }

    u8 codeU8(u32 address) const { return u8(codeS8(address)); }

    u16 codeU16(u32 address) const { return u16(codeU8(address) | codeU8(address + 1) << 8); }

    // 0x3fa1a0: |a - b| between directions, 0..8.
    int turn(int a, int b) const { return codeS8(0x3fa1a0 + u32(a - b + 0x10)); }

    // 0x3fa1c4: the signed turn from b to a, -7..8.
    int signedTurn(int a, int b) const { return codeS8(0x3fa1c4 + u32(a - b + 0x10)); }

    Segment& seg(u16 i) { return w.seg[i % kMaxSegments]; }

    Point& point(u16 i) { return w.point[i % kMaxPoints]; }

    // A segment's direction and length from its end points.
    int measure(u16 i) {
        Segment& s = seg(i);
        s16 length = 0;
        const int d =
            direction(point(s.first).x, point(s.first).y, point(s.last).x, point(s.last).y, length);
        s.length = u16(length);
        s.dir = s8(d);
        return d;
    }

    // ---- database (offsets are file offsets; the file's pointers are too)
    u8 db8(u32 o) const { return o < db.size() ? db[o] : 0; }

    u16 db16(u32 o) const { return u16(db8(o) | db8(o + 1) << 8); }

    u32 db32(u32 o) const { return u32(db16(o) | u32(db16(o + 2)) << 16); }

    // 0x12115c: the class table (+0x30): count, candidate limit (0 -> 0x20),
    // per-class largest stroke counts, 64-byte histograms, record lists.
    u16 classCount = 0, classLimit = 0x20;
    u32 classStrokes = 0, classHist = 0;
    std::vector<u32> classList; // each: count, then record offsets

    // ---- the packed segment string (0x240f2c, at 0x477120)
    std::array<u8, 0x40> packed{};
    u16 packedCount = 0, packedLast = 0, packedStrokes = 0;
    std::array<u16, kMaxStrokes + 1> strokeStart{}, strokeEnd{}; // +0x4e / +0x8c

    // ---- histograms (0x3cf7f4 / 0x3d0cf8, at 0x478808 / 0x423608)
    std::array<u8, 5> binsX{}, binsY{};
    std::array<u16, 0x180> lookup{}; // x -> column * 4 at +0x20, y -> row * 16 at +0xe0
    std::array<u16, 64> hist{};      // 4 rows x 4 columns x 4 directions

    // ---- class pre-selection and candidate characters
    struct ClassScore {
        u16 cls = 0, score = 0;
    };

    std::array<ClassScore, 0x100> classes{}; // 0x457710
    u16 classesFound = 0;                    // 0x423594
    std::array<u32, 0x100> candRecord{};     // 0x476a20: character records
    std::array<u8, 0x100> candRank{};        // 0x476e20
    std::array<u16, 0x100> candCode{};       // 0x476f20
    u16 candCount = 0;                       // 0x423612

    // ---- matching
    struct Alignment { // 0x479a14: per prototype row
        std::array<u16, 0x41> start{}, longest{}, last{};
    };

    struct RankEntry { // 12 bytes
        u32 proto = 0;
        u16 score = 0, first = 0, strokes = 0, extra = 0;
    };

    struct RankList { // +0 entries, +4 capacity, +6 count, +8 limit, +0xa worst
        std::array<RankEntry, 0x20> entries{};
        u16 cap = 0, count = 0, limit = 0, worstAt = 0;
    };

    Alignment align;
    std::array<RankList, kMaxStrokes> lists{}; // 0x474fd0 + 0x1800 + n * 0xc
    // The input view of the packed string (0x477120 +0x40 offset, +0x44
    // count, +0x4a first stroke, +0x4c strokes).
    u16 inOffset = 0, inCount = 0, inFirst = 0, inStrokes = 0;
    std::array<u16, kMaxStrokes + 2> penUp{}; // 0x4769e0, from 1
    RankList results;                         // 0x4769d4 (32 entries)

    struct Rerun {
        u32 proto = 0;
        u16 at = 0;
    };

    std::array<Rerun, 0x20> rerun{}; // 0x479888 (+0x186 count)
    u16 rerunCount = 0;
    u16 classMask16 = 0x8000; // 0x42358c (the mask | 0x8000)
    u16 stateMask = 0x8000;   // 0x4566e4 +0x10

    bool crosses(u16 a, u16 b);
    bool related(u32 rec, u32 a, u32 an, u32 b, u32 bn);
    u32 partScore(u32 proto, u32 first, u32 count, u32 limit);
    u32 composite(u32 rec, u32 first, u32 count);
    u32 classBits(u32 code) const;
    u32 candidateRank(u32 code) const;
    void rerank(RankList& list);
    u16 recognizeAll(std::array<RankEntry, 0x20>& out, u16 cap);

    u16 dtw(Alignment& align, u32 proto, u32 len, u32 limit);
    void insert(RankList& list, u32 proto, u32 score, u32 first, u32 strokes, u16 extra, int mode);
    void sortList(RankList& list, s32 lo, s32 hi);
    void matchPrototypes(RankList& list, int mode);
    void matchStrokes(u16 strokes, u32 flags);

    u32 rules(u32 prog, u32 first, u32 strokes, u32 base, s32 budget);
    void strokeBox(u32 first, u32 count, s32& x0, s32& y0, s32& x1, s32& y1);

    void substitute(u16 at, u16 code, u16 penalty);

    u32 db32Code(u32 address) const {
        return u32(codeU16(address)) | u32(codeU16(address + 2)) << 16;
    }

    // The recogniser's settings (0x4566e4: box width, height, area) and the
    // pass state (0x4235aa pre-pass flag, 0x4235ac its penalty).
    struct {
        s32 w = 0, h = 0;
        u32 area = 0;
    } screen;

    u16 prePass = 0, passPenaltyValue = 10;

    void packSegments(u16 lastStroke);
    void quantiles(const std::array<u16, 128>& h, std::array<u8, 5>& b);
    void projectionBins(u16 count, const u8* pts);
    void trace(u16 count, const u8* pts);
    void histogramFeature();
    u16 preselect();
    void dedupeCandidates(s32 count);
    u16 classify();

    void recomputeStroke(u16 first);
    void recomputeAll();
    int mergeTurns();
    int mergeShort();
    int trimEnds();
    int smoothCorners();
    void renumber();
    void markDotsAndLevels();
    int addStroke(Stroke& in);
    int input(std::span<const u8> strokes);
};

// 0x23e500: directions and lengths along the current stroke from `first`,
// with the stroke's total and longest length.
void Recognizer::Engine::recomputeStroke(u16 first) {
    const u16 n = w.strokes;
    w.strokeLength[n] = 0;
    w.longest[n] = 0;
    for (u16 i = first; i != kNone; i = seg(i).next) {
        measure(i);
        w.strokeLength[n] = u16(w.strokeLength[n] + seg(i).length);
        if (w.longest[n] < seg(i).length) {
            w.longest[n] = seg(i).length;
        }
    }
}

// 0x23e11c: all segments measured again, the per-stroke counts rebuilt
// (a stroke's first segment counted by position).
void Recognizer::Engine::recomputeAll() {
    w.length = 0;
    w.segments = 0;
    w.strokeLength[0] = 0;
    w.segmentCount[0] = 0;
    w.longest[0] = 0;
    w.firstSegment[0] = 0;
    u16 stroke = 0;
    u16 i = 0;
    do {
        measure(i);
        Segment& s = seg(i);
        if (!s.inStroke) {
            stroke = u16(stroke + 1);
            const std::size_t k = stroke % kMaxStrokes;
            w.strokeLength[k] = 0;
            w.segmentCount[k] = 0;
            w.longest[k] = 0;
            w.firstSegment[k] = u16(w.segments + 1);
        } else {
            const std::size_t k = stroke % kMaxStrokes;
            w.segmentCount[k] = u16(w.segmentCount[k] + 1);
            w.strokeLength[k] = u16(w.strokeLength[k] + s.length);
            if (w.longest[k] < s.length) {
                w.longest[k] = s.length;
            }
        }
        w.length = u16(w.length + s.length);
        w.segments = u16(w.segments + 1);
        i = s.next;
    } while (i != kNone);
}

// 0x3cceb0: a short segment between two of the same direction goes when
// it turns little or is short for its turn (turn * length < size / 4).
int Recognizer::Engine::mergeTurns() {
    const u16 size = w.size;
    const u16 first = w.firstSegment[w.strokes];
    int result = 0;
    for (;;) {
        u16 count = 0;
        if (seg(first).next == kNone) {
            return result;
        }
        u16 i = first;
        u16 nx;
        do {
            nx = seg(i).next;
            if (i != first) {
                const u16 p = seg(i).prev;
                const int t = turn(seg(nx).dir, seg(i).dir);
                if (seg(p).dir == seg(nx).dir &&
                    (t < 2 || s32(t * u32(seg(i).length)) < s32(u32(size >> 2)))) {
                    seg(p).last = seg(nx).last;
                    seg(p).next = seg(nx).next;
                    if (seg(nx).next != kNone) {
                        seg(seg(nx).next).prev = p;
                    }
                    measure(p);
                    result = 2;
                    count = u16(count + 2);
                    nx = p;
                }
            }
            i = nx;
        } while (seg(nx).next != kNone);
        if (count == 0) {
            break;
        }
        w.segmentCount[w.strokes] = u16(w.segmentCount[w.strokes] - count);
        recomputeStroke(first);
    }
    return result;
}

// 0x3cd098: a segment shorter than size / 8 joins a neighbour 16 times its
// length (or 4 times with a turn of at most one step); the lengths add.
int Recognizer::Engine::mergeShort() {
    const u32 limit = u32(w.size >> 3);
    const u16 first = w.firstSegment[w.strokes];
    int result = 0;
    for (;;) {
        u16 count = 0;
        if (first == kNone) {
            return result;
        }
        u16 i = first;
        do {
            const u16 p = seg(i).prev;
            u16 next;
            bool viaNext = i == first || p == kNone || u32(seg(p).length) >= limit;
            if (!viaNext) {
                const u32 len = seg(i).length, plen = seg(p).length;
                if (len > plen * 16 || (len > plen * 4 && turn(seg(i).dir, seg(p).dir) < 2)) {
                    if (p == first) {
                        // The first segment takes this one in.
                        seg(p).next = seg(i).next;
                        seg(p).last = seg(i).last;
                        seg(p).length = u16(seg(p).length + seg(i).length);
                        if (seg(i).next != kNone) {
                            seg(seg(i).next).prev = p;
                        }
                        next = p;
                    } else {
                        // This segment takes the previous one in.
                        seg(i).prev = seg(p).prev;
                        seg(i).first = seg(p).first;
                        seg(i).length = u16(seg(i).length + plen);
                        if (seg(p).prev != kNone) {
                            seg(seg(p).prev).next = i;
                        }
                        next = i;
                    }
                    count = u16(count + 1);
                    result = 4;
                    i = next;
                    continue;
                }
                viaNext = true;
            }
            const u16 nx = seg(i).next;
            if (nx != kNone && u32(seg(nx).length) < limit) {
                const u32 len = seg(i).length, nlen = seg(nx).length;
                if (nlen * 16 < len || (nlen * 4 < len && turn(seg(i).dir, seg(nx).dir) < 2)) {
                    const u16 nn = seg(nx).next;
                    seg(i).next = nn;
                    if (nn != kNone) {
                        seg(nn).prev = i;
                    }
                    count = u16(count + 1);
                    result = 2;
                    seg(i).last = seg(nx).last;
                    seg(i).length = u16(seg(i).length + seg(nx).length);
                }
            }
            i = seg(i).next;
        } while (i != kNone);
        if (count == 0) {
            return result;
        }
        w.segmentCount[w.strokes] = u16(w.segmentCount[w.strokes] - count);
        recomputeStroke(first);
    }
}

// 0x3cd2dc: short segments at the stroke's start and end join their
// neighbour (unless the stroke is a short scribble of short segments).
int Recognizer::Engine::trimEnds() {
    const u16 n = w.strokes;
    const u32 th = n < 6 ? (u32(w.size) >> 4) + 2 : (u32(w.size) >> 5) + 1;
    const u16 count = w.segmentCount[n];
    const u16 first = w.firstSegment[n];
    const bool scribble =
        (count > 3 && w.strokeLength[n] < th * 4) || (count > 4 && w.strokeLength[n] < th * 6);
    if (scribble && w.longest[n] < th) {
        return 0;
    }
    if (count < 2) {
        return 0;
    }
    int result = 0;
    bool done = false;
    while (!done) {
        Segment& s = seg(first);
        if (th < s.length) {
            break;
        }
        const u16 nx = s.next;
        s.last = seg(nx).last;
        s.next = seg(nx).next;
        if (seg(nx).next != kNone) {
            seg(seg(nx).next).prev = first;
        }
        w.segmentCount[w.strokes] = u16(w.segmentCount[w.strokes] - 1);
        measure(first);
        result = 8;
        if (w.segmentCount[w.strokes] < 2) {
            done = true;
        }
    }
    if (!done) {
        if (w.segmentCount[w.strokes] < 2) {
            if (result == 0) {
                return 0;
            }
        } else {
            for (;;) {
                u16 tail = first;
                while (seg(tail).next != kNone) {
                    tail = seg(tail).next;
                }
                if (seg(tail).length > th) {
                    if (result == 0) {
                        return 0;
                    }
                    break;
                }
                const u16 p = seg(tail).prev;
                seg(p).last = seg(tail).last;
                seg(p).next = kNone;
                w.segmentCount[w.strokes] = u16(w.segmentCount[w.strokes] - 1);
                measure(p);
                result = 8;
                if (w.segmentCount[w.strokes] < 2) {
                    break;
                }
            }
        }
    }
    recomputeStroke(first);
    return result;
}

// 0x3cd544: corner smoothing over all strokes with a growing length limit
// (size / 16 up to size): a short in-stroke segment between two others
// joins the neighbour it turns less against; then, while 64 or more
// segments remain, strokes are thinned.
int Recognizer::Engine::smoothCorners() {
    u32 limit = u32(w.size >> 4);
    int result = 0;
    do {
        u16 count = 0;
        if (seg(0).next == kNone) {
            recomputeAll();
            if (w.segments < 0x40) {
                break;
            }
        } else {
            u16 i = 0;
            do {
                Segment& rec = seg(i);
                if (!rec.inStroke || i == 0) {
                    i = rec.next;
                } else {
                    const u16 nx = rec.next;
                    const u16 p = rec.prev;
                    i = nx;
                    if (seg(nx).inStroke) {
                        Segment& q = seg(p);
                        bool viaNext = !q.inStroke || limit <= rec.length;
                        if (!viaNext) {
                            const int a = turn(seg(nx).dir, rec.dir);
                            const int b = turn(q.dir, rec.dir);
                            if (a > 3 || b > 3) {
                                viaNext = true;
                            } else {
                                if (b < a || (b == a && seg(nx).length <= q.length)) {
                                    q.last = rec.last;
                                    q.next = rec.next;
                                    seg(nx).prev = p;
                                    i = p;
                                } else {
                                    seg(nx).first = rec.first;
                                    seg(nx).prev = rec.prev;
                                    q.next = nx;
                                }
                                measure(i);
                                result = 0x10;
                                count = u16(count + 1);
                            }
                        }
                        if (viaNext) {
                            const bool bothLong =
                                limit <= u32(rec.length) + 4 && limit <= u32(seg(nx).length) + 4;
                            if (!bothLong && turn(seg(nx).dir, rec.dir) <= 2) {
                                seg(nx).first = rec.first;
                                seg(nx).prev = rec.prev;
                                q.next = nx;
                                measure(nx);
                                result = 0x10;
                                count = u16(count + 1);
                            }
                        }
                    }
                }
            } while (seg(i).next != kNone);
            recomputeAll();
            if (count == 0 && w.segments < 0x40) {
                break;
            }
        }
        limit = (limit + 1) & 0xffff;
    } while (limit <= w.size);
    u16 target = 2;
    while (w.segments >= 0x40) {
        u16 i = 0;
        u16 stroke = 0;
        bool found = true;
        for (;;) {
            const u16 c = w.segmentCount[stroke % kMaxStrokes];
            if (c == target) {
                break;
            }
            if (c != 0) {
                if (c & 1) {
                    i = seg(i).next;
                }
                for (u16 k = u16(c >> 1); k != 0; --k) {
                    i = seg(seg(i).next).next;
                }
            }
            if (w.strokes <= stroke) {
                target = u16(target + 1);
                if (target > 0x100) {
                    target = 2;
                }
                found = false;
                break;
            }
            i = seg(i).next;
            stroke = u16(stroke + 1);
        }
        if (!found) {
            continue;
        }
        const u16 nx = seg(i).next;
        seg(i).next = seg(nx).next;
        seg(i).last = seg(nx).last;
        if (seg(nx).next != kNone) {
            seg(seg(nx).next).prev = i;
        }
        recomputeAll();
    }
    return result;
}

// 0x3cca5c: the segment chain renumbered 0, 1, 2 ... with each segment's
// points as consecutive point numbers.
void Recognizer::Engine::renumber() {
    u16 i = 0;
    while (seg(i).next != kNone) {
        const u16 n1 = u16(i + 1);
        if (seg(i).next != n1) {
            seg(n1) = seg(seg(i).next);
            seg(i).next = n1;
            seg(n1).prev = i;
        }
        i = n1;
    }
    w.segments = u16(i + 1);
    for (u16 k = 0; k < w.segments; ++k) {
        const u16 k1 = u16(k + 1);
        if (seg(k).last != k1) {
            point(k1) = point(seg(k).last);
            seg(k1).first = k1;
            seg(k).last = k1;
        }
    }
    w.nextSegment = w.segments;
    w.points = u16(w.segments + 1);
}

// 0x3cdb14: a lone short segment (no in-stroke neighbours, length < 3)
// becomes a dot (direction 12); length levels 0..7 relative to the longest.
void Recognizer::Engine::markDotsAndLevels() {
    u16 longest = 0;
    u16 i = 0;
    do {
        Segment& s = seg(i);
        if (longest < s.length) {
            longest = s.length;
        }
        const bool prevOut = s.prev == kNone || !seg(s.prev).inStroke;
        const bool nextOut = s.next == kNone || !seg(s.next).inStroke;
        if (prevOut && nextOut && s.length < 3) {
            s.dir = 12;
        }
        i = s.next;
    } while (i != kNone);
    if (longest == 0) {
        return;
    }
    const s32 scale = s32(0xe00u / longest);
    i = 0;
    do {
        Segment& s = seg(i);
        const s32 v = s32(s.length) * scale;
        s.level = u8((v + s32(u32(v >> 31) >> 23)) >> 9);
        i = s.next;
    } while (i != kNone);
}

// 0x3c8fdc: one stroke into segments (see the header's outline). Returns
// the stroke count, or -1 when there are too many strokes or points.
int Recognizer::Engine::addStroke(Stroke& in) {
    const u16 n = w.strokes;
    const std::size_t k = n % kMaxStrokes;
    w.firstPoint[k] = w.points;
    w.firstSegment[k] = w.nextSegment;
    w.segmentCount[k] = 0;
    w.strokeLength[k] = 0;
    w.longest[k] = 0;
    if (n >= 0x20 || in.p.size() > 0x100) {
        return -1;
    }
    if (in.p.size() == 1) {
        in.p.push_back(in.p[0]);
        in.p[1].y = u8(in.p[1].y + 1);
    }
    if (n == 0) {
        w.boxMinX = w.boxMinY = 0xff;
        w.boxMaxX = w.boxMaxY = 0;
    } else {
        // The pen-up link from the previous stroke's end.
        const Point last = point(u16(w.points - 1));
        s16 len = 0;
        int d = direction(last.x, last.y, in.p[0].x, in.p[0].y, len);
        if (d == 12 && len == 0) {
            d = seg(u16(w.nextSegment - 1)).dir;
        }
        const u16 s = w.nextSegment;
        seg(s).first = u16(w.points - 1);
        seg(s).last = w.points;
        seg(s).prev = u16(s - 1);
        seg(s).next = kNone;
        seg(s).dir = s8(d);
        seg(s).length = u16(len + 1);
        seg(s).inStroke = 0;
        seg(u16(s - 1)).next = s;
        w.length = u16(w.length + len);
        w.nextSegment = u16(s + 1);
        w.firstSegment[k] = u16(w.firstSegment[k] + 1);
    }
    point(w.points) = in.p[0];
    w.minX[k] = w.maxX[k] = in.p[0].x;
    w.minY[k] = w.maxY[k] = in.p[0].y;
    w.points = u16(w.points + 1);
    std::size_t kept = 0;
    for (std::size_t i = 1; i < in.p.size(); ++i) {
        Point& q = in.p[i];
        w.minX[k] = std::min(w.minX[k], q.x);
        w.maxX[k] = std::max(w.maxX[k], q.x);
        w.minY[k] = std::min(w.minY[k], q.y);
        w.maxY[k] = std::max(w.maxY[k], q.y);
        s16 len = 0;
        const int d = direction(in.p[kept].x, in.p[kept].y, q.x, q.y, len);
        if (i + 1 == in.p.size()) {
            if (d == 12 && len == 0) {
                q.y = u8(q.y + 1);
            }
        } else if (u16(len) < 2) {
            continue;
        }
        point(w.points) = q;
        const u16 s = w.nextSegment;
        seg(s).first = u16(w.points - 1);
        seg(s).last = w.points;
        seg(s).prev = u16(s - 1);
        seg(s).next = kNone;
        seg(s).dir = s8(d);
        seg(s).length = u16(len + 1);
        seg(s).inStroke = 1;
        if (s != 0) {
            seg(u16(s - 1)).next = s;
        }
        w.points = u16(w.points + 1);
        w.nextSegment = u16(s + 1);
        w.segmentCount[k] = u16(w.segmentCount[k] + 1);
        w.strokeLength[k] = u16(w.strokeLength[k] + u16(len));
        if (w.longest[k] < u16(len)) {
            w.longest[k] = u16(len);
        }
        w.length = u16(w.length + u16(len));
        kept = i;
    }
    w.boxMinX = std::min(w.boxMinX, w.minX[k]);
    w.boxMaxX = std::max(w.boxMaxX, w.maxX[k]);
    w.boxMinY = std::min(w.boxMinY, w.minY[k]);
    w.boxMaxY = std::max(w.boxMaxY, w.maxY[k]);
    w.width = u16(w.boxMaxX - w.boxMinX + 1);
    w.height = u16(w.boxMaxY - w.boxMinY + 1);
    w.size = w.width > w.height ? u16(w.width + (w.height >> 1)) : u16(w.height + (w.width >> 1));
    bool hookOpen = true;
    for (;;) {
        int changed;
        do {
            int sameDir = 0;
            const u16 first = w.firstSegment[w.strokes];
            for (;;) {
                u16 count = 0;
                u16 s = first;
                u16 nx = seg(first).next;
                s8 d = seg(first).dir;
                if (nx == kNone) {
                    break;
                }
                do {
                    Segment& r = seg(nx);
                    if (r.dir != d) {
                        seg(s).next = nx;
                        r.prev = s;
                        seg(s).last = r.first;
                        d = r.dir;
                        s = nx;
                    } else {
                        if (r.next == kNone) {
                            seg(s).next = kNone;
                            seg(s).last = r.last;
                        }
                        count = u16(count + 1);
                        sameDir = 1;
                    }
                    nx = r.next;
                } while (nx != kNone);
                if (count == 0) {
                    break;
                }
                w.segmentCount[w.strokes] = u16(w.segmentCount[w.strokes] - count);
                recomputeStroke(first);
            }
            const int a = mergeTurns();
            const int b = mergeShort();
            changed = u16(sameDir + a + b);
        } while (changed != 0);
        if (hookOpen && n < 6 && w.segmentCount[k] >= 3) {
            // A hook at the stroke's start (its first few segments within
            // a 7 x 7 box): it joins one segment when it turns enough and
            // is short against the rest of the stroke.
            const u16 first = w.firstSegment[k];
            int turned = 0;    // r6, unsigned 16-bit sum of turns
            int signedSum = 0; // r4
            u16 prev = kNone, i = first;
            u16 sumLength = 0, taken = 0;
            u8 x1 = point(seg(first).first).x, x0 = x1;
            u8 y1 = point(seg(first).first).y, y0 = y1;
            u16 boxW = 0, boxH = 0;
            bool keepDims = false;
            do {
                boxH = u16(y1 - y0);
                boxW = u16(x1 - x0);
                const Point e = point(seg(i).last);
                x0 = std::min(x0, e.x);
                x1 = std::max(x1, e.x);
                y0 = std::min(y0, e.y);
                y1 = std::max(y1, e.y);
                if (prev != kNone) {
                    turned = u16(turned + turn(seg(prev).dir, seg(i).dir));
                    signedSum = s16(signedSum + signedTurn(seg(prev).dir, seg(i).dir));
                }
                if (x1 - x0 > 6) {
                    keepDims = y1 - y0 > 6;
                    break;
                }
                if (y1 - y0 > 6) {
                    break;
                }
                sumLength = u16(sumLength + seg(i).length);
                taken = u16(taken + 1);
                prev = i;
                i = seg(i).next;
            } while (i != kNone);
            if (!keepDims) {
                boxW = u16(x1 - x0);
                boxH = u16(y1 - y0);
            }
            if (signedSum <= 0) {
                signedSum = -signedSum;
            }
            s32 diff = s16(turned - signedSum);
            if (diff <= 0) {
                diff = -diff;
            }
            const bool loop = u32(turned - 12) <= 12 && diff <= 8 && boxW >= 3 && boxH >= 3;
            const s32 rest = s32(w.strokeLength[k]) - s32(sumLength);
            if (!loop && s32(sumLength) >= rest * 2 && taken >= 2 &&
                (turned > 8 || boxW <= 3 || boxH <= 3)) {
                seg(first).last = seg(prev).last;
                seg(first).next = i;
                w.segmentCount[k] = u16(w.segmentCount[k] - taken + 1);
                if (i != kNone) {
                    seg(i).prev = first;
                }
                hookOpen = false;
            }
        }
        if (trimEnds() == 0) {
            smoothCorners();
            renumber();
            markDotsAndLevels();
            w.strokes = u16(w.strokes + 1);
            w.segments = w.nextSegment;
            return w.strokes;
        }
    }
}

// 0x3c1f4c with 0x3c4a48: the stroke list into strokes. Points with bit 7
// of x set continue a stroke, the first without it ends it; lone points
// before a stroke are skipped; y is clamped to 0x7f; a repeated point is
// dropped (not the last) and a stroke keeps at most 0xff points before its
// last; 0xff 0xff ends the list (an error inside a stroke). From the 32nd
// stroke on, strokes are ignored.
int Recognizer::Engine::input(std::span<const u8> strokes) {
    auto byte = [&](std::size_t i) -> u8 {
        return i < strokes.size() ? strokes[i] : 0;
    };
    auto end = [&](std::size_t i) {
        return byte(i) == 0xff && byte(i + 1) == 0xff;
    };
    std::size_t at = 0;
    for (;;) {
        for (;;) {
            if (end(at)) {
                return 0;
            }
            if (at >= 0x7fe) {
                return -1;
            }
            if (byte(at) & 0x80) {
                break;
            }
            at = (at + 2) & 0xffff;
        }
        Stroke s;
        do {
            if (end(at)) {
                return -1;
            }
            const Point q{u8(byte(at) & 0x7f), u8(std::min<int>(byte(at + 1), 0x7f))};
            at = (at + 2) & 0xffff;
            if (!s.p.empty() && s.p.back().x == q.x && s.p.back().y == q.y) {
                continue;
            }
            if (s.p.size() > 0xfe) {
                s.p.pop_back();
            }
            s.p.push_back(q);
        } while (byte(at) & 0x80);
        s.p.push_back({u8(byte(at) & 0x7f), u8(std::min<int>(byte(at + 1), 0x7f))});
        at = (at + 2) & 0xffff;
        if (w.strokes >= 0x1f) {
            continue;
        }
        // 0x3c4a48: the stroke, then its points into the raw list (marked
        // as an overflow once they no longer fit in 0x100 bytes).
        if (addStroke(s) == -1) {
            return -1;
        }
        const std::vector<Point>& copy = s.p; // as addStroke left them
        const u16 count = u16(copy.size());
        const u32 used = u32(rawUsed) + u32(count) * 2;
        if (used < 0x101) {
            for (u16 j = 0; j < count; ++j) {
                raw[rawUsed + 2 * j] = u8(copy[j].x | 0x80);
                raw[rawUsed + 2 * j + 1] = copy[j].y;
            }
            rawUsed = u16(used);
            raw[rawUsed - 2] &= 0x7f;
        } else {
            rawUsed = u16(used);
        }
    }
}

// 0x240f2c: the segments of strokes 0..lastStroke packed one byte each
// (bit 7 in-stroke, bits 4-6 the length level, bits 0-3 the direction),
// with each stroke's first and last segment number.
void Recognizer::Engine::packSegments(u16 lastStroke) {
    u32 total = 0;
    for (u32 st = 0; st <= lastStroke; ++st) {
        total = (total + w.segmentCount[st % kMaxStrokes]) & 0xffff;
    }
    const u16 count = u16(total + lastStroke);
    u16 stroke = 1;
    strokeStart[0] = 0;
    u16 k = 0;
    for (; k < count; ++k) {
        const Segment& s = seg(k);
        const u8 in = u8(s.inStroke << 7) & 0x80;
        packed[k % packed.size()] = u8(in | (u8(s.level << 4) & 0x70) | (u8(s.dir) & 0x0f));
        if (in == 0) {
            strokeEnd[stroke % strokeEnd.size()] = u16(k - 1);
            strokeStart[stroke % strokeStart.size()] = u16(k + 1);
            stroke = u16(stroke + 1);
        }
    }
    strokeEnd[stroke % strokeEnd.size()] = u16(k - 1);
    packedCount = count;
    packedLast = u16(count - 1);
    packedStrokes = u16(lastStroke + 1);
    inOffset = 0;
    inCount = count;
    inFirst = 0;
    inStrokes = packedStrokes;
}

// 0x3d1a5c: four bins over a projection histogram (128 counts): the span
// of non-zero counts widened to at least 11 and by one more on each side,
// cut at the quarter, half and three quarters of the counts (each count
// plus 3); equal neighbouring cuts move one down.
void Recognizer::Engine::quantiles(const std::array<u16, 128>& h, std::array<u8, 5>& b) {
    b.fill(0);
    u32 lo = 0;
    while (lo < 0x80 && h[lo] == 0) {
        lo = (lo + 1) & 0xff;
    }
    u32 hi = 0x7f;
    if (lo < 0x7f) {
        do {
            if (h[hi] != 0) {
                hi = (hi + 1) & 0xff;
                break;
            }
            hi = (hi - 1) & 0xff;
        } while (hi > lo);
    }
    if (lo >= hi) {
        return;
    }
    const s32 span = s32(hi - lo) + 1;
    s32 pad = 0;
    if (span < 0xb) {
        pad = s16((0xb - span) / 2);
    }
    s32 l = s16(s32(lo) - pad - 1);
    if (l < 0) {
        l = 0;
    }
    s32 hh = s16(s32(hi) + pad + 1);
    if (hh >= 0x80) {
        hh = 0x7f;
    }
    const u32 a = u32(l) & 0xff, e = u32(hh) & 0xff;
    b[0] = u8(a);
    b[4] = u8(e);
    u32 total = 0;
    for (u32 i = a; i < e; ++i) {
        total += 3 + h[i];
    }
    u32 acc = 0;
    b[1] = u8(a);
    for (u32 i = a; i < e; i = (i + 1) & 0xffff) {
        if (acc > (total >> 2)) {
            b[1] = u8(i);
            break;
        }
        acc += 3 + h[i];
    }
    b[2] = b[1];
    for (u32 i = b[1]; i < e; i = (i + 1) & 0xffff) {
        if (acc > (total >> 1)) {
            b[2] = u8(i);
            break;
        }
        acc += 3 + h[i];
    }
    b[3] = b[2];
    for (u32 i = b[2]; i < e; i = (i + 1) & 0xffff) {
        if (acc > ((total * 3) >> 2)) {
            b[3] = u8(i);
            break;
        }
        acc += 3 + h[i];
    }
    for (int j = 4; j >= 2; j -= 2) {
        if (b[j] == b[j - 1] && b[j - 1] != 0) {
            b[j - 1] = u8(b[j - 1] - 1);
        }
        if (b[j - 1] == b[j - 2] && b[j - 2] != 0) {
            b[j - 2] = u8(b[j - 2] - 1);
        }
    }
}

// 0x3d0cf8: projections of the trace (each point pair walked like a
// line) on x and y, smoothed over three pixels, cut into four bins each;
// the lookup tables map x and y (with 32 guard entries on both sides) to
// bin offsets.
void Recognizer::Engine::projectionBins(u16 count, const u8* pts) {
    std::array<u16, 128> px{}, py{};
    auto bx = [&](u32 x) -> u16& {
        return px[x & 0x7f];
    };
    auto by = [&](u32 y) -> u16& {
        return py[y & 0x7f];
    };
    for (s32 k = 0; k < s32(count) - 1; ++k) {
        const u8* p = pts + 2 * k;
        u32 y = p[1];
        const u32 y1 = p[3];
        u32 x = p[0] & 0x7f;
        const u32 x1 = p[2] & 0x7f;
        s32 dx = s32(x1) - s32(x), dy = s32(y1) - s32(y);
        const s32 sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
        if (dx == 0) {
            while (y != y1) {
                bx(x) = u16(bx(x) + 1);
                const u32 at = y;
                y = u32(s16(s16(y) + s16(sy)));
                by(at) = u16(by(at) + 1);
            }
        } else if (dy == 0) {
            while (x != x1) {
                const u32 at = x;
                x = u32(s16(s16(x) + s16(sx)));
                bx(at) = u16(bx(at) + 1);
                by(y) = u16(by(y) + 1);
            }
        } else {
            dx = dx < 1 ? -dx : dx;
            dy = dy < 1 ? -dy : dy;
            const u32 X2 = u32(dx << 1) & 0xffff, Y2 = u32(dy << 1) & 0xffff;
            if (Y2 < X2) {
                s16 e = s16(-s16(X2 >> 1));
                while (x != x1) {
                    e = s16(e + s16(Y2));
                    if (e > 0 || (e == 0 && sx == 1)) {
                        e = s16(e - s16(X2));
                        y = u32(s16(s16(y) + s16(sy)));
                    }
                    x = u32(s16(s16(x) + s16(sx)));
                    bx(x) = u16(bx(x) + 1);
                    by(y) = u16(by(y) + 1);
                }
            } else {
                s16 e = s16(-s16(Y2 >> 1));
                while (y != y1) {
                    e = s16(e + s16(X2));
                    if (e > 0 || (e == 0 && sy == 1)) {
                        e = s16(e - s16(Y2));
                        x = u32(s16(s16(x) + s16(sx)));
                    }
                    y = u32(s16(s16(y) + s16(sy)));
                    bx(x) = u16(bx(x) + 1);
                    by(y) = u16(by(y) + 1);
                }
            }
        }
    }
    std::array<u16, 128> smooth{};
    smooth[0] = u16(px[0] + px[1]);
    for (int i = 1; i < 127; ++i) {
        smooth[i] = u16(px[i - 1] + px[i] + px[i + 1]);
    }
    smooth[127] = u16(px[126] + px[127]);
    quantiles(smooth, binsX);
    smooth[0] = u16(py[0] + py[1]);
    for (int i = 1; i < 127; ++i) {
        smooth[i] = u16(py[i - 1] + py[i] + py[i + 1]);
    }
    smooth[127] = u16(py[127] << 1);
    quantiles(smooth, binsY);
    for (int i = 0; i < 0x20; ++i) {
        lookup[i] = 0;
        lookup[0xc0 + i] = 0;
        lookup[0xa0 + i] = 0xc;
        lookup[0x160 + i] = 0x30;
    }
    for (int i = 0; i < 0x80; ++i) {
        lookup[0x20 + i] = i < binsX[1] ? 0 : i < binsX[2] ? 4 : i < binsX[3] ? 8 : 0xc;
        lookup[0xe0 + i] = i < binsY[1] ? 0 : i < binsY[2] ? 0x10 : i < binsY[3] ? 0x20 : 0x30;
    }
}

// 0x3cf7f4: the 4 x 4 x 4 direction histogram of the trace: every point
// pair walked like a line; each step counts in its direction's plane
// (0 across, 1 down, 2 / 3 the diagonals), smeared over 2r + 1 cells
// across the step (r a tenth of the smaller bin span); counts are capped
// at 0x3ff, scaled by bin width (<< 6 / (width * (2r + 1))) and capped at
// 0xff.
void Recognizer::Engine::trace(u16 count, const u8* pts) {
    projectionBins(count, pts);
    hist.fill(0);
    const s32 vy = s16(s32(binsY[4]) - s32(binsY[0]) + 1) * 0xcd;
    const s32 vx = s16(s32(binsX[4]) - s32(binsX[0]) + 1) * 0xcd;
    const s32 rx = (vx >> 11) - (vx >> 31), ry = (vy >> 11) - (vy >> 31);
    const s32 r = std::min(ry, rx);
    const s32 width = s16(r * 2 + 1);
    auto col = [&](s32 x) {
        return s32(lookup[std::size_t(x + 0x20) % lookup.size()]);
    };
    auto row = [&](s32 y) {
        return s32(lookup[std::size_t(y + 0xe0) % lookup.size()]);
    };
    auto add = [&](s32 cell) {
        hist[std::size_t(cell) % hist.size()] += 1;
    };
    // A straight step across (dir 0, rows y - r .. y + r) or down (dir 1,
    // columns x - r .. x + r).
    auto across = [&](s32 x, s32 y) {
        const s32 c = col(x);
        for (s32 k = -r; k <= r; ++k) {
            add(row(y + k) + c);
        }
    };
    auto down = [&](s32 x, s32 y) {
        const s32 rr = s16(row(y) + 1);
        for (s32 k = -r; k <= r; ++k) {
            add(col(x + k) + rr);
        }
    };
    // A diagonal step: plane 2 for opposite signs (cells (x + k, y + k)),
    // plane 3 for equal signs (cells (x - r + k, y + r - k)).
    auto diagonal = [&](s32 x, s32 y, bool same) {
        for (s32 k = 0; k <= 2 * r; ++k) {
            if (!same) {
                add(col(x - r + k) + row(y - r + k) + 2);
            } else {
                add(col(x - r + k) + row(y + r - k) + 3);
            }
        }
    };
    for (s32 k = 0; k < s32(count) - 1; ++k) {
        const u8* p = pts + 2 * k;
        u32 y = p[1];
        const u32 y1 = p[3];
        u32 x = p[0] & 0x7f;
        const u32 x1 = p[2] & 0x7f;
        s32 dx = s32(x1) - s32(x), dy = s32(y1) - s32(y);
        const s32 sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
        const bool same = sx == sy;
        if (dx == 0) {
            while (y != y1) {
                down(s32(x), s32(y));
                y = (y + u32(sy)) & 0xff;
            }
        } else if (dy == 0) {
            while (x != x1) {
                across(s32(x), s32(y));
                x = (x + u32(sx)) & 0xff;
            }
        } else {
            dx = dx < 1 ? -dx : dx;
            dy = dy < 1 ? -dy : dy;
            const u32 X2 = u32(dx << 1) & 0xffff, Y2 = u32(dy << 1) & 0xffff;
            if (Y2 < X2) {
                s16 e = s16(-s16(X2 >> 1));
                while (x != x1) {
                    e = s16(e + s16(Y2));
                    if (e < 1 && (e != 0 || sx != 1)) {
                        x = (x + u32(sx)) & 0xff;
                        across(s32(x), s32(y));
                    } else {
                        e = s16(e - s16(X2));
                        x = (x + u32(sx)) & 0xff;
                        y = (y + u32(sy)) & 0xff;
                        diagonal(s32(x), s32(y), same);
                    }
                }
            } else {
                s16 e = s16(-s16(Y2 >> 1));
                while (y != y1) {
                    e = s16(e + s16(X2));
                    if (e < 1 && (e != 0 || sy != 1)) {
                        y = (y + u32(sy)) & 0xff;
                        down(s32(x), s32(y));
                    } else {
                        e = s16(e - s16(Y2));
                        x = (x + u32(sx)) & 0xff;
                        y = (y + u32(sy)) & 0xff;
                        diagonal(s32(x), s32(y), same);
                    }
                }
            }
        }
    }
    for (auto& v : hist) {
        if (v > 0x3ff) {
            v = 0x3ff;
        }
    }
    auto scale = [&](u16& v, s32 span) {
        if (span < 1) {
            v = 0;
            return;
        }
        const s32 d = s32(s16(span)) * width;
        v = u16(d ? s32(u32(v) << 6) / d : s32(u32(v) << 6));
    };
    for (u32 ry2 = 0; ry2 < 4; ++ry2) {
        for (u32 cx = 0; cx < 4; ++cx) {
            const std::size_t i = ry2 * 16 + cx * 4;
            const s32 wx = s32(binsX[cx + 1]) - s32(binsX[cx]);
            const s32 wy = s32(binsY[ry2 + 1]) - s32(binsY[ry2]);
            scale(hist[i], wx);
            scale(hist[i + 1], wy);
            const s32 m = s32(std::min(u32(wx), u32(wy)));
            if (m < 1) {
                hist[i + 2] = hist[i + 3] = 0;
            } else {
                scale(hist[i + 2], m);
                scale(hist[i + 3], m);
            }
        }
    }
    for (auto& v : hist) {
        if (v > 0xff) {
            v = 0xff;
        }
    }
}

// 0x3cdbec: the trace for the histogram: the raw points while they fit
// (0x3c4a48), else each stroke's segment ends.
void Recognizer::Engine::histogramFeature() {
    if (rawUsed <= 0x100) {
        trace(u16(rawUsed >> 1), raw.data());
        return;
    }
    std::vector<u8> pts;
    for (u16 st = 0; st < w.strokes; ++st) {
        u16 i = w.firstSegment[st % kMaxStrokes];
        u16 taken = 0;
        for (;;) {
            pts.push_back(u8(point(seg(i).first).x | 0x80));
            pts.push_back(point(seg(i).first).y);
            taken = u16(taken + 1);
            if (w.segmentCount[st % kMaxStrokes] == taken) {
                break;
            }
            i = seg(i).next;
        }
        pts.push_back(point(seg(i).last).x);
        pts.push_back(point(seg(i).last).y);
    }
    trace(u16(pts.size() / 2), pts.data());
}

// 0x3cbdd8: the classes whose histograms are nearest the input's (sum of
// absolute byte differences), best first: at most classLimit (0x50 while
// the list is empty and the class's histogram is flat), classes that take
// fewer strokes left out, and beyond the fifth only those within 500 of
// the best.
u16 Recognizer::Engine::preselect() {
    std::array<u8, 64> in{};
    for (int i = 0; i < 64; ++i) {
        in[i] = u8(hist[i]);
    }
    u32 cap = classLimit;
    u32 worst = 0x1900;
    classesFound = 0;
    for (u16 c = 0; c < classCount; ++c) {
        if (w.strokes > db8(classStrokes + c)) {
            continue;
        }
        const u32 hc = classHist + u32(c) * 0x40;
        const u32 count = classesFound;
        if (count == 0) {
            cap = classLimit;
            int i = 0;
            for (; i < 0x3f; ++i) {
                if (db8(hc + i) != db8(hc + i + 1)) {
                    break;
                }
            }
            if (i == 0x3f) {
                cap = 0x50;
            }
        }
        if (true) { // 0x4566e4 +0x10 bit 0x8000 is always set (0x121018)
            // A class goes when every record of its list is flagged (bit 7
            // of byte 2) and of a kind outside the class mask.
            const u32 list = classList[c];
            const u32 n = db32(list);
            if (n != 0) {
                u32 excluded = 0;
                u32 rec = db32(list + 4) + 2;
                for (u32 j = 0; j < n; ++j, rec += 0x1c) {
                    const u8 kind = db8(rec);
                    if ((stateMask & (1u << ((kind & 0xf) - 1))) == 0 && (kind & 0x80)) {
                        ++excluded;
                    }
                }
                if (excluded == n) {
                    continue;
                }
            }
        }
        u32 dist = 0;
        for (int i = 0; i < 64; ++i) {
            const s32 d = s32(in[i]) - s32(db8(hc + i));
            dist += u32(d < 0 ? -d : d);
        }
        if (dist >= worst) {
            continue;
        }
        u32 at = 0;
        while (at < count && classes[at].score < dist) {
            at = (at + 1) & 0xffff;
        }
        if (count < cap) {
            for (u32 j = count; j > at; --j) {
                classes[j] = classes[j - 1];
            }
            classes[at] = {c, u16(dist)};
            classesFound = u16(count + 1);
            if (classesFound == cap) {
                worst = classes[classesFound - 1].score;
            }
        } else {
            for (u32 j = count - 1; j > at; --j) {
                classes[j] = classes[j - 1];
            }
            classes[at] = {c, u16(dist)};
            worst = classes[count - 1].score;
            worst = std::min<u32>(worst, u32(classes[0].score) + 600);
        }
    }
    if (classesFound > 5) {
        u32 k = 5;
        while (k < cap && k < classCount && classes[k].score <= u32(classes[0].score) + 500) {
            k = (k + 1) & 0xffff;
            if (k >= classesFound) {
                break;
            }
        }
        classesFound = u16(k);
    }
    return classesFound;
}

// 0x3ccddc: repeated characters dropped from the candidate list (the
// first kept), with their ranks and class scores.
void Recognizer::Engine::dedupeCandidates(s32 count) {
    for (s32 i = 0; i < count - 1; ++i) {
        for (s32 j = i + 1; j < count; ++j) {
            if (candRecord[std::size_t(i)] == candRecord[std::size_t(j)]) {
                for (s32 k = j; k < count; ++k) {
                    candRecord[std::size_t(k)] = candRecord[std::size_t(k + 1) % 0x100];
                    candRank[std::size_t(k)] = candRank[std::size_t(k + 1) % 0x100];
                    classes[std::size_t(k)] = classes[std::size_t(k + 1) % 0x100];
                }
                --j;
                count = s16(count - 1);
            }
        }
    }
    candCount = u16(count);
}

// 0x3c829c: the histogram, the nearest classes, rescored by row sums and
// by the normalised histograms (the input's normalised in place), sorted;
// their first records are the candidate characters, then the further
// records of each class (scored one worse than the last class).
u16 Recognizer::Engine::classify() {
    histogramFeature();
    const u16 n = preselect();
    candCount = 0;
    if (n >= 2) {
        for (u16 k = 0; k < n; ++k) {
            ClassScore& cs = classes[k];
            const u32 hc = classHist + u32(cs.cls) * 0x40;
            for (int r = 0; r < 4; ++r) {
                for (int g = 0; g < 4; ++g) {
                    s32 a = 0, b = 0;
                    for (int j = 0; j < 4; ++j) {
                        a += hist[std::size_t(r * 16 + g * 4 + j)];
                        b += db8(hc + u32(r * 16 + g * 4 + j));
                    }
                    s32 d = a - b;
                    if (d < 0) {
                        d = b - a;
                    }
                    cs.score = u16(cs.score + ((d + s32(u32(d >> 31) >> 30)) >> 2));
                }
            }
        }
        s32 sum = 0;
        for (auto v : hist) {
            sum += s16(v);
        }
        if (sum != 0) {
            for (auto& v : hist) {
                v = u16(u32(s16(v) * 0xe00) / u32(sum));
            }
        }
        for (u16 k = 0; k < n; ++k) {
            ClassScore& cs = classes[k];
            const u32 hc = classHist + u32(cs.cls) * 0x40;
            std::array<u8, 64> norm{};
            u32 total = 0;
            for (int i = 0; i < 64; ++i) {
                total += db8(hc + u32(i));
            }
            if (total != 0) {
                for (int i = 0; i < 64; ++i) {
                    norm[i] = u8(u32(db8(hc + u32(i))) * 0xe00 / total);
                }
            }
            for (int i = 0; i < 64; ++i) {
                s32 d = s32(hist[i]) - s32(norm[i]);
                if (d < 0) {
                    d = s32(norm[i]) - s32(hist[i]);
                }
                cs.score = u16(cs.score + ((d + s32(u32(d >> 31) >> 29)) >> 3));
            }
        }
    }
    for (s32 i = 0; i < s32(n) - 1; ++i) {
        for (s32 j = i + 1; j < n; ++j) {
            if (classes[j].score < classes[i].score) {
                std::swap(classes[i], classes[j]);
            }
        }
    }
    for (u16 k = 0; k < n; ++k) {
        if (classes[k].score == 0xffff) {
            break;
        }
        const u16 c = candCount;
        candRank[c] = u8(c < 0xff ? c : 0xff);
        candRecord[c] = db32(classList[classes[k].cls] + 4);
        candCount = u16(c + 1);
    }
    dedupeCandidates(candCount);
    const u16 kept = candCount;
    const u16 last = kept ? classes[kept - 1].score : 0;
    for (u16 k = 0; k < kept && candCount < 0x100; ++k) {
        if (classes[k].score == 0xffff) {
            break;
        }
        const u32 list = classList[classes[k].cls];
        const u32 records = db32(list);
        if (records == 1) {
            continue;
        }
        for (u32 e = 2; e <= records; ++e) {
            if (candCount > 0xff) {
                break;
            }
            const u16 c = candCount;
            classes[c] = {classes[k].cls, u16(last + 1)};
            candRecord[c] = db32(list + 4 * e);
            candCount = u16(c + 1);
            dedupeCandidates(candCount);
        }
    }
    for (u16 i = 0; i < 0x100; ++i) {
        candCode[i] = i < candCount ? db16(candRecord[i]) : 0;
    }
    return candCount;
}

// 0x23fe58: dynamic time warping of the input's packed segments (columns)
// against a prototype's direction string (rows), cost by direction
// difference (0x3fa0c0). The first row grows a band (stops once a cell
// passes the first cell's cost + 12 + 6 per column); later rows extend
// rightwards only while within the row minimum + a slack (12, + 6 per
// rightward step), start at the first column within it, and give up when
// the row minimum passes 0x3fa0e0[row]. A first segment of a low level
// gets a bonus (0x3fa0d0). The alignment is backtracked into `align`
// (start column per row, then +0x41 the longest input segment within each
// row's span, +0x82 its last column). Returns the cost or 0xff.
u16 Recognizer::Engine::dtw(Alignment& align, u32 proto, u32 len, u32 limit) {
    const u32 n = inCount;
    std::array<u16, 0x41> d{};
    auto in = [&](u32 i) {
        return packed[(inOffset + i) % packed.size()];
    };
    std::vector<u8> trace(std::size_t(std::max<u32>(1, n * len)), 0);
    std::size_t tp = 0;
    const s32 costBase = 0x3fa0c0;
    auto cost = [&](u8 in, u32 off) {
        return u16(codeS8(costBase + ((u32(in) + off) & 0xf)));
    };
    u32 off = 0x10 - (db8(proto) & 0xf);
    u32 slack = 0xc;
    u32 first = 0xff;
    u32 last = 0; // sp+0xcc: the last column reached
    for (u32 i = 0; i < n; ++i) {
        const u16 c = cost(in(i), off);
        d[i] = c;
        if (i == 0) {
            first = d[0];
            trace[tp++] = 3;
        } else {
            d[i] = u16(d[i] + d[i - 1]);
            last = (i - 1) & 0xffff;
            if (len != 1 && d[i] > first + slack) {
                tp += n - i;
                break;
            }
            trace[tp++] = 3;
        }
        slack = (slack + 6) & 0xffff;
    }
    u32 lo = 0;
    if (len != 1 && len != 0) {
        for (u32 j = 1; j < len; ++j) {
            const u32 pd = db8(proto + j);
            off = 0x10 - (pd & 0xf);
            u32 hi = (last + 1) & 0xffff;
            u32 diag = 0xff, left = 0xff, rowMin = 0xff, minAt = 0xff;
            slack = 0xc;
            tp += lo;
            u32 i = lo;
            for (; i < hi; ++i) {
                const u16 c = cost(in(i), off);
                const u32 up = d[i];
                u32 v, from;
                if (diag > up) {
                    if (up <= left) {
                        v = c + up;
                        from = 2;
                    } else {
                        v = c + left;
                        from = 3;
                    }
                } else if (left >= diag) {
                    v = diag + c;
                    from = 1;
                } else {
                    v = c + left;
                    from = 3;
                }
                d[i] = u16(v);
                trace[tp++] = u8(from);
                left = d[i];
                diag = up;
                if (left < rowMin) {
                    minAt = i;
                    rowMin = left;
                }
            }
            i = hi;
            bool pruned = false;
            for (; i < n; ++i) {
                const u16 c = cost(in(i), off);
                u32 from;
                if (left < diag) {
                    d[i] = u16(c + left);
                    slack = (slack + 6) & 0xffff;
                    from = 3;
                } else {
                    d[i] = u16(diag + c);
                    from = 1;
                }
                trace[tp++] = u8(from);
                left = d[i];
                if (left < rowMin) {
                    rowMin = left;
                    minAt = i;
                } else if (s32(rowMin + slack) < s32(s16(d[i]))) {
                    last = (i - 1) & 0xffff;
                    if (j == len - 1) {
                        return 0xff;
                    }
                    d[i] = 0xff;
                    tp = tp - 1 + (n - i);
                    pruned = true;
                    break;
                }
                last = i;
                diag = 0xff;
            }
            (void)pruned;
            if (lo < minAt) {
                const u32 bound = rowMin + slack;
                for (u32 k = lo; k < minAt; k = (k + 1) & 0xffff) {
                    if (bound > d[k]) {
                        lo = k;
                        break;
                    }
                }
            }
            if (rowMin > codeU16(0x3fa0e0 + 2 * j)) {
                return 0xff;
            }
        }
    }
    u32 total = d[(n - 1) % d.size()];
    if (total >= limit) {
        return 0xff;
    }
    const u8 in0 = in(0);
    if (total < 0x7f && ((in0 >> 4) & 7) < 3) {
        total = u16(total - u16(s16(codeS8(0x3fa0d0 + ((u32(in0) - db8(proto) + 0x10) & 0xf)))));
        d[(n - 1) % d.size()] = u16(total);
    }
    // Backtrack from the last cell (row len - 1, column n - 1).
    std::size_t cell = std::size_t(n) * len - 1;
    align.start[len % align.start.size()] = u16(n);
    u32 row = (len - 1) & 0xffff, col = (n - 1) & 0xffff;
    while (cell != 0) {
        align.start[row % align.start.size()] = u16(col);
        const u8 t = cell < trace.size() ? trace[cell] : 0;
        if (t == 1) {
            row = (row - 1) & 0xffff;
            col = (col - 1) & 0xffff;
            cell -= n + 1;
        } else if (t == 2) {
            row = (row - 1) & 0xffff;
            cell -= n;
        } else if (t == 3) {
            col = (col - 1) & 0xffff;
            cell -= 1;
        } else {
            break; // stale cell (never seen on the console)
        }
    }
    align.start[row % align.start.size()] = u16(col);
    for (u32 k = 0; k < len; ++k) {
        const u32 a = align.start[k], b = align.start[k + 1];
        u32 best = 0;
        if (b == a) {
            align.longest[k] = u16(a);
            align.last[k] = u16(a);
        } else if (b > a) {
            for (u32 i = a; i < b; ++i) {
                align.last[k] = u16(i);
                const u32 lvl = ((in(i) >> 4) & 7) + 1;
                if (lvl > best) {
                    align.longest[k] = u16(i);
                    best = lvl;
                }
            }
        }
    }
    return u16(d[(n - 1) % d.size()]);
}

// 0x240ca8: an entry into a ranked list (mode 2: the result list, one entry
// per record, keeping the lower score; another non-zero mode skips exact
// repeats); when the list is full the worst entry is replaced, and a full
// list's limit becomes its worst score.
void Recognizer::Engine::insert(RankList& list, u32 proto, u32 score, u32 first, u32 strokes,
                                u16 extra, int mode) {
    auto& e = list.entries;
    bool update = false;
    if (mode == 2) {
        if (proto != 0) {
            first = db16(proto); // the record's character
            if (first == 0) {
                return;
            }
        }
        for (u16 i = 0; i < list.count; ++i) {
            if (e[i].proto == proto) {
                if (e[i].score <= score) {
                    return;
                }
                e[i].score = u16(score);
                update = true;
                break;
            }
        }
    } else if (mode != 0) {
        for (u16 i = 0; i < list.count; ++i) {
            if (e[i].proto == proto && e[i].first == u16(first) && e[i].strokes == u16(strokes)) {
                return;
            }
        }
    }
    if (!update) {
        const u16 at = list.count < list.cap ? list.count : list.worstAt;
        e[at] = {proto, u16(score), u16(first), u16(strokes), extra};
        if (list.count < list.cap) {
            list.count = u16(list.count + 1);
        }
    }
    if (list.count == list.cap) {
        list.limit = 0;
        for (u16 i = 0; i < list.cap; ++i) {
            if (list.limit < e[i].score) {
                list.limit = e[i].score;
                list.worstAt = i;
            }
        }
    }
}

// 0x240770: quicksort of entries lo..hi by score, then record (Hoare
// partition around the middle entry; the left part recursively).
void Recognizer::Engine::sortList(RankList& list, s32 lo, s32 hi) {
    auto& e = list.entries;
    auto key = [&](const RankEntry& x) {
        return std::pair<u16, u32>(x.score, x.proto);
    };
    for (;;) {
        const auto pivot = key(e[std::size_t((lo + hi) / 2)]);
        s32 i = lo, j = hi;
        if (lo <= hi) {
            do {
                while (key(e[std::size_t(i)]) < pivot) {
                    i = s16(i + 1);
                }
                while (pivot < key(e[std::size_t(j)])) {
                    j = s16(j - 1);
                }
                if (i <= j) {
                    std::swap(e[std::size_t(i)], e[std::size_t(j)]);
                    i = s16(i + 1);
                    j = s16(j - 1);
                }
            } while (i <= j);
        }
        if (lo < j) {
            sortList(list, lo, j);
        }
        lo = i;
        if (i >= hi) {
            break;
        }
    }
}

// 0x3cd97c: every prototype of every candidate character whose stroke
// range (with tolerances +4 / +1 unless flagged) and segment range fit is
// aligned (0x23fe58) and checked by its rule program (0x23e770); fitting
// ones go into the current list.
void Recognizer::Engine::matchPrototypes(RankList& list, int mode) {
    for (u16 u = 0; u < candCount; ++u) {
        const u32 rec = candRecord[u];
        if (rec == 0xffff) {
            continue;
        }
        u32 proto = db32(rec + 0xc);
        if (proto == 0xffffffff) {
            continue;
        }
        do {
            if (candRecord[u] != db32(proto + 0xc)) {
                break;
            }
            u32 more = 4, fewer = 1; // 0x4566e4 +0x18 / +0x16 (0x120da0)
            if (db8(proto + 0x15) != 0) {
                more = fewer = 0;
            }
            if (db8(proto + 0x18) <= more + packedStrokes &&
                packedStrokes <= db8(proto + 0x19) + fewer) {
                const u32 rule = db32(proto + 8);
                const u32 n = packedCount & 0xff;
                if (db8(rule) <= n && n <= db8(rule + 1)) {
                    u32 score = dtw(align, proto + 0x1c, db8(proto + 0x1b), 0x7f);
                    if (score != 0xff && score < list.limit) {
                        score = (score + rules(rule, inFirst, inStrokes, 0, 0)) & 0xffff;
                        if (score < list.limit) {
                            insert(list, proto, score, inFirst, inStrokes, 0, mode);
                        }
                    }
                }
            }
            proto = db32(proto + 4);
        } while (proto != 0xffffffff);
    }
}

// 0x3c8e14: the list for `strokes` strokes (limit 0x3fa060[segments - 1]
// with flag 2, else 0x7f): matched, the worse of two entries of one
// character pushed to the limit, sorted, cut after the last entry under
// the limit (keeping two).
void Recognizer::Engine::matchStrokes(u16 strokes, u32 flags) {
    const u16 last = u16(strokes - 1);
    u32 segs = 0;
    for (u32 st = 0; st <= last; ++st) {
        segs = (segs + w.segmentCount[st % kMaxStrokes]) & 0xffff;
    }
    u16 limit = codeU8(0x3fa060 + ((segs + last) & 0xffff) - 1);
    if ((flags & 2) == 0) {
        limit = 0x7f;
    } else {
        packSegments(last);
    }
    RankList& list = lists[last % lists.size()];
    list.cap = 0x10;
    list.count = 0;
    list.limit = limit;
    matchPrototypes(list, 0);
    if (list.count > 1) {
        for (u16 u = 1; u < list.count; ++u) {
            for (u16 v = 0; v < u; ++v) {
                RankEntry& a = list.entries[u];
                RankEntry& b = list.entries[v];
                if (db32(a.proto + 0xc) == db32(b.proto + 0xc)) {
                    if (a.score > b.score) {
                        a.score = limit;
                    } else {
                        b.score = limit;
                    }
                }
            }
        }
        if (list.count > 1) {
            sortList(list, 0, s16(list.count - 1));
        }
    }
    for (u16 i = 0; i < list.count; ++i) {
        if (list.entries[i].score >= limit && i >= 2) {
            list.count = i;
            return;
        }
    }
}

// 0x23e258: the box of strokes first .. first + count - 1.
void Recognizer::Engine::strokeBox(u32 first, u32 count, s32& x0, s32& y0, s32& x1, s32& y1) {
    const std::size_t f = first % kMaxStrokes;
    x0 = w.minX[f];
    y0 = w.minY[f];
    x1 = w.maxX[f];
    y1 = w.maxY[f];
    for (u32 k = first + 1; k < first + count; ++k) {
        const std::size_t i = k % kMaxStrokes;
        x0 = std::min<s32>(x0, w.minX[i]);
        y0 = std::min<s32>(y0, w.minY[i]);
        x1 = std::max<s32>(x1, w.maxX[i]);
        y1 = std::max<s32>(y1, w.maxY[i]);
    }
}

// 0x3d10bc: the direction between two points (as 0x23e5f4) with 16 for no
// step.
static int stepDirection(int x0, int y0, int x1, int y1) {
    if (s16(x1 - x0) == 0 && s16(y1 - y0) == 0) {
        return 0x10;
    }
    s16 unused = 0;
    return direction(x0, y0, x1, y1, unused);
}

// 0x23e770: a prototype's rule program. A prefix boolean language over
// tests on the aligned segments (A and B operands: a row's first or last
// input segment and its point, or its longest segment), with NOT (0xc0),
// AND (0xc1), OR (0xc2) and skips (0xc3 true, 0xc4 false, 0xc8 / 0xc9
// depending on the pre-pass flag); a failed top-level test adds the
// current penalty (set by bytes below 0x40; 0xc9 with penalty 0x3f takes
// the pass's own instead). 0xce starts a substitution (rerank mode,
// budget < 0). A positive budget stops after that many bytes. Returns the
// penalty sum (0x3f at most while the budget is not negative).
u32 Recognizer::Engine::rules(u32 prog, u32 first, u32 strokes, u32 base, s32 budget) {
    u32 n = (db8(prog + 2) - 4) & 0xffff;
    u32 ip = prog + 4;
    std::array<u8, 0x40> stack{};
    u32 depth = 0;
    u32 penalty = 0x3f, acc = 0;
    u32 subst = 0;
    bool passPenalty = false;
    bool boxDone = false;
    s32 bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    s32 aSeg = -1, bSeg = -1, aPt = -1, bPt = -1;
    u32 aIdx = 0, bIdx = 0, lo = 0, hi = 0;
    auto box = [&] {
        if (!boxDone) {
            strokeBox(first, strokes, bx0, by0, bx1, by1);
            boxDone = true;
        }
    };
    auto segAt = [&](u32 pos) {
        return s32((base + pos) & 0xffff);
    };
    auto pt = [&](s32 p) {
        return point(u16(p));
    };
    auto within = [&](s32 scale, s32 v) {
        return scale * s32(lo) <= v && v <= scale * s32(hi);
    };
    auto withinU = [&](u32 scale, u32 v) {
        return scale * lo <= v && v <= scale * hi;
    };
    auto inRange = [&](u32 v) {
        if (lo <= hi) {
            return lo <= v && v <= hi;
        }
        return !(v > hi && lo > v);
    };
    // The angle code between two vectors (0x3fa0a0 thresholds on cos^2 *
    // 1024; 0..7 within a quarter, mirrored by the dot and cross signs).
    auto angle = [&](s32 ax, s32 ay, s32 vx, s32 vy) -> u32 {
        const s32 dot = s32(s16(ax)) * s32(s16(vx)) + s32(s16(ay)) * s32(s16(vy));
        const bool back = dot < 0;
        const s32 la = ax * ax + ay * ay, lv = vx * vx + vy * vy;
        const s32 prod = la * lv;
        const s32 sq = dot * dot;
        if (prod == 0) {
            return 0;
        }
        const s32 ratio = sq >= 0x100000 ? sq / (prod >> 10) : (sq << 10) / prod;
        s32 k = 0;
        for (; k < 7; ++k) {
            if (u32(db32Code(0x3fa0a0 + u32(k) * 4)) > u32(ratio)) {
                break;
            }
        }
        s32 v = s16(7 - k);
        if (back) {
            v = s16(0xf - v);
        }
        if (ax * vy > ay * vx) {
            v = s16(0x1f - v);
        }
        return u32(v) & 0xffff;
    };
    while (n != 0) {
        if (budget > 0) {
            budget = s16(budget - 1);
            if (budget == 0) {
                return acc;
            }
        }
        const u32 op = db8(ip++);
        n = (n - 1) & 0xffff;
        if (op == 0xce) {
            if (budget >= 0) {
                return acc;
            }
            if (subst != 0 && acc < 0x3f) {
                substitute(u16(~u32(budget)), u16(subst), u16(acc));
            }
            n = (n - 2) & 0xffff;
            subst = (u32(db8(ip)) << 8 | db8(ip + 1)) & 0xffff;
            acc = 0;
            ip += 2;
            continue;
        }
        if ((op & 0xc0) == 0) {
            penalty = op;
            continue;
        }
        if (op == 0xc9 && penalty == 0x3f) {
            passPenalty = true;
            continue;
        }
        if ((op & 0xe0) == 0xc0) {
            switch (op) {
            case 0xc0:
            case 0xc3:
            case 0xc4:
            case 0xc8:
            case 0xc9:
                depth = (depth + 1) & 0xffff;
                stack[(depth - 1) % stack.size()] = u8(op);
                break;
            case 0xc1:
            case 0xc2:
                depth = (depth + 2) & 0xffff;
                stack[(depth - 1) % stack.size()] = u8(op);
                stack[(depth - 2) % stack.size()] = 0xff;
                break;
            default:
                break;
            }
            continue;
        }
        // Operands.
        if (op & 0x20) {
            aIdx = db8(ip++);
            n = (n - 1) & 0xffff;
            aSeg = segAt(align.longest[aIdx % align.longest.size()]);
        } else if ((op & 0xe0) == 0x80) {
            aIdx = db8(ip++);
            n = (n - 1) & 0xffff;
            if (aIdx & 0x80) {
                aIdx &= 0x7f;
                aSeg = segAt(align.last[aIdx % align.last.size()]);
                aPt = seg(u16(aSeg)).last;
            } else {
                aSeg = segAt(align.start[aIdx % align.start.size()]);
                aPt = seg(u16(aSeg)).first;
            }
        }
        if ((op & 0x40) == 0) {
            bIdx = db8(ip++);
            n = (n - 1) & 0xffff;
            if (bIdx & 0x80) {
                bIdx &= 0x7f;
                bSeg = segAt(align.last[bIdx % align.last.size()]);
                bPt = seg(u16(bSeg)).last;
            } else {
                bSeg = segAt(align.start[bIdx % align.start.size()]);
                bPt = seg(u16(bSeg)).first;
            }
        } else if ((op & 0xe0) == 0xe0) {
            bIdx = db8(ip++);
            n = (n - 1) & 0xffff;
            bSeg = segAt(align.longest[bIdx % align.longest.size()]);
        }
        const u32 t = op & 0x1f;
        if (t >= 8) {
            lo = db8(ip++);
            hi = db8(ip++);
            n = (n - 2) & 0xffff;
        }
        bool result = false;
        bool evaluate = true;
        if (depth != 0) {
            const u8 top = stack[(depth - 1) % stack.size()];
            if (top == 0xc3) {
                depth = (depth - 1) & 0xffff;
                result = true;
                evaluate = false;
            } else if (top == 0xc4) {
                depth = (depth - 1) & 0xffff;
                result = false;
                evaluate = false;
            } else if ((top == 0xc9 && prePass == 0) || (top == 0xc8 && prePass == 1)) {
                depth = (depth - 1) & 0xffff;
                result = true;
                evaluate = false;
            }
        }
        if (evaluate) {
            const bool hasA = aPt >= 0, hasB = bPt >= 0;
            const bool hasAS = aSeg >= 0, hasBS = bSeg >= 0;
            auto walk = [&](auto&& f) { // segments from A to B, following next
                s32 p = aSeg;
                for (;;) {
                    if (!f(p)) {
                        return false;
                    }
                    if (p == bSeg) {
                        return true;
                    }
                    p = seg(u16(p)).next;
                }
            };
            if (t <= 7) {
                switch (op) {
                case 0x80:
                    result = hasA && hasB && pt(bPt).x > pt(aPt).x;
                    break;
                case 0x81:
                    result = hasA && hasB && pt(aPt).x > pt(bPt).x;
                    break;
                case 0x82:
                    result = hasA && hasB && pt(bPt).y > pt(aPt).y;
                    break;
                case 0x83:
                    result = hasA && hasB && pt(aPt).y > pt(bPt).y;
                    break;
                case 0x84: // B follows A within its stroke
                    if (hasAS && hasBS) {
                        result = false;
                        for (s32 p = aSeg; seg(u16(p)).inStroke; p = seg(u16(p)).next) {
                            if (p == bSeg) {
                                result = true;
                                break;
                            }
                        }
                    }
                    break;
                case 0x85: // B does not follow A within its stroke
                    if (hasAS && hasBS) {
                        result = true;
                        for (s32 p = aSeg; seg(u16(p)).inStroke; p = seg(u16(p)).next) {
                            if (p == bSeg) {
                                result = false;
                                break;
                            }
                        }
                    }
                    break;
                default: // 0x40 and the rest: false
                    break;
                }
            } else if (t <= 0xd) {
                switch (op) {
                case 0x48:
                    box();
                    result = within(s16(screen.w), s32(u32(s16(bx0) << 4) & 0xffff));
                    break;
                case 0x49: {
                    box();
                    const s32 v = s16(by0) >= 3 ? s32(u32((s16(by0) << 4) - 0x30) & 0xffff)
                                                : s32(u32(s16(by0) << 4) & 0xffff);
                    result = within(s16(screen.h), v);
                    break;
                }
                case 0x4a: {
                    box();
                    const u32 v = u32((s16(bx1) - s16(bx0) + 1) * (s16(by1) - s16(by0) + 1)) << 4;
                    result = withinU(screen.area, v);
                    break;
                }
                case 0x4b:
                    box();
                    result = within(s16(screen.w),
                                    s32(u32(((u16(bx1) - u16(bx0)) << 4) + 0x10) & 0xffff));
                    break;
                case 0x4c:
                    box();
                    result = within(s16(screen.h),
                                    s32(u32(((u16(by1) - u16(by0)) << 4) + 0x10) & 0xffff));
                    break;
                case 0x88:
                    if (hasA && hasB && hasAS && hasBS) {
                        const u32 v =
                            u32(distance(pt(aPt).x, pt(aPt).y, pt(bPt).x, pt(bPt).y) << 4) & 0xffff;
                        u32 sum = 1;
                        for (s32 p = aSeg;; p = seg(u16(p)).next) {
                            sum = (seg(u16(p)).length + sum) & 0xffff;
                            if (p == bSeg) {
                                break;
                            }
                        }
                        result = withinU(sum, v);
                    }
                    break;
                case 0x89:
                    if (hasA && hasB) {
                        box();
                        s32 d = s32(pt(aPt).y) - s32(pt(bPt).y);
                        if (d <= 0) {
                            d = s32(pt(bPt).y) - s32(pt(aPt).y);
                        }
                        result = within(s16(by1) - s16(by0) + 1, s32(u32(d << 4) & 0xffff));
                    }
                    break;
                case 0x8a:
                    if (hasA && hasB) {
                        box();
                        s32 d = s32(pt(aPt).x) - s32(pt(bPt).x);
                        if (d <= 0) {
                            d = s32(pt(bPt).x) - s32(pt(aPt).x);
                        }
                        result = within(s16(bx1) - s16(bx0) + 1, s32(u32(d << 4) & 0xffff));
                    }
                    break;
                default:
                    break;
                }
            } else if (t <= 0x13) {
                switch (op) {
                case 0x4e: {
                    box();
                    const s32 hgt = s32(u32(u16(by1) - u16(by0)) + 1);
                    const s32 v = s32(((hgt << 16) >> 12) & 0xfff0);
                    result = within(s16(bx1) - s16(bx0) + 1, v);
                    break;
                }
                case 0x8e:
                case 0x8f:
                    if (hasA && hasB) {
                        box();
                        const s32 v =
                            s32(u32(distance(pt(aPt).x, pt(aPt).y, pt(bPt).x, pt(bPt).y) << 4) &
                                0xffff);
                        result = op == 0x8e ? within(s16(bx1) - s16(bx0) + 1, v)
                                            : within(s16(by1) - s16(by0) + 1, v);
                    }
                    break;
                case 0x6e:
                case 0x6f:
                    if (hasAS) {
                        box();
                        const s32 v = s32(u32((seg(u16(aSeg)).length << 4) + 0x10) & 0xffff);
                        result = op == 0x6e ? within(s16(bx1) - s16(bx0) + 1, v)
                                            : within(s16(by1) - s16(by0) + 1, v);
                    }
                    break;
                case 0xee: { // span length of row B against row A
                    const s32 b0 = segAt(align.start[bIdx % align.start.size()]);
                    const s32 b1 = segAt(align.last[bIdx % align.last.size()]);
                    u32 sumB = 1;
                    for (s32 p = b0;; p = seg(u16(p)).next) {
                        sumB = (seg(u16(p)).length + sumB) & 0xffff;
                        if (p == b1) {
                            break;
                        }
                    }
                    aSeg = segAt(align.start[aIdx % align.start.size()]);
                    bSeg = segAt(align.last[aIdx % align.last.size()]);
                    u32 sumA = 1;
                    for (s32 p = aSeg;; p = seg(u16(p)).next) {
                        sumA = (seg(u16(p)).length + sumA) & 0xffff;
                        if (p == bSeg) {
                            break;
                        }
                    }
                    result = withinU(sumA, u32(sumB << 4) & 0xffff);
                    break;
                }
                default:
                    break;
                }
            } else if (t <= 0x16) {
                switch (op) {
                case 0x94:
                    if (hasA && hasB) {
                        const u32 d =
                            u32(stepDirection(pt(aPt).x, pt(aPt).y, pt(bPt).x, pt(bPt).y)) & 0xffff;
                        result = d == 0x10 || inRange(d);
                    }
                    break;
                case 0x95:
                    if (hasAS && hasBS) {
                        result = walk([&](s32 p) { return inRange(u32(u8(seg(u16(p)).dir))); });
                    }
                    break;
                case 0x74:
                    if (hasAS) {
                        result = inRange(u32(u8(seg(u16(aSeg)).dir)));
                    }
                    break;
                default:
                    break;
                }
            } else if (t <= 0x19) {
                if (hasAS && hasBS) {
                    u32 count = 0;
                    if (op == 0x97) { // pen-up links within A..B
                        walk([&](s32 p) {
                            if (!seg(u16(p)).inStroke) {
                                count = (count + 1) & 0xffff;
                            }
                            return true;
                        });
                        result = lo <= count && count <= hi;
                    } else if (op == 0x98) { // segments within A..B
                        walk([&](s32) {
                            count = (count + 1) & 0xffff;
                            return true;
                        });
                        result = lo <= count && count <= hi;
                    }
                }
            } else if (t <= 0x1b) {
                // Turns along A..B: the largest left and right turn in range.
                if (hasAS && hasBS) {
                    s32 most = 0, least = 0;
                    for (s32 p = aSeg; p != bSeg;) {
                        const u16 nx = seg(u16(p)).next;
                        const s32 d = signedTurn(seg(u16(p)).dir, seg(nx).dir);
                        if (d >= 0) {
                            most = std::max(most, d);
                        } else {
                            least = std::min(least, d);
                        }
                        p = nx;
                    }
                    result = s8(lo) <= s8(least) && s8(most) <= s8(hi);
                }
            } else if (t <= 0x1d) {
                // The sum of turns along A..B in range (a reversal passes).
                if (hasAS && hasBS) {
                    s32 sum = 0;
                    bool reversal = false;
                    for (s32 p = aSeg; p != bSeg;) {
                        const u16 nx = seg(u16(p)).next;
                        const s32 d = signedTurn(seg(u16(p)).dir, seg(nx).dir);
                        if (d == 8) {
                            reversal = true;
                            break;
                        }
                        sum = s16(sum + d);
                        p = nx;
                    }
                    result = reversal || (s8(lo) <= s8(sum) && s8(sum) <= s8(hi));
                }
            } else if (t <= 0x1f) {
                if (op == 0xfe && hasAS && hasBS) {
                    const Segment& sa = seg(u16(aSeg));
                    const Segment& sb = seg(u16(bSeg));
                    const s32 ax = s32(pt(sa.last).x) - s32(pt(sa.first).x);
                    const s32 ay = s32(pt(sa.last).y) - s32(pt(sa.first).y);
                    const s32 vx = s32(pt(sb.last).x) - s32(pt(sb.first).x);
                    const s32 vy = s32(pt(sb.last).y) - s32(pt(sb.first).y);
                    result = inRange(angle(ax, ay, vx, vy));
                } else if (op == 0xbe && hasAS) {
                    const Segment& sa = seg(u16(aSeg));
                    const s32 ax = s32(pt(sa.last).x) - s32(pt(sa.first).x);
                    const s32 ay = s32(pt(sa.last).y) - s32(pt(sa.first).y);
                    const s32 vx = s32(hasB ? pt(bPt).x : 0) - s32(pt(sa.first).x);
                    const s32 vy = s32(hasB ? pt(bPt).y : 0) - s32(pt(sa.first).y);
                    result = inRange(angle(ax, ay, vx, vy));
                }
            }
        }
        // 0x23fc48: the operators on the stack take the result.
        for (;;) {
            if (depth == 0) {
                if (!result) {
                    acc = (acc + (passPenalty ? passPenaltyValue : penalty)) & 0xffff;
                    if (budget >= 0 && acc >= 0x3f) {
                        return 0x3f;
                    }
                }
                passPenalty = false;
                break;
            }
            const u8 top = stack[(depth - 1) % stack.size()];
            bool stop = false;
            switch (top) {
            case 0xc0:
                result = !result;
                depth = (depth - 1) & 0xffff;
                break;
            case 0xc1: {
                const u8 prev = stack[(depth - 2) % stack.size()];
                if (prev != 0xff) {
                    result = result && (prev & 1);
                    depth = (depth - 2) & 0xffff;
                } else if (!result) {
                    depth = (depth - 1) & 0xffff;
                    stack[(depth - 1) % stack.size()] = 0xc4;
                    stop = true;
                } else {
                    stack[(depth - 2) % stack.size()] = u8(result);
                    stop = true;
                }
                break;
            }
            case 0xc2: {
                const u8 prev = stack[(depth - 2) % stack.size()];
                if (prev != 0xff) {
                    result = result || (prev & 1);
                    depth = (depth - 2) & 0xffff;
                } else if (result) {
                    depth = (depth - 1) & 0xffff;
                    stack[(depth - 1) % stack.size()] = 0xc3;
                    stop = true;
                } else {
                    stack[(depth - 2) % stack.size()] = u8(result);
                    stop = true;
                }
                break;
            }
            case 0xc3:
                depth = (depth - 1) & 0xffff;
                result = true;
                break;
            case 0xc4:
                depth = (depth - 1) & 0xffff;
                result = false;
                break;
            case 0xc8:
                if (result || prePass == 1) {
                    result = true;
                }
                depth = (depth - 1) & 0xffff;
                break;
            case 0xc9:
                if (result || prePass == 0) {
                    result = true;
                }
                depth = (depth - 1) & 0xffff;
                break;
            default:
                stop = true;
                break;
            }
            if (stop) {
                break;
            }
        }
    }
    if (subst != 0) {
        if (acc >= 0x3f) {
            acc = 0x3f;
        } else {
            substitute(u16(~u32(budget)), u16(subst), u16(acc));
        }
    }
    return acc;
}

// 0x3c9a68: whether two strokes cross near the middle of two of their
// longer segments (length > 7; both intersection parameters within
// [0.4, 0.6]).
bool Recognizer::Engine::crosses(u16 a, u16 b) {
    const u32 af = w.firstSegment[a % kMaxStrokes];
    const u32 ae = af + w.segmentCount[a % kMaxStrokes];
    const u32 bf = w.firstSegment[b % kMaxStrokes];
    const u32 be = bf + w.segmentCount[b % kMaxStrokes];
    for (u32 s = af; s < ae; s = (s + 1) & 0xffff) {
        const Segment& sa = seg(u16(s));
        if (sa.length <= 7) {
            continue;
        }
        const s32 px0 = point(sa.first).x, py0 = point(sa.first).y;
        const s32 px1 = point(sa.last).x, py1 = point(sa.last).y;
        const s32 dyA = py0 - py1;
        for (u32 t = bf; t < be; t = (t + 1) & 0xffff) {
            const Segment& sb = seg(u16(t));
            if (sb.length <= 7) {
                continue;
            }
            const s32 qx0 = point(sb.first).x, qy0 = point(sb.first).y;
            const s32 qx1 = point(sb.last).x, qy1 = point(sb.last).y;
            const s32 dxB = qx0 - qx1, dyB = qy0 - qy1;
            s32 det = (px1 - px0) * dyB + dxB * dyA;
            if (det == 0) {
                continue;
            }
            const s32 ey = py0 - qy0, ex = qx0 - px0;
            const s32 n1 = ex * dyB + dxB * ey;
            const s32 n2 = ex * dyA + (px0 - px1) * ey;
            s32 a1 = n1 * 10, a2 = n2 * 10;
            if (det < 0) {
                det = -det;
                a1 = n1 * -10;
                a2 = n2 * -10;
            }
            if (a1 >= det * 4 && a2 >= det * 4 && a1 <= det * 6 && a2 <= det * 6) {
                return true;
            }
        }
    }
    return false;
}

// 0x3cc1a0: the composite record's two relations between its parts'
// boxes (byte 10 + k: kind in the high nibble, the corners in bits 0-1
// and 2-3; kind 9: the second part inside the right half of the first).
bool Recognizer::Engine::related(u32 rec, u32 a, u32 an, u32 b, u32 bn) {
    s32 x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    auto corner = [&](u32 first, u32 count, u32 which, s32& x, s32& y) {
        s32 bx0, by0, bx1, by1;
        strokeBox(first, count, bx0, by0, bx1, by1);
        x = which & 2 ? bx1 : bx0;
        y = which & 1 ? by1 : by0;
    };
    for (u32 k = 0; k < 2; ++k) {
        const u8 r = db8(rec + 10 + k);
        if ((r >> 4) == 0) {
            return true;
        }
        corner(a, an, r & 3, x1, y1);
        corner(b, bn, (r & 0xf) >> 2, x2, y2);
        switch (r >> 4) {
        case 1:
            if (x2 < x1) {
                return false;
            }
            break;
        case 2:
            if (x1 < x2) {
                return false;
            }
            break;
        case 3:
            if (y2 < y1) {
                return false;
            }
            break;
        case 4:
            if (y2 < y1 || x2 < x1) {
                return false;
            }
            break;
        case 5:
            if (y2 < y1 || x1 < x2) {
                return false;
            }
            break;
        case 6:
            if (y1 < y2) {
                return false;
            }
            break;
        case 7:
            if (y1 < y2 || x2 < x1) {
                return false;
            }
            break;
        case 8:
            if (y1 < y2 || x1 < x2) {
                return false;
            }
            break;
        case 9: {
            s32 ax0, ay0, ax1, ay1, bx0, by0, bx1, by1;
            strokeBox(a, an, ax0, ay0, ax1, ay1);
            strokeBox(b, bn, bx0, by0, bx1, by1);
            if (u32(bx0) < u32(ax0 + ax1) >> 1) {
                return false;
            }
            const s32 aw = ax1 - ax0, ah = ay1 - ay0, bw = bx1 - bx0, bh = by1 - by0;
            if (aw < bw || ah < bh) {
                return false;
            }
            if (bw > aw / 2 && bh > ah / 2) {
                return false;
            }
            break;
        }
        default:
            break;
        }
    }
    return true;
}

// 0x3ccb68: the best of a record's prototypes on strokes first .. first +
// count - 1 (box aspect within the prototype's range, segment range from
// its rule program), each aligned and checked (rules on longer programs).
u32 Recognizer::Engine::partScore(u32 proto, u32 first, u32 count, u32 limit) {
    inFirst = u16(first);
    inStrokes = u16(count);
    inCount = u16(strokeEnd[(first + count) % strokeEnd.size()] -
                  strokeStart[first % strokeStart.size()] + 1);
    inOffset = strokeStart[first % strokeStart.size()];
    s32 x0, y0, x1, y1;
    strokeBox(first, count, x0, y0, x1, y1);
    u32 aspect = u32(s32(u32(y1 - y0) * 16 + 16) / (x1 - x0 + 1)) & 0xffff;
    if (aspect > 0xf0) {
        aspect = 0xf0;
    }
    for (; proto != 0xffffffff; proto = db32(proto + 4)) {
        u32 more = 4, fewer = 1;
        if (db8(proto + 0x15) != 0) {
            more = fewer = 0;
        }
        if (!(db8(proto + 0x18) <= more + inStrokes && inStrokes <= db8(proto + 0x19) + fewer &&
              db8(proto + 0x16) <= aspect && aspect <= db8(proto + 0x17))) {
            continue;
        }
        const u32 rule = db32(proto + 8);
        if (rule != 0) {
            const s32 c = s8(u8(inCount));
            if (s8(db8(rule)) > c || c > s8(db8(rule + 1))) {
                continue;
            }
        }
        u32 d = dtw(align, proto + 0x1c, db8(proto + 0x1b), limit);
        if (d < limit && db8(rule + 2) > 4) {
            d = (d + rules(rule, first, count, strokeStart[first % strokeStart.size()], 0)) &
                0xffff;
        }
        if (d < limit) {
            limit = d;
        }
    }
    return limit;
}

// 0x240878: a composite record (byte 3: the pen-up directions allowed
// between its parts) split at a stroke-group boundary into its two part
// records (+0x10, +0x14; byte 0x7e also in reverse order), each scored
// recursively, the mean taken (+10 when the parts' relation fails); a
// record with prototypes also tries them directly.
u32 Recognizer::Engine::composite(u32 rec, u32 first, u32 count) {
    u32 best = 0x7f;
    const u8 dirs = db8(rec + 3);
    if (count > 1 && dirs != 0) {
        const u32 pa = db32(rec + 0x10), pb = db32(rec + 0x14);
        for (u32 k = 1; k < count; k = (k + 1) & 0xffff) {
            const u32 split = first + k;
            if (w.group[(split - 1) % kMaxStrokes] == w.group[split % kMaxStrokes]) {
                continue;
            }
            const u16 pu = penUp[split % penUp.size()];
            const s32 rest = s32(count) - s32(k);
            auto mean = [&](u32 sa, u32 sb, bool rel) {
                u32 s = (sa + sb) >> 1;
                if (!rel) {
                    s = (s + 10) & 0xffff;
                }
                best = std::min(best, s);
            };
            if ((pu & dirs) != 0 && db8(pa + 4) <= k && s32(db8(pb + 4)) <= rest &&
                k <= db8(pa + 5) && rest <= s32(db8(pb + 5))) {
                const bool rel = related(rec, first, k, split & 0xffff, u32(rest) & 0xffff);
                if (first == 0) {
                    passPenaltyValue = 0x20;
                    const u32 sa = composite(pa, 0, k);
                    passPenaltyValue = 10;
                    if (sa != 0x7f) {
                        mean(sa, composite(pb, k, u32(rest) & 0xffff), rel);
                    }
                } else if (s32(k) < rest) {
                    const u32 sa = composite(pa, first, k);
                    if (sa != 0x7f) {
                        const u32 sb = composite(pb, split & 0xffff, u32(rest) & 0xffff);
                        if (sb != 0x7f) {
                            mean(sa, sb, rel);
                        }
                    }
                } else {
                    const u32 sb = composite(pb, split & 0xffff, u32(rest) & 0xffff);
                    if (sb != 0x7f) {
                        const u32 sa = composite(pa, first, k);
                        if (sa != 0x7f) {
                            mean(sa, sb, rel);
                        }
                    }
                }
            }
            if (dirs == 0x7e && (pu & 0x7e) != 0 && s32(db8(pa + 4)) <= rest && db8(pb + 4) <= k &&
                rest <= s32(db8(pa + 5)) && k <= db8(pb + 5)) {
                const bool rel = related(rec, split & 0xffff, u32(rest) & 0xffff, first, k);
                if (first == 0) {
                    passPenaltyValue = 0x20;
                    const u32 sb = composite(pb, 0, k);
                    passPenaltyValue = 10;
                    if (sb != 0x7f) {
                        mean(sb, composite(pa, k, u32(rest) & 0xffff), rel);
                    }
                } else if (s32(k) < rest) {
                    const u32 sb = composite(pb, first, k);
                    if (sb != 0x7f) {
                        const u32 sa = composite(pa, split & 0xffff, u32(rest) & 0xffff);
                        if (sa != 0x7f) {
                            mean(sb, sa, rel);
                        }
                    }
                } else {
                    const u32 sa = composite(pa, split & 0xffff, u32(rest) & 0xffff);
                    if (sa != 0x7f) {
                        const u32 sb = composite(pb, first, k);
                        if (sb != 0x7f) {
                            mean(sa, sb, rel);
                        }
                    }
                }
            }
        }
    }
    if (db32(rec + 0xc) != 0xffffffff) {
        best = partScore(db32(rec + 0xc), first, count, best);
    }
    return best;
}

// 0x240688: a character's class bits (1 << (kind - 1) of its record, and
// 0x40 for the capitals shared with other scripts).
u32 Recognizer::Engine::classBits(u32 code) const {
    u32 bits = 0;
    const u32 recs = db32(0x24);
    u32 r = recs;
    for (u32 i = 0; i < db16(0); ++i) {
        if (db16(r) == code) {
            bits = (1u << ((db8(r + 2) & 0x7f) - 1)) & 0xffff;
        }
        r += u32(db16(r + 8)) * 4 + 0x18;
    }
    switch (code) {
    case 'A':
    case 'B':
    case 'E':
    case 'H':
    case 'I':
    case 'K':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'T':
    case 'X':
    case 'Z':
        bits |= 0x40;
        break;
    default:
        break;
    }
    return bits;
}

// 0x3ccd20: a character's rank among the candidate codes (Latin letters,
// digits and kana counted in order; others by position + 10).
u32 Recognizer::Engine::candidateRank(u32 code) const {
    u32 r = 0;
    for (u32 u = 0; u < candCount; ++u) {
        const u32 c = candCode[u];
        const bool common = c - 0x30 <= 9 || c - 0x41 <= 0x19 || c - 0x61 <= 0x19 ||
                            c - 0x3041 <= 0x5e || c - 0x30a1 <= 0x5e;
        if (common) {
            if (c == code) {
                return r;
            }
            r = (r + 1) & 0xff;
        } else if (u < 0x100 && c == code) {
            return (u + 10) & 0xff;
        }
    }
    return 0xff;
}

// 0x3d0b24: a rule program's substitution (code) placed into the result
// list next to the entry being rechecked (before it when the penalty is
// under its +8 bound), replacing a later entry of that code.
void Recognizer::Engine::substitute(u16 at, u16 code, u16 penalty) {
    auto& e = results.entries;
    const u16 score = e[at % e.size()].score;
    u16 count = results.count;
    u32 u = 0;
    for (; u < count; u = (u + 1) & 0xffff) {
        const RankEntry& x = e[u];
        if (x.score > score || (x.score <= score && x.strokes > penalty)) {
            u32 v = u;
            while (v < count && e[v].first != code) {
                v = (v + 1) & 0xffff;
            }
            if (v != count) {
                for (; v + 1 < count; ++v) {
                    e[v] = e[v + 1];
                }
                count = u16(count - 1);
                results.count = count;
            }
            for (s32 k = 0x1f; k > s32(u); --k) {
                e[std::size_t(k)] = e[std::size_t(k - 1)];
            }
            for (u16 j = 0; j < rerunCount; ++j) {
                if (u <= rerun[j].at) {
                    rerun[j].at = u16(rerun[j].at + 1);
                }
            }
            break;
        }
        if (x.first == code) {
            return;
        }
    }
    if (u < results.cap) {
        e[u] = {0xffffffff, score, code, penalty, 0xff};
        if (results.count < 0x20) {
            results.count = u16(results.count + 1);
        }
    }
}

// 0x3c8abc: equal scores ordered by candidate rank; then each result that
// came from a prototype (+0xa) runs its prototype's rule program again in
// substitution mode (the results' +8 bounds: 0 before it, 0x20 at it, 0x7f
// after it).
void Recognizer::Engine::rerank(RankList& list) {
    auto& e = results.entries;
    rerunCount = 0;
    u32 i = 0;
    while (i < results.count) {
        u32 j = i;
        do {
            j = (j + 1) & 0xffff;
        } while (j < results.count && e[i].score == e[j].score);
        if (j - i > 1) {
            std::array<u8, 0x40> rank{};
            for (u32 k = i; k < j; ++k) {
                rank[k % rank.size()] = u8(candidateRank(e[k].first));
            }
            for (u32 a = i; s32(a) < s32(j) - 1; ++a) {
                u32 m = a;
                u8 best = rank[a % rank.size()];
                for (u32 b = a + 1; b < j; ++b) {
                    if (rank[b % rank.size()] < best) {
                        m = b;
                        best = rank[b % rank.size()];
                    }
                }
                const RankEntry keep = e[m];
                rank[m % rank.size()] = rank[a % rank.size()];
                e[m] = e[a];
                rank[a % rank.size()] = best;
                e[a] = keep;
            }
        }
        i = j;
    }
    for (u16 k = 0; k < results.count; ++k) {
        if (e[k].extra != 0x3e7 && rerunCount < rerun.size()) {
            rerun[rerunCount++] = {list.entries[e[k].extra % list.entries.size()].proto, k};
        }
    }
    if (rerunCount == 0) {
        return;
    }
    inFirst = 0;
    inStrokes = w.strokes;
    inCount = w.segments;
    inOffset = 0;
    for (u16 v = 0; v < rerunCount; ++v) {
        const u16 at = rerun[v].at;
        if (at > 0x1f) {
            return;
        }
        for (u16 k = 0; k < results.count; ++k) {
            e[k].strokes = k == at ? 0x20 : at < k ? 0x7f : 0;
        }
        const u32 proto = rerun[v].proto;
        if (dtw(align, proto + 0x1c, db8(proto + 0x1b), 0x7f) != 0xff) {
            rules(db32(proto + 8), 0, w.strokes, 0, ~s32(at));
        }
    }
}

// 0x3c4b6c: the recognition proper (see the header's outline). Fills
// `out` (12-byte entries: record, score, code) and returns the count.
u16 Recognizer::Engine::recognizeAll(std::array<RankEntry, 0x20>& out, u16 cap) {
    const u16 n = w.strokes;
    if (n > 2) {
        for (u16 i = 0; i < n - 2; ++i) {
            lists[i].count = 1;
        }
    }
    // Stroke groups: a stroke crossing an earlier one joins its group.
    w.group[0] = 0;
    for (u16 s = 1; s < n; ++s) {
        w.group[s] = s;
        for (u16 t = 0; t < s; ++t) {
            if (crosses(t, s)) {
                for (u16 k = u16(t + 1); k <= s; ++k) {
                    w.group[k] = w.group[t];
                    lists[(k - 1) % lists.size()].count = 0;
                }
                break;
            }
        }
    }
    for (u16 s = 1; s < n; ++s) {
        packSegments(u16(s - 1));
    }
    packSegments(u16(n - 1));
    prePass = 1;
    passPenaltyValue = 0x3f;
    const u16 cands = classify();
    matchStrokes(n, 1);
    prePass = 0;
    passPenaltyValue = 10;
    // The pen-up links' directions (bit per octant; 0xff for short ones),
    // numbered from 1.
    u16 k = 0;
    for (u16 i = 0; i < w.segments; ++i) {
        const Segment& s = seg(i);
        if (s.inStroke) {
            continue;
        }
        u16 v;
        if (distance(point(s.first).x, point(s.first).y, point(s.last).x, point(s.last).y) < 6) {
            v = 0xff;
        } else {
            u32 o = (u32(s32(s.dir) << 15) + 0x8000) >> 16;
            if (o == 8) {
                o = 0;
            }
            v = u16(1u << o);
        }
        k = u16(k + 1);
        penUp[k % penUp.size()] = v;
    }
    results.cap = cap;
    results.count = 0;
    results.limit = 0x7f;
    results.entries.fill(RankEntry{});
    u16 prevCode = 0xffff;
    const u16 c0 = classes[0].score;
    const u16 worst = cands ? classes[cands - 1].score : 0;
    for (u16 u = 0; u < cands; ++u) {
        if (cands >= 2) {
            const s32 spread = s32(classes[1].score) - s32(c0);
            const s32 s = worst ? (s32(classes[u].score) - s32(c0)) * 0x8c / s32(worst)
                                : (s32(classes[u].score) - s32(c0)) * 0x8c;
            if (spread * 128 >= s32(worst) * 20) {
                insert(results, candRecord[u], u32(s + 0x28) & 0xffff, 0, 0, 0x3e7, 2);
            } else if (u < 0x10 && n >= 6) {
                insert(results, candRecord[u], u32(s + 0x4b) & 0xffff, 0, 0, 0x3e7, 2);
            }
        }
        const u32 rec = candRecord[u];
        if (db8(rec + 3) != 0 && db8(rec + 4) <= n && n <= db8(rec + 5) && db16(rec) != prevCode) {
            prevCode = db16(rec);
            u32 score = composite(rec, 0, n);
            if (worst != 0) {
                score =
                    (score + u32((s32(classes[u].score) - s32(c0)) * 0x8c / s32(worst))) & 0xffff;
            }
            if (results.limit > score) {
                insert(results, rec, (score + 0x26) & 0xffff, 0, 0, 0x3e7, 2);
            }
        }
    }
    RankList& list = lists[(n - 1) % lists.size()];
    for (u16 e = 0; e < list.count; ++e) {
        const u16 score = list.entries[e].score;
        if (score < results.limit) {
            insert(results, db32(list.entries[e].proto + 0xc), (score + 0x26) & 0xffff, 0, 0, e, 2);
        }
    }
    sortList(results, 0, s16(results.count - 1));
    rerank(list);
    // The output: the class mask filters (with case folding for the
    // letter classes), each character once.
    auto& r = results.entries;
    u16 count = 0;
    auto fold = [&](u16 code, bool down) -> int {
        if (down && (code == 0xdf || code == 0xff)) {
            return -1;
        }
        if (u32(code - 0x100) > 0x7f) {
            return down ? code - 0x20 : code + 0x20;
        }
        if (code == 0x138 || code == 0x149 || code == 0x178 || code == 0x17f) {
            return code;
        }
        return down ? code - 1 : code + 1;
    };
    for (u16 i = 0; i < std::min(results.count, cap); ++i) {
        RankEntry& e = r[i];
        const u32 bits = classBits(e.first);
        const u32 mask = classMask16;
        if ((bits & mask) == 0) {
            int f = -2;
            if (mask & 2) {
                continue;
            }
            if ((mask & 4) && bits == 8) {
                f = fold(e.first, true);
            } else if ((mask & 8) && bits == 4) {
                f = fold(e.first, false);
            }
            if (f < 0) {
                continue;
            }
            e.first = u16(f);
        }
        bool dup = false;
        for (u16 j = 0; j < count; ++j) {
            if (out[j].first == e.first) {
                dup = true;
            }
        }
        if (dup) {
            continue;
        }
        out[count++] = e;
    }
    // Characters without prototypes go before the first output entry whose
    // character has a worse class score.
    u16 at = 0;
    for (u16 u = 0; u < cands; ++u) {
        const u32 rec = candRecord[u];
        if (rec == 0) {
            break;
        }
        if (db8(rec + 3) != 0 || db32(rec + 0xc) != 0xffffffff) {
            continue;
        }
        const u16 code = db16(rec);
        bool dup = false;
        for (u16 j = 0; j < count; ++j) {
            dup = dup || out[j].first == code;
        }
        if (dup) {
            continue;
        }
        auto bound = [&] {
            return count < cap ? count : u16(cap - 1);
        };
        while (at < bound()) {
            const u16 c = out[at].first;
            u32 t = 0;
            bool found = false;
            while (t < cands && candRecord[t] != 0) {
                if (db16(candRecord[t]) == c) {
                    found = true;
                    break;
                }
                ++t;
            }
            if (found && classes[t].score > classes[u].score) {
                const s32 last = count < cap - 1 ? count : cap - 2;
                for (s32 q = last + 1; q > s32(at); --q) {
                    out[std::size_t(q)] = out[std::size_t(q - 1)];
                }
                out[at].first = code;
                out[at].score = 0;
                if (count < cap - 1) {
                    count = u16(count + 1);
                    results.count = u16(results.count + 1);
                }
                break;
            }
            at = u16(at + 1);
        }
    }
    results.count = count;
    if (count == 0) {
        // Nothing passed: the first candidate character that does.
        for (u16 u = 0; u < cands; ++u) {
            u16 code = candCode[u];
            if (code == 0) {
                continue;
            }
            const u32 bits = classBits(code);
            if ((bits & classMask16) == 0) {
                int f = -1;
                if ((classMask16 & 4) && bits == 8) {
                    f = fold(code, true);
                } else if ((classMask16 & 8) && bits == 4) {
                    f = fold(code, false);
                }
                if (f < 0) {
                    continue;
                }
                candCode[u] = code = u16(f);
            }
            out[count].first = code;
            out[count].proto = 0;
            out[count].score =
                u16(s32((-3 * s32(c0) + 2 * s32(worst) + s32(classes[u].score)) << 6) /
                    (s32(worst) + 1));
            out[count].strokes = 0;
            count = u16(count + 1);
            break;
        }
    }
    count = std::min(count, cap);
    // Scores of 0 (inserted characters) between their neighbours.
    u16 prev = 0;
    for (u16 i = 0; i < count; ++i) {
        if (out[i].score != 0) {
            prev = out[i].score;
            continue;
        }
        u32 next = 0xff;
        for (u16 j = u16(i + 1); j < count; ++j) {
            if (out[j].score != 0) {
                next = out[j].score;
                break;
            }
        }
        if (prev == 0) {
            prev = u16(next >> 1);
        }
        out[i].score = u16((prev + next) >> 1);
    }
    return count;
}

Recognizer::Recognizer(std::vector<std::uint8_t> database, std::span<const std::uint8_t> code,
                       int width, int height, unsigned classMask)
    : engine_(std::make_unique<Engine>()) {
    engine_->db = std::move(database);
    engine_->code = code;
    // The tables sit at another place in the sixth adventure's code: find
    // the turn table (0x3fa1a0: |a - b| between 16 directions, 0..8) by its
    // contents.
    {
        std::array<u8, 32> turns{};
        for (int i = 0; i < 32; ++i) {
            const int d = std::abs(i - 16) % 16;
            turns[std::size_t(i)] = u8(std::min(d, 16 - d));
        }
        const u32 home = 0x3fa1a0 - kCodeBase;
        if (home + turns.size() > code.size() ||
            !std::equal(turns.begin(), turns.end(), code.begin() + home)) {
            const auto at = std::search(code.begin(), code.end(), turns.begin(), turns.end());
            if (at != code.end()) {
                engine_->codeShift = s32(at - code.begin()) - s32(home);
            }
        }
    }
    engine_->width = std::clamp(width, 0x32, 0x7f);
    engine_->height = std::clamp(height, 0x32, 0x7f);
    engine_->classMask = classMask;
    Engine& e = *engine_;
    e.screen.w = e.width;
    e.screen.h = e.height;
    e.screen.area = u32(e.width * e.height);
    // 0x120df0 / 0x121018: the class masks.
    e.classMask16 = u16(classMask | 0x8000);
    u32 m = 0x8000;
    for (u32 bit : {2u, 4u, 8u, 1u, 0x10u, 0x20u, 0x80u, 0x100u, 0x200u, 0x400u, 0x800u}) {
        if (classMask & bit) {
            m |= bit;
        }
    }
    if (classMask & 0x40) {
        m |= 0x44;
    }
    if (u32(classMask * 0x80000) == 0xfff80000u) {
        m |= 0xfff80000u >> 0x13;
    }
    e.stateMask = u16((m & 0x5fff) | 0x8000);
    // 0x12115c: the class table.
    const u32 table = e.db32(0x30);
    e.classCount = e.db16(table);
    e.classLimit = e.db16(table + 2) ? e.db16(table + 2) : 0x20;
    e.classStrokes = e.db32(table + 4);
    e.classHist = e.db32(table + 8);
    const u32 lists = e.db32(table + 0xc);
    u32 k = 0;
    for (u16 c = 0; c < e.classCount; ++c) {
        e.classList.push_back(lists + 4 * k);
        k += e.db32(lists + 4 * k) + 1;
    }
}

Recognizer::~Recognizer() = default;

std::vector<Candidate> Recognizer::recognize(std::span<const std::uint8_t> strokes) {
    Engine& e = *engine_;
    e.w = Engine::Work{};
    e.rawUsed = 0;
    if (e.input(strokes) == -1 || e.w.strokes == 0) {
        return {};
    }
    std::array<Engine::RankEntry, 0x20> out{};
    const u16 count = e.recognizeAll(out, 0x20);
    std::vector<Candidate> result;
    for (u16 i = 0; i < count; ++i) {
        result.push_back({out[i].first, out[i].score});
    }
    return result;
}

std::vector<std::uint8_t> Recognizer::workImage() const {
    const Engine::Work& w = engine_->w;
    std::vector<std::uint8_t> out(0x1614, 0);
    auto put16 = [&](std::size_t o, u16 v) {
        out[o] = u8(v);
        out[o + 1] = u8(v >> 8);
    };
    put16(0, w.strokes);
    put16(2, w.segments);
    put16(4, w.nextSegment);
    put16(6, w.points);
    put16(8, w.length);
    for (std::size_t i = 0; i < kMaxPoints; ++i) {
        out[10 + 2 * i] = w.point[i].x;
        out[11 + 2 * i] = w.point[i].y;
    }
    for (std::size_t i = 0; i < kMaxSegments; ++i) {
        const auto& s = w.seg[i];
        const std::size_t o = 0x28a + i * 0xe;
        put16(o, s.first);
        put16(o + 2, s.last);
        put16(o + 4, s.prev);
        put16(o + 6, s.next);
        put16(o + 8, s.length);
        out[o + 10] = u8(s.dir);
        out[o + 11] = s.level;
        out[o + 12] = s.inStroke;
        out[o + 13] = s.pad;
    }
    for (std::size_t i = 0; i < kMaxStrokes; ++i) {
        put16(0x140a + 2 * i, w.firstPoint[i]);
        put16(0x144a + 2 * i, w.firstSegment[i]);
        put16(0x148a + 2 * i, w.segmentCount[i]);
        put16(0x14ca + 2 * i, w.strokeLength[i]);
        put16(0x150a + 2 * i, w.longest[i]);
        out[0x154a + i] = w.minX[i];
        out[0x156a + i] = w.minY[i];
        out[0x158a + i] = w.maxX[i];
        out[0x15aa + i] = w.maxY[i];
    }
    out[0x15ca] = w.boxMinX;
    out[0x15cb] = w.boxMinY;
    out[0x15cc] = w.boxMaxX;
    out[0x15cd] = w.boxMaxY;
    put16(0x15ce, w.width);
    put16(0x15d0, w.height);
    put16(0x15d2, w.size);
    return out;
}

std::vector<std::uint8_t> Recognizer::stateImage(std::uint32_t address,
                                                 std::uint32_t dbBase) const {
    const Engine& e = *engine_;
    std::vector<std::uint8_t> out;
    auto put16 = [&](u16 v) {
        out.push_back(u8(v));
        out.push_back(u8(v >> 8));
    };
    auto put32 = [&](u32 v) {
        put16(u16(v));
        put16(u16(v >> 16));
    };
    switch (address) {
    case 0x478b08:
        for (auto v : e.hist) {
            put16(v);
        }
        break;
    case 0x457710:
        for (const auto& c : e.classes) {
            put16(c.cls);
            put16(c.score);
        }
        break;
    case 0x476a20:
        for (auto r : e.candRecord) {
            put32(dbBase + r);
        }
        break;
    case 0x476e20:
        out.assign(e.candRank.begin(), e.candRank.end());
        break;
    case 0x476f20:
        for (auto v : e.candCode) {
            put16(v);
        }
        break;
    case 0x477120:
        out.assign(e.packed.begin(), e.packed.end());
        out.resize(0x44, 0);
        put16(e.packedCount);
        put16(0);
        put16(e.packedLast);
        put16(0);
        put16(e.packedStrokes);
        for (std::size_t i = 0; i < 0x1f; ++i) {
            put16(e.strokeStart[i]);
        }
        for (std::size_t i = 0; i < 0x21; ++i) {
            put16(e.strokeEnd[i]);
        }
        break;
    case 0x474fd0: // the candidate list for the last stroke count
    {
        const auto& l = e.lists[(e.w.strokes - 1) % kMaxStrokes];
        for (const auto& x : l.entries) {
            put32(x.proto ? dbBase + x.proto : 0);
            put16(x.score);
            put16(x.first);
            put16(x.strokes);
            put16(x.extra);
        }
        break;
    }
    case 0x423608:
        out.assign(e.binsX.begin(), e.binsX.end());
        out.insert(out.end(), e.binsY.begin(), e.binsY.end());
        break;
    default:
        break;
    }
    return out;
}

} // namespace picarat::adventure56::hwr
