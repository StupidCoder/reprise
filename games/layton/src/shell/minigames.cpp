// Minigame free play's progress. See minigames.hpp.
#include "shell/minigames.hpp"
#include "graphics/text.hpp"
#include "minigames/a4_minigames.hpp"
#include "story/a1_state.hpp"
#include "story/a2_game_state.hpp"
#include "system/a4_services.hpp"
#include "system/resources.hpp"
#include "system/script.hpp"
#include <array>
#include <fstream>

namespace picarat::shell {

namespace {

constexpr int kParrotStages = 12;

std::vector<std::uint8_t> readAll(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return in ? std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in), {}}
              : std::vector<std::uint8_t>{};
}

void writeAll(const std::filesystem::path& file, const std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const auto temporary = file.string() + ".new";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!out) {
            return;
        }
    }
    std::filesystem::rename(temporary, file, ec);
}

// How many of each hamster item (rewards 40..49) the game data hands out:
// the puzzles' completion values (ov27 020f25dc) and the events' 0x78
// SetReward before their first command 1 (the pre-pass of 0207cff0).
std::array<int, 10> hamsterItems(const std::filesystem::path& pack) {
    std::array<int, 10> counts{};
    auto count = [&](int reward) {
        if (reward >= 40 && reward < 50) {
            ++counts[std::size_t(reward - 40)];
        }
    };
    const GameText text(pack);
    for (const auto& e : std::filesystem::directory_iterator(pack / "puzzle")) {
        const auto name = e.path().filename().string();
        if (name.size() > 5 && name[0] == 'n' && e.path().extension() == ".dat") {
            count(PuzzleData(readFile(e.path()), text).completionValue);
        }
    }
    for (const auto& e : std::filesystem::directory_iterator(pack / "event")) {
        if (e.path().extension() != ".gds") {
            continue;
        }
        const auto file = readFile(e.path());
        ScriptReader scan(file);
        while (scan.position() < scan.size()) {
            const auto token = scan.next();
            if (token.tag == 12) {
                break;
            }
            if (token.tag != 0) {
                continue;
            }
            const auto command = scan.values().command;
            if (command == 1) {
                break;
            }
            if (command == 0x78) {
                ScriptArguments args(scan);
                count(args.integer());
            }
        }
    }
    return counts;
}

std::string countOf(int done, int total, const char* what) {
    return std::to_string(done) + " of " + std::to_string(total) + " " + what;
}

} // namespace

Adventure2Minigames::Adventure2Minigames(std::filesystem::path pack, std::filesystem::path file)
    : pack_(std::move(pack)), file_(std::move(file)) {}

std::vector<std::uint8_t> Adventure2Minigames::progress() {
    auto block = readAll(file_);
    if (block.size() == SaveProgress::size) {
        return block;
    }
    GameState state(pack_);
    state.newGame();
    SaveProgress& p = state.progress;
    for (std::uint32_t i = 0; i < 10; ++i) {
        p.setBit(0x24c, 10, i, true); // camera parts
    }
    for (std::uint32_t i = 0; i < 8; ++i) {
        p.setHerb(i, true);
    }
    const auto items = hamsterItems(pack_);
    for (std::size_t i = 0; i < items.size(); ++i) {
        p.bytes[0x284 + i] = std::uint8_t(items[i]);
    }
    p.bytes[0x283] = 1; // the hamster's level (02082ac8)
    for (std::uint32_t i = 0; i < 9; ++i) {
        p.setPhotoTaken(i, true);
    }
    block.assign(p.bytes.begin(), p.bytes.end());
    store(block);
    return block;
}

void Adventure2Minigames::store(const std::vector<std::uint8_t>& block) {
    if (block.size() == SaveProgress::size) {
        writeAll(file_, block);
    }
}

void Adventure2Minigames::reset() {
    std::error_code ec;
    std::filesystem::remove(file_, ec);
}

std::vector<MinigameStatus> Adventure2Minigames::status() {
    SaveProgress p;
    const auto block = readAll(file_);
    const bool fresh = block.size() != SaveProgress::size;
    if (!fresh) {
        std::copy(block.begin(), block.end(), p.bytes.begin());
    }
    int photos = 0, teas = 0;
    for (std::uint32_t i = 0; i < 9; ++i) {
        photos += p.photoComplete(i);
    }
    for (std::uint32_t i = 0; i < 12; ++i) {
        teas += p.tea(i);
    }
    const int level = fresh ? 1 : std::max(1, int(p.bytes[0x283]));
    std::vector<MinigameStatus> list(4);
    list[0] = {"Camera repair", true, p.cameraSolved(),
               p.cameraSolved() ? "The camera works" : "Not repaired yet",
               p.cameraSolved() ? 1.0f : 0.0f};
    list[1] = {"Photo hunt", true, photos == 9, countOf(photos, 9, "photos solved"), photos / 9.0f};
    list[2] = {"Hamster", true, level >= 6, "Level " + std::to_string(std::min(level, 6)) + " of 6",
               // levels passed: a new hamster (level 1) has passed none
               (std::min(level, 6) - 1) / 5.0f};
    list[3] = {"Herb tea", true, teas == 12, countOf(teas, 12, "teas brewed"), teas / 12.0f};
    return list;
}

Adventure3Minigames::Adventure3Minigames(std::filesystem::path file) : file_(std::move(file)) {}

std::vector<std::uint8_t> Adventure3Minigames::progress() const {
    auto bytes = readAll(file_);
    if (bytes.size() < kStickers) {
        bytes.resize(kStickers, 0);
    }
    return bytes;
}

void Adventure3Minigames::store(const std::vector<std::uint8_t>& progress) {
    if (progress.size() >= kStickers) {
        writeAll(file_, progress);
    }
}

void Adventure3Minigames::reset() {
    std::error_code ec;
    std::filesystem::remove(file_, ec);
}

std::vector<MinigameStatus> Adventure3Minigames::status() const {
    const auto bytes = progress();
    auto count = [&](std::size_t from, std::size_t n) {
        int done = 0;
        for (std::size_t i = from; i < from + n; ++i) {
            done += bytes[i] != 0;
        }
        return done;
    };
    constexpr int kCourses = 10, kBookCount = 3;
    const int delivered = count(kParrot, kParrotStages), cleared = count(kCar, kCourses),
              solved = count(kBooks, kBookCount);
    std::vector<MinigameStatus> list(3);
    list[0] = {"Parrot", true, delivered == kParrotStages,
               countOf(delivered, kParrotStages, "deliveries made"),
               delivered / float(kParrotStages)};
    list[1] = {"Toy car", true, cleared == kCourses, countOf(cleared, kCourses, "courses cleared"),
               cleared / float(kCourses)};
    list[2] = {"Picture book", true, solved == kBookCount,
               countOf(solved, kBookCount, "books solved"), solved / float(kBookCount)};
    return list;
}

Adventure1Minigames::Adventure1Minigames(std::filesystem::path file) : file_(std::move(file)) {}

void Adventure1Minigames::apply(const std::vector<std::uint8_t>& progress,
                                adventure1::GameState& state) {
    if (progress.size() < kSize) {
        // Everything the story hands out: the dog's parts waiting (0xc4
        // 1..20), the painting's pieces in the box (0xc5 1..20, place 0xff),
        // the hotel's pieces (0xcc 1..30) alternately in the two rooms.
        state.gizmos = {};
        for (int n = 1; n <= 20; ++n) {
            state.gizmos[std::size_t(n - 1)] = std::uint8_t(n);
        }
        state.dogParts = {};
        state.pieceOwned.fill(1);
        state.piecePlace.fill(0xff);
        state.piece670 = {};
        state.furnitureA = state.furnitureB = state.furniture6c4 = state.furniture6e4 = {};
        for (int n = 1; n <= 30; ++n) {
            (n % 2 ? state.furnitureA : state.furnitureB)[std::size_t(n - 1)] = 1;
        }
        return;
    }
    const std::uint8_t* p = progress.data();
    auto take = [&](auto& list) {
        std::copy(p, p + list.size(), list.begin());
        p += list.size();
    };
    take(state.gizmos);
    take(state.dogParts);
    take(state.pieceOwned);
    take(state.piecePlace);
    take(state.piece670);
    take(state.furnitureA);
    take(state.furnitureB);
    take(state.furniture6c4);
    take(state.furniture6e4);
    for (int k = 0; k < 3; ++k) {
        state.events[std::size_t(0x100 + k)] = (*p >> k & 1) ? 1 : 0;
    }
    ++p;
    take(state.dogName);
}

std::vector<std::uint8_t> Adventure1Minigames::capture(const adventure1::GameState& state) {
    std::vector<std::uint8_t> out;
    auto put = [&](const auto& list) {
        out.insert(out.end(), list.begin(), list.end());
    };
    put(state.gizmos);
    put(state.dogParts);
    put(state.pieceOwned);
    put(state.piecePlace);
    put(state.piece670);
    put(state.furnitureA);
    put(state.furnitureB);
    put(state.furniture6c4);
    put(state.furniture6e4);
    std::uint8_t done = 0;
    for (int k = 0; k < 3; ++k) {
        done = std::uint8_t(done | ((state.events[std::size_t(0x100 + k)] & 1) << k));
    }
    out.push_back(done);
    put(state.dogName);
    return out;
}

std::vector<std::uint8_t> Adventure1Minigames::progress() const {
    auto bytes = readAll(file_);
    return bytes.size() == kSize ? bytes : std::vector<std::uint8_t>{};
}

void Adventure1Minigames::store(const std::vector<std::uint8_t>& progress) {
    if (progress.size() == kSize) {
        writeAll(file_, progress);
    }
}

void Adventure1Minigames::reset() {
    std::error_code ec;
    std::filesystem::remove(file_, ec);
}

std::vector<MinigameStatus> Adventure1Minigames::status() const {
    adventure1::GameState state;
    apply(progress(), state);
    int parts = 0, pieces = 0;
    for (int i = 0; i < 20; ++i) {
        parts += state.dogParts[std::size_t(i >> 3)] >> (i & 7) & 1;
        pieces += state.piecePlace[std::size_t(i)] == i && !state.piece670[std::size_t(i)];
    }
    const int happiness = state.hotelHappiness(0) + state.hotelHappiness(1);
    std::vector<MinigameStatus> list(3);
    list[0] = {"Robot dog", true, state.dogComplete(), countOf(parts, 20, "parts in place"),
               parts / 20.0f};
    list[1] = {"Hotel", true, state.hotelComplete(),
               state.hotelComplete() ? "Both guests are happy"
                                     : countOf(happiness, 10, "steps of happiness"),
               state.hotelComplete() ? 1.0f : happiness / 10.0f};
    list[2] = {"Torn painting", true, state.paintingComplete(),
               countOf(pieces, 20, "pieces in place"),
               state.paintingComplete() ? 1.0f : pieces / 20.0f};
    return list;
}

Adventure4Minigames::Adventure4Minigames(std::filesystem::path file) : file_(std::move(file)) {}

std::vector<std::uint8_t> Adventure4Minigames::progress() const {
    return readAll(file_);
}

void Adventure4Minigames::store(const std::vector<std::uint8_t>& progress) {
    if (!progress.empty()) {
        writeAll(file_, progress);
    }
}

void Adventure4Minigames::reset() {
    std::error_code ec;
    std::filesystem::remove(file_, ec);
}

std::vector<MinigameStatus> Adventure4Minigames::status() const {
    adventure4::GameData data;
    adventure4::applyMinigameRecord(data, progress());
    const auto p = adventure4::minigameProgress(data);
    std::vector<MinigameStatus> list(3);
    list[0] = {"Model railway", true, p.courses == 10, countOf(p.courses, 10, "courses cleared"),
               p.courses / 10.0f};
    list[1] = {"Puppet show", true, p.plays == 3, countOf(p.plays, 3, "plays succeeded"),
               p.plays / 3.0f};
    list[2] = {"Fish", true, p.tanks == 10, countOf(p.tanks, 10, "tanks cleared"), p.tanks / 10.0f};
    return list;
}

} // namespace picarat::shell
