// The fifth adventure's save slots. See a5_saves.hpp.
#include "system/a5_saves.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>

namespace picarat::adventure5 {

namespace {
constexpr char kMagic[8] = {'P', 'C', 'R', '5', 'S', 'A', 'V', '1'};

struct Writer {
    std::vector<std::uint8_t> out;

    void u32(std::uint64_t v, int bytes = 4) {
        for (int i = 0; i < bytes; ++i) {
            out.push_back(std::uint8_t(v >> (8 * i)));
        }
    }
    void number(int v) { u32(std::uint32_t(v)); }
    void real(float f) {
        std::uint32_t v = 0;
        std::memcpy(&v, &f, 4);
        u32(v);
    }
    void text(const std::string& s) {
        u32(s.size());
        out.insert(out.end(), s.begin(), s.end());
    }
    void bytes(const std::vector<std::uint8_t>& b) {
        u32(b.size());
        out.insert(out.end(), b.begin(), b.end());
    }
    void numbers(const std::map<int, int>& m) {
        u32(m.size());
        for (const auto& [k, v] : m) {
            number(k);
            number(v);
        }
    }
};

struct Reader {
    const std::vector<std::uint8_t>& in;
    std::size_t at = 0;
    bool ok = true;

    std::uint64_t u32(int bytes = 4) {
        if (at + std::size_t(bytes) > in.size()) {
            ok = false;
            return 0;
        }
        std::uint64_t v = 0;
        for (int i = 0; i < bytes; ++i) {
            v |= std::uint64_t(in[at++]) << (8 * i);
        }
        return v;
    }
    int number() { return int(std::int32_t(u32())); }
    float real() {
        const auto v = std::uint32_t(u32());
        float f = 0;
        std::memcpy(&f, &v, 4);
        return f;
    }
    // A count that cannot exceed what is left (a damaged file).
    std::size_t count(std::size_t each) {
        const std::size_t n = u32();
        if (!ok || n > (in.size() - at) / each) {
            ok = false;
            return 0;
        }
        return n;
    }
    std::string text() {
        const std::size_t n = count(1);
        std::string s(in.begin() + std::ptrdiff_t(at), in.begin() + std::ptrdiff_t(at + n));
        at += n;
        return s;
    }
    std::vector<std::uint8_t> bytes() {
        const std::size_t n = count(1);
        std::vector<std::uint8_t> b(in.begin() + std::ptrdiff_t(at), in.begin() + std::ptrdiff_t(at + n));
        at += n;
        return b;
    }
    std::map<int, int> numbers() {
        std::map<int, int> m;
        for (std::size_t i = 0, n = count(8); i < n && ok; ++i) {
            const int k = number();
            m[k] = number();
        }
        return m;
    }
};

void writeStand(Writer& w, const Game::RuinRecord::Stand& s) {
    w.number(s.row);
    w.number(s.column);
    w.real(s.angle);
}

Game::RuinRecord::Stand readStand(Reader& r) {
    Game::RuinRecord::Stand s;
    s.row = r.number();
    s.column = r.number();
    s.angle = r.real();
    return s;
}

void writeRuins(Writer& w, const Game::RuinRecord& g) {
    w.number(g.floor);
    writeStand(w, g.layton);
    writeStand(w, g.randall);
    w.number(g.retryFloor);
    writeStand(w, g.retryLayton);
    writeStand(w, g.retryRandall);
    w.u32(g.retryFlags);
    for (const auto v : g.notedNames) {
        w.u32(v);
    }
    w.u32(g.flags);
    for (const auto v : g.roomsSeen) {
        w.u32(v);
    }
    for (const auto v : g.roomsMapped) {
        w.u32(v);
    }
    for (std::size_t f = 0; f < 8; ++f) {
        w.numbers(g.eventNotes[f]);
        w.u32(g.raisedHeights[f].size());
        for (const auto& [cell, h] : g.raisedHeights[f]) {
            w.number(cell.first);
            w.number(cell.second);
            w.number(h);
        }
        w.numbers(g.switchNotes[f]);
        w.numbers(g.plateNotes[f]);
        w.u32(g.blockNotes[f].size());
        for (const auto& [n, b] : g.blockNotes[f]) {
            w.number(n);
            w.number(b.sunk ? 1 : 0);
            w.number(b.row);
            w.number(b.column);
        }
    }
}

// The rabbit's record (a trailing section, tagged).
constexpr std::uint32_t kRabbitTag = 0x31544252; // "RBT1"

void writeRabbit(Writer& w, const Game::RabbitRecord& g) {
    w.u32(kRabbitTag);
    w.u32(g.learned);
    w.u32(g.mastered);
    w.u32(g.fresh);
    w.bytes({g.variants.begin(), g.variants.end()});
    w.bytes({g.flags.begin(), g.flags.end()});
}

Game::RabbitRecord readRabbit(Reader& r) {
    Game::RabbitRecord g;
    if (r.at >= r.in.size()) {
        return g;
    }
    if (r.u32() != kRabbitTag) {
        r.ok = false;
        return g;
    }
    g.learned = std::uint32_t(r.u32());
    g.mastered = std::uint32_t(r.u32());
    g.fresh = std::uint32_t(r.u32());
    const auto variants = r.bytes(), flags = r.bytes();
    std::copy_n(variants.begin(), std::min(variants.size(), g.variants.size()), g.variants.begin());
    std::copy_n(flags.begin(), std::min(flags.size(), g.flags.size()), g.flags.begin());
    return g;
}

Game::RuinRecord readRuins(Reader& r) {
    Game::RuinRecord g;
    g.floor = r.number();
    g.layton = readStand(r);
    g.randall = readStand(r);
    g.retryFloor = r.number();
    g.retryLayton = readStand(r);
    g.retryRandall = readStand(r);
    g.retryFlags = std::uint32_t(r.u32());
    for (auto& v : g.notedNames) {
        v = std::uint32_t(r.u32());
    }
    g.flags = std::uint32_t(r.u32());
    for (auto& v : g.roomsSeen) {
        v = std::uint32_t(r.u32());
    }
    for (auto& v : g.roomsMapped) {
        v = std::uint32_t(r.u32());
    }
    for (std::size_t f = 0; f < 8 && r.ok; ++f) {
        g.eventNotes[f] = r.numbers();
        for (std::size_t i = 0, n = r.count(12); i < n && r.ok; ++i) {
            const int row = r.number(), column = r.number();
            g.raisedHeights[f][{row, column}] = r.number();
        }
        g.switchNotes[f] = r.numbers();
        g.plateNotes[f] = r.numbers();
        for (std::size_t i = 0, n = r.count(16); i < n && r.ok; ++i) {
            const int number = r.number();
            Game::RuinRecord::BlockNote b;
            b.sunk = r.number() != 0;
            b.row = r.number();
            b.column = r.number();
            g.blockNotes[f][number] = b;
        }
    }
    return g;
}

std::vector<std::uint8_t> encode(const SaveRecord& s) {
    Writer w;
    w.out.assign(std::begin(kMagic), std::end(kMagic));
    const SaveHeader& h = s.header;
    w.text(h.name);
    for (int v : {h.cleared, h.seconds, h.picarats, h.phase, h.place, h.found, h.solved, h.coinsFound,
                  h.coinsHeld, h.past}) {
        w.number(v);
    }
    w.u32(s.flags.size());
    for (int f : s.flags) {
        w.number(f);
    }
    w.bytes(s.variables);
    w.u32(s.progress.size());
    for (const auto& [id, p] : s.progress) {
        w.number(id);
        w.number(p.state);
        w.number(p.reductions);
        w.number(p.hints);
        w.number(p.passedBy ? 1 : 0);
    }
    w.u32(s.passedBy.size());
    for (int p : s.passedBy) {
        w.number(p);
    }
    for (const auto& n : s.names) {
        w.text(n);
    }
    writeRuins(w, s.ruins);
    writeRabbit(w, s.rabbit);
    return w.out;
}

std::optional<SaveRecord> decode(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < sizeof kMagic || !std::equal(std::begin(kMagic), std::end(kMagic), bytes.begin())) {
        return std::nullopt;
    }
    Reader r{bytes, sizeof kMagic};
    SaveRecord s;
    SaveHeader& h = s.header;
    h.name = r.text();
    for (int* v : {&h.cleared, &h.seconds, &h.picarats, &h.phase, &h.place, &h.found, &h.solved, &h.coinsFound,
                   &h.coinsHeld, &h.past}) {
        *v = r.number();
    }
    for (std::size_t i = 0, n = r.count(4); i < n && r.ok; ++i) {
        s.flags.push_back(r.number());
    }
    s.variables = r.bytes();
    for (std::size_t i = 0, n = r.count(20); i < n && r.ok; ++i) {
        const int id = r.number();
        Game::PuzzleProgress p;
        p.state = r.number();
        p.reductions = r.number();
        p.hints = r.number();
        p.passedBy = r.number() != 0;
        s.progress[id] = p;
    }
    for (std::size_t i = 0, n = r.count(4); i < n && r.ok; ++i) {
        s.passedBy.push_back(r.number());
    }
    for (auto& n : s.names) {
        n = r.text();
    }
    s.ruins = readRuins(r);
    s.rabbit = readRabbit(r);
    if (!r.ok) {
        return std::nullopt;
    }
    return s;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return in ? std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in), {}} : std::vector<std::uint8_t>{};
}

bool writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return bool(out);
}

int flagNamed(const Game& game, const char* name) {
    const int id = game.flagId(name);
    return id >= 0 && game.flag(id) ? 1 : 0;
}
} // namespace

SaveSlots::SaveSlots(std::filesystem::path folder) : folder_(std::move(folder)) {
    if (folder_.empty()) {
        return;
    }
    for (int i = 0; i < kCount; ++i) {
        slots_[std::size_t(i)] = decode(readFile(file(i)));
    }
}

std::filesystem::path SaveSlots::file(int index) const {
    return folder_ / ("slot" + std::to_string(index) + ".sav");
}

const SaveRecord* SaveSlots::slot(int index) const {
    if (index < 0 || index >= kCount || !slots_[std::size_t(index)]) {
        return nullptr;
    }
    return &*slots_[std::size_t(index)];
}

bool SaveSlots::write(int index, SaveRecord record) {
    if (index < 0 || index >= kCount) {
        return false;
    }
    if (!folder_.empty() && !writeFile(file(index), encode(record))) {
        return false;
    }
    slots_[std::size_t(index)] = std::move(record);
    return true;
}

SaveHeader saveHeader(const Game& game) {
    // 0x2acb98 (names from its string pool, 0x2acf28..).
    SaveHeader h;
    h.name = game.names[0];
    if (flagNamed(game, "\x83\x51\x81\x5b\x83\x80\x83\x4e\x83\x8a\x83\x41\x81\x5b")) { // ゲームクリアー
        // (0x2cdd08: 135 story puzzles unless the DX version's 150.)
        const int dx = flagNamed(game, "\x82\x63\x82\x77\x94\xc5"); // ＤＸ版
        if (game.variable("\x82\xc6\x82\xaf\x82\xbd\x83\x69\x83\x5d\x90\x94") < (dx ? 150 : 135)) { // とけたナゾ数
            h.cleared = 1;
        } else {
            h.cleared = game.variable("\x92\xa7\x90\xed\x8f\xf3\x82\xc6\x82\xaf\x82\xbd\x83\x69\x83\x5d\x90\x94") < 15
                            ? 2 // 挑戦状とけたナゾ数
                            : 3;
        }
    }
    h.seconds = int(std::max<std::int64_t>(0, std::int64_t(game.frame) - game.playStart) / 60);
    h.picarats = game.variable("\x91\x8d\x8e\xe6\x93\xbe\x83\x73\x83\x4a\x83\x89\x83\x62\x83\x67");   // 総取得ピカラット
    h.phase = game.variable("\x8c\xbb\x8d\xdd\x82\xcc\x83\x74\x83\x46\x81\x5b\x83\x59");                // 現在のフェーズ
    h.place = game.variable("\x8c\xbb\x8d\xdd\x82\xcc\x83\x76\x83\x8c\x83\x43\x83\x58");                // 現在のプレイス
    h.found = game.variable("\x82\xdd\x82\xc2\x82\xaf\x82\xbd\x83\x69\x83\x5d\x91\x8d\x90\x94");          // みつけたナゾ総数
    h.solved = game.variable("\x82\xc6\x82\xaf\x82\xbd\x83\x69\x83\x5d\x91\x8d\x90\x94");                 // とけたナゾ総数
    h.coinsFound = game.variable("\x82\xd0\x82\xe7\x82\xdf\x82\xab\x83\x52\x83\x43\x83\x93\x8e\xe6\x93\xbe\x90\x94"); // ひらめきコイン取得数
    h.coinsHeld = game.variable("\x82\xd0\x82\xe7\x82\xdf\x82\xab\x83\x52\x83\x43\x83\x93\x8f\x8a\x8e\x9d\x90\x94");  // ひらめきコイン所持数
    h.past = flagNamed(game, "\x89\xdf\x8b\x8e");                                                             // 過去
    return h;
}

SaveRecord saveRecord(const Game& game) {
    SaveRecord s;
    s.header = saveHeader(game);
    s.flags.assign(game.flagSet().begin(), game.flagSet().end());
    s.variables = game.variableData();
    s.progress = game.progress;
    s.passedBy = game.passedBy;
    s.names = game.names;
    s.ruins = game.ruins;
    s.rabbit = game.rabbit;
    return s;
}

void restore(Game& game, const SaveRecord& s) {
    game.setFlagSet({s.flags.begin(), s.flags.end()});
    game.setVariableData(s.variables);
    game.progress = s.progress;
    game.passedBy = s.passedBy;
    game.names = s.names;
    game.ruins = s.ruins;
    game.rabbit = s.rabbit;
    // The play time goes on from the saved seconds.
    game.playStart = std::int64_t(game.frame) - std::int64_t(s.header.seconds) * 60;
}

SaveSlots& Game::saveSlots() {
    if (!saves) {
        saves = std::make_shared<SaveSlots>();
    }
    return *saves;
}

int headerField(const SaveHeader& h, int field) {
    switch (field) {
    case 1:
        return 1;
    case 2:
        return h.cleared;
    case 3:
        return h.seconds;
    case 4:
        return h.picarats;
    case 5:
        return h.phase;
    case 6:
        return h.place;
    case 7:
        return h.found;
    case 8:
        return h.solved;
    case 9:
        return h.coinsFound;
    case 10:
        return h.coinsHeld;
    case 11:
        return h.past;
    default:
        return 0;
    }
}

bool writeCheckpoint(const std::filesystem::path& file, const SaveRecord& record) {
    return writeFile(file, encode(record));
}

std::optional<SaveRecord> readCheckpoint(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        return std::nullopt;
    }
    return decode(readFile(file));
}

} // namespace picarat::adventure5
