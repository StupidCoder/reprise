// Recovered ARM9 overlay 0 handwriting recognizer. See handwriting.hpp.
//
// Function comments cite the guest entry point. Integer widths, truncating
// divisions, shift rounding and the binary32/binary64 operation order follow
// the original code; the soft-float helpers in the resident ARM9 binary are
// IEEE operations (0203af44 i2f, 0203afd4 fmul, 0203a290 fadd, 0203b2a4 fsub,
// 0203aa94 fdiv, 0203aed0 f2i, 020399c4 dmul, 020393fc dadd, 02039edc dsub,
// 0203bb4c ddiv, comparisons 0203a4b4..0203a95c). Their flag results become
// ordinary C++ comparisons; NaN orders false exactly like the helpers.
#include "puzzles/handwriting.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace picarat::handwriting {
namespace {
using u8 = std::uint8_t;
using s8 = std::int8_t;
using u16 = std::uint16_t;
using s16 = std::int16_t;
using u32 = std::uint32_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

// ---- Resident runtime helpers -------------------------------------------------
// 0203b744: signed divide; division by zero returns the dividend, remainder 0.
s32 sdiv(s32 a, s32 b) {
    if (b == 0) {
        return a;
    }
    if (b == -1) {
        return s32(0u - u32(a));
    }
    return a / b;
}

s32 smod(s32 a, s32 b) {
    if (b == 0) {
        return 0;
    }
    if (b == -1) {
        return 0;
    }
    return a % b;
}

// 0203b950: unsigned divide; division by zero returns the dividend.
u32 udiv(u32 a, u32 b) {
    return b ? a / b : a;
}

// Two's-complement arithmetic as executed by the ARM9 (no C++ overflow UB).
s32 add(s32 a, s32 b) {
    return s32(u32(a) + u32(b));
}

s32 sub(s32 a, s32 b) {
    return s32(u32(a) - u32(b));
}

s32 mul(s32 a, s32 b) {
    return s32(u32(a) * u32(b));
}

s32 neg(s32 a) {
    return s32(0u - u32(a));
}

s32 shl(s32 a, unsigned n) {
    return n >= 32 ? 0 : s32(u32(a) << n);
}

s32 asr(s32 a, unsigned n) {
    return n >= 32 ? (a < 0 ? -1 : 0) : (a >> n);
}

s16 h(s32 v) {
    return s16(u16(u32(v)));
} // strh truncation.

s8 b8(s32 v) {
    return s8(u8(u32(v)));
} // strb truncation.

s32 h16(s32 a, s32 b) {
    return s16(u16(u32(a) - u32(b)));
} // 16-bit difference.

// Truncating signed division by 2^n written as (x + ((x>>(n-1))>>>(32-n))) >> n.
s32 tdivp2(s32 x, unsigned n) {
    return asr(add(x, s32(u32(asr(x, n - 1)) >> (32 - n))), n);
}

float i2f(s32 v) {
    return float(v);
}

float u2f(u32 v) {
    return float(v);
}

s32 f2i(float f) { // 0203aed0: truncate, saturate by sign (NaN also by sign bit).
    if (std::isnan(f)) {
        return std::signbit(f) ? std::numeric_limits<s32>::min() : std::numeric_limits<s32>::max();
    }
    if (f >= 2147483648.0f) {
        return std::numeric_limits<s32>::max();
    }
    if (f <= -2147483648.0f) {
        return std::numeric_limits<s32>::min();
    }
    return s32(f);
}

u32 f2u(float f) { // 0203af04
    if (std::signbit(f)) {
        return std::isnan(f) ? 0xffffffffu : 0;
    }
    if (std::isnan(f) || f >= 4294967296.0f) {
        return 0xffffffffu;
    }
    return u32(f);
}

// ---- Database access ----------------------------------------------------------
// The database is little-endian with self-relative s32 offsets.
s32 r32(const u8* p, s32 o = 0) {
    s32 v;
    std::memcpy(&v, p + o, 4);
    return v;
}

u16 r16(const u8* p, s32 o = 0) {
    u16 v;
    std::memcpy(&v, p + o, 2);
    return v;
}

s16 rs16(const u8* p, s32 o = 0) {
    return s16(r16(p, o));
}

const u8* rel(const u8* p, s32 o) {
    return p + r32(p, o);
}

const u8* subA(const u8* db) {
    return rel(db, 4);
} // 02065658

const u8* subB(const u8* db) {
    return rel(db, 8);
} // 0206564c

const u8* subTable(const u8* a) {
    return rel(a, 0x1c);
} // 02065310

// 0206531c: number of A prototypes with `strokes` strokes.
u16 protoCount(const u8* a, s32 strokes) {
    s32 i = strokes - 1;
    if (i < 0 || i >= s32(a[2])) {
        return 0;
    }
    return r16(rel(a, 4), i * 8);
}

// 02065468: number of B part prototypes with `strokes` strokes.
u16 partCount(const u8* b, s32 strokes) {
    s32 i = strokes - 1;
    if (i < 0 || i >= s32(r16(b))) {
        return 0;
    }
    return r16(rel(b, 4), i * 8);
}

// 02065498: B mask of permitted part prototypes for a symbol class.
u16 partMask(const u8* b, s32 cls, s32 strokes) {
    return r16(rel(b, 0x20), (strokes + cls * 2 - 1) * 2);
}

// 02065418, 02065428, 0206543c, 0206545c
const u8* bEntry8(const u8* b, s32 i) {
    return rel(b, 0x10) + i * 8;
}

u16 bPeriod(const u8* b, s32 i) {
    return r16(rel(b, 0x14), i * 2);
}

const u8* bSpecial(const u8* b) {
    return rel(b, 0x34);
}

const u8* bBase(const u8* b) {
    return rel(b, 0xc);
}

// First 0x20 bytes of the 0x4c-byte candidate record: a prototype reference.
struct Head {
    const u8* a = nullptr;       // +0x00 prototype database.
    const u8* b = nullptr;       // +0x04 part database.
    const u8* table = nullptr;   // +0x08 category table.
    s16 index = 0;               // +0x0c prototype index.
    u8 strokes = 0;              // +0x0e prototype strokes.
    s8 partIndex = 0;            // +0x0f part prototype index.
    u8 partStrokes = 0;          // +0x10 part strokes (0: none).
    s16 partX = 0, partY = 0;    // +0x12,+0x14 part offset.
    const u8* rec = nullptr;     // +0x18 A prototype record (0x20 bytes).
    const u8* partRec = nullptr; // +0x1c B part record (0x20 bytes).
};

struct Candidate {
    Head h;
    s16 score = 0;             // +0x20 shape distance.
    s16 penalty = 0;           // +0x22 context penalties.
    const u8* text = nullptr;  // +0x24 UTF-16 symbol string in A.
    s32 scale = 0;             // +0x28 fitted scale.
    s32 angle = 0;             // +0x2c fitted rotation.
    s32 v30 = 0, v34 = 0;      // +0x30,+0x34 fitted proto centre.
    s32 cx = 0, cy = 0;        // +0x38,+0x3c fitted input centre.
    s8 cut0 = 0, cut1 = 0;     // +0x40,+0x41 trimmed points at stroke ends.
    std::array<s8, 6> order{}; // +0x42 stroke mapping: ±(input stroke+1).
    s32 caseKind = 0;          // +0x48 0 lower, 1 upper, 2 both/unknown.
};

// 02065880: A prototype record = A + A.table4[strokes-1].offset + index*0x20.
void setRec(Head& h) {
    h.rec = h.a + r32(rel(h.a, 4), (h.strokes - 1) * 8 + 4) + h.index * 0x20;
}

// 020658b0: B part record from partStrokes/partIndex.
void setPartRec(Head& h) {
    h.partRec = h.b + r32(rel(h.b, 4), (h.partStrokes - 1) * 8 + 4) + h.partIndex * 0x20;
}

// 02065358: 8-byte symbol record selected by the prototype record.
const u8* symbolRec(const Head& h) {
    return rel(h.a, 8) + rs16(h.rec) * 8;
}

// 02065374: 8-byte character record (+0 text offset, +4 class, +5 period
// index, +6 flags, +7 stroke-end mask).
const u8* charRec(const Head& h) {
    return rel(h.a, 0x10) + r16(symbolRec(h)) * 8;
}

// 0206591c: composed character record selected by the part prototype.
const u8* composedRec(const Head& h) {
    if (!h.partStrokes) {
        return nullptr;
    }
    const u8* c = charRec(h);
    const u8* b = h.b;
    s8 i = s8(rel(b, 0x24)[c[5] * b[0x3b] + h.partRec[0]]);
    return rel(b, 0xc) + i * 4;
}

// 020658e0: UTF-16 symbol text.
const u8* textOf(const Head& h) {
    return h.a + r32(h.partStrokes ? composedRec(h) : charRec(h));
}

s32 symbolClass(const Head& h) {
    return h.partStrokes ? -1 : s32(s8(charRec(h)[4]));
}

// 020655b4: angular period used by the rotation penalty.
u16 periodOf(const Head& h) {
    if (!h.partStrokes) {
        return bPeriod(h.b, symbolRec(h)[5]);
    }
    if (!h.strokes) {
        return bPeriod(h.b, h.partRec[1]);
    }
    return bPeriod(h.b, 0);
}

// 0206560c: rotation penalty window {lo,loWeight,hi,hiWeight}.
const u8* rotationWindow(const Head& h) {
    return bEntry8(h.b, h.partStrokes ? 0 : symbolRec(h)[4]);
}

// 020656c0: category mask record (4 words).
const u8* categoryRec(const Head& h) {
    if (!h.partStrokes) {
        return rel(h.table, 4) + r16(h.rec) * 16;
    }
    s32 d = s32(composedRec(h) - bBase(h.b));
    return rel(h.table, 8) + tdivp2(d, 2) * 16;
}

// 0206571c: does the candidate belong to the requested categories?
void categories(const Head& h, u32 m0, u32 m1, s32& primary, s32& secondary) {
    const u8* c = categoryRec(h);
    primary = (u32(r32(c, 0)) & m0) && (u32(r32(c, 4)) & m1);
    secondary = (u32(r32(c, 8)) & m0) && (u32(r32(c, 12)) & m1);
}

// 02065a2c: 64-byte prototype stroke shape (32 s8 x, 32 s8 y).
const s8* shapeOf(const Head& h, s32 stroke) {
    if (!h.partStrokes || stroke < s32(h.strokes)) {
        const u8* p = h.a + r32(rel(h.a, 0x18), (h.strokes - 1) * 8 + 4) +
                      r16(h.rec, 2) * h.strokes * 0x40 + stroke * 0x40;
        return reinterpret_cast<const s8*>(p);
    }
    const u8* p = h.b + r32(rel(h.b, 8), (h.partStrokes - 1) * 8 + 4) +
                  h.partStrokes * r16(h.partRec, 4) * 0x40 + (stroke - h.strokes) * 0x40;
    return reinterpret_cast<const s8*>(p);
}

// 02065bcc: prototype sum of squared coordinates (moment).
s32 protoMoment(const Head& h) {
    if (!h.partStrokes) {
        return r32(h.rec, 8);
    }
    const u8* p = h.partRec;
    s32 n = h.partStrokes;
    s32 x = h.partX, y = h.partY;
    s32 r = add(r32(h.rec, 8), r32(p, 0xc));
    r = add(r, mul(x, add(shl(mul(s16(x), s16(n)), 5), shl(rs16(p, 8), 1))));
    return add(r, mul(y, add(shl(mul(s16(y), s16(n)), 5), shl(rs16(p, 0xa), 1))));
}

// 02065974: prototype normalization term for the shape score.
s32 protoNorm(const Head& h) {
    if (!h.partStrokes) {
        return r32(h.rec, 0x18);
    }
    if (!h.strokes) {
        return r32(h.partRec, 0x18);
    }
    const u8* r = h.rec;
    const u8* p = h.partRec;
    s32 n = h.partStrokes;
    s32 px = rs16(p, 0x1c), py = rs16(p, 0x1e), x = h.partX, y = h.partY;
    s32 mx = mul(s16(x), s16(n)), my = mul(s16(y), s16(n));
    s32 sx = add(add(rs16(r, 0x1c), px), shl(mx, 3));
    s32 sy = add(add(rs16(r, 0x1e), py), shl(my, 3));
    s32 m = add(r32(r, 0x14), r32(p, 0x14));
    m = add(m, mul(x, add(shl(px, 1), shl(mx, 3))));
    m = add(m, mul(y, add(shl(py, 1), shl(my, 3))));
    return sub(sub(mul(m, shl(add(h.strokes, n), 3)), mul(sx, sx)), mul(sy, sy));
}

// 02065c5c: bit mask of strokes whose position is relevant.
u32 positionMask(const Head& h) {
    if (!h.strokes) {
        return h.partRec[3];
    }
    u32 m = symbolRec(h)[7];
    if (!h.partStrokes) {
        return m;
    }
    u32 pm = h.partRec[3];
    if (h.partStrokes == 1 && bPeriod(h.b, h.partRec[1]) == 0) {
        pm = 1;
    }
    if (h.strokes == 1 && bPeriod(h.b, symbolRec(h)[5]) == 0) {
        m = 1;
    }
    return u8(m | (pm << h.strokes));
}

u8 charFlags(const Head& h) {
    return charRec(h)[6];
} // 02065c34 (&3) / 02065c48 (&0x30)

// 02065398: list of up to `max` alternative stroke orders for the candidate.
s32 orderList(const Head& h, std::array<const s8*, 8>& out, s32 max) {
    const u8* b = h.b;
    const u8* base = rel(b, 0x1c);
    u8 i = h.partStrokes ? h.partRec[2] : symbolRec(h)[6];
    const u8* e = base + i * 8;
    const u8* off = b + r32(e, 4);
    const u8* orders = rel(b, 0x18);
    s32 n = 0;
    for (; n < s32(r16(e)) && n < max; ++n) {
        out[n] = reinterpret_cast<const s8*>(orders + h.a[1] * s8(off[n]));
    }
    return n;
}

// 02065664
s8 signedAdd(s32 v, s8 d) {
    return b8(v < 0 ? v - d : v + d);
}

// 0206567c: which order slot maps to input stroke `stroke`.
s32 slotOf(const std::array<s8, 6>& order, s32 stroke) {
    for (s32 i = 0; i < 6; ++i) {
        s32 v = order[i];
        if (v < 0) {
            v = -v;
        }
        if (stroke == v - 1) {
            return i;
        }
    }
    return -1;
}

// Input stroke index and direction for prototype stroke i (repeated idiom).
s32 mapped(const std::array<s8, 6>& order, s32 i) {
    if (i >= 6) {
        return i;
    }
    s32 v = order[i];
    if (v < 0) {
        v = -v;
    }
    return v - 1;
}

bool reversed(const std::array<s8, 6>& order, s32 i) {
    return i < 6 && order[i] < 1;
}

// ---- Candidate lists (0205e12c..0205e58c) ------------------------------------
// A list owns up to 15 nodes, each permanently bound to one storage record.
// The used list is ordered by descending score: the head is the worst entry.
struct CandidateList {
    struct Node {
        Node* next = nullptr;
        s32 score = 0;
        Candidate* cand = nullptr;
    };

    Candidate* storage = nullptr; // +0x00
    std::array<Node, 15> nodes{}; // +0x04
    Node* free = nullptr;         // +0xb8
    Node* head = nullptr;         // +0xc4
    s32 capacity = 0;             // +0xd0
    s32 maxSame = 0;              // +0xd4 permitted entries with the same text.
    s32 count = 0;                // +0xd8
    Node* cursor = nullptr;       // +0xdc

    // 0205e12c. The node chain uses the uncapped count exactly like the
    // original; all callers pass at most 15.
    void init(Candidate* records, s32 n, s32 same) {
        if (n > 15) {
            throw std::logic_error("candidate list larger than its node array");
        }
        storage = records;
        capacity = std::min(n, 15);
        maxSame = same;
        head = nullptr;
        for (s32 i = 0; i < n; ++i) {
            nodes[i].next = i + 1 < n ? &nodes[i + 1] : nullptr;
            nodes[i].cand = records + i;
        }
        free = &nodes[0];
        count = 0;
    }

    void unlink(Node** link) {
        Node* n = *link;
        *link = n->next;
        n->next = free;
        free = n;
        --count;
    } // 0205e1c8

    void link(Node** at, Node* n) {
        n->next = *at;
        *at = n;
        ++count;
    } // 0205e1f0

    Node* take(const Candidate& c, s32 score) { // 0205e20c
        Node* n = free;
        if (!n) {
            return nullptr;
        }
        free = n->next;
        *n->cand = c;
        n->score = score;
        return n;
    }

    Candidate* first(s32* score) {
        cursor = head;
        if (!cursor) {
            return nullptr;
        }
        if (score) {
            *score = cursor->score;
        }
        return cursor->cand;
    } // 0205e4ac

    Candidate* next(s32* score) { // 0205e4dc
        if (!cursor || !cursor->next) {
            return nullptr;
        }
        cursor = cursor->next;
        if (score) {
            *score = cursor->score;
        }
        return cursor->cand;
    }

    Candidate* last(s32* score) { // 0205e518
        Node* n = head;
        if (!n) {
            return nullptr;
        }
        while (n->next) {
            n = n->next;
        }
        if (score) {
            *score = n->score;
        }
        return n->cand;
    }

    void keep(s32 n) {
        for (s32 d = count - n; d > 0; --d) {
            unlink(&head);
        }
    } // 0205e558
};

s32 textCompare(const u8* a, const u8* b) { // 02064f0c (UTF-16 strcmp)
    for (;; a += 2, b += 2) {
        u32 x = r16(a), y = r16(b);
        if (x == 0 || y == 0) {
            if (x != y) {
                return x == 0 ? -1 : 1;
            }
            return 0;
        }
        if (x > y) {
            return 1;
        }
        if (x < y) {
            return -1;
        }
    }
}

// 0205df08: shared-part record for a single-part candidate, if any.
const u8* sharedPart(const Candidate& c) {
    if (c.h.partStrokes) {
        return nullptr;
    }
    const u8* p = rel(c.h.b, 0x28);
    u16 n = r16(c.h.b, 0x30);
    u16 key = r16(c.h.rec);
    for (s32 i = 0; i < n; ++i, p += 8) {
        if (r16(p) == key) {
            return p;
        }
    }
    return nullptr;
}

bool sharesPart(const u8* s, const Candidate& c) { // 0205df80
    if (!s) {
        return false;
    }
    u16 key = r16(c.h.rec);
    for (s32 i = 0; i < s32(s[2]); ++i) {
        if (key == r16(s, 4 + i * 2)) {
            return true;
        }
    }
    return false;
}

// 0205e25c: enforce the same-text quota before an insertion.
bool quotaRejects(CandidateList& l, const Candidate& c, CandidateList::Node**& at, s32 worseSame,
                  CandidateList::Node** firstSame) {
    s32 left = l.maxSame - worseSame;
    for (CandidateList::Node* n = *at; n && left;) {
        bool same = textCompare(n->cand->text, c.text) == 0;
        n = n->next;
        if (same) {
            --left;
        }
    }
    if (left) {
        return false;
    }
    if (!worseSame) {
        return true;
    }
    // The insertion link may be the next field of the removed node.
    if (reinterpret_cast<void*>(at) == reinterpret_cast<void*>(*firstSame)) {
        at = firstSame;
    }
    l.unlink(firstSame);
    return false;
}

// 0205e2f0: sorted insertion with optional shared-part compatibility.
void insert(CandidateList& l, s32 score, const Candidate& c, bool compatible) {
    s32 same = 0;
    CandidateList::Node** firstSame = nullptr;
    const u8* mine = compatible ? sharedPart(c) : nullptr;
    CandidateList::Node** at = &l.head;
    for (CandidateList::Node* n; (n = *at); at = &n->next) {
        if (n->score > score && !(compatible && sharesPart(mine, *n->cand))) {
        } else if (!compatible || !sharesPart(sharedPart(*n->cand), c)) {
            break;
        }
        if (textCompare(c.text, n->cand->text) == 0) {
            if (!same) {
                firstSame = at;
            }
            ++same;
        }
    }
    if (at != &l.head) {
        if (quotaRejects(l, c, at, same, firstSame)) {
            return;
        }
        if (!l.free) {
            if (reinterpret_cast<void*>(at) == reinterpret_cast<void*>(l.head)) {
                at = &l.head;
            }
            l.unlink(&l.head);
        }
        l.link(at, l.take(c, score));
        return;
    }
    if (!l.free) {
        return;
    }
    if (quotaRejects(l, c, at, 0, nullptr)) {
        return;
    }
    l.link(&l.head, l.take(c, score));
}

// 0205e58c: copy the list into storage[0..count) best-first. Occupied slots
// always form a prefix (removed nodes are reused immediately), so the
// original's uninitialized stack entries for unused slots are never consulted.
s32 compact(CandidateList& l) {
    std::array<s32, 15> rank;
    rank.fill(-1);
    s32 r = l.count - 1;
    for (auto* n = l.head; n; n = n->next) {
        rank[size_t(n->cand - l.storage)] = r--;
    }
    for (s32 i = 0; i < l.count; ++i) {
        if (rank[i] < 0) {
            throw std::logic_error("candidate storage is not a prefix");
        }
    }
    std::array<Candidate, 15> copy;
    for (s32 i = 0; i < l.count; ++i) {
        copy[i] = l.storage[i];
    }
    for (s32 i = 0; i < l.count; ++i) {
        l.storage[rank[i]] = copy[i];
    }
    l.head = nullptr;
    return l.count;
}

// 0205b5d0: how many candidates to keep (drop those worse than twice the best).
s32 keepCount(CandidateList& l) {
    s32 best = 0;
    l.last(&best);
    s32 n = l.count;
    if (best > 0) {
        s32 s = 0;
        for (Candidate* c = l.first(&s); c && n > 8 && s > add(best, best); c = l.next(&s)) {
            --n;
        }
    }
    return n;
}

void clearCandidate(Candidate& c) {
    c = Candidate{};
    c.h.index = -1;
} // 0205de6c

// ---- Prototype iterator (020604d8..02060888) ----------------------------------
struct CandidateArray {
    Candidate* storage = nullptr;
    s32 count = 0;
};

struct ProtoIterator {
    Head h;                          // +0x00
    s32 partRequest = 0;             // +0x20 part strokes wanted (0: whole prototypes).
    s32 limit = 0;                   // +0x24
    s32 fromArray = 0;               // +0x28 iterate earlier candidates instead of A.
    s32 special = 0;                 // +0x2c
    CandidateArray* array = nullptr; // +0x30
    s32 position = -1;               // +0x34

    void init(s32 strokes, s32 parts, const Head& base, CandidateArray* list) { // 020604d8
        h = Head{};
        h.strokes = u8(strokes);
        h.a = base.a;
        h.b = base.b;
        h.table = base.table;
        h.index = -1;
        fromArray = parts > 0 && list->count > -1;
        special = parts > 0 && list->count == -2;
        array = list;
        position = -1;
        partRequest = parts;
        if (strokes > s32(h.a[2]) || parts > s32(r16(h.b))) {
            limit = 0;
            return;
        }
        limit = fromArray ? list->count : protoCount(h.a, strokes);
    }

    Candidate* current() const {
        return fromArray ? array->storage + position : nullptr;
    } // 02060888

    bool settle() { // 02060724
        bool ok = position < limit;
        if (fromArray && ok) {
            h = array->storage[position].h;
        }
        if (ok && !h.rec) {
            setRec(h);
        }
        return ok;
    }

    bool next() { // 020605b4
        if (fromArray) {
            for (;;) {
                if (++position >= limit) {
                    return settle();
                }
                Candidate* c = current();
                if (partRequest > 0 && h.strokes > 1 && r16(textOf(c->h)) == 0x69 &&
                    c->caseKind == 0) {
                    continue; // 'i' without dot.
                }
                s32 cls = symbolClass(c->h);
                if (cls == -1 || !partMask(h.b, cls, partRequest)) {
                    continue;
                }
                return settle();
            }
        }
        if (partRequest == 0) {
            ++position;
            ++h.index;
            h.rec = nullptr;
            return settle();
        }
        for (;;) {
            if (++position >= limit) {
                return settle();
            }
            h.index = s16(position);
            setRec(h);
            s32 cls = s8(h.partStrokes ? -1 : symbolClass(h));
            if (cls == -1 || !partMask(h.b, cls, partRequest)) {
                continue;
            }
            return settle();
        }
    }
};

// 020607b0/02060828: enumerate permitted part prototypes for a candidate.
struct PartIterator {
    u32 mask = 0;
    s32 count = 0;
    Head h;
    s32 pad = 0;

    void init(const Head& base, s32 parts) {
        h = base;
        h.partStrokes = u8(parts);
        mask = partMask(base.b, base.partStrokes ? -1 : symbolClass(base), parts);
        count = partCount(h.b, parts);
        h.partIndex = b8(h.partIndex - 1);
    }

    bool next() {
        bool found = false;
        for (;;) {
            h.partIndex = b8(h.partIndex + 1);
            h.partRec = nullptr;
            if (h.partIndex >= count) {
                break;
            }
            if (((h.partIndex < 32 ? mask >> h.partIndex : 0u)) & 1) {
                found = true;
                break;
            }
        }
        if (found && !h.partRec) {
            setPartRec(h);
        }
        return found;
    }
};

// ---- Fixed-point geometry (02064210..02064bdc) -------------------------------
// 02064210: integer square root (16 iterations of the digit-by-digit method).
s32 isqrt(u32 v) {
    u32 rem = 0;
    s32 root = 0;
    for (u32 i = 0; i < 16; ++i) {
        s32 r2 = root * 2;
        rem = (v >> 30) + rem * 4;
        u32 t = u32(root << 2) | 1;
        if (t <= rem) {
            rem -= t;
            ++r2;
        }
        v <<= 2;
        root = r2;
    }
    return root;
}

s32 bitLength(u32 v) {
    s32 n = 0;
    u32 s = 16;
    for (; v; v >>= 1) {
        if (v >> s) {
            n += s;
            v >>= s;
        }
        s >>= 1;
        ++n;
    }
    return n;
} // 02064258

constexpr std::array<s8, 64> kHypotRefine{
    // ov0 data 02065dc4
    0x40, 0x40, 0x41, 0x41, 0x42, 0x42, 0x43, 0x43, 0x44, 0x44, 0x45, 0x45, 0x46, 0x46, 0x47, 0x47,
    0x48, 0x48, 0x48, 0x49, 0x49, 0x4a, 0x4a, 0x4b, 0x4b, 0x4b, 0x4c, 0x4c, 0x4d, 0x4d, 0x4e, 0x4e,
    0x4e, 0x4f, 0x4f, 0x50, 0x50, 0x50, 0x51, 0x51, 0x52, 0x52, 0x52, 0x53, 0x53, 0x54, 0x54, 0x54,
    0x55, 0x55, 0x55, 0x56, 0x56, 0x57, 0x57, 0x57, 0x58, 0x58, 0x58, 0x59, 0x59, 0x59, 0x5a, 0x5a};

s32 hypotSeed(s32 a, s32 b) { // 0206428c
    if (a < 0) {
        a = neg(a);
    }
    if (b < 0) {
        b = neg(b);
    }
    if (a == 0) {
        return b;
    }
    if (b == 0) {
        return a;
    }
    if (a == b) {
        return asr(mul(a, 0x5b), 6);
    }
    if (a > b) {
        s32 q = sdiv(shl(b, 3), a);
        return sdiv(add(mul(a, a), mul(b, b)), asr(mul(a, kHypotRefine[size_t(mul(q, q))]), 6));
    }
    s32 q = sdiv(shl(a, 3), b);
    return sdiv(add(mul(b, b), mul(a, a)), asr(mul(b, kHypotRefine[size_t(mul(q, q))]), 6));
}

s32 hypot(s32 x, s32 y) { // 02064384: Newton-refined vector length
    s32 a = x < 0 ? neg(x) : x, b = y < 0 ? neg(y) : y;
    if (a == 0) {
        return b;
    }
    if (b == 0) {
        return a;
    }
    s32 hi = bitLength(u32(a > b ? a : b)), k = bitLength(0xb504);
    s32 s = add(sub(hi, k), 1);
    if (s > 0) {
        a = asr(a, s);
        b = asr(b, s);
    }
    s32 aa = mul(a, a), bb = mul(b, b);
    s32 g = hypotSeed(a, b);
    u32 m = u32(add(g, s32(udiv(u32(add(aa, bb)), u32(g))))) >> 1;
    if (s > 0) {
        u32 v = m + udiv(u32(add(aa, bb)), m);
        return s32(v << u32(sub(hi, k)));
    }
    return s32((m + udiv(u32(add(aa, bb)), m)) >> 1);
}

constexpr std::array<s8, 33> kArcTan{0x00, 0x03, 0x06, 0x09, 0x0c, 0x0f, 0x13, 0x16, 0x18,
                                     0x1b, 0x1e, 0x21, 0x24, 0x27, 0x29, 0x2c, 0x2e, 0x31,
                                     0x33, 0x36, 0x38, 0x3a, 0x3c, 0x3e, 0x40, 0x42, 0x44,
                                     0x46, 0x48, 0x4a, 0x4b, 0x4d, 0x4f}; // ov0 data 02065e88

s32 atanIndex(s32 q) { // Shared lookup of 0206447c: q is |ratio*64|.
    if (q < 0) {
        q = neg(q);
    }
    s32 i = tdivp2(q, 1);
    if (q == shl(i, 1)) {
        return kArcTan[size_t(i)];
    }
    return tdivp2(add(add(kArcTan[size_t(i)], kArcTan[size_t(i + 1)]), 1), 1);
}

// 0206447c: direction in centiradians (pi = 314), measured from the x axis.
s32 direction(s32 x, s32 y) {
    if (y == 0 && x == 0) {
        return 0;
    }
    s32 ay = y < 0 ? neg(y) : y, ax = x < 0 ? neg(x) : x, r;
    if (ax >= ay) {
        r = atanIndex(sdiv(shl(y, 6), x));
    } else {
        r = sub(0x9d, atanIndex(sdiv(shl(x, 6), y)));
    }
    if (y < 0 && x > 0) {
        return r;
    }
    if (y < 0 && x <= 0) {
        return sub(0x13a, r);
    }
    if (y >= 0 && x < 0) {
        return add(r, s32(0xfffffec6));
    }
    return neg(r);
}

s32 approxLength(s32 x, s32 y) { // 020645ac
    if (x < 0) {
        x = neg(x);
    }
    if (y < 0) {
        y = neg(y);
    }
    if (x == 0) {
        return y;
    }
    if (y == 0) {
        return x;
    }
    if (x > y) {
        return add(x, sdiv(add(mul(y, y), x), shl(x, 1)));
    }
    return add(y, sdiv(add(mul(x, x), y), shl(y, 1)));
}

s32 wrapAngle(s32 a, s32 period) { // 0206460c
    if (period == 0) {
        return 0;
    }
    s32 half = asr(period, 1), r = smod(a, period);
    if (r > half) {
        return sub(r, period);
    }
    if (r <= neg(half)) {
        return add(r, period);
    }
    return r;
}

// 0206472c: n*sqrt(a/b) by four Newton steps in binary32; -1 when b == 0.
u32 scaledRoot(float a, float b, s32 n) {
    if (double(b) == 0.0) {
        return 0xffffffffu;
    }
    float q = a / b;
    float g = (1.0f + q) / 2.0f;
    for (int i = 0; i < 4; ++i) {
        g = (g + q / g) / 2.0f;
    }
    return f2u(g * i2f(n));
}

constexpr std::array<s16, 26> kRootScore{
    1000, 980, 959, 938, 917, 894, 872, 849, 825, 800, 775, 748, 721,
    693,  663, 632, 600, 566, 529, 490, 447, 400, 346, 283, 200, 100}; // ov0 data 02065d90

// 020647dc: score ~ n*sqrt(1-f), refined from a table by two integer Newton steps.
s16 rootScore(float f, s32 n) {
    s32 i = f2i(25.0f * f + 0.5f);
    if (double(f) < 0.0) {
        return s16(n);
    }
    if (double(f) >= 1.0) {
        return 0;
    }
    if (i < 0 || i > 25) {
        throw std::logic_error("root-score index outside the ROM table");
    }
    s32 r = kRootScore[size_t(i)];
    if (r != 0) {
        s32 t = mul(n, f2i(i2f(n) * (1.0f - f)));
        for (int k = 0; k < 2; ++k) {
            r = tdivp2(add(add(r, sdiv(t, r)), 1), 1);
        }
    }
    return h(r);
}

s32 roundAway(float f) {
    return f > 0.0f ? f2i(f + 0.5f) : f2i(f - 0.5f);
} // 02064448

constexpr std::array<s32, 33> kSine{0,   13,  25,  38,  50,  62,  74,  86,  98,  109, 121, 132,
                                    142, 152, 162, 172, 181, 190, 198, 206, 213, 220, 226, 231,
                                    237, 241, 245, 248, 251, 253, 255, 256, 256}; // ov0 data
                                                                                  // 02065e04

// 0206490c: sine of an angle in centiradians, scaled by 256.
s32 sine(s32 angle) {
    s32 a = wrapAngle(angle, 0x274);
    auto seg = [](s32 v) {
        s64 p = s64(0x342da7f3) * v;
        return s32(u32(v) >> 31) + s32(p >> 38);
    }; // v/157*... exact smull/asr
    auto frac = [](s32 a, s32 i) {
        return sub(a, tdivp2(mul(i, 0x13a), 6));
    };
    auto lerp = [](s32 w, s32 d) {
        s32 p = mul(w, d);
        return add(s32(u32(p) >> 31), s32((s64(0x51eb851f) * p) >> 37));
    };
    if (a >= 0) {
        s32 i = seg(add(shl(a, 6), 0x9d));
        if (i < 0x21) {
            return add(kSine[size_t(i)], lerp(kSine[size_t(0x20 - i)], frac(a, i)));
        }
        return sub(kSine[size_t(0x40 - i)], lerp(kSine[size_t(i - 0x20)], frac(a, i)));
    }
    s32 i = seg(sub(shl(a, 6), 0x9d));
    if (i > -0x21) {
        return sub(lerp(kSine[size_t(i + 0x20)], frac(a, i)), kSine[size_t(-i)]);
    }
    return neg(add(kSine[size_t(i + 0x40)], lerp(kSine[size_t(-0x20 - i)], frac(a, i))));
}

s32 cosine(s32 angle) {
    return sine(add(angle, 0x9d));
} // 02064a7c

// 02064a8c: rotate/scale a point about a centre {scale, angle, ox, oy, cx, cy}.
void transform(std::array<s32, 2>& out, const std::array<s32, 2>& in, const std::array<s32, 6>& t) {
    float c = u2f(u32(t[0])) * i2f(cosine(t[1])) / 512.0f;
    float s = u2f(u32(t[0])) * i2f(sine(t[1])) / 512.0f;
    s32 dx = sub(in[0], t[4]), dy = sub(in[1], t[5]);
    s32 x = f2i(c * i2f(dx) + s * i2f(dy));
    out[0] = add(t[2], tdivp2(x, 8));
    s32 y = f2i((0.0f - s) * i2f(dx) + c * i2f(dy));
    out[1] = add(t[3], tdivp2(y, 8));
}

// 02064bdc: resample a raw stroke to `n` points equally spaced along its path.
void resample(const Point* p, s32 count, Point* out, s32 n, s32 ox, s32 oy, u32 shift) {
    auto X = [&](s32 i) {
        return asr(add(ox, p[i].x), shift);
    };
    auto Y = [&](s32 i) {
        return asr(add(oy, p[i].y), shift);
    };
    if (count == 1) {
        for (s32 i = 0; i < n; ++i) {
            out[i].x = h(asr(add(p[0].x, ox), shift));
            out[i].y = h(asr(add(p[0].y, oy), shift));
        }
        return;
    }
    s32 total = 0;
    for (s32 i = 0; i < count - 1; ++i) {
        total =
            add(total, hypot(mul(sub(p[i + 1].x, p[i].x), 100), mul(sub(p[i + 1].y, p[i].y), 100)));
    }
    s32 walked = 0, prev = 0, seg = 0, target = 0;
    for (s32 k = 0; k < n; ++k) {
        s32 want = sdiv(target, n - 1);
        while (walked <= want && seg < count - 1) {
            s32 d =
                hypot(mul(sub(p[seg].x, p[seg + 1].x), 100), mul(sub(p[seg].y, p[seg + 1].y), 100));
            prev = walked;
            walked = add(walked, d);
            ++seg;
        }
        s32 gap = asr(sub(prev, walked), shift);
        if (gap < 0) {
            gap = neg(gap);
        }
        if (gap < 1) {
            out[k].x = h(X(seg - 1));
            out[k].y = h(Y(seg - 1));
        } else {
            s32 a = sub(want, prev), b = sub(walked, want), len = sub(walked, prev);
            out[k].x = h(sdiv(add(add(mul(X(seg - 1), b), mul(X(seg), a)), sdiv(len, 2)), len));
            out[k].y = h(sdiv(add(add(mul(Y(seg - 1), b), mul(Y(seg), a)), sdiv(len, 2)), len));
        }
        target = add(target, total);
    }
    out[n - 1].x = h(X(count - 1));
    out[n - 1].y = h(Y(count - 1));
}

// ---- Normalized strokes (0x11c-byte records) and stroke sets (0x34) ------------
struct Stroke32 {
    std::array<s16, 32> x{}, y{}, dx{}, dy{}; // +0x00,+0x40,+0x80,+0xc0
    s32 sumX = 0, sumY = 0;                   // +0x100,+0x104
    u32 energy = 1;                           // +0x108 sum of squares (never 0)
    s32 weight = 0;                           // +0x10c number of weighted points
    s32 unused110 = 0, unused114 = 0;
    u8 minX = 0, maxX = 0, minY = 0, maxY = 0; // +0x118..+0x11b indices
};

struct StrokeSet {
    std::array<Stroke32*, 6> strokes{}; // +0x00
    s32 count = 0;                      // +0x18
    s16 scale = 0;                      // +0x1c
    s32 mask = 0;                       // +0x20 point-weighting pattern
    s32 sumX = 0, sumY = 0;             // +0x24,+0x28
    s32 total = 0;                      // +0x2c
    float norm = 1.0f;                  // +0x30
};

// 020648c4: forward differences with the last one repeated (16-bit arithmetic).
void differences(const std::array<s16, 32>& in, std::array<s16, 32>& out) {
    for (s32 i = 0; i < 31; ++i) {
        out[i] = h(add(h(neg(in[i])), in[i + 1]));
    }
    out[31] = out[30];
}

constexpr std::array<std::array<u8, 2>, 12> kWeightPatterns{{{1, 1},
                                                             {2, 1},
                                                             {2, 2},
                                                             {3, 2},
                                                             {3, 1},
                                                             {4, 1},
                                                             {4, 2},
                                                             {3, 3},
                                                             {4, 1},
                                                             {3, 4},
                                                             {6, 2},
                                                             {0, 0}}}; // 02065d5c

// 0205ddc0: does pattern `p` weight position `pos` of `total`?
s32 weighted(s32 p, s32 total, s32 pos) {
    if (p == 0) {
        return 1;
    }
    if (p < 0 || p >= s32(kWeightPatterns.size())) {
        throw std::logic_error("weight pattern outside the ROM table");
    }
    return asr(kWeightPatterns[size_t(p)][1],
               u32(sdiv(mul(pos, kWeightPatterns[size_t(p)][0]), total))) &
           1;
}

s32 weights(s32 p, std::array<s16, 32>& out, s32 total, s32 start, s32 n) { // 0205de08
    s32 sum = 0;
    for (s32 pos = start; pos < start + n; ++pos) {
        out[size_t(pos - start)] = h(weighted(p, total, pos));
        sum += out[size_t(pos - start)];
    }
    return sum;
}

// 0205b030: scale differences, accumulate moments and extreme point indices.
void analyze(Stroke32& s, s32 scale, const std::array<s16, 32>* w) {
    differences(s.x, s.dx);
    differences(s.y, s.dy);
    s32 n = 0, sx = 0, sy = 0;
    u32 e = 0;
    s32 minX = 0x7fff, minY = 0x7fff, maxX = -0x8000, maxY = -0x8000;
    for (u32 i = 0; i < 32; ++i) {
        if (w) {
            if ((*w)[i] == 0) {
                continue;
            }
            n += (*w)[i];
        } else {
            ++n;
        }
        s16 odx = s.dx[i];
        s.dx[i] = h(mul(odx, scale));
        e = u32(add(s32(e), mul(odx, s.dx[i])));
        s16 ody = s.dy[i];
        s.dy[i] = h(mul(ody, scale));
        s32 x = s.x[i], y = s.y[i];
        e = u32(add(add(add(s32(e), mul(ody, s.dy[i])), mul(x, x)), mul(y, y)));
        if (x < minX) {
            minX = x;
            s.minX = u8(i);
        }
        sx = add(sx, x);
        sy = add(sy, y);
        if (x > maxX) {
            maxX = x;
            s.maxX = u8(i);
        }
        if (y < minY) {
            minY = y;
            s.minY = u8(i);
        }
        if (y > maxY) {
            maxY = y;
            s.maxY = u8(i);
        }
    }
    s.sumX = sx;
    s.sumY = sy;
    s.energy = e ? e : 1;
    s.weight = n;
}

// 0205b2cc: set totals and the binary32 normalization term.
void summarize(StrokeSet& set, s32 mask, s16 scale) {
    set.scale = scale;
    set.mask = mask;
    set.sumX = set.sumY = set.total = 0;
    set.norm = 0.0f;
    u32 acc = 0, sh = 0;
    for (s32 i = 0; i < set.count; ++i) {
        Stroke32& s = *set.strokes[size_t(i)];
        set.sumX = add(set.sumX, s.sumX);
        set.sumY = add(set.sumY, s.sumY);
        u32 e = s.energy;
        if (acc + (sh >= 32 ? 0 : e >> sh) < acc) {
            do {
                ++sh;
                acc >>= 1;
            } while (acc + (sh >= 32 ? 0 : e >> sh) < acc);
        }
        set.total = add(set.total, s.weight);
        acc += sh >= 32 ? 0 : e >> sh;
    }
    float f = i2f(shl(1, sh));
    f = f * u2f(acc);
    f = f * i2f(set.total);
    f = f - i2f(set.sumX) * i2f(set.sumX);
    f = f - i2f(set.sumY) * i2f(set.sumY);
    set.norm = double(f) <= 1.0 ? 1.0f : f;
}

s32 pointsOf(const StrokeSet& s) {
    return s.count > 0 ? s.count * 32 : 0;
} // 0205b000

// 0205af6c: copy stroke contents and totals (stroke pointers are kept).
void copySet(StrokeSet& dst, const StrokeSet& src) {
    for (s32 i = 0; i < src.count; ++i) {
        if (dst.strokes[size_t(i)] != src.strokes[size_t(i)]) {
            *dst.strokes[size_t(i)] = *src.strokes[size_t(i)];
        }
    }
    dst.scale = src.scale;
    dst.total = src.total;
    dst.mask = src.mask;
    dst.count = src.count;
    dst.norm = src.norm;
    dst.sumX = src.sumX;
    dst.sumY = src.sumY;
}

// 0205b180: bounding box of the first n strokes.
void bounds(const StrokeSet& set, s32 n, s16* minX, s16* maxX, s16* minY, s16* maxY) {
    s32 lx = 0x7fff, hx = -0x8000, ly = 0x7fff, hy = -0x8000;
    for (s32 i = 0; i < n; ++i) {
        const Stroke32& s = *set.strokes[size_t(i)];
        if (s.x[s.minX] < lx) {
            lx = s.x[s.minX];
        }
        if (s.x[s.maxX] > hx) {
            hx = s.x[s.maxX];
        }
        if (s.y[s.minY] < ly) {
            ly = s.y[s.minY];
        }
        if (s.y[s.maxY] > hy) {
            hy = s.y[s.maxY];
        }
    }
    if (minX) {
        *minX = h(lx);
    }
    if (maxX) {
        *maxX = h(hx);
    }
    if (minY) {
        *minY = h(ly);
    }
    if (maxY) {
        *maxY = h(hy);
    }
}

// 0205b234: centroid of strokes from..to (clamped).
void centroid(const StrokeSet& set, s32 from, s32 to, s16* mx, s16* my) {
    if (from <= 0) {
        from = 0;
    }
    if (to >= set.count - 1) {
        to = set.count - 1;
    }
    s32 sx = 0, sy = 0, n = 0;
    for (; from <= to; ++from) {
        const Stroke32& s = *set.strokes[size_t(from)];
        sx = add(sx, s.sumX);
        sy = add(sy, s.sumY);
        n = add(n, s.weight);
    }
    if (mx) {
        *mx = h(sdiv(sx, n));
    }
    if (my) {
        *my = h(sdiv(sy, n));
    }
}

// 0205b440: resample positions start..(32-end) (units of 1/128 point) to 32 points.
void trim(Stroke32& d, const Stroke32& s, s32 start, s32 end) {
    s32 t = mul(start, 31), step = sub(end, start);
    for (u32 i = 0; i < 32; ++i, t = add(t, step)) {
        s32 k = tdivp2(t, 7);
        if (k < 0) {
            d.x[i] = s.x[0];
            d.y[i] = s.y[0];
        } else if (k >= 31) {
            d.x[i] = s.x[31];
            d.y[i] = s.y[31];
        } else if (t > shl(k, 7)) {
            s32 a = sub(shl(add(k, 1), 7), t), b = sub(t, shl(k, 7));
            d.x[i] = h(tdivp2(add(mul(s.x[size_t(k)], a), mul(s.x[size_t(k + 1)], b)), 7));
            d.y[i] = h(tdivp2(add(mul(s.y[size_t(k)], a), mul(s.y[size_t(k + 1)], b)), 7));
        } else {
            d.x[i] = s.x[size_t(k)];
            d.y[i] = s.y[size_t(k)];
        }
    }
}

// 0205b544: re-analyze every stroke with point-weighting pattern `mask`.
void reweigh(StrokeSet& set, s32 mask, s16 scale) {
    s32 total = pointsOf(set);
    for (s32 i = 0; i < set.count; ++i) {
        std::array<s16, 32> w{};
        weights(mask, w, total, i * 32, 32);
        analyze(*set.strokes[size_t(i)], scale, &w);
    }
    summarize(set, mask, scale);
}

// 0205e780: is a single stroke small enough to be a dot (relative to the guides)?
bool isDot(const StrokeSet& set, s32 base, s32 help) {
    if (set.count != 1) {
        return false;
    }
    s16 lx, hx, ly, hy;
    bounds(set, 1, &lx, &hx, &ly, &hy);
    s32 limit = 5;
    if (base != help) {
        limit =
            add(s32((s64(0x55555556) * sub(base, help)) >> 32), s32(u32(sub(base, help)) >> 31));
        if (limit < 5) {
            limit = 5;
        }
    }
    return approxLength(add(sub(hx, lx), 1), add(sub(hy, ly), 1)) <= limit;
}

// 020651d4: punctuation prototype for a dot, chosen relative to the guides.
bool dotPrototype(const u8* db, s32 maxY, s32 base, s32 help, Head& out, const u8* table) {
    const u8* b = subB(db);
    u16 n = r16(b, 0x32);
    const u8* p = nullptr;
    if (n) {
        p = (base == help || maxY > help || n == 1) ? bSpecial(b) : bSpecial(b) + 4;
    }
    if (!p) {
        return false;
    }
    out = Head{};
    out.a = subA(db);
    out.b = b;
    out.table = table;
    out.partStrokes = 0;
    out.partIndex = -1;
    out.strokes = p[0];
    out.index = s16(r16(p, 2));
    setRec(out);
    return true;
}

// 0205dfd0: initialize a candidate from a prototype for the given strokes.
void makeCandidate(Candidate& c, const StrokeSet& set, s16 score, const Head& h) {
    clearCandidate(c);
    c.h = h;
    std::array<const s8*, 8> orders{};
    orderList(h, orders, 1);
    for (s32 i = 0; i < set.count; ++i) {
        c.order[size_t(i)] = orders[0][i];
    }
    c.cx = sdiv(shl(set.sumX, 5), set.total);
    c.cy = sdiv(shl(set.sumY, 5), set.total);
    s32 px = h.partStrokes ? add(rs16(h.partRec, 8), shl(mul(h.partX, h.partStrokes), 5)) : 0;
    c.v30 = sdiv(shl(add(h.strokes ? rs16(h.rec, 4) : 0, px), 5), set.total);
    s32 py = h.partStrokes ? add(rs16(h.partRec, 0xa), shl(mul(h.partY, h.partStrokes), 5)) : 0;
    c.v34 = sdiv(shl(add(h.strokes ? rs16(h.rec, 6) : 0, py), 5), set.total);
    c.scale = 0x200;
    c.score = score;
    c.text = textOf(h);
}

// ---- Shape matching ------------------------------------------------------------
using Order = std::array<s8, 6>;

// Fit record at candidate +0x28: {scale, angle, protoX, protoY, inputX, inputY}.
std::array<s32, 6> fitOf(const Candidate& c) {
    return {c.scale, c.angle, c.v30, c.v34, c.cx, c.cy};
}

void setFit(Candidate& c, const std::array<s32, 6>& f) {
    c.scale = f[0];
    c.angle = f[1];
    c.v30 = f[2];
    c.v34 = f[3];
    c.cx = f[4];
    c.cy = f[5];
}

// 0205f734: similarity score from the rotation-invariant correlation.
s16 similarity(s32 a, s32 b, s32 extra, float norm, s32 protoNorm) {
    s32 l = hypot(a, b);
    float v = i2f(add(l, extra));
    v = v * i2f(add(l, extra));
    float d = norm * u2f(u32(protoNorm));
    if (0.0f == d) {
        return 1000;
    }
    return rootScore(v / d, 1000);
}

// 0205f7c4: integer variant of the similarity score for the coarse match.
s16 scoreCoarse(s32 a, s32 b, u32 extra, float norm, u32 protoNorm) {
    u32 sh = 0;
    while (norm >= 4294967296.0f) {
        norm = norm / 4.0f;
        ++sh;
    }
    u32 nn = f2u(norm);
    if (a < 0) {
        a = neg(a);
    }
    if (b < 0) {
        b = neg(b);
    }
    u32 ua = u32(asr(a, sh)), ub = u32(asr(b, sh));
    extra = sh >= 32 ? 0 : extra >> sh;
    u32 m = ua > ub ? ua : ub, k = 0;
    while (m > 0xb504) {
        m >>= 2;
        k += 2;
    }
    for (u32 j = 0; j < k; ++j) {
        if (nn < protoNorm) {
            protoNorm >>= 2;
        } else {
            nn >>= 2;
        }
    }
    while ((nn > protoNorm ? nn : protoNorm) > 0xb504) {
        if (nn > protoNorm) {
            nn >>= 2;
        } else {
            protoNorm >>= 2;
        }
        ++k;
    }
    auto shr = [](u32 v, u32 n) {
        return n >= 32 ? 0u : v >> n;
    };
    ub = shr(ub, k);
    ua = shr(ua, k);
    u32 len2 = ua * ua + ub * ub, den = nn * protoNorm;
    extra = shr(extra, k);
    if (extra) {
        u32 r = u32(isqrt(len2));
        if (extra > extra + r) {
            do {
                extra >>= 1;
                r >>= 1;
                den >>= 2;
            } while (extra + r < extra);
        }
        r += extra;
        while (r > 0xffff) {
            r >>= 1;
            den >>= 2;
        }
        len2 = r * r;
    }
    while (len2 > 0x3fffff) {
        len2 >>= 3;
        den >>= 3;
    }
    if (den == 0) {
        return 1000;
    }
    if (len2 >= den) {
        return 0;
    }
    return s16(1000 - s32(udiv(len2 * 1000, den)));
}

// 0205f99c: fitted transform {scale, angle, proto centre, input centre}.
void fitTransform(std::array<s32, 6>& out, const StrokeSet& set, s32 a, s32 b, s32 extra,
                  s32 protoNorm, float norm, s32 total, s32 psx, s32 psy) {
    out[4] = sdiv(shl(set.sumX, 5), total);
    out[5] = sdiv(shl(set.sumY, 5), total);
    out[2] = sdiv(shl(psx, 5), total);
    out[3] = sdiv(shl(psy, 5), total);
    out[1] = direction(a, b);
    u32 s;
    if (extra > 0) {
        s = scaledRoot(i2f(protoNorm), norm, 0x200);
    } else {
        float fa = i2f(a), fb = i2f(b);
        s = scaledRoot(fa * fa + fb * fb, norm * norm, 0x200);
    }
    if (s >= 0x8000) {
        s = 0x8000;
    }
    if (s <= 1) {
        s = 1;
    }
    out[0] = s32(s);
}

// Prototype centroid sums of the candidate's part (shared idiom).
s32 protoSumX(const Head& h) {
    return add(h.strokes ? rs16(h.rec, 4) : 0,
               h.partStrokes ? add(rs16(h.partRec, 8), shl(mul(h.partX, h.partStrokes), 5)) : 0);
}

s32 protoSumY(const Head& h) {
    return add(h.strokes ? rs16(h.rec, 6) : 0,
               h.partStrokes ? add(rs16(h.partRec, 0xa), shl(mul(h.partY, h.partStrokes), 5)) : 0);
}

// 0205e824: full 32-point match of every stroke (positions and directions).
void matchFull(s16* score, std::array<s32, 6>* fit, const StrokeSet& set, const Head& h,
               const Order& order) {
    u32 pos = positionMask(h);
    s32 points = 0, acc = 0, sumA = 0, sumB = 0;
    for (s32 i = 0; i < set.count; ++i) {
        const Stroke32& s = *set.strokes[size_t(mapped(order, i))];
        const s8* p = shapeOf(h, i);
        s32 a = 0, b = 0;
        auto term = [&](s32 px, s32 py, s32 x, s32 y) {
            a = add(a, add(mul(py, y), mul(px, x)));
            b = add(b, sub(mul(py, x), mul(px, y)));
        };
        if (reversed(order, i)) {
            for (s32 k = 0; k < 32; ++k) {
                term(p[k], p[32 + k], s.x[size_t(31 - k)], s.y[size_t(31 - k)]);
            }
            for (s32 k = 0; k < 31; ++k) {
                s32 dpy = b8(p[32 + k + 1] - p[32 + k]), dpx = b8(p[k + 1] - p[k]);
                s32 dx = s.dx[size_t(31 - k)], dy = s.dy[size_t(31 - k)];
                a = sub(a, add(mul(dpy, dy), mul(dpx, dx)));
                b = sub(b, sub(mul(dpy, dx), mul(dpx, dy)));
            }
            s32 dpy = b8(p[63] - p[62]), dpx = b8(p[31] - p[30]);
            s32 dx = s.dx[0], dy = s.dy[0];
            a = sub(a, add(mul(dpx, dx), mul(dpy, dy)));
            b = sub(b, sub(mul(dpy, dx), mul(dpx, dy)));
        } else {
            for (s32 k = 0; k < 32; ++k) {
                term(p[k], p[32 + k], s.x[size_t(k)], s.y[size_t(k)]);
            }
            for (s32 k = 0; k < 31; ++k) {
                s32 dpy = b8(p[32 + k + 1] - p[32 + k]), dpx = b8(p[k + 1] - p[k]);
                s32 dx = s.dx[size_t(k)], dy = s.dy[size_t(k)];
                a = add(a, add(mul(dpy, dy), mul(dpx, dx)));
                b = add(b, sub(mul(dpy, dx), mul(dpx, dy)));
            }
            s32 dpy = b8(p[63] - p[62]), dpx = b8(p[31] - p[30]);
            s32 dx = s.dx[31], dy = s.dy[31];
            a = add(a, add(mul(dpx, dx), mul(dpy, dy)));
            b = add(b, sub(mul(dpy, dx), mul(dpx, dy)));
        }
        points = add(points, 32);
        if (i >= s32(h.strokes)) {
            a = add(add(mul(h.partX, s.sumX), a), mul(h.partY, s.sumY));
            b = sub(add(mul(h.partY, s.sumX), b), mul(h.partX, s.sumY));
        }
        if ((pos >> u32(i)) & 1) {
            s32 spx = 0, spy = 0;
            for (s32 k = 0; k < 32; ++k) {
                spy = add(spy, p[32 + k]);
                spx = add(spx, p[k]);
            }
            if (i >= s32(h.strokes)) {
                spx = add(spx, shl(h.partX, 5));
                spy = add(spy, shl(h.partY, 5));
            }
            s32 cr = add(mul(s.sumX, spx), mul(s.sumY, spy)),
                ci = sub(mul(s.sumX, spy), mul(s.sumY, spx));
            s32 l = hypot(sub(shl(a, 5), cr), sub(shl(b, 5), ci));
            acc = add(mul(set.count, l), acc);
            sumA = add(sumA, tdivp2(cr, 5));
            sumB = add(sumB, tdivp2(ci, 5));
        } else {
            sumA = add(sumA, a);
            sumB = add(sumB, b);
        }
    }
    s32 psx = protoSumX(h), psy = protoSumY(h);
    s32 A = sub(sub(mul(sumA, set.total), mul(set.sumX, psx)), mul(set.sumY, psy));
    s32 B = add(sub(mul(sumB, set.total), mul(set.sumX, psy)), mul(set.sumY, psx));
    s32 e = add(h.strokes ? r32(h.rec, 0xc) : 0, h.partStrokes ? r32(h.partRec, 0x10) : 0);
    s32 pn =
        sub(sub(mul(points, add(mul(set.scale, e), protoMoment(h))), mul(psx, psx)), mul(psy, psy));
    if (score) {
        *score = similarity(A, B, acc, set.norm, pn);
    }
    if (fit) {
        fitTransform(*fit, set, A, B, acc, pn, set.norm, set.total, psx, psy);
    }
}

// 0205ede8 (L4 02055164): like matchFull, but only over the points selected
// by weighting pattern `mask`, with the prototype centre taken from those
// same points and the step terms weighted by `scale`. Used by the pairwise
// rules 6 and 8. (0205f2f4, a single-stroke variant, is only called by rule 7,
// which never gets that far; it is not reconstructed.)
void matchWeighted(s16* score, std::array<s32, 6>* fit, const StrokeSet& set, const Head& h,
                   s32 mask, s32 scale, const Order& order) {
    u32 pos = positionMask(h);
    s32 total = pointsOf(set), g = 0;
    s32 energy = 0, A = 0, B = 0, psx = 0, psy = 0, extra = 0;
    for (s32 i = 0; i < set.count; ++i) {
        const Stroke32& s = *set.strokes[size_t(mapped(order, i))];
        const s8* p = shapeOf(h, i);
        bool rev = reversed(order, i);
        s32 at = rev ? 31 : 0, step = rev ? -1 : 1;
        s32 ox = i >= s32(h.strokes) ? h.partX : 0, oy = i >= s32(h.strokes) ? h.partY : 0;
        s32 a = 0, b = 0, sx = 0, sy = 0, n = 0;
        for (s32 k = 0; k < 32; ++k, at += step, ++g) {
            if (!weighted(mask, total, g)) {
                continue;
            }
            ++n;
            s32 dpx, dpy;
            if (k + 1 == 32) {
                dpx = b8(p[k] - p[k - 1]);
                dpy = b8(p[32 + k] - p[31 + k]);
            } else {
                dpx = b8(p[k + 1] - p[k]);
                dpy = b8(p[33 + k] - p[32 + k]);
            }
            s32 px = b8(p[k] + b8(ox)), py = b8(p[32 + k] + b8(oy));
            s32 dx = mul(s.dx[size_t(at)], step), dy = mul(s.dy[size_t(at)], step);
            s32 x = s.x[size_t(at)], y = s.y[size_t(at)];
            a = add(add(add(add(a, mul(dpx, dx)), mul(dpy, dy)), mul(px, x)), mul(py, y));
            b = add(sub(add(sub(b, mul(dpx, dy)), mul(dpy, dx)), mul(px, y)), mul(py, x));
            energy = add(energy, add(mul(scale, add(mul(dpx, dpx), mul(dpy, dpy))),
                                     add(mul(px, px), mul(py, py))));
            sx = add(sx, px);
            sy = add(sy, py);
        }
        psx = add(psx, sx);
        psy = add(psy, sy);
        if (((pos >> u32(i)) & 1) && n != 0) {
            s32 cr = add(mul(s.sumY, sy), mul(s.sumX, sx)),
                ci = sub(mul(s.sumX, sy), mul(s.sumY, sx));
            s32 l = hypot(sub(mul(n, a), cr), sub(mul(n, b), ci));
            extra = add(extra, sdiv(mul(set.total, l), n));
            A = add(A, sdiv(cr, n));
            B = add(B, sdiv(ci, n));
        } else {
            A = add(A, a);
            B = add(B, b);
        }
    }
    s32 pn = sub(sub(mul(energy, set.total), mul(psx, psx)), mul(psy, psy));
    s32 a2 = sub(sub(mul(A, set.total), mul(set.sumX, psx)), mul(set.sumY, psy));
    s32 b2 = add(sub(mul(B, set.total), mul(set.sumX, psy)), mul(set.sumY, psx));
    if (score) {
        *score = similarity(a2, b2, extra, set.norm, pn);
    }
    if (fit) {
        fitTransform(*fit, set, a2, b2, extra, pn, set.norm, set.total, psx, psy);
    }
}

constexpr std::array<s32, 8> kSamples{0, 4, 8, 13, 18, 23, 27, 31}; // Coarse sample points.

// 020603d8: moments of the eight sample points of every stroke.
void sampleMoments(StrokeSet& set) {
    s32 n = 0, sx = 0, sy = 0;
    u32 e = 0;
    for (s32 i = 0; i < set.count; ++i) {
        const Stroke32& s = *set.strokes[size_t(i)];
        for (s32 k : kSamples) {
            s32 x = s.x[size_t(k)], y = s.y[size_t(k)];
            ++n;
            e = u32(add(s32(e), add(mul(y, y), mul(x, x))));
            sx = add(sx, x);
            sy = add(sy, y);
        }
    }
    float f = u2f(e) * i2f(n);
    f = f - i2f(mul(sx, sx));
    f = f - i2f(mul(sy, sy));
    set.norm = f;
    set.sumX = sx;
    set.sumY = sy;
    set.mask = -1;
}

// 0205faac: coarse eight-point match; returns score and fitted angle.
void matchCoarse(s16* score, s32* angle, const StrokeSet& set, const Head& h, const Order& order) {
    u32 pos = positionMask(h);
    s32 ppx =
        add(h.strokes ? rs16(h.rec, 0x1c) : 0,
            h.partStrokes ? add(rs16(h.partRec, 0x1c), shl(mul(h.partX, h.partStrokes), 3)) : 0);
    s32 ppy =
        add(h.strokes ? rs16(h.rec, 0x1e) : 0,
            h.partStrokes ? add(rs16(h.partRec, 0x1e), shl(mul(h.partY, h.partStrokes), 3)) : 0);
    s32 acc = 0, A = 0, B = 0;
    for (s32 i = 0; i < set.count; ++i) {
        const Stroke32& s = *set.strokes[size_t(mapped(order, i))];
        const s8* p = shapeOf(h, i);
        bool rev = reversed(order, i);
        s32 a = 0, b = 0, sx = 0, sy = 0, spx = 0, spy = 0;
        for (s32 j = 0; j < 8; ++j) {
            s32 pk = kSamples[size_t(j)], sk = rev ? 31 - pk : pk;
            s32 px = p[pk], py = p[32 + pk], x = s.x[size_t(sk)], y = s.y[size_t(sk)];
            a = add(a, add(mul(px, x), mul(py, y)));
            b = add(b, sub(mul(py, x), mul(px, y)));
            sx = add(sx, x);
            sy = add(sy, y);
            spx = add(spx, px);
            spy = add(spy, py);
        }
        if (i >= s32(h.strokes)) {
            a = add(add(a, mul(h.partX, sx)), mul(h.partY, sy));
            b = add(sub(b, mul(h.partX, sy)), mul(h.partY, sx));
        }
        if ((pos >> u32(i)) & 1) {
            if (i >= s32(h.strokes)) {
                spx = add(spx, shl(h.partX, 3));
                spy = add(spy, shl(h.partY, 3));
            }
            s32 cr = add(mul(sx, spx), mul(sy, spy)), ci = sub(mul(sx, spy), mul(sy, spx));
            s32 u = sub(shl(a, 3), cr), v = sub(shl(b, 3), ci);
            s32 m = std::max(u < 0 ? neg(u) : u, v < 0 ? neg(v) : v);
            u32 sh = 0;
            while (m > 0x7f79) {
                m = asr(m, 1);
                ++sh;
            }
            s32 l = isqrt(u32(add(mul(asr(v, sh), asr(v, sh)), mul(asr(u, sh), asr(u, sh)))));
            acc = add(acc, shl(mul(set.count, l), sh));
            A = add(A, tdivp2(cr, 3));
            B = add(B, tdivp2(ci, 3));
        } else {
            A = add(A, a);
            B = add(B, b);
        }
    }
    s32 n8 = shl(set.count, 3);
    s32 a2 = sub(sub(mul(A, n8), mul(set.sumX, ppx)), mul(set.sumY, ppy));
    s32 b2 = add(sub(mul(B, n8), mul(set.sumX, ppy)), mul(set.sumY, ppx));
    if (score) {
        *score = scoreCoarse(a2, b2, u32(acc), set.norm, u32(protoNorm(h)));
    }
    if (angle) {
        *angle = direction(a2, b2);
    }
}

// 0205d270: least-squares change of trim scale/shift for one prototype stroke.
// mode 0 refits all strokes with point pattern `mask`; otherwise only stroke
// `slot`, points from..to, without weighting.
void solveTrim(const StrokeSet& set, const Head& h, s32 mask, s32 slot, float& outScale,
               float& outShift, const Order& order, s32 mode, s32 from, s32 to) {
    s32 total = pointsOf(set), first, last;
    if (mode) {
        mask = 0;
        first = last = slot;
    } else {
        from = 0;
        to = 31;
        first = 0;
        last = set.count - 1;
    }
    s32 n = 0, sx = 0, sy = 0, spx = 0, spy = 0;
    for (s32 i = first, g = 0; i <= last; ++i) {
        bool rev = reversed(order, i);
        s32 at = rev ? 31 : 0, step = rev ? -1 : 1;
        const s8* p = shapeOf(h, i);
        s32 py0 = i < s32(h.strokes) ? 0 : s32(s8(u8(h.partY)));
        const Stroke32& s = *set.strokes[size_t(mapped(order, i))];
        for (s32 k = 0; k < 32; ++k, at += step, ++g) {
            if (k < from || k > to || !weighted(mask, total, g)) {
                continue;
            }
            sx = add(sx, s.x[size_t(at)]);
            sy = add(sy, s.y[size_t(at)]);
            spx = add(spx, p[k]);
            spy = add(spy, add(p[32 + k], py0));
            ++n;
        }
    }
    s32 c64 = 0, c60 = 0, c5c = 0, c58 = 0, cpp = 0;
    for (s32 i = first, g = 0; i <= last; ++i) {
        bool rev = reversed(order, i);
        s32 at = rev ? 31 : 0, step = rev ? -1 : 1;
        const s8* p = shapeOf(h, i);
        s32 py0 = i < s32(h.strokes) ? 0 : s32(s8(u8(h.partY)));
        const Stroke32& s = *set.strokes[size_t(mapped(order, i))];
        for (s32 k = 0; k < 32; ++k, at += step, ++g) {
            if (k < from || k > to || !weighted(mask, total, g)) {
                continue;
            }
            s32 cx = tdivp2(sub(mul(s.x[size_t(at)], n), sx), 4),
                cy = tdivp2(sub(mul(s.y[size_t(at)], n), sy), 4);
            s32 cpx = tdivp2(sub(mul(p[k], n), spx), 4),
                cpy = tdivp2(sub(mul(add(p[32 + k], py0), n), spy), 4);
            c64 = add(c64, mul(cpx, cx));
            c60 = add(c60, mul(cpy, cx));
            c5c = add(c5c, mul(cpx, cy));
            c58 = add(c58, mul(cpy, cy));
            cpp = add(cpp, add(mul(cpx, cpx), mul(cpy, cpy)));
        }
    }
    const s8* p = shapeOf(h, slot);
    s32 py0 = slot < s32(h.strokes) ? 0 : s32(s8(u8(h.partY)));
    bool rev = reversed(order, slot);
    s32 at = rev ? 31 : 0, step = rev ? -1 : 1;
    const Stroke32& s = *set.strokes[size_t(mapped(order, slot))];
    s32 d30 = 0, d34 = 0, d38 = 0, d3c = 0, d28 = 0, e84 = 0, e80 = 0, e7c = 0, e78 = 0, e74 = 0,
        e70 = 0, e6c = 0, e68 = 0;
    for (s32 k = 0, g = slot * 32; k < 32; ++k, at += step, ++g) {
        if (k < from || k > to || !weighted(mask, total, g)) {
            continue;
        }
        s32 x = s.x[size_t(at)], y = s.y[size_t(at)], py = add(p[32 + k], py0);
        s32 cpy = tdivp2(sub(mul(py, n), spy), 4);
        s32 cx = tdivp2(sub(mul(x, n), sx), 4), cy = tdivp2(sub(mul(y, n), sy), 4);
        s32 cpx = tdivp2(sub(mul(p[k], n), spx), 4);
        s32 dx, dy;
        if (k < 31) {
            dx = sub(s.x[size_t(at + step)], x);
            dy = sub(s.y[size_t(at + step)], y);
        } else {
            dx = sub(x, s.x[size_t(at - step)]);
            dy = sub(y, s.y[size_t(at - step)]);
        }
        s32 ddx = tdivp2(mul(n, dx), 4), ddy = tdivp2(mul(n, dy), 4);
        s32 proj = add(mul(cx, ddx), mul(cy, ddy)), dd = add(mul(ddx, ddx), mul(ddy, ddy));
        d30 = add(d30, dd);
        s32 dk = mul(dd, k);
        d38 = add(d38, dk);
        d3c = add(d3c, mul(dk, k));
        d34 = add(d34, mul(proj, k));
        d28 = add(d28, proj);
        s32 a = mul(cpx, ddx), b = mul(cpy, ddx), c = mul(cpx, ddy), d = mul(cpy, ddy);
        e84 = add(e84, mul(a, k));
        e80 = add(e80, mul(b, k));
        e7c = add(e7c, mul(c, k));
        e78 = add(e78, mul(d, k));
        e74 = add(e74, a);
        e70 = add(e70, b);
        e6c = add(e6c, c);
        e68 = add(e68, d);
    }
    float a1 = i2f(add(e84, e78)), a2 = i2f(sub(e7c, e80));
    float m11 = a1 * a1 + a2 * a2;
    float m12 = a1 * i2f(add(e74, e68)) + a2 * i2f(sub(e6c, e70));
    float m13 = a1 * i2f(add(c64, c58)) + a2 * i2f(sub(c5c, c60));
    float b1 = i2f(add(e74, e68)), b2 = i2f(sub(e6c, e70));
    float m22 = b1 * b1 + b2 * b2;
    float m23 = b1 * i2f(add(c64, c58)) + b2 * i2f(sub(c5c, c60));
    float q = i2f(cpp);
    float p11 = i2f(d3c) * q - m11, p12 = i2f(d38) * q - m12, p22 = i2f(d30) * q - m22;
    float p21 = i2f(d38) * q - m12, p13 = i2f(d34) * q - m13, p23 = i2f(d28) * q - m23;
    float det = p11 * p22 - p21 * p12;
    if (0.0f == det) {
        outScale = 1.0f;
        outShift = 0.0f;
        return;
    }
    outScale = 1.0f + (0.0f - (p22 * p13 - p12 * p23)) / det;
    outShift = (0.0f - ((0.0f - p21) * p13 + p11 * p23)) / det;
}

// Truncating division by 31 as compiled (multiply by 0x84210843).
s32 div31(s32 v) {
    return add(s32(u32(v) >> 31), asr(add(s32((s64(s32(0x84210843)) * v) >> 32), v), 4));
}

// 0205ce00: refine the trimmed stroke ends of candidate stroke `idx`.
void refineEnds(Candidate& c, s32 mask, s16 scale, s32 idx, const StrokeSet& src, StrokeSet& dst,
                s32 noPeriod, s32 mode, s32 from, s32 to, s32 maxCut) {
    s32 r10 = div31(add(shl(c.cut0, 7), 15)), r9 = div31(add(shl(c.cut1, 7), 15));
    s32 slot = slotOf(c.order, idx);
    bool rev = slot < 6 && c.order[size_t(slot)] <= 0;
    if (!(c.h.partStrokes && noPeriod && periodOf(c.h))) {
        // f0 accumulates unclamped; the clamped copies set the next trim.
        float f0 = i2f(c.cut0), k = 1.0f - i2f(add(c.cut1, c.cut0)) / 31.0f, start = 0.0f,
              end = 0.0f;
        for (int it = 0; it < 2; ++it) {
            if (rev) {
                trim(*dst.strokes[size_t(idx)], *src.strokes[size_t(idx)], r9, sub(0x80, r10));
            } else {
                trim(*dst.strokes[size_t(idx)], *src.strokes[size_t(idx)], r10, sub(0x80, r9));
            }
            float sc, sh;
            solveTrim(dst, c.h, mask, slot, sc, sh, c.order, mode, from, to);
            f0 = f0 + k * sh;
            k = k * sc;
            float f1 = 31.0f - 31.0f * k;
            f1 = f1 - f0;
            start = f0 < -4.0f ? -4.0f : (f0 > 16.0f ? 16.0f : f0);
            end = f1 < -4.0f ? -4.0f : (f1 > 16.0f ? 16.0f : f1);
            r10 = div31(f2i(15.0f + 128.0f * start));
            r9 = div31(f2i(15.0f + 128.0f * end));
        }
        c.cut0 = b8(roundAway(start));
        c.cut1 = b8(roundAway(end));
        if (c.cut0 >= maxCut) {
            c.cut0 = b8(maxCut);
        }
        if (c.cut1 >= maxCut) {
            c.cut1 = b8(maxCut);
        }
    }
    s32 lim = tdivp2(shl(maxCut, 7), 5);
    if (rev) {
        if (r10 >= lim) {
            r10 = lim;
        }
        if (r9 >= lim) {
            r9 = lim;
        }
        trim(*dst.strokes[size_t(idx)], *src.strokes[size_t(idx)], r9, sub(0x80, r10));
    } else {
        if (r9 >= lim) {
            r9 = lim;
        }
        if (r10 >= lim) {
            r10 = lim;
        }
        trim(*dst.strokes[size_t(idx)], *src.strokes[size_t(idx)], r10, sub(0x80, r9));
    }
    if (mode == 0) {
        reweigh(dst, mask, scale);
    }
}

// ---- Candidate generation and rescoring -----------------------------------------
s32 cutPenalty(s32 a, s32 b) {
    if (a <= b) {
        a = b;
    }
    s32 v = mul(a, 20);
    return v >= 200 ? 200 : v;
} // 0205c810

// 0205aeb8: penalty for a fitted rotation outside the symbol's permitted window.
s32 rotationPenalty(s32 angle, const Head& h, s32 centre, s32 weight) {
    s32 cap = tdivp2(mul(1000, 3), 2); // 0205af68 holds 1000.
    s32 a = wrapAngle(sub(angle, centre), periodOf(h));
    const u8* w = rotationWindow(h);
    s32 p = 0;
    s32 lo = sub(rs16(w, 0), a);
    if (lo > 0) {
        p = add(p, tdivp2(mul(weight, mul(lo, rs16(w, 2))), 10));
    }
    s32 hi = sub(a, rs16(w, 4));
    if (hi > 0) {
        p = add(p, tdivp2(mul(weight, mul(hi, rs16(w, 6))), 10));
    }
    return p > cap ? cap : p;
}

void setCase(Candidate& c, s32 primary, s32 secondary) {
    c.caseKind = (primary && secondary) ? 2 : (primary ? 0 : (secondary ? 1 : 0));
} // 0205c8fc

void addCutPenalty(Candidate& c, s32 factor) {
    s32 a = c.cut0 < 0 ? 0 : c.cut0, b = c.cut1 < 0 ? 0 : c.cut1;
    c.penalty = h(add(mul(factor, add(a, b)), c.penalty));
} // 0205cdd4

// 0205cd7c: cut penalty factor and maximum cut from the best score.
void cutLimits(s32 best, s32& factor, s32& maxCut) {
    if (best <= 100) {
        best = 100;
    }
    if (best >= 600) {
        best = 600;
    }
    s32 v = sub(best, 100);
    auto d500 = [](s32 x) {
        return add(s32(u32(x) >> 31), s32((s64(0x10624dd3) * x) >> 37));
    };
    factor = sub(30, d500(mul(v, 25)));
    maxCut = add(d500(mul(v, 5)), 1);
}

// 0205c938: coarse match of a whole prototype against four trimmed variants.
void coarseCandidate(const Head& h, std::array<StrokeSet, 4>& sets, s32 n,
                     const std::array<s32, 2>& cuts, Candidate& out,
                     const std::array<s32, 8>& settings) {
    Candidate L;
    L.h = h;
    L.text = textOf(h);
    L.penalty = 0; // Fit fields are recomputed before use.
    out.score = 10000;
    std::array<const s8*, 8> orders{};
    s32 count = orderList(h, orders, 8);
    for (s32 o = 0; o < count; ++o) {
        std::memcpy(L.order.data(), orders[size_t(o)], h.strokes);
        for (s32 a = 0; a < n; ++a) {
            for (s32 b = 0; b < n; ++b) {
                L.cut0 = b8(cuts[size_t(a)]);
                L.cut1 = b8(cuts[size_t(b)]);
                matchCoarse(&L.score, &L.angle, sets[size_t(a * n + b)], h, L.order);
                L.score = h16(add(L.score, cutPenalty(L.cut0, L.cut1)), 0);
                L.score =
                    h16(add(L.score, rotationPenalty(L.angle, h, settings[2], settings[3])), 0);
                if (L.score < out.score) {
                    out = L;
                }
            }
        }
    }
}

// 0205cad8: best order of a part prototype; strokes is cleared during the match.
bool partCandidate(Head& h, const StrokeSet& parts, Candidate& out, const Candidate& base,
                   s32 offset, s32 n, const std::array<s32, 8>& settings) {
    u8 saved = h.strokes;
    h.strokes = 0;
    bool found = false;
    std::array<const s8*, 8> orders{};
    s32 count = orderList(h, orders, 8);
    for (s32 o = 0; o < count; ++o) {
        Order ord{};
        std::memcpy(ord.data(), orders[size_t(o)], 6);
        s16 score;
        s32 angle;
        matchCoarse(&score, &angle, parts, h, ord);
        score = h16(add(score, rotationPenalty(angle, h, settings[2], settings[3])), 0);
        if (score < out.score) {
            out.score = score;
            out.h = h;
            found = true;
            out.cut0 = base.cut0;
            out.cut1 = base.cut1;
            for (s32 j = 0; j < n; ++j) {
                out.order[size_t(offset + j)] = signedAdd(orders[size_t(o)][j], s8(offset));
            }
        }
    }
    h.strokes = saved;
    return found;
}

// 0205c788: centroid of the last n strokes mapped into the prototype frame.
void mappedCentroid(std::array<s32, 2>& out, const std::array<s32, 6>& fit, const StrokeSet& set,
                    s32 n) {
    s16 mx, my;
    centroid(set, set.count - n, set.count - 1, &mx, &my);
    out = {shl(mx, 5), shl(my, 5)};
    transform(out, out, fit);
    out[0] = tdivp2(out[0], 5);
    out[1] = tdivp2(out[1], 5);
}

// 0205cc24: stroke set view of strokes start..start+n.
void subset(const StrokeSet& set, s32 start, s32 n, StrokeSet& dst) {
    dst.count = n;
    dst.scale = 8;
    dst.mask = 0;
    for (s32 j = 0; j < n; ++j) {
        dst.strokes[size_t(j)] = set.strokes[size_t(start + j)];
    }
    sampleMoments(dst);
}

// 0205cc68: coarse score of a composed candidate with part offset (x,y).
void composedScore(Candidate& c, const StrokeSet& set, s32 x, s32 y,
                   const std::array<s32, 8>& settings) {
    StrokeSet L = set;
    Stroke32 first;
    L.strokes[0] = &first;
    c.text = textOf(c.h);
    c.h.partX = s16(x);
    c.h.partY = s16(y);
    trim(first, *set.strokes[0], tdivp2(shl(c.cut0, 7), 5), tdivp2(shl(sub(32, c.cut1), 7), 5));
    analyze(first, set.scale, nullptr);
    sampleMoments(L);
    matchCoarse(&c.score, &c.angle, L, c.h, c.order);
    c.score = h16(add(c.score, cutPenalty(c.cut0, c.cut1)), 0);
    c.score = h16(add(c.score, rotationPenalty(c.angle, c.h, settings[2], settings[3])), 0);
}

// 02065ac8/02065b48: prototype x/y extremes.
void protoRange(const Head& h, bool y, s8& lo, s8& hi) {
    hi = -128;
    lo = 127;
    for (s32 i = 0; i < s32(h.strokes); ++i) {
        const s8* p = shapeOf(h, i) + (y ? 32 : 0);
        for (s32 k = 0; k < 32; ++k) {
            if (hi < p[k]) {
                hi = p[k];
            }
            if (p[k] < lo) {
                lo = p[k];
            }
        }
    }
}

// 0205c830: does the list already hold a valid part of class `cls`?
bool listHasPart(CandidateList& l, s32 cls);

bool validHead(const Head& h) {
    if (!h.a || !h.b || !h.table || h.index < 0 || h.index >= s32(protoCount(h.a, h.strokes))) {
        return false;
    }
    if (h.partStrokes && (h.partIndex < 0 || h.partIndex >= s32(partCount(h.b, h.partStrokes)))) {
        return false;
    }
    return true;
}

bool listHasPart(CandidateList& l, s32 cls) {
    if (cls < 0) {
        return false;
    }
    for (auto* n = l.head; n; n = n->next) {
        const Head& h = n->cand->h;
        if (!validHead(h)) {
            continue;
        }
        s32 v = h.partStrokes ? s32(h.partRec[0]) : -1;
        if (cls == v) {
            return true;
        }
    }
    return false;
}

struct Bases {
    const u8* a;
    const u8* b;
    const u8* table;
}; // 0205be7c param_1

// 0205be7c: generate candidates for whole prototypes and composed (base+part) symbols.
void generate(const Head& dbs, CandidateList& list, const StrokeSet& set, CandidateArray* user,
              const std::array<s32, 8>& settings) {
    s32 maxParts = r16(dbs.b);
    if (maxParts >= set.count - 1) {
        maxParts = set.count - 1;
    }
    if (set.count > s32(dbs.a[1])) {
        return;
    }
    const std::array<s32, 2> cuts{0, 6}; // ov0 data 02065d54/02065d58
    std::array<StrokeSet, 4> sets{};
    std::array<Stroke32, 4> firsts{};
    for (s32 a = 0; a < 2; ++a) {
        for (s32 b = 0; b < 2; ++b) {
            s32 i = b + a * 2;
            StrokeSet& S = sets[size_t(i)];
            for (s32 j = 1; j < set.count; ++j) {
                S.strokes[size_t(j)] = set.strokes[size_t(j)];
            }
            S.strokes[0] = &firsts[size_t(i)];
            copySet(S, set);
            trim(firsts[size_t(i)], *set.strokes[0], tdivp2(shl(cuts[size_t(a)], 7), 5),
                 tdivp2(shl(sub(32, cuts[size_t(b)]), 7), 5));
            analyze(firsts[size_t(i)], set.scale, nullptr);
            sampleMoments(S);
        }
    }
    // The original keeps these records on its stack across iterations; fields
    // a search does not overwrite therefore persist exactly as here.
    Candidate whole, baseOut, C;
    CandidateArray none{nullptr, -1};
    ProtoIterator it;
    it.init(set.count, 0, dbs, &none);
    while (it.next()) {
        s32 p, q;
        categories(it.h, u32(settings[0]), u32(settings[1]), p, q);
        if (!p && !q) {
            continue;
        }
        coarseCandidate(it.h, sets, 2, cuts, whole, settings);
        setCase(whole, p, q);
        insert(list, add(whole.score, whole.penalty), whole, false);
    }
    for (s32 parts = 1; parts <= maxParts; ++parts) {
        s32 base = set.count - parts;
        std::array<Candidate, 4> store{};
        CandidateList l2;
        l2.init(store.data(), 4, 2);
        if (user) {
            ProtoIterator ui;
            ui.init(base, parts, dbs, &user[parts - 1]);
            while (ui.next()) {
                Candidate L = *ui.current();
                insert(l2, 0, L, false);
            }
        } else {
            ProtoIterator bi;
            bi.init(base, parts, dbs, &none);
            for (auto& S : sets) {
                S.count = base;
                sampleMoments(S);
            }
            while (bi.next()) {
                coarseCandidate(bi.h, sets, 2, cuts, baseOut, settings);
                insert(l2, add(baseOut.score, baseOut.penalty), baseOut, false);
            }
        }
        s16 cnt = h(l2.count);
        if (cnt <= 0) {
            continue;
        }
        compact(l2);
        CandidateArray arr2{store.data(), cnt};
        StrokeSet partSet{};
        subset(set, base, parts, partSet);
        s16 bx0, bx1, by0, by1, px0, px1, py0, py1, cx, cy;
        bounds(set, base, &bx0, &bx1, &by0, &by1);
        bounds(partSet, parts, &px0, &px1, &py0, &py1);
        centroid(partSet, 0, parts - 1, &cx, &cy);
        cx = h(sub(cx, bx0));
        cy = h(sub(cy, by1));
        ProtoIterator it2;
        it2.init(base, parts, dbs, &arr2);
        while (it2.next()) {
            Candidate* b = it2.current();
            C.score = 10000;
            C.penalty = 0;
            std::memcpy(C.order.data(), b->order.data(), it2.h.strokes);
            s8 xlo, xhi, ylo, yhi;
            protoRange(it2.h, false, xlo, xhi);
            protoRange(it2.h, true, ylo, yhi);
            PartIterator pi;
            pi.init(it2.h, parts);
            while (pi.next()) {
                if (user && listHasPart(list, s32(s8(pi.h.partStrokes ? pi.h.partRec[0] : 0xff)))) {
                    continue;
                }
                s32 p, q;
                categories(pi.h, u32(settings[0]), u32(settings[1]), p, q);
                if (!p && !q) {
                    continue;
                }
                bool found;
                s32 h8 = tdivp2(sub(by1, by0), 3);
                if (parts == 1 && pi.h.partIndex == 8 && sub(px1, px0) <= h8 &&
                    sub(py1, py0) <= h8 && ((pi.mask >> 8) & 1)) {
                    C.score = 1;
                    found = true;
                    C.h = pi.h;
                    C.cut0 = b->cut0;
                    C.cut1 = b->cut1;
                    C.order[size_t(base)] = b8(base + 1);
                } else {
                    found = partCandidate(pi.h, partSet, C, *b, base, parts, settings);
                }
                if (found) {
                    setCase(C, p, q);
                }
            }
            if (C.score == 10000 || sub(by1, by0) <= 0) {
                continue;
            }
            s32 unit = C.h.b[0x3a];
            s32 lowY = s16(neg(unit)), highY = s16(tdivp2(mul(unit, -5), 1));
            s32 dy;
            if (user) {
                C.h.strokes = u8(base);
                std::array<s32, 6> fit = fitOf(*b);
                if (periodOf(b->h) == 0) {
                    fit[1] = 0;
                }
                std::array<s32, 2> m;
                mappedCentroid(m, fit, set, parts);
                dy = s16(m[1]);
            } else {
                dy = s16(sdiv(mul(cy, sub(yhi, ylo)), sub(by1, by0)));
            }
            C.h.strokes = u8(base);
            if (dy >= lowY) {
                dy = lowY;
            }
            if (dy <= highY) {
                dy = highY;
            }
            s32 mid = s16(tdivp2(add(xhi, xlo), 1));
            if (mid >= xhi) {
                mid = xhi;
            }
            s32 dx = xlo >= mid ? s32(xlo) : mid;
            composedScore(C, set, s16(dx), s16(dy), settings);
            insert(list, add(C.score, C.penalty), C, false);
        }
    }
}

// 0205bbe8: size mismatch penalty (0..200).
s32 sizePenalty(s32 a, s32 b, s32 unit) {
    s32 q = tdivp2(unit, 2);
    a = add(a, q);
    b = add(b, q);
    s32 r = a > b ? sdiv(mul(sub(a, b), 200), b) : sdiv(mul(sub(b, a), 200), a);
    return r >= 200 ? 200 : r;
}

// 0205bc34: vertical position penalty (0..200).
s32 positionPenalty(s32 a, s32 b, s32 unit) {
    s32 r = sdiv(mul(sub(b, a), 200), unit);
    if (r < 0) {
        r = neg(r);
    }
    return r >= 200 ? 200 : r;
}

// 020652a4: a quotation mark must lie above the guide midpoint and be small.
bool smallQuote(s32 minX, s32 maxX, s32 minY, s32 maxY, const Head& h, s32 g1, s32 g2) {
    static const u8 quote[4] = {0x22, 0, 0, 0}; // ov0 data 02065eac
    if (textCompare(textOf(h), quote) != 0) {
        return false;
    }
    if (!(maxY < tdivp2(add(g1, g2), 1))) {
        return false;
    }
    s32 d = sub(g1, g2);
    return sub(maxY, minY) < d && sub(maxX, minX) < tdivp2(d, 1);
}

// 0205bc60: is the candidate implausibly small relative to the writing guides?
bool tooSmall(const StrokeSet& set, const Head& h, s32 g1, s32 g2) {
    s32 psx = protoSumX(h), psy = protoSumY(h), n = set.total;
    s32 m = protoMoment(h), unit = h.b[0x3a], d = sub(g1, g2);
    s32 v = sub(sub(mul(n, m), mul(psx, psx)), mul(psy, psy));
    s32 big = mul(n, mul(n, mul(unit, unit)));
    if (big > shl(v, 5)) {
        float f = 32.0f * set.norm;
        if (f < i2f(mul(n, mul(n, mul(d, d))))) {
            return true;
        }
    }
    s16 x0, x1, y0, y1;
    bounds(set, set.count, &x0, &x1, &y0, &y1);
    return smallQuote(x0, x1, y0, y1, h, g1, g2);
}

// 0205b9d4: letter-case decision and size/position penalties from the guides.
void guidePenalties(Candidate& c, const StrokeSet& set, const Head& h, s32 g1, s32 g2) {
    const u8* sym = symbolRec(h);
    s32 unit = h.b[0x3a];
    if (g1 == g2) {
        return;
    }
    s8 pylo, pyhi;
    protoRange(h, true, pylo, pyhi);
    s16 ylo, yhi;
    bounds(set, h.strokes, nullptr, nullptr, &ylo, &yhi);
    s32 d = sub(g1, g2);
    s32 mid = sdiv(mul(unit, sub(tdivp2(add(yhi, ylo), 1), g1)), d);
    s32 height = sdiv(mul(unit, sub(yhi, ylo)), d);
    s32 pmid = s16(tdivp2(add(pyhi, pylo), 1)), ph = s16(sub(pyhi, pylo));
    if (c.caseKind == 2) {
        if (sym[3] == 2) {
            if (neg(shl(sub(mid, pmid), 1)) <= unit) {
                c.caseKind = 0;
            } else {
                c.caseKind = 1;
                pmid = s16(sub(pmid, unit));
            }
        } else {
            s32 ph2 = s16(shl(ph, 1)), dd = sub(ph2, ph), ad = dd < 0 ? neg(dd) : dd;
            if (ad >= shl(sub(height, ph), 1)) {
                c.caseKind = 0;
            } else {
                pmid = s16(sub(pmid, tdivp2(dd, 1)));
                ph = ph2;
                c.caseKind = 1;
            }
        }
    } else if (c.caseKind == 1) {
        if (sym[3] == 2) {
            pmid = s16(sub(pmid, unit));
        } else {
            s32 ph2 = s16(shl(ph, 1));
            pmid = s16(sub(pmid, tdivp2(sub(ph2, ph), 1)));
            ph = ph2;
        }
    }
    if ((charFlags(h) & 3) < 3) {
        c.penalty = h16(add(c.penalty, s16(sizePenalty(ph, height, unit))), 0);
    }
    if ((charFlags(h) & 0x30) >= 3) {
        return;
    }
    c.penalty = h16(add(c.penalty, s16(positionPenalty(pmid, mid, unit))), 0);
}

// 0205b648: full rescoring of the pruned candidates with stroke-end refinement.
void rescore(CandidateList& out, const StrokeSet& set, s32 noPeriod, CandidateList& list,
             const s32* g1, const s32* g2) {
    Stroke32 first;
    StrokeSet L{};
    L.strokes[0] = &first;
    for (s32 i = 1; i < set.count; ++i) {
        L.strokes[size_t(i)] = set.strokes[size_t(i)];
    }
    copySet(L, set);
    L.scale = 8;
    s32 best = 2000;
    for (Candidate* c = list.first(nullptr); c; c = list.next(nullptr)) {
        std::array<s32, 6> f = fitOf(*c);
        matchFull(&c->score, &f, set, c->h, c->order);
        setFit(*c, f);
        c->penalty = h(rotationPenalty(c->angle, c->h, 0, 1000));
        s32 t = add(c->score, c->penalty);
        if (best >= t) {
            best = t;
        }
    }
    s32 factor, maxCut;
    cutLimits(best, factor, maxCut);
    for (Candidate* c = list.first(nullptr); c; c = list.next(nullptr)) {
        Head H = c->h;
        if (!validHead(H)) {
            continue;
        }
        Candidate C = *c;
        C.penalty = 0;
        refineEnds(C, 0, 8, 0, set, L, noPeriod, 0, 0, 0, maxCut);
        if (C.cut0 != 0 || C.cut1 != 0) {
            std::array<s32, 6> f = fitOf(C);
            matchFull(&C.score, &f, L, H, C.order);
            setFit(C, f);
            addCutPenalty(C, factor);
            C.penalty = h16(add(C.penalty, rotationPenalty(C.angle, H, 0, 1000)), 0);
            if (add(C.score, C.penalty) > add(c->score, c->penalty)) {
                C = *c;
            }
        } else {
            C = *c;
        }
        if (g1 && g2) {
            if (tooSmall(L, H, *g1, *g2)) {
                C.score = h(tdivp2(C.score, 1));
            }
            guidePenalties(C, L, H, *g1, *g2);
        }
        insert(out, add(C.score, C.penalty), C, true);
    }
}

void disambiguate(Candidate* cands, s32 count, const StrokeSet& set, s32 rotation, s32 weight,
                  s32 maxCut);

// 0205aba4: generate, prune, rescore and order the candidates.
s32 recognizeCore(Candidate* results, s32 capacity, StrokeSet& set, CandidateArray* user,
                  const std::array<s32, 8>& settings, const u8* db,
                  std::array<Candidate, 15>& pool) {
    s32 g1 = s16(settings[4] & 0xffff), g2 = s16(u32(settings[4]) >> 16);
    const u8* a = subA(db);
    s32 mode = (set.count == 1 || set.count == 2) ? 15 : 5;
    const s32* p1 = nullptr;
    const s32* p2 = nullptr;
    if (g1 != g2) {
        p1 = &g1;
        p2 = &g2;
    }
    if (settings[7]) {
        throw std::logic_error("custom category table is not used by this game");
    }
    const u8* table = subTable(a);
    for (s32 i = 0; i < capacity; ++i) {
        results[i] = Candidate{};
        results[i].score = 0x7fff;
        results[i].h.index = -1;
    } // 0205deb8
    if (isDot(set, g1, g2)) {
        s16 x0, x1, y0, y1;
        bounds(set, set.count, &x0, &x1, &y0, &y1);
        Head dot;
        if (dotPrototype(db, y1, g1, g2, dot, table)) {
            s32 p, q;
            categories(dot, u32(settings[0]), u32(settings[1]), p, q);
            if (p || q) {
                makeCandidate(results[0], set, 200, dot);
                if (p && q) {
                    results[0].caseKind = g1 == g2 ? 2 : (g2 < tdivp2(add(y0, y1), 1) ? 0 : 1);
                } else {
                    results[0].caseKind = q ? 1 : 0;
                }
                return 1;
            }
        }
    }
    CandidateList list;
    list.init(pool.data(), mode, 2);
    Head dbs;
    dbs.a = a;
    dbs.b = subB(db);
    dbs.table = table;
    generate(dbs, list, set, user, settings);
    if (settings[6]) {
        throw std::logic_error("user database is not used by this game");
    }
    s32 n = 0;
    if (list.count > 0) {
        list.keep(keepCount(list));
        CandidateList out;
        out.init(results, capacity, 1);
        rescore(out, set, user ? 1 : 0, list, p1, p2);
        n = compact(out);
        disambiguate(results, n, set, settings[2], settings[3], 4);
        for (s32 i = n - 1; i >= 0; --i) {
            results[i].penalty = h(sub(results[i].penalty, tdivp2(results[0].penalty, 1)));
        }
    }
    return n;
}

// 020654b4: pairwise rule for the top two candidates, if the database has one.
const u8* pairRule(const Candidate& c0, const Candidate& c1, s8& swapped) {
    u16 t0 = r16(textOf(c0.h)), t1 = r16(textOf(c1.h));
    if (c0.h.a[0] != 0 || c1.h.a[0] != 0) {
        return nullptr;
    }
    u16 lo = t1 < t0 ? t1 : t0, hi = t1 < t0 ? t0 : t1;
    s32 row = c0.h.strokes + c0.h.partStrokes - 1;
    const u8* table = rel(c0.h.b, 0x2c);
    u32 n = r16(table, row * 8);
    const u8* r = c0.h.b + r32(table, row * 8 + 4);
    for (u32 i = 0; i < n; ++i, r += 12) {
        if (r16(r) == lo && r16(r, 2) == hi) {
            swapped = ((r[4] != 0 || t0 <= t1) && (r[4] != 1 || t1 < t0)) ? 0 : 1;
            return r;
        }
    }
    return nullptr;
}

// ---- Pairwise disambiguation (02060c88..020646f8) ------------------------------
// Some pairs of symbols are easily confused (a "9" and a "4", say). The
// database lists such pairs (pairRule); for a listed pair the rule decides
// from the shape of the input whether the two best candidates change places.
// Most rules look at the first stroke as point and step arrays with y pointing
// up and follow it from turn to turn. Function comments cite overlay 0 of the
// second adventure, with the fourth adventure's overlay 2 address in brackets;
// both builds were compared and use the same constants and calls.

double i2d(s32 v) {
    return double(v);
} // 02039948

double u2d(u32 v) {
    return double(v);
} // 02039988

// 02039864 [L4 020293d8]: truncate; negative values give 0, large ones saturate.
u32 d2u(double d) {
    if (std::isnan(d)) {
        return std::signbit(d) ? 0xffffffffu : 0;
    }
    if (d <= 0.0) {
        return 0;
    }
    return d >= 4294967296.0 ? 0xffffffffu : u32(d);
}

s32 sign16(s32 v) {
    return v < 0 ? -1 : (v > 0 ? 1 : 0);
} // 020641f0

bool sameSign(s32 a, s32 b) {
    return sign16(a) == sign16(b);
} // 02064178

// 02064108: do n consecutive values all have the same sign?
bool steady(const s16* v, s32 n) {
    for (s32 k = 1; k < n; ++k) {
        if (!sameSign(v[k], v[k - 1])) {
            return false;
        }
    }
    return true;
}

// 02064644, 02064684, 020646c4, 020646f8: first index of the minimum or
// maximum, and the minimum or maximum itself (n >= 1 values are read).
s32 argMin(const s16* v, s32 n) {
    s32 best = v[0], at = 0;
    for (s32 i = 1; i < n; ++i) {
        if (v[i] < best) {
            best = v[i];
            at = i;
        }
    }
    return at;
}

s32 argMax(const s16* v, s32 n) {
    s32 best = v[0], at = 0;
    for (s32 i = 1; i < n; ++i) {
        if (v[i] > best) {
            best = v[i];
            at = i;
        }
    }
    return at;
}

s32 minOf(const s16* v, s32 n) {
    return v[argMin(v, n)];
}

s32 maxOf(const s16* v, s32 n) {
    return v[argMax(v, n)];
}

// One input stroke as points and steps with y pointing up. Entry 32 of x and
// y is only filled by rule 12, which can read one point past the end.
struct Outline {
    std::array<s16, 33> x{}, y{};
    std::array<s16, 32> dx{}, dy{};
};

s16 flip(s16 v) {
    return h(neg(v <= -0x7fff ? -0x7fff : v));
}

// 0206344c [020595cc]: the stroke in drawing order.
Outline forward(const Stroke32& s) {
    Outline o;
    for (u32 i = 0; i < 32; ++i) {
        o.x[i] = s.x[i];
        o.y[i] = flip(s.y[i]);
        o.dx[i] = s.dx[i];
        o.dy[i] = flip(s.dy[i]);
    }
    return o;
}

// 02063500 [0205967c]: the stroke from its end; the steps change direction.
Outline backward(const Stroke32& s) {
    Outline o;
    o.x[31] = s.x[0];
    o.y[31] = flip(s.y[0]);
    o.dx[31] = flip(s.dx[0]);
    o.dy[31] = s.dy[0];
    for (u32 i = 1; i < 32; ++i) {
        o.x[31 - i] = s.x[i];
        o.y[31 - i] = flip(s.y[i]);
        o.dx[31 - i] = flip(s.dx[i - 1]);
        o.dy[31 - i] = s.dy[i - 1];
    }
    return o;
}

// 02063fb8 [0205a118]: is there a sharp corner at point i? Compares the
// change of step with the lengths of the two steps (all scaled to stay in
// range); `limit` is in 1/64 units.
bool sharpTurn(const Outline& o, s32 i, u32 limit) {
    if (u32(i) >= 32 || i == 0 || i == 31) {
        return false;
    }
    s32 x0 = o.dx[size_t(i - 1)], y0 = o.dy[size_t(i - 1)], x1 = o.dx[size_t(i)],
        y1 = o.dy[size_t(i)];
    u32 before = u32(add(mul(x0, x0), mul(y0, y0))), after = u32(add(mul(x1, x1), mul(y1, y1)));
    u32 change = u32(add(mul(sub(x1, x0), sub(x1, x0)), mul(sub(y1, y0), sub(y1, y0))));
    u32 range = 0x40000, unit = 0x40, factor = 0x1000;
    while (before > range || after > range || change > range) {
        unit >>= 1;
        factor >>= 2;
        range <<= 2;
        if (!unit) {
            return false;
        }
    }
    s32 sum = add(isqrt(factor * before), isqrt(factor * after));
    if (sum == 0) {
        return false;
    }
    return udiv(unit * u32(isqrt(factor * change)), u32(sum)) > limit;
}

// First point after the corners at the start of the stroke.
s32 skipCorners(const Outline& o) {
    s32 i = 0;
    while (sharpTurn(o, i + 1, 50)) {
        ++i;
    }
    return i;
}

struct Box {
    s16 x0 = -0x8000, x1 = 0x7fff, y0 = -0x8000, y1 = 0x7fff;

    bool inside(s32 x, s32 y) const {
        return x > x0 && x < x1 && y > y0 && y < y1;
    } // 020640b0 answers the opposite (outside).
};

enum class Along { x, y };

// 0206372c [020598a4] and its wrappers 02063614 (x, -1), 02063660 (x, +1),
// 020636ac (y, +1), 020636ec (y, -1): follow the stroke from point i while
// it stays in the box and the steps along one axis keep the given sign.
// With `turns`, a single wobble (one or two steps of the other sign while
// the other axis keeps its direction) does not stop the walk.
s32 walk(const Outline& o, s32 i, Along along, s32 sign, Box box = {}, bool turns = true) {
    const auto& d = along == Along::x ? o.dx : o.dy;
    const auto& e = along == Along::x ? o.dy : o.dx;
    if (i < 0) {
        return 0;
    }
    auto wobble = [&](s32 back, s32 n) {
        return sameSign(d[size_t(i - 1)], d[size_t(i + back)]) &&
               !sameSign(d[size_t(i - 1)], d[size_t(i)]) && steady(&e[size_t(i - 1)], n) &&
               sign16(d[size_t(i - 1)]) != 0;
    };
    while (i < 31 && box.inside(o.x[size_t(i)], o.y[size_t(i)])) {
        bool go = mul(d[size_t(i)], sign) >= 0;
        if (!go && turns) {
            if (i <= 0 || i >= 30) {
                break;
            }
            go = wobble(1, 3);
            if (i < 29) {
                go = wobble(2, 4) || go;
            }
        }
        if (!go) {
            break;
        }
        ++i;
    }
    return i >= 31 ? 31 : i;
}

// 02063924 [02059a9c]: area between the stroke and its chord from point
// `from` to `to` (trapezoids of the distance from the chord). The sign
// follows the side of the chord; `keepSign` false reverses it. `flat` is set
// when nothing could be measured.
double chordArea(const Outline& o, s32 from, s32 to, bool keepSign, bool& flat) {
    flat = true;
    if (from < 0 || to >= 32) {
        return 0.0;
    }
    double dx = i2d(sub(o.x[size_t(to)], o.x[size_t(from)]));
    double dy = i2d(sub(o.y[size_t(to)], o.y[size_t(from)]));
    if (dx == 0.0 && dy == 0.0) {
        return 0.0;
    }
    flat = false;
    double len = u2d(u32(isqrt(d2u(dx * dx + dy * dy))));
    double area = 0.0, lastT = 0.0, lastD = 0.0;
    for (s32 k = from + 1; k <= to; ++k) {
        double px = i2d(sub(o.x[size_t(k)], o.x[size_t(from)]));
        double py = i2d(sub(o.y[size_t(k)], o.y[size_t(from)]));
        double t = (px * dx + py * dy) / len;
        if (t <= 0.0 || !(t <= len)) {
            area = area + 0.0;
            continue;
        }
        double ex = dx * t / len, ey = dy * t / len;
        double d = (ex * (py - ey) - ey * (px - ex)) / t;
        double side = d + lastD;
        side = side > 0.0 ? 1.0 : (side < 0.0 ? -1.0 : 0.0); // 020641a0
        double width = t - lastT;
        width = width < 0.0 ? 0.0 - width : width;
        double mean = d + lastD;
        mean = (mean < 0.0 ? 0.0 - mean : mean) / 2.0;
        if (!keepSign) {
            side = 0.0 - side;
        }
        area = area + width * (side * mean);
        lastT = t;
        lastD = d;
    }
    return area;
}

// 02063e14 [02059f74]: area of the triangle through points i, j and k.
double triangleArea(const Outline& o, s32 i, s32 j, s32 k, bool& flat) {
    flat = true;
    if (i < 0 || j < 0 || k < 0 || i >= 32 || j >= 32 || k >= 32) {
        return 0.0;
    }
    s32 xi = o.x[size_t(i)], xj = o.x[size_t(j)], xk = o.x[size_t(k)];
    s32 v = add(add(mul(o.y[size_t(i)], sub(xk, xj)), mul(sub(xi, xk), o.y[size_t(j)])),
                mul(o.y[size_t(k)], sub(xj, xi)));
    flat = false;
    return 0.5 * i2d(v < 0 ? neg(v) : v);
}

// 02063ed8 [0205a038]: widest horizontal gap between the points before `mid`
// and the points after it at the nearest height (to <= 31).
s32 widestGap(const Outline& o, s32 from, s32 to, s32 mid) {
    if (from < 0 || to >= 32 || from > to || to <= mid || from >= mid) {
        return 0;
    }
    s32 widest = 0;
    for (s32 i = from; i < mid; ++i) {
        s32 nearest = 0x7fffffff, at = 0;
        for (s32 j = mid + 1; j <= to; ++j) {
            s32 d = sub(o.y[size_t(i)], o.y[size_t(j)]);
            d = d < 0 ? neg(d) : d;
            if (d < nearest) {
                nearest = d;
                at = j;
            }
        }
        s32 w = sub(o.x[size_t(i)], o.x[size_t(at)]);
        w = w < 0 ? neg(w) : w;
        if (w > widest) {
            widest = w;
        }
    }
    return widest;
}

s32 abs32(s32 v) {
    return v < 0 ? neg(v) : v;
}

// Truncating division by ten as compiled (multiply by 0x66666667).
s32 div10(s32 v) {
    return add(s32(u32(v) >> 31), asr(s32((s64(0x66666667) * v) >> 32), 2));
}

// What a rule sees (stack record built by 020608c0).
struct RuleContext {
    s32 mask;               // rule +8: point-weighting pattern for refits.
    u8 scale;               // rule +6: step weight for refits.
    s32 swapped;            // 1 when the pair is listed the other way round.
    const StrokeSet& input; // The normalized input.
    Candidate* cands;       // The two best candidates.
    StrokeSet& work;        // Copy of the input for refits (scale 8).
    s32 maxCut;             // Largest end trim for refits.
};

// Shared ending of rules 0 and 1: back up from point c over a gentle
// down-left tail, then compare the width of the opening with the widest gap.
s32 openingRule(const RuleContext& ctx, const Outline& o, s32 a, s32 b, s32 c, bool sameStart,
                s32 limit) {
    for (;;) {
        s32 d = o.dx[size_t(c)], e = o.dy[size_t(c)];
        if (d >= 0 || e <= 0 || sdiv(mul(abs32(e), 10000), add(mul(abs32(d), 100), 1)) >= 100) {
            break;
        }
        if (--c < 0) {
            return 0;
        }
    }
    if (!(o.y[size_t(a)] > o.y[size_t(b)] && o.y[size_t(c)] > o.y[size_t(b)])) {
        return 0;
    }
    if (!((sameStart ? a <= b : a < b) && b < c)) {
        return 0;
    }
    if (c < 16) { // The original compares the byte offset 2c with 32.
        return 0;
    }
    s32 width = abs32(sub(o.x[size_t(c)], o.x[size_t(a)]));
    s32 gap = widestGap(o, a, c, b);
    if (gap == 0) {
        return 0;
    }
    return sdiv(mul(width, 100), gap) <= limit ? ctx.swapped : !ctx.swapped;
}

// The end of the bowl that starts at the turn b: up to 70% of the rise from b
// to a, then while left of a.
s32 bowlEnd(const Outline& o, s32 a, s32 b) {
    s16 y1 = h(add(o.y[size_t(b)], div10(mul(sub(o.y[size_t(a)], o.y[size_t(b)]), 7))));
    s32 c = walk(o, b, Along::y, 1, Box{-0x8000, 0x7fff, -0x8000, y1});
    Box left{-0x8000, o.x[size_t(a)], -0x8000, 0x7fff};
    c = walk(o, c, Along::y, 1, left);
    if (!sharpTurn(o, c, 80)) {
        c = walk(o, c, Along::x, 1, left);
    }
    return c;
}

// 02061b20 [02057dc4] rule 0.
s32 rule0(const RuleContext& ctx) {
    if (!ctx.input.strokes[0]) {
        return 0;
    }
    Outline o = backward(*ctx.input.strokes[0]);
    s32 a = walk(o, walk(o, skipCorners(o), Along::y, -1), Along::y, 1);
    if (a < 0 || a >= 31) {
        return 0;
    }
    while (sharpTurn(o, a + 1, 50)) {
        ++a;
    }
    if (a < 0 || a >= 32) {
        return 0;
    }
    s32 b = walk(o, a, Along::y, -1);
    return openingRule(ctx, o, a, b, bowlEnd(o, a, b), false, 55);
}

// 02061f38 [02058190] rule 1.
s32 rule1(const RuleContext& ctx) {
    if (!ctx.input.strokes[0]) {
        return 0;
    }
    Outline o = backward(*ctx.input.strokes[0]);
    s32 lo = argMin(o.y.data(), 32), hi = argMax(o.y.data(), 32);
    s32 i = 0;
    while (i < 30 && (sharpTurn(o, i + 1, 50) || sharpTurn(o, i + 2, 50))) {
        ++i;
    }
    i = walk(o, walk(o, walk(o, i, Along::y, -1), Along::x, -1), Along::y, -1);
    s32 ylo = o.y[size_t(lo)], span = sub(o.y[size_t(hi)], ylo);
    s16 y1 = h(add(ylo, div10(mul(span, 3))));
    i = walk(o, i, Along::y, 1, Box{-0x8000, 0x7fff, -0x8000, y1});
    while (i < 32 &&
           sdiv(mul(sub(o.y[size_t(i)], ylo), 10000), add(mul(abs32(span), 1000), 1)) < 4) {
        ++i;
    }
    s32 a = walk(o, i, Along::y, 1, {}, false);
    s32 b = walk(o, a, Along::y, -1);
    return openingRule(ctx, o, a, b, bowlEnd(o, a, b), true, 60);
}

// Lowest point of the first dip after point a (ties: the last of a flat run).
s32 dipAfter(const Outline& o, s32 a, s32 end) {
    s32 m = minOf(&o.y[size_t(a)], 32 - a), b = a;
    while (b < end && o.y[size_t(b)] != m) {
        ++b;
    }
    while (b < end && o.y[size_t(b)] <= m) {
        ++b;
    }
    return b > 0 ? b - 1 : b;
}

// 02062460 [02058628] rule 2.
s32 rule2(const RuleContext& ctx) {
    if (!ctx.input.strokes[0]) {
        return 0;
    }
    Outline o = forward(*ctx.input.strokes[0]);
    s32 a = walk(o, skipCorners(o), Along::y, 1);
    s32 b = dipAfter(o, a, 31);
    if (!(o.y[size_t(b)] <= o.y[size_t(a)] && o.y[size_t(b)] <= o.y[31] &&
          o.x[size_t(a)] < o.x[31])) {
        return 0;
    }
    if (b <= a || b >= 31) {
        return 0;
    }
    bool flat;
    double tri = triangleArea(o, a, b, 31, flat);
    double first = chordArea(o, a, b, true, flat), second = chordArea(o, b, 31, true, flat);
    if (flat) {
        return 0;
    }
    double rest = tri - second, inner = second;
    if (first > 0.0) {
        inner = second + 0.5 * first;
        rest = rest - first;
    }
    if (rest <= 0.0) {
        return ctx.swapped;
    }
    if (inner / rest > 0.38 && second > 0.0) {
        return ctx.swapped;
    }
    return !ctx.swapped;
}

// 020627b0 [02058964] rule 3.
s32 rule3(const RuleContext& ctx) {
    if (!ctx.input.strokes[0]) {
        return 0;
    }
    Outline o = forward(*ctx.input.strokes[0]);
    s32 a = walk(o, skipCorners(o), Along::y, 1);
    s32 b = dipAfter(o, a, 32);
    s32 base = o.y[size_t(b)], rise = sub(maxOf(o.y.data(), 32), base);
    if (rise == 0) {
        return 0;
    }
    s32 c = b; // First point that climbed half the rise (0/rise < 0.5 always holds).
    if (i2d(0) / i2d(rise) < 0.5) {
        while (c != 31) {
            ++c;
            if (!(i2d(sub(o.y[size_t(c)], base)) / i2d(rise) < 0.5)) {
                break;
            }
        }
    }
    c = walk(o, c, Along::y, 1);
    if (!(o.y[size_t(b)] <= o.y[size_t(a)] && o.y[size_t(b)] <= o.y[size_t(c)] &&
          o.x[size_t(a)] < o.x[size_t(c)])) {
        return 0;
    }
    if (!(b > a && c > b)) {
        return 0;
    }
    bool flat;
    double tri = triangleArea(o, a, b, c, flat);
    double first = chordArea(o, a, b, false, flat), second = chordArea(o, b, c, false, flat);
    double whole = second + (tri + first);
    if (flat || tri == 0.0) {
        return 0;
    }
    if (whole / tri > 1.5 && first > 0.0 && second > 0.0) {
        return ctx.swapped;
    }
    return !ctx.swapped;
}

// 02060d7c [020570d0] rule 4 and 02060f7c [020572e4] rule 7 first look the
// candidates' prototypes up in a list that is empty in both builds, so they
// always return without exchanging the candidates.
s32 noRule(const RuleContext&) {
    return 0;
}

// 020610f0 [02057470] rule 5: the pair is always written in database order.
s32 rule5(const RuleContext& ctx) {
    return ctx.swapped;
}

// 02060c88 [02056fdc] rule 6: refit the ends of both candidates and compare
// the weighted matches.
s32 rule6(const RuleContext& ctx) {
    std::array<s32, 2> total{};
    for (u32 k = 0; k < 2; ++k) {
        Candidate c = ctx.cands[k];
        c.penalty = 0;
        refineEnds(c, ctx.mask, ctx.scale, 0, ctx.input, ctx.work, 0, 0, 0, 0, ctx.maxCut);
        std::array<s32, 6> f = fitOf(c);
        matchWeighted(&c.score, &f, ctx.work, ctx.cands[k].h, ctx.mask, ctx.scale, c.order);
        total[k] = add(c.score, c.penalty);
    }
    return total[0] > total[1] ? 1 : 0;
}

// 02061104 [02057484] rule 8: trim both candidates' first strokes by the
// smaller common cuts and compare the weighted matches.
s32 rule8(const RuleContext& ctx) {
    auto common = [](s32 a, s32 b) {
        s32 m = a < b ? a : b;
        return m <= 0 ? 0 : m;
    };
    s32 cut0 = common(ctx.cands[0].cut0, ctx.cands[1].cut0);
    s32 cut1 = common(ctx.cands[0].cut1, ctx.cands[1].cut1);
    std::array<s32, 2> total{};
    for (u32 k = 0; k < 2; ++k) {
        Candidate c = ctx.cands[k];
        c.penalty = 0;
        c.cut0 = b8(c.cut0 < cut0 ? c.cut0 : cut0);
        c.cut1 = b8(c.cut1 < cut1 ? c.cut1 : cut1);
        s32 start = div31(add(shl(c.cut0, 7), 15)), end = div31(add(shl(c.cut1, 7), 15));
        s32 s = abs32(c.order[0]) - 1;
        trim(*ctx.work.strokes[size_t(s)], *ctx.input.strokes[size_t(s)], start, sub(0x80, end));
        reweigh(ctx.work, ctx.mask, ctx.scale);
        matchWeighted(&c.score, nullptr, ctx.work, c.h, ctx.mask, ctx.scale, c.order);
        total[k] = add(c.score, c.penalty);
    }
    return total[0] > total[1] ? 1 : 0;
}

// Four alternating vertical turns of a single stroke (rules 9 and 11).
bool fourTurns(const Outline& o, std::array<s32, 4>& p) {
    p[0] = walk(o, skipCorners(o), Along::y, -1);
    p[1] = walk(o, p[0], Along::y, 1);
    p[2] = walk(o, p[1], Along::y, -1);
    p[3] = walk(o, p[2], Along::y, 1);
    for (s32 i = 3; i >= 0; --i) {
        if (p[size_t(i)] < 0 || p[size_t(i)] >= 32) {
            return false;
        }
    }
    if (!(p[3] > p[2] && p[2] > p[1] && p[1] > p[0])) {
        return false;
    }
    s32 y0 = o.y[size_t(p[0])], y1 = o.y[size_t(p[1])], y2 = o.y[size_t(p[2])],
        y3 = o.y[size_t(p[3])];
    return y3 > y2 && y3 > y0 && y1 > y2 && y1 > y0;
}

// 02061858 [02057b48] rule 9.
s32 rule9(const RuleContext& ctx) {
    if (!ctx.input.strokes[0] || ctx.input.count > 1) {
        return 0;
    }
    Outline o = backward(*ctx.input.strokes[0]);
    std::array<s32, 4> p{};
    if (!fourTurns(o, p) || !(o.x[size_t(p[0])] > o.x[size_t(p[2])])) {
        return 0;
    }
    s32 y1 = o.y[size_t(p[1])], y2 = o.y[size_t(p[2])], y3 = o.y[size_t(p[3])];
    s32 d = sub(y3, y2);
    if (d == 0) {
        return 0;
    }
    return sdiv(mul(sub(y1, y2), 100), d) <= 68 ? ctx.swapped : !ctx.swapped;
}

// 020612c8 [02057640] rule 10.
s32 rule10(const RuleContext& ctx) {
    if (!ctx.input.strokes[0] || ctx.input.count > 1) {
        return 0;
    }
    Outline o = backward(*ctx.input.strokes[0]);
    s32 p0 = walk(o, walk(o, skipCorners(o), Along::x, 1), Along::x, -1);
    s32 p1 = walk(o, p0, Along::x, 1), p2 = walk(o, p1, Along::x, -1);
    for (s32 v : {p2, p1, p0}) {
        if (v < 0 || v >= 32) {
            return 0;
        }
    }
    if (!(p2 > p1 && p1 > p0)) {
        return 0;
    }
    if (!(o.y[size_t(p2)] > o.y[size_t(p0)] && o.y[size_t(p1)] > o.y[size_t(p0)] &&
          o.x[size_t(p1)] > o.x[size_t(p2)] && o.x[size_t(p1)] > o.x[size_t(p0)])) {
        return 0;
    }
    bool flat;
    double area = chordArea(o, p1, p2, false, flat);
    double tri = triangleArea(o, p0, p1, p2, flat);
    if (flat || tri <= 0.0) {
        return 0;
    }
    return !(area / tri < 0.4) ? ctx.swapped : 0;
}

// 020615bc [020578f8] rule 11.
s32 rule11(const RuleContext& ctx) {
    if (!ctx.input.strokes[0] || ctx.input.count > 1) {
        return 0;
    }
    Outline o = backward(*ctx.input.strokes[0]);
    std::array<s32, 4> p{};
    if (!fourTurns(o, p)) {
        return 0;
    }
    s32 y0 = o.y[size_t(p[0])], y1 = o.y[size_t(p[1])], y2 = o.y[size_t(p[2])],
        y3 = o.y[size_t(p[3])];
    return sub(y2, y0) > sub(y3, y1) ? ctx.swapped : !ctx.swapped;
}

// Length of the stroke between points from and to (from <= to).
double pathLength(const Outline& o, s32 from, s32 to, bool downwards) {
    double sum = 0.0;
    s32 k = downwards ? to : from;
    s32 px = o.x[size_t(k)], py = o.y[size_t(k)];
    for (; downwards ? k >= from : k <= to; k += downwards ? -1 : 1) {
        s32 dy = sub(py, o.y[size_t(k)]), dx = sub(px, o.x[size_t(k)]);
        sum = sum + u2d(u32(isqrt(u32(add(mul(dx, dx), mul(dy, dy))))));
        px = o.x[size_t(k)];
        py = o.y[size_t(k)];
    }
    return sum;
}

// 02062bb4 [02058d54] rule 12: works on the taller of the first two strokes.
s32 rule12(const RuleContext& ctx) {
    const Stroke32* s = ctx.input.strokes[0];
    if (!s) {
        return 0;
    }
    Outline o = forward(*s);
    s32 lo = argMin(o.y.data(), 32), hi = argMax(o.y.data(), 32);
    double height = i2d(abs32(sub(o.y[size_t(hi)], o.y[size_t(lo)])));
    if (ctx.input.count > 1) {
        Outline second = forward(*ctx.input.strokes[1]);
        s32 l2 = argMin(second.y.data(), 32), h2 = argMax(second.y.data(), 32);
        s32 d = abs32(sub(second.y[size_t(h2)], second.y[size_t(l2)]));
        if (i2d(d) > height) {
            height = i2d(d);
            s = ctx.input.strokes[1];
            o = second;
            lo = l2;
            hi = h2;
        }
    }
    if (o.y[0] < o.y[31]) {
        o = backward(*s);
        lo = argMin(o.y.data(), 32);
        hi = argMax(o.y.data(), 32);
    }
    // Past the last point the original reads its neighbouring stack slots:
    // y[32] is x[0], and x[32] is the saved rule step weight.
    o.y[32] = o.x[0];
    o.x[32] = ctx.scale;
    s32 ylo = o.y[size_t(lo)], m = hi;
    for (;;) {
        s32 v = sub(o.y[size_t(m)], ylo);
        double d = i2d(v) < 0.0 ? 0.0 - i2d(v) : i2d(v);
        if (!(d > 0.5 * height) || m >= 32) {
            break;
        }
        ++m;
    }
    s32 right = argMax(o.x.data(), 32 - m);
    s32 left = m + argMin(&o.x[size_t(m)], 32 - m);
    double before = pathLength(o, 0, hi, true), after = pathLength(o, lo, 31, false);
    bool flat;
    double head = chordArea(o, right, m, false, flat);
    double tail = chordArea(o, m, 31, false, flat);
    if (flat) {
        return 0;
    }
    s32 xm = o.x[size_t(m)];
    if (!((tail < 0.0 ? 0.0 - tail : tail) >= 1.0)) {
        if (before > 0.2 * height) {
            return ctx.swapped;
        }
        s32 lean = abs32(sub(o.x[size_t(right)], xm));
        if (i2d(lean) > 0.15 * height) {
            return ctx.swapped;
        }
        if (i2d(lean) > 0.1 * height && i2d(sub(xm, o.x[size_t(left)])) < -0.05 * height) {
            return ctx.swapped;
        }
        return !ctx.swapped;
    }
    double size = tail < 0.0 ? 0.0 - tail : tail;
    if (head / size > 1.7 && before > 0.1 * height) {
        return ctx.swapped;
    }
    if (head / size > 2.0 && i2d(sub(o.x[size_t(right)], xm)) > 0.1 * height) {
        return ctx.swapped;
    }
    if (head < 0.0) {
        return !ctx.swapped;
    }
    if (after > 0.2 * height && i2d(sub(o.x[size_t(left)], xm)) < -0.1 * height) {
        return ctx.swapped;
    }
    return !ctx.swapped;
}

using Rule = s32 (*)(const RuleContext&);

// 02060ba4 [02056ef8]: rule by index.
Rule ruleFunction(u8 index) {
    static constexpr std::array<Rule, 13> kRules{rule0,  rule1, rule2, rule3,  noRule, rule5, rule6,
                                                 noRule, rule8, rule9, rule10, rule11, rule12};
    return index < kRules.size() ? kRules[index] : nullptr;
}

// 020608c0 [02056c14]: if the two best candidates form a listed pair, let the
// pair's rule decide their order. Only the records change places; the scores
// stay with the ranks.
void disambiguate(Candidate* c, s32 n, const StrokeSet& set, s32 rotation, s32 weight, s32 maxCut) {
    if (n < 2 || !validHead(c[0].h) || !validHead(c[1].h)) {
        return;
    }
    if (rotation != 0 || weight <= 0) {
        return;
    }
    if (sharesPart(sharedPart(c[1]), c[0])) {
        return;
    }
    s8 swapped = 0;
    const u8* rule = pairRule(c[0], c[1], swapped);
    if (!rule) {
        return;
    }
    Rule fn = ruleFunction(rule[5]);
    if (!fn) {
        return;
    }
    std::array<Stroke32, 6> strokes{};
    StrokeSet work{};
    for (u32 i = 0; i < 6; ++i) {
        work.strokes[i] = &strokes[i];
    }
    copySet(work, set);
    work.scale = 8;
    RuleContext ctx{r32(rule, 8), rule[6], swapped, set, c, work, maxCut};
    if (fn(ctx) == 1) {
        std::swap(c[0], c[1]);
        std::swap(c[0].score, c[1].score);
        std::swap(c[0].penalty, c[1].penalty);
    }
}

// 0205a654: normalize the resampled strokes and run the recognizer core.
s32 normalizeAndRecognize(Candidate* results, s32 capacity, const std::array<s32, 8>& settingsIn,
                          const u8* db, const std::array<const Point*, 6>& strokes, s32 count) {
    std::array<s32, 8> settings = settingsIn;
    std::array<Stroke32, 6> norm{};
    StrokeSet set{};
    set.count = count;
    s32 lx = 0x7fff, ly = 0x7fff, hx = -0x8000, hy = -0x8000;
    for (s32 i = 0; i < count; ++i) {
        for (s32 k = 0; k < 32; ++k) {
            s32 x = strokes[size_t(i)][k].x, y = strokes[size_t(i)][k].y;
            if (x < lx) {
                lx = x;
            }
            if (y < ly) {
                ly = y;
            }
            if (x > hx) {
                hx = x;
            }
            if (y > hy) {
                hy = y;
            }
        }
    }
    if (lx == 0x7fff) {
        lx = 0;
    }
    if (ly == 0x7fff) {
        ly = 0;
    }
    if (hx == -0x8000) {
        hx = 0;
    }
    if (hy == -0x8000) {
        hy = 0;
    }
    s32 ext = sub(hx, lx);
    if (ext <= sub(hy, ly)) {
        ext = sub(hy, ly);
    }
    u32 sh = 0;
    if (ext > 1000) {
        sh = 1;
        while (asr(ext, sh) > 1000) {
            sh = (sh + 1) & 0xff;
        }
    }
    settings[1] |= 1;
    s16 g1 = s16(settings[4] & 0xffff), g2 = s16(u32(settings[4]) >> 16);
    if (g2 != 0 || g1 != 0) {
        s16 d = h(sub(ly, 1));
        g2 = h(sub(g2, d));
        g1 = h(sub(g1, d));
        g2 = h(asr(g2, sh));
        g1 = h(asr(g1, sh));
        settings[4] = s32(u32(u16(g1)) | (u32(u16(g2)) << 16));
    }
    for (s32 i = 0; i < count; ++i) {
        set.strokes[size_t(i)] = &norm[size_t(i)];
        for (s32 k = 0; k < 32; ++k) {
            norm[size_t(i)].x[size_t(k)] = h(asr(h(sub(strokes[size_t(i)][k].x, sub(lx, 1))), sh));
            norm[size_t(i)].y[size_t(k)] = h(asr(h(sub(strokes[size_t(i)][k].y, sub(ly, 1))), sh));
        }
        analyze(norm[size_t(i)], 8, nullptr);
    }
    summarize(set, 0, 8);
    std::array<Candidate, 15> pool{};
    for (s32 i = 0; i < capacity; ++i) {
        results[i] = Candidate{}; // 0205de8c
    }
    if (capacity > 0) {
        results[0].h.index = -1;
    }
    s32 n = recognizeCore(results, capacity, set, nullptr, settings, db, pool);
    for (s32 i = 0; i < n; ++i) {
        results[i].cx = shl(results[i].cx, sh);
        results[i].cy = shl(results[i].cy, sh);
        u32 sc = u32(results[i].scale) >> sh;
        results[i].scale = s32(sc <= 1 ? 1 : sc);
        results[i].cx = add(results[i].cx, shl(lx, 5));
        results[i].cy = add(results[i].cy, shl(ly, 5));
    }
    return n;
}
} // namespace

namespace {
// 02064f40: case folding used for upper-case results.
u16 upper(u32 c) {
    if (c >= 0x61 && c <= 0x7a) {
        return u16(c + 0xffe0);
    }
    if (c >= 0xe0 && c <= 0xf6) {
        return u16(c - 0x20);
    }
    if (c >= 0xf8 && c <= 0xfe) {
        return u16(c - 0x20);
    }
    if (c == 0x5f) {
        return 0x2d;
    }
    if (c == 0x2c) {
        return 0x27;
    }
    if (c == 0xff) {
        return 0x178;
    }
    if (c >= 0x100 && c <= 0x137 && (c & 1) == 1) {
        return c == 0x131 ? 0x49 : u16(c - 1);
    }
    if (c >= 0x139 && c <= 0x148 && (c & 1) == 0) {
        return u16(c - 1);
    }
    if (c >= 0x14a && c <= 0x177 && (c & 1) == 1) {
        return u16(c - 1);
    }
    if (c >= 0x179 && c <= 0x17e && (c & 1) == 0) {
        return u16(c - 1);
    }
    if (c >= 0x200 && c <= 0x21b && (c & 1) == 1) {
        return u16(c - 1);
    }
    if (c < 0x3041 || c > 0x30fe) {
        return u16(c);
    }
    if (c == 0x30ab) {
        return 0x30f5;
    }
    if (c == 0x30b1) {
        return 0x30f6;
    }
    return u16(c - 1);
}

// 02065148/02065184: bounded copy (optionally case folded); NUL if room remains.
std::u16string copyText(const u8* t, s32 limit, bool fold) {
    std::u16string out;
    for (s32 i = 0; r16(t, i * 2) != 0 && i < limit; ++i) {
        out.push_back(fold ? char16_t(upper(r16(t, i * 2))) : char16_t(r16(t, i * 2)));
    }
    return out;
}
} // namespace

Recognizer::Recognizer(std::span<const std::uint8_t> database)
    : database_(database.begin(), database.end()) {
    const u8* db = database_.data();
    // 0205ab20/02065794: the game's database must be the version-2 format.
    if (database_.size() < 0x40 || r16(db) != 2) {
        throw std::runtime_error("handwriting database has an unsupported version");
    }
    const u8* a = subA(db);
    if (r16(a, 0x20) != 0x20 || a[0x22] != 8 || a[1] > 6 || a[0x23] >= 9 ||
        !(a[3] == 'P' && a[0xe] == 'P' && a[0xf] == 'P')) {
        throw std::runtime_error("handwriting prototype table is not the expected layout");
    }
    const u8* b = subB(db);
    if (r16(b) >= 3 || !(b[0x3c] == 2 && b[0x3d] == 1 && b[0x3e] == 2 && b[0x3f] == 0xff)) {
        throw std::runtime_error("handwriting part table is not the expected layout");
    }
}

// 0205a12c: validate, resample to 32 points, recognize and format results.
int Recognizer::recognize(std::span<const Stroke> strokes, const Settings& p, unsigned capacity,
                          std::vector<Result>& out) {
    out.clear();
    lastCandidates.clear();
    // 0205a3e4: stroke list validation.
    if (strokes.empty()) {
        return 0x10;
    }
    for (const Stroke& s : strokes) {
        if (s.empty()) {
            return 0xe; // A null point list (8) cannot be expressed natively.
        }
    }
    // 0205a46c: settings validation and conversion.
    if (p.rotation < 0 || p.rotation > 0x167) {
        return 200;
    }
    if (p.slant < 0 || p.slant > 0x400) {
        return 0xc9;
    }
    if (p.rotation == 0) {
        if (p.baseline < p.helpline) {
            return 0xca;
        }
    } else if (p.baseline != 0 || p.helpline != 0) {
        return 0xca;
    }
    std::array<s32, 8> ws{};
    ws[0] = p.categories;
    ws[1] = p.categories2;
    s32 r = mul(p.rotation, 0x274);
    ws[2] = s16(sub(asr(add(r, s32((s64(s32(0xb60b60b7)) * r) >> 32)), 8), asr(r, 31)));
    ws[3] = p.slant;
    s16 g1 = h(asr(add(p.baseline, p.offsetY), u32(p.shift) & 0xff)),
        g2 = h(asr(add(p.helpline, p.offsetY), u32(p.shift) & 0xff));
    ws[4] = s32(u32(u16(g1)) | (u32(u16(g2)) << 16));
    ws[6] = p.userDatabase;
    ws[7] = p.characterTable;
    if (g1 < g2) {
        return 0xc;
    }
    if (strokes.size() > 6) {
        return 0;
    }
    // 0205a12c: resample every stroke to 32 points (ws+0x120).
    std::array<std::array<Point, 32>, 6> pts{};
    std::array<const Point*, 6> ptr{};
    for (size_t i = 0; i < strokes.size(); ++i) {
        resample(strokes[i].data(), s32(strokes[i].size()), pts[i].data(), 32, p.offsetX, p.offsetY,
                 u32(p.shift));
        ptr[i] = pts[i].data();
    }
    s32 want = s32(capacity);
    if (u32(want) < 3) {
        want = 2;
    }
    if (want > 2) {
        want = 3;
    }
    std::array<Candidate, 3> results{};
    s32 n =
        normalizeAndRecognize(results.data(), want, ws, database_.data(), ptr, s32(strokes.size()));
    // 0205a2e4: copy texts (upper-cased variants for capitals).
    s32 made = 0;
    for (s32 i = 0; i < n && made < s32(capacity); ++i) {
        const Candidate& c = results[size_t(i)];
        s32 score = add(c.score, c.penalty);
        if (score > 1000) {
            score = 1000;
        }
        ++made;
        if (c.caseKind == 1) {
            out.push_back({copyText(c.text, 10, true), score});
        } else if (c.caseKind == 2) {
            out.push_back({copyText(c.text, 10, false), score});
            if (made < s32(capacity)) {
                out.push_back({copyText(c.text, 10, true), score});
                ++made;
            }
        } else {
            out.push_back({copyText(c.text, 10, false), score});
        }
        lastCandidates.push_back(
            {c.h.index, c.h.strokes, c.score, c.penalty, copyText(c.text, 10, false), c.caseKind});
    }
    return 0;
}
} // namespace picarat::handwriting
