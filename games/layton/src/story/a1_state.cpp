// The first adventure's game state and save slots. See a1_state.hpp.
#include "story/a1_state.hpp"
#include <algorithm>
#include <cstring>

namespace picarat::adventure1 {

std::uint8_t& GameState::progress(int number) {
    // 020011e0..02001210: numbers 1..254 index the table directly, others
    // count down from entry 199.
    static std::uint8_t outside = 0;
    const int index = number >= 1 && number <= 0xfe ? number : 199 - number;
    if (index < 0 || index >= int(puzzle.puzzles.size())) {
        outside = 0;
        return outside;
    }
    return puzzle.puzzles[std::size_t(index)];
}

void GameState::setPuzzle(int number) {
    // 02012ac0: numbers from 0x100 up become 0; a different number clears
    // the "last answer was right" flag (+6).
    if (number > 0xff) {
        number = 0;
    }
    if (puzzleNumber != number) {
        puzzle.solved = false;
    }
    puzzleNumber = std::int16_t(number);
}

void GameState::setFlag(int n, bool on) {
    // 0201aee8.
    if (n < 0 || n > 0x3ff) {
        return;
    }
    if (n >= 300 && n < 320) {
        // A mystery found: new unless its flag was already set.
        if (!flag(n)) {
            newMystery = true;
            mysteries = std::uint16_t(mysteries | 1u << ((n - 300) % 20 & 15));
        }
    } else if (n >= 320 && n < 340) {
        mysteries = std::uint16_t(mysteries & ~(1u << ((n - 300) % 20 & 15)));
    }
    flags[std::size_t(n)] = on;
}

bool GameState::hotelCondition(int room, int n) const {
    // The same tests on either room's list (+0x684 / +0x6a4).
    const auto& has = room == 0 ? furnitureA : furnitureB;
    auto in = [&](int i) {
        return has[std::size_t(i)] != 0;
    };
    auto all = [&](std::initializer_list<int> list) {
        for (int i : list) {
            if (!in(i)) {
                return false;
            }
        }
        return true;
    };
    const auto count = std::count_if(has.begin(), has.end(), [](std::uint8_t b) { return b != 0; });
    switch (n) {
    case 0:
        return true;
    case 1:
        return count == 0;
    case 2:
        return count > 0;
    case 3:
        return !in(0) && !in(1);
    case 4:
        return !in(2) && !in(3);
    case 5:
        return count > 3;
    case 6:
        return !in(5);
    case 7:
        return !in(0xe);
    case 8:
        return count > 7;
    case 9:
        return !in(6) && !in(0x14);
    case 10:
        return !in(0x18);
    case 11:
        return !in(0x1c);
    case 12:
        return !in(0x12);
    case 13:
        return all({0, 2, 5, 0xa, 0xb, 0xc, 0x18, 0x1c, 0x1d});
    case 14:
        return all({1, 3, 4, 6, 7, 0xd, 0xe, 0x12, 0x14});
    case 15:
        return !in(8);
    case 16:
        return !in(0x11);
    case 17:
        return !in(9);
    case 18:
        return !in(0x19);
    case 19:
        return !in(0x13);
    case 20:
        return !in(0x1a);
    case 21:
        return !in(0x1b);
    case 22:
        return all({8, 9, 0xf, 0x10, 0x11, 0x15, 0x16, 0x17, 0x19});
    case 23:
        return all({0x13, 0x1a, 0x1b});
    case 24:
        return hotelCondition(room, 20) && hotelCondition(room, 21);
    default:
        return false;
    }
}

int GameState::hotelHappiness(int room) const {
    // Room 0 needs sets 13 and 22 for the top, room 1 sets 14 and 23.
    const int set = room == 0 ? 13 : 14, extra = room == 0 ? 22 : 23;
    if (hotelCondition(room, set) && hotelCondition(room, extra)) {
        return 5;
    }
    if (hotelCondition(room, set)) {
        return 4;
    }
    if (hotelCondition(room, 8)) {
        return 3;
    }
    if (hotelCondition(room, 5)) {
        return 2;
    }
    return hotelCondition(room, 2) ? 1 : 0;
}

int GameState::shackWaiting() const {
    int count = 0;
    for (int n = 1; n < 0x100; ++n) {
        count += !solvedAt(n) && flag(800 + n);
    }
    return count;
}

int GameState::nextShackPuzzle() {
    if (shackWaiting() < 1) {
        return -1;
    }
    if (++shackCursor > 0xff || shackCursor < 1) {
        shackCursor = 1;
    }
    for (int n = shackCursor; n < 0x100; ++n) {
        if (!solvedAt(n) && flag(800 + n)) {
            return shackCursor = n;
        }
    }
    for (int n = 1; n < 0x100; ++n) {
        if (!solvedAt(n) && flag(800 + n)) {
            return shackCursor = n;
        }
    }
    return -1;
}

bool GameState::isWifiPuzzle(int number) const {
    for (const auto& w : wifiPuzzles) {
        if (w.puzzle == number) {
            return true;
        }
    }
    return false;
}

int GameState::solvedCount() const {
    // 0201ad4c: puzzles 1..199 marked solved, the WiFi puzzles left out.
    int count = 0;
    for (int n = 1; n <= 199; ++n) {
        count += (puzzle.puzzles[std::size_t(n)] & 2) && !isWifiPuzzle(n) ? 1 : 0;
    }
    return count;
}

int GameState::seenCount() const {
    // 0201ac40: the same for puzzles seen.
    int count = 0;
    for (int n = 1; n <= 199; ++n) {
        count += (puzzle.puzzles[std::size_t(n)] & 1) && !isWifiPuzzle(n) ? 1 : 0;
    }
    return count;
}

void GameState::addMemo(int n) {
    // 0201b9c0(n, 1).
    if (n >= 0 && n < 64) {
        memoUnread[std::size_t(n)] = true;
        newMemo = true;
    }
}

std::uint64_t GameState::playTime() const {
    // 02019be0: at most 359999 seconds. A frame is 560190 cycles of the
    // 33513982 Hz bus clock (59.8261 frames a second).
    return std::min<std::uint64_t>(playSeconds + sessionFrames * 560190 / 33513982, 359999);
}

void GameState::reset() {
    // 0201974c.
    const std::string name = playerName;
    auto wifi = std::move(wifiPuzzles);
    *this = GameState{};
    playerName = name;
    wifiPuzzles = std::move(wifi);
    puzzleNumber = 0x6f;
    room = 1;
    event = 1;
    returnScene = 5;
    puzzle.coins = 10;
    puzzle.secondJudge = true; // +0x4f0 = 1
    setFlag(300, true);
    newMystery = false;
}

// ---------------------------------------------------------------------------
// The backup image.

namespace {
constexpr char kMagic[] = "ATAMFIREBELLNY";
constexpr std::size_t kHeaderSize = 0x150, kSlotBase = 1000, kSlotStride = 2000, kSlotSize = 0x53c;

void put16(std::uint8_t* p, unsigned v) {
    p[0] = std::uint8_t(v);
    p[1] = std::uint8_t(v >> 8);
}

void put32(std::uint8_t* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        p[i] = std::uint8_t(v >> (8 * i));
    }
}

unsigned get16(const std::uint8_t* p) {
    return unsigned(p[0] | p[1] << 8);
}

std::uint32_t get32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 |
           std::uint32_t(p[3]) << 24;
}

std::string getString(const std::uint8_t* p, std::size_t size) {
    std::string s;
    for (std::size_t i = 0; i < size && p[i]; ++i) {
        s += char(p[i]);
    }
    return s;
}

void putString(std::uint8_t* p, std::size_t size, const std::string& s) {
    std::memset(p, 0, size);
    std::memcpy(p, s.data(), std::min(s.size(), size - 1));
}

template <std::size_t N>
void putBits(std::uint8_t* p, const std::bitset<N>& bits) {
    for (std::size_t i = 0; i < N / 8; ++i) {
        std::uint8_t b = 0;
        for (int k = 0; k < 8; ++k) {
            b = std::uint8_t(b | (bits[i * 8 + std::size_t(k)] ? 1 << k : 0));
        }
        p[i] = b;
    }
}

template <std::size_t N>
void getBits(const std::uint8_t* p, std::bitset<N>& bits) {
    for (std::size_t i = 0; i < N; ++i) {
        bits[i] = (p[i / 8] >> (i % 8)) & 1;
    }
}
} // namespace

std::uint32_t saveChecksum(const std::uint8_t* data, std::size_t size) {
    // 0200b7d0: Fletcher-like over halfwords, folded every 360 of them.
    std::uint32_t a = 0xffff, b = 0xffff;
    std::size_t count = size >> 1, at = 0;
    while (count) {
        std::size_t run = std::min<std::size_t>(count, 0x168);
        count -= run;
        while (run--) {
            a += get16(data + at);
            b += a;
            at += 2;
        }
        a = (a >> 16) + (a & 0xffff);
        b = (b >> 16) + (b & 0xffff);
    }
    return ((a >> 16) + (a & 0xffff)) | ((b >> 16) + (b & 0xffff)) << 16;
}

SaveImage::SaveImage(std::string defaultName)
    : bytes_(kSize, 0xff), defaultName_(std::move(defaultName)) {
    // 02024364: the header cleared, the magic, three empty slots.
    std::fill(bytes_.begin(), bytes_.begin() + kHeaderSize, 0);
    bytes_[0x14] = 0xff;
    std::memcpy(bytes_.data() + 4, kMagic, sizeof kMagic - 1);
    for (int n = 0; n < 3; ++n) {
        clearSlotInfo(n);
    }
    writeHeaderChecksum();
}

SaveImage::SaveImage(std::string defaultName, std::vector<std::uint8_t> bytes)
    : SaveImage(std::move(defaultName)) {
    // 02024538: a header without the magic is reset.
    if (bytes.size() < kSize) {
        return;
    }
    if (getString(bytes.data() + 4, 16) != kMagic) {
        return;
    }
    bytes.resize(kSize);
    bytes_ = std::move(bytes);
}

void SaveImage::clearSlotInfo(int n) {
    // 02024464.
    std::uint8_t* h = bytes_.data();
    h[0x15 + n] = 0;
    putString(h + 0x18 + n * 0x40, 0x40, "NO DATA");
    put32(h + 0xd8 + 4 * n, 0);
    put32(h + 0xe4 + 4 * n, 0);
    put32(h + 0xf0 + 4 * n, 0);
    put32(h + 0xfc + 4 * n, 0);
    h[0x144 + n] = 0;
    putString(h + 0x108 + n * 0x14, 0x14, defaultName_);
}

void SaveImage::writeHeaderChecksum() {
    put32(bytes_.data(), saveChecksum(bytes_.data() + 4, 0x14c));
}

SaveImage::SlotInfo SaveImage::slot(int n) const {
    const std::uint8_t* h = bytes_.data();
    SlotInfo s;
    s.used = h[0x15 + n] != 0;
    s.place = getString(h + 0x18 + n * 0x40, 0x40);
    s.minutes = get32(h + 0xd8 + 4 * n);
    s.hours = get32(h + 0xe4 + 4 * n);
    s.seen = get32(h + 0xf0 + 4 * n);
    s.solved = get32(h + 0xfc + 4 * n);
    s.name = getString(h + 0x108 + n * 0x14, 0x14);
    s.cleared = h[0x144 + n] != 0;
    return s;
}

std::vector<GameState::WifiPuzzle> SaveImage::wifiPuzzles() const {
    const std::uint8_t* p = bytes_.data() + 0x1b58;
    std::vector<GameState::WifiPuzzle> list;
    const std::uint32_t sum = saveChecksum(p + 4, 0x74);
    if (get32(p) != sum || sum == 0xffffffffu) {
        return list;
    }
    const std::uint32_t count = std::min<std::uint32_t>(get32(p + 4), (0x78 - 0xc) / 4);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* e = p + 0xc + 4 * i;
        list.push_back({e[0], e[1], e[2], e[3]});
    }
    return list;
}

bool SaveImage::anyUsed() const {
    // 02024338.
    return bytes_[0x15] || bytes_[0x16] || bytes_[0x17];
}

void SaveImage::store(int n, const GameState& g) {
    // 02019c9c: the slot.
    std::uint8_t* p = bytes_.data() + kSlotBase + std::size_t(n) * kSlotStride;
    std::memset(p, 0, kSlotSize);
    p[0] = std::uint8_t(g.puzzleNumber);
    p[1] = g.room;
    p[2] = g.puzzle.solved;
    p[3] = g.puzzle.retry;
    p[4] = 0; // +8 quit
    p[5] = g.byte9;
    p[6] = g.reward;
    p[7] = std::uint8_t(g.puzzle.retries);
    p[8] = g.puzzle.answerPicture;
    p[9] = g.scene;
    p[0xa] = g.returnScene;
    p[0xb] = g.byte12;
    std::copy(g.puzzle.puzzles.begin(), g.puzzle.puzzles.end(), p + 0xc);
    put32(p + 0x10c, std::uint32_t(g.afterPuzzle));
    put32(p + 0x110, g.event);
    std::copy(g.events.begin(), g.events.end(), p + 0x114);
    put16(p + 0x2f4, g.value1e);
    for (std::size_t k = 0; k < g.puzzle.kindTutorialSeen.size(); ++k) {
        p[0x2f6 + k] = g.puzzle.kindTutorialSeen[k];
    }
    putBits(p + 0x336, g.coins);
    put16(p + 0x376, g.puzzle.coins);
    for (int k = 0; k < 8; ++k) {
        p[0x378 + k] = std::uint8_t(g.playTime() >> (8 * k)); // 02019c58 adds the session
    }
    std::copy(g.items.begin(), g.items.end(), p + 0x380);
    putBits(p + 0x388, g.flags);
    p[0x408] = g.chapter;
    std::copy(g.gizmos.begin(), g.gizmos.end(), p + 0x409);
    std::copy(g.dogParts.begin(), g.dogParts.end(), p + 0x429);
    put32(p + 0x42d, g.puzzle.score);
    std::copy(g.pieceOwned.begin(), g.pieceOwned.end(), p + 0x431);
    std::copy(g.piecePlace.begin(), g.piecePlace.end(), p + 0x445);
    std::copy(g.piece670.begin(), g.piece670.end(), p + 0x459);
    std::copy(g.furnitureA.begin(), g.furnitureA.end(), p + 0x46d);
    std::copy(g.furnitureB.begin(), g.furnitureB.end(), p + 0x48d);
    std::copy(g.furniture6c4.begin(), g.furniture6c4.end(), p + 0x4ad);
    std::copy(g.furniture6e4.begin(), g.furniture6e4.end(), p + 0x4cd);
    p[0x4ed] = g.newMemo;
    p[0x4ee] = g.newGizmo;
    p[0x4ef] = g.newFurniture;
    p[0x4f0] = g.newPainting;
    p[0x4f1] = g.newMystery;
    std::copy(g.puzzleMarks.begin(), g.puzzleMarks.end(), p + 0x4f2);
    putBits(p + 0x512, g.memoUnread);
    put16(p + 0x51a, g.mysteries);
    put32(p + 0x51c, std::uint32_t(g.nextEvent));
    std::copy(g.dogName.begin(), g.dogName.end(), p + 0x520);
    put32(p + 0x534, std::uint32_t(g.dogFinds));
    put32(p + 0x538, saveChecksum(p, 0x538));
    // 0201a8e8: the header's line for the slot.
    std::uint8_t* h = bytes_.data();
    const auto seconds = g.playTime();
    putString(h + 0x18 + n * 0x40, 0x40, g.place);
    put32(h + 0xd8 + 4 * n, std::uint32_t(seconds / 60 % 60));
    put32(h + 0xe4 + 4 * n, std::uint32_t(seconds / 3600));
    put32(h + 0xf0 + 4 * n, std::uint32_t(g.seenCount()));
    put32(h + 0xfc + 4 * n, std::uint32_t(g.solvedCount()));
    putString(h + 0x108 + n * 0x14, 0x14, g.playerName);
    h[0x14] = std::uint8_t(n);
    h[0x15 + n] = 1;
    h[0x144 + n] = g.flag(0x200);
    writeHeaderChecksum();
}

bool SaveImage::load(int n, GameState& g) const {
    // 0201a1dc.
    const std::uint8_t* p = bytes_.data() + kSlotBase + std::size_t(n) * kSlotStride;
    if (!bytes_[0x15 + n] || get32(p + 0x538) != saveChecksum(p, 0x538)) {
        return false;
    }
    g = GameState{};
    g.firstTouchHint = false;
    g.puzzleNumber = std::int8_t(p[0]);
    g.room = p[1];
    g.puzzle.solved = p[2];
    g.puzzle.retry = p[3];
    g.byte9 = p[5];
    g.reward = p[6];
    g.puzzle.retries = p[7];
    g.puzzle.answerPicture = p[8];
    g.scene = p[9];
    g.returnScene = p[0xa];
    g.byte12 = p[0xb];
    std::copy(p + 0xc, p + 0x10c, g.puzzle.puzzles.begin());
    g.afterPuzzle = std::int32_t(get32(p + 0x10c));
    g.event = get32(p + 0x110);
    std::copy(p + 0x114, p + 0x114 + 480, g.events.begin());
    g.value1e = std::uint16_t(get16(p + 0x2f4));
    for (std::size_t k = 0; k < g.puzzle.kindTutorialSeen.size(); ++k) {
        g.puzzle.kindTutorialSeen[k] = p[0x2f6 + k];
    }
    getBits(p + 0x336, g.coins);
    g.puzzle.coins = std::uint16_t(get16(p + 0x376));
    g.playSeconds = 0;
    for (int k = 0; k < 8; ++k) {
        g.playSeconds |= std::uint64_t(p[0x378 + k]) << (8 * k);
    }
    std::copy(p + 0x380, p + 0x388, g.items.begin());
    getBits(p + 0x388, g.flags);
    g.chapter = p[0x408];
    std::copy(p + 0x409, p + 0x429, g.gizmos.begin());
    std::copy(p + 0x429, p + 0x42d, g.dogParts.begin());
    g.puzzle.score = get32(p + 0x42d);
    std::copy(p + 0x431, p + 0x445, g.pieceOwned.begin());
    std::copy(p + 0x445, p + 0x459, g.piecePlace.begin());
    std::copy(p + 0x459, p + 0x46d, g.piece670.begin());
    std::copy(p + 0x46d, p + 0x48d, g.furnitureA.begin());
    std::copy(p + 0x48d, p + 0x4ad, g.furnitureB.begin());
    std::copy(p + 0x4ad, p + 0x4cd, g.furniture6c4.begin());
    std::copy(p + 0x4cd, p + 0x4ed, g.furniture6e4.begin());
    g.newMemo = p[0x4ed];
    g.newGizmo = p[0x4ee];
    g.newFurniture = p[0x4ef];
    g.newPainting = p[0x4f0];
    g.newMystery = p[0x4f1];
    std::copy(p + 0x4f2, p + 0x512, g.puzzleMarks.begin());
    getBits(p + 0x512, g.memoUnread);
    g.mysteries = std::uint16_t(get16(p + 0x51a));
    g.nextEvent = std::int32_t(get32(p + 0x51c));
    std::copy(p + 0x520, p + 0x534, g.dogName.begin());
    g.dogFinds = std::int32_t(get32(p + 0x534));
    // 0201aadc: the slot, the place and the player's name from the header.
    const SlotInfo info = slot(n);
    g.slot = std::int8_t(n);
    g.place = info.place;
    g.playerName = info.name;
    return true;
}

} // namespace picarat::adventure1
