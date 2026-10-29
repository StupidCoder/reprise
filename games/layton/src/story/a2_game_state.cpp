// The game state and its tables. See a2_game_state.hpp for how the story moves.
#include "story/a2_game_state.hpp"
#include "system/resources.hpp"
#include <stdexcept>

namespace picarat {
namespace {
std::int32_t s16(const std::uint8_t* p) {
    return std::int16_t(p[0] | p[1] << 8);
}
} // namespace

bool SaveProgress::bit(std::size_t offset, unsigned count, std::uint32_t index) const {
    if (index >= count) {
        return false;
    }
    return bytes[offset + (index >> 3)] & (1u << (index & 7));
}

void SaveProgress::setBit(std::size_t offset, unsigned count, std::uint32_t index, bool value) {
    if (index >= count) {
        return;
    }
    auto& b = bytes[offset + (index >> 3)];
    const auto mask = std::uint8_t(1u << (index & 7));
    b = value ? std::uint8_t(b | mask) : std::uint8_t(b & ~mask);
}

bool SaveProgress::compareCounter(std::uint32_t index, std::uint32_t value,
                                  std::uint32_t mode) const {
    // 02082608: index >= 128 is false; modes compare the low value byte.
    if (index >= 128) {
        return false;
    }
    const auto have = bytes[0x94 + index];
    const auto want = std::uint8_t(value);
    switch (mode) {
    case 0:
        return want == have;
    case 1:
        return want != have;
    case 2:
        return have >= want;
    default:
        return false;
    }
}

DlzTable::DlzTable(Bytes data, std::uint16_t rowSize) {
    // 02084a3c pattern: zero rows or a different row size leave no table.
    if (data.size() < 8) {
        throw std::runtime_error("dlz header");
    }
    const auto count = read16(data, 0), header = read16(data, 2), size = read16(data, 4);
    if (!count || size != rowSize) {
        throw std::runtime_error("dlz row size");
    }
    if (header + std::size_t(count) * size > data.size()) {
        throw std::runtime_error("dlz bounds");
    }
    rows_.assign(data.begin() + header, data.begin() + header + std::size_t(count) * size);
    count_ = count;
    rowSize_ = size;
    step_ = 1;
    while (step_ < count_) {
        step_ <<= 1; // e.g. 020848a0..020848d0
    }
}

template <class Key>
const std::uint8_t* DlzTable::find(std::uint32_t key, Key keyOf) const {
    if (!count_) {
        return nullptr;
    }
    if (key > keyOf(rows_.data() + (count_ - 1) * rowSize_)) {
        return nullptr; // unsigned MOVHI
    }
    int found = -1;
    unsigned lo = 0;
    int step = int(step_);
    while (step > 0) {
        step >>= 1;
        const unsigned i = lo + unsigned(step);
        if (i < count_) {
            const std::int32_t d = std::int32_t(key - keyOf(rows_.data() + i * rowSize_));
            if (d == 0) {
                found = int(i);
            } else if (d > 0) {
                lo = i;
            }
        }
    }
    return found < 0 ? nullptr : rows_.data() + found * rowSize_;
}

GameState::GameState(const std::filesystem::path& root) {
    placeFlags_ = readFile(root / "place" / "placeflag.dat");
    storyFlags_ = readFile(root / "place" / "storyflag2.dat");
    autoEvents_ = readFile(root / "place" / "autoevent2.dat");
    if (placeFlags_.size() != 0x3800 || storyFlags_.size() != 0x2200 ||
        autoEvents_.size() != 0x2000) {
        throw std::runtime_error("place data sizes");
    }
    eventInfo_ = DlzTable(readFile(root / "rc" / "ev_inf2.dlz"), 12);   // 020847c8
    eventFix_ = DlzTable(readFile(root / "rc" / "ev_fix.dlz"), 6);      // 02084a3c
    puzzles_ = DlzTable(readFile(root / "rc" / "nz_lst.dlz"), 54);      // 02084c64
    chapters_ = DlzTable(readFile(root / "rc" / "chp_inf.dlz"), 8);     // 02084078
    goals_ = DlzTable(readFile(root / "rc" / "goal_inf.dlz"), 6);       // 0208445c
    teaHerbs_ = DlzTable(readFile(root / "rc" / "ht_elm.dlz"), 0x30);   // 02085128
    teaRecipes_ = DlzTable(readFile(root / "rc" / "ht_rcp.dlz"), 0xe0); // 02085334
    teaTalk_ = DlzTable(readFile(root / "rc" / "ht_tlk.dlz"), 0x12);    // 02085758
    teaEvents_ = DlzTable(readFile(root / "rc" / "ht_event.dlz"), 4);   // 02085574
    photos_ = DlzTable(readFile(root / "rc" / "sb_pht.dlz"), 12);       // 02084ed0
}

void GameState::newGame() {
    // 02083368: progress cleared (02082564), scenes 1/1, place data loaded,
    // puzzle 1 info, story step 5, place 1, ten hint coins, no pending event.
    progress = SaveProgress{};
    scene = following = scene::Room;
    loadPuzzle(1);
    progress.setStoryStep(5);
    setPlace(1);
    progress.setHintCoins(10);
    progress.setHintCoinsFound(10);
    progress.put16(0x310, 0xffff);
    progress.setPendingEvent(-1);
    // 020835e0: 02082f08(progress, "NO NAME") - the default hamster name.
    static constexpr char noName[] = "NO NAME";
    std::copy(std::begin(noName), std::end(noName), progress.bytes.begin() + 0x2f7);
    savedEvent = 0;
    reward = -1;
}

void GameState::advanceStory() {
    // 02083aa8: every storyflag2 row (34 bytes) whose step equals the current
    // one checks up to eight conditions; type 1 needs a story flag, type 2 a
    // solved puzzle (by internal ID). Any failure ends the whole pass. A row
    // that passes sets the step to the NEXT row's step and the scan continues.
    for (unsigned r = 0; r < 256; ++r) {
        const auto* row = storyFlags_.data() + r * 34;
        if (std::int32_t(le16(row)) != progress.storyStep()) {
            continue;
        }
        for (unsigned c = 0; c < 8; ++c) {
            const auto* cond = row + 2 + c * 4;
            const auto value = le16(cond + 2);
            if (cond[0] == 1) {
                if (!progress.storyFlag(value)) {
                    return;
                }
            } else if (cond[0] == 2) {
                if (puzzleStatus(puzzleNumber(std::int32_t(value))) != 2) {
                    return;
                }
            }
        }
        // Row 255 reads the halfword after the table: state+0x5e7c (event ID).
        progress.setStoryStep(std::int32_t(r < 255 ? le16(row + 34) : eventId));
    }
}

void GameState::updatePlaceVersion() {
    // 020839fc: versions 1..15 hold step ranges (placeflag+place*64+i*4) and a
    // counter condition (placeflag+0x2000+place*48+i*3); the last match wins.
    advanceStory();
    const auto place = progress.place();
    if (place < 0 || place >= 128) {
        throw std::runtime_error("place index"); // native bound; the ROM does not check
    }
    int version = 0;
    for (int i = 1; i < 16; ++i) {
        const auto* range = placeFlags_.data() + place * 64 + i * 4;
        const auto lo = le16(range), hi = le16(range + 2);
        if (!lo && !hi) {
            break;
        }
        const auto step = progress.storyStep();
        if (step < std::int32_t(lo) || step > std::int32_t(hi)) {
            continue;
        }
        const auto* c = placeFlags_.data() + 0x2000 + place * 48 + i * 3;
        if (!c[0] || progress.compareCounter(c[0], c[2], c[1])) {
            version = i;
        }
    }
    progress.setPlaceVersion(version);
}

void GameState::setPlace(std::int32_t place) {
    // 020839b0: remembers the old place and whether it changed.
    const auto old = progress.place();
    previousPlace = old;
    placeChanged = old != place;
    progress.setPlace(place);
    updatePlaceVersion();
}

bool GameState::findAutoEvent() {
    // 02083b70: eight (event, first step, last step) records per place; an
    // unviewed event in range becomes the current event. The last one wins.
    const auto* rows = autoEvents_.data() + std::size_t(progress.place()) * 64;
    bool found = false;
    for (unsigned i = 0; i < 8; ++i) {
        const auto* r = rows + i * 8;
        const auto event = le16(r), lo = le16(r + 2), hi = le16(r + 4);
        if (!lo && !hi) {
            continue;
        }
        const auto step = progress.storyStep();
        if (step < std::int32_t(lo) || step > std::int32_t(hi)) {
            continue;
        }
        if (!eventViewed(event)) {
            eventId = std::uint16_t(event);
            found = true;
        }
    }
    return found;
}

const std::uint8_t* GameState::eventFix(std::uint32_t event) const {
    return eventFix_.find(event, [](const std::uint8_t* r) { return le16(r); });
}

bool GameState::eventViewed(std::uint32_t event) const {
    const auto* r = eventFix(event);
    return r && progress.eventViewed(le16(r + 4));
}

void GameState::setEventViewed(std::uint32_t event, bool value) {
    if (const auto* r = eventFix(event)) {
        progress.setEventViewed(le16(r + 4), value);
    }
}

bool GameState::loadEventInfo(std::uint32_t event) {
    current = {};
    const auto* r = eventInfo_.find(event, [](const std::uint8_t* p) { return le16(p); });
    if (!r) {
        return false;
    }
    current = {std::uint16_t(le16(r)),     std::uint16_t(le16(r + 2)), std::uint16_t(le16(r + 4)),
               std::uint16_t(le16(r + 6)), std::uint16_t(le16(r + 8)), std::uint16_t(le16(r + 10))};
    return true;
}

std::optional<GoalInfo> GameState::goalInfo(std::uint32_t key) const {
    const auto* r = goals_.find(key, [](const std::uint8_t* p) { return le16(p); });
    if (!r) {
        return std::nullopt;
    }
    return GoalInfo{std::uint16_t(le16(r)), std::uint16_t(le16(r + 2)), std::uint16_t(le16(r + 4))};
}

int GameState::puzzleNumber(std::int32_t internal) const {
    // 02083c00 -> 02084d8c: linear search of nz_lst by signed internal ID.
    for (std::size_t i = 0; i < puzzles_.count(); ++i) {
        const auto* r = puzzles_.row(i).data();
        if (s16(r) == internal) {
            return s16(r + 2);
        }
    }
    return -1;
}

unsigned GameState::puzzleStatus(std::int32_t number) const {
    return progress.puzzleByte(std::uint32_t(number - 1)) & 3; // 020826f0: index >= 216 reads 0
}

void GameState::setPuzzleStatus(std::int32_t number, std::uint32_t value, bool force) {
    // 02083d3c -> 02082710 -> 02082454 (status cap 2, monotonic unless forced).
    const auto index = std::uint32_t(number - 1);
    if (index >= 216) {
        return;
    }
    auto& b = progress.puzzleByte(index);
    if (value >= 3) {
        value = 2;
    }
    if (value < (b & 3u) && !force) {
        return;
    }
    b = std::uint8_t((b & 0xfc) | value);
}

unsigned GameState::solvedCount() const {
    unsigned n = 0;
    for (int i = 1; i <= 138; ++i) {
        if (puzzleStatus(i) == 2) {
            ++n;
        }
    }
    return n; // 02083e7c
}

bool GameState::loadPuzzle(std::int32_t internal) {
    // 02083c38: clears the current puzzle (02083c18), then copies its row.
    puzzle = {};
    for (std::size_t i = 0; i < puzzles_.count(); ++i) {
        const auto* r = puzzles_.row(i).data();
        if (s16(r) != internal) {
            continue;
        }
        const auto* t = reinterpret_cast<const char*>(r + 4);
        std::size_t n = 0;
        while (n < 48 && t[n]) {
            ++n;
        }
        puzzle = {std::int16_t(s16(r)), std::int16_t(s16(r + 2)), std::string(t, n),
                  std::int16_t(s16(r + 52))};
        return true;
    }
    return false;
}

std::optional<PuzzleInfo> GameState::puzzleByNumber(std::int32_t number) const {
    // 02084dec: sorted lookup on the signed number at +2.
    const auto* r = puzzles_.find(std::uint32_t(number),
                                  [](const std::uint8_t* p) { return std::uint32_t(s16(p + 2)); });
    if (!r) {
        return std::nullopt;
    }
    const auto* t = reinterpret_cast<const char*>(r + 4);
    std::size_t n = 0;
    while (n < 48 && t[n]) {
        ++n;
    }
    return PuzzleInfo{std::int16_t(s16(r)), std::int16_t(s16(r + 2)), std::string(t, n),
                      std::int16_t(s16(r + 52))};
}

bool GameState::allPuzzlesSolved(int last) const {
    for (int i = 1; i <= last; ++i) {
        if (puzzleStatus(i) != 2) {
            return false;
        }
    }
    return true;
}

const std::uint8_t* GameState::teaHerbInfo(std::uint32_t herb) const {
    return teaHerbs_.find(herb, [](const std::uint8_t* r) { return std::uint32_t(le16(r)); });
}

const std::uint8_t* GameState::teaRecipe(std::uint32_t index) const {
    return index < teaRecipes_.count() ? teaRecipes_.row(index).data() : nullptr;
}

const std::uint8_t* GameState::teaRecipeByCode(std::uint32_t code) const {
    // 02085514: linear scan of the recipe code at +2.
    for (std::size_t i = 0; i < teaRecipes_.count(); ++i) {
        if (le16(teaRecipes_.row(i).data() + 2) == code) {
            return teaRecipes_.row(i).data();
        }
    }
    return nullptr;
}

const std::uint8_t* GameState::teaTalk(std::uint32_t tea) const {
    // 02085880: the key is a signed halfword compared as an unsigned word.
    return teaTalk_.find(tea,
                         [](const std::uint8_t* r) { return std::uint32_t(std::int32_t(s16(r))); });
}

const std::uint8_t* GameState::teaEvent(std::uint32_t event) const {
    return teaEvents_.find(
        event, [](const std::uint8_t* r) { return std::uint32_t(std::int32_t(s16(r))); });
}

const std::uint8_t* GameState::photoByIndex(std::uint32_t photo) const {
    return photos_.find(photo, [](const std::uint8_t* r) { return std::uint32_t(r[0]); });
}

const std::uint8_t* GameState::photoByPlace(std::uint32_t place) const {
    for (std::size_t i = 0; i < photos_.count(); ++i) {
        if (photos_.row(i)[1] == place) {
            return photos_.row(i).data();
        }
    }
    return nullptr;
}

std::uint32_t GameState::teaRecipeCode(int a, int b, int c) {
    // 0208545c: exchange sort ascending, then smallest<<8 | middle<<4 | largest.
    int v[3]{a, b, c};
    for (int i = 1; i < 3; ++i) {
        for (int j = 0; j < i; ++j) {
            if (v[i] < v[j]) {
                std::swap(v[i], v[j]);
            }
        }
    }
    return std::uint32_t(v[2] | v[1] << 4 | v[0] << 8);
}

bool GameState::teaComplete() const {
    // 02085970: all 24 tea flags and hint-coin bits 127:0 and 127:1.
    for (unsigned i = 0; i < 24; ++i) {
        if (!progress.teaCorrect(i)) {
            return false;
        }
    }
    return progress.hintCoin(127, 0) && progress.hintCoin(127, 1);
}

unsigned GameState::foundCount() const {
    unsigned n = 0;
    for (int i = 1; i <= 138; ++i) {
        if (puzzleStatus(i) - 1u <= 1) {
            ++n; // 02083e40: status 1 or 2
        }
    }
    return n;
}

std::optional<std::uint16_t> GameState::continueEvent() const {
    // 02084184: binary search on the signed story step (row +0); the row
    // gives the event (+2), a condition event (+4) and its replacement (+6).
    const auto* r = chapters_.find(std::uint32_t(progress.storyStep()),
                                   [](const std::uint8_t* p) { return std::uint32_t(s16(p)); });
    if (!r) {
        return std::nullopt;
    }
    std::uint16_t event = std::uint16_t(s16(r + 2));
    // ov9 020f05d4 tests the seen-event bit itself (02082578), not an event ID.
    if (s16(r + 4) > 0 && progress.eventViewed(std::uint32_t(s16(r + 4)))) {
        event = std::uint16_t(s16(r + 6));
    }
    return event;
}
} // namespace picarat
