// The fourth adventure's save slots. See a4_saves.hpp.
#include "system/a4_saves.hpp"
#include "graphics/a4_assets.hpp"
#include "system/a4_definitions.hpp"
#include <algorithm>
#include <fstream>

namespace picarat::adventure4 {

namespace {
// Version 2 adds the puzzles' favourite / bit-8 marks, version 3 the
// puzzles passed by; older files (v0.52, v0.53..0.55) read without them.
constexpr char kMagic[8] = {'P', 'C', 'R', '4', 'S', 'A', 'V', '3'};
constexpr char kMagic2[8] = {'P', 'C', 'R', '4', 'S', 'A', 'V', '2'};
constexpr char kMagic1[8] = {'P', 'C', 'R', '4', 'S', 'A', 'V', '1'};

struct Writer {
    std::vector<std::uint8_t> out;

    void u32(std::uint64_t v, int bytes = 4) {
        for (int i = 0; i < bytes; ++i) {
            out.push_back(std::uint8_t(v >> (8 * i)));
        }
    }

    void text(const std::string& s) {
        u32(s.size());
        out.insert(out.end(), s.begin(), s.end());
    }

    void map(const std::map<std::string, int>& m) {
        u32(m.size());
        for (const auto& [k, v] : m) {
            text(k);
            u32(std::uint32_t(v));
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

    std::map<std::string, int> map() {
        std::map<std::string, int> m;
        for (std::size_t i = 0, n = count(8); i < n && ok; ++i) {
            std::string k = text();
            m[std::move(k)] = number();
        }
        return m;
    }
};

std::vector<std::uint8_t> encode(const SaveRecord& r) {
    Writer w;
    w.out.assign(std::begin(kMagic), std::end(kMagic));
    const SaveHeader& h = r.header;
    for (int v : {h.hours, h.minutes, h.found, h.solved, h.cleared}) {
        w.u32(std::uint32_t(v));
    }
    w.text(h.name);
    w.text(h.place);
    w.map(r.flags);
    w.map(r.variables);
    w.map(r.marks);
    w.u32(r.puzzles.size());
    for (const auto& [number, p] : r.puzzles) {
        w.u32(std::uint32_t(number));
        w.u32(std::uint32_t(p.state));
        w.u32(std::uint32_t(p.failures));
        w.u32(std::uint32_t(p.hints));
        w.u32(unsigned(p.favourite) | unsigned(p.flag100) << 1);
    }
    w.u32(r.names.size());
    for (const auto& [i, n] : r.names) {
        w.u32(std::uint32_t(i));
        w.text(n);
    }
    w.u32(r.playFrames, 8);
    w.u32(r.memo.size());
    for (std::uint16_t p : r.memo) {
        w.u32(p, 2);
    }
    w.u32(r.passedBy.size());
    for (int row : r.passedBy) {
        w.u32(std::uint32_t(row));
    }
    return w.out;
}

std::optional<SaveRecord> decode(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < sizeof kMagic) {
        return std::nullopt;
    }
    const bool v3 = std::equal(std::begin(kMagic), std::end(kMagic), bytes.begin());
    const bool v2 = v3 || std::equal(std::begin(kMagic2), std::end(kMagic2), bytes.begin());
    if (!v2 && !std::equal(std::begin(kMagic1), std::end(kMagic1), bytes.begin())) {
        return std::nullopt;
    }
    Reader r{bytes, sizeof kMagic};
    SaveRecord s;
    SaveHeader& h = s.header;
    h.hours = r.number();
    h.minutes = r.number();
    h.found = r.number();
    h.solved = r.number();
    h.cleared = r.number();
    h.name = r.text();
    h.place = r.text();
    s.flags = r.map();
    s.variables = r.map();
    s.marks = r.map();
    for (std::size_t i = 0, n = r.count(v2 ? 20 : 16); i < n && r.ok; ++i) {
        const int number = r.number();
        GameData::Progress p;
        p.state = r.number();
        p.failures = r.number();
        p.hints = r.number();
        if (v2) {
            const auto marks = r.u32();
            p.favourite = marks & 1;
            p.flag100 = marks & 2;
        }
        s.puzzles[number] = p;
    }
    for (std::size_t i = 0, n = r.count(8); i < n && r.ok; ++i) {
        const int index = r.number();
        s.names[index] = r.text();
    }
    s.playFrames = r.u32(8);
    for (std::size_t i = 0, n = r.count(2); i < n && r.ok; ++i) {
        s.memo.push_back(std::uint16_t(r.u32(2)));
    }
    if (v3) {
        for (std::size_t i = 0, n = r.count(4); i < n && r.ok; ++i) {
            s.passedBy.push_back(r.number());
        }
    }
    if (!r.ok) {
        return std::nullopt;
    }
    return s;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return in ? std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in), {}}
              : std::vector<std::uint8_t>{};
}

bool writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return bool(out);
}

// A name the save routine reads from overlay 7's string pool (020b8964..).
std::string ov7Name(Assets& assets, std::uint32_t pointer) {
    return assets.codeText(7, 0x020b6820, assets.codeWord(7, 0x020b6820, pointer));
}
} // namespace

SaveSlots::SaveSlots(std::filesystem::path folder) : folder_(std::move(folder)) {
    if (folder_.empty()) {
        return;
    }
    for (int i = 0; i < kCount; ++i) {
        slots_[std::size_t(i)] = decode(readFile(file(i)));
    }
    const auto table = readFile(folder_ / "table.sav");
    if (table.size() == 4) {
        flags_ = std::uint32_t(table[0] | table[1] << 8 | table[2] << 16 |
                               std::uint32_t(table[3]) << 24);
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

bool SaveSlots::erase(int index) {
    if (index < 0 || index >= kCount) {
        return false;
    }
    slots_[std::size_t(index)].reset();
    if (!folder_.empty()) {
        std::error_code ec;
        std::filesystem::remove(file(index), ec);
    }
    return true;
}

void SaveSlots::setFlag(int index, bool on) {
    if (index < 0 || index >= 32) {
        return;
    }
    const std::uint32_t before = flags_;
    flags_ = on ? flags_ | (1u << index) : flags_ & ~(1u << index);
    if (flags_ != before) {
        writeTable();
    }
}

void SaveSlots::writeTable() const {
    if (!folder_.empty()) {
        writeFile(folder_ / "table.sav", {std::uint8_t(flags_), std::uint8_t(flags_ >> 8),
                                          std::uint8_t(flags_ >> 16), std::uint8_t(flags_ >> 24)});
    }
}

SaveHeader saveHeader(Assets& assets, const GameData& data) {
    SaveHeader h;
    // Play time: 99:59 from 100 hours on.
    const std::uint64_t minutes = std::uint64_t(double(data.playFrames) / (59.8261 * 60.0));
    if (minutes >= 100 * 60) {
        h.hours = 99;
        h.minutes = 59;
    } else {
        h.hours = int(minutes / 60);
        h.minutes = int(minutes % 60);
    }
    // Story and challenge puzzles together (the counters named at
    // 020b8964..020b8970), the finished level (020b8974).
    const std::string found = ov7Name(assets, 0x020b8964),
                      foundChallenge = ov7Name(assets, 0x020b8968);
    const std::string solved = ov7Name(assets, 0x020b896c),
                      solvedChallenge = ov7Name(assets, 0x020b8970);
    h.found = data.variable(found) + data.variable(foundChallenge);
    h.solved = data.variable(solved) + data.variable(solvedChallenge);
    if (!data.flag(ov7Name(assets, 0x020b8974))) {
        h.cleared = 0;
    } else if (data.variable(solved) < 155) {
        h.cleared = 1;
    } else {
        h.cleared = data.variable(solvedChallenge) < 15 ? 2 : 3;
    }
    if (const auto it = data.names.find(0); it != data.names.end()) {
        h.name = it->second;
    }
    // The current place's title (its record's text; lt4_def 2151 field 2).
    const std::string place =
        assets.codeText(7, 0x020b6820, assets.codeWord(7, 0x020b6820, 0x020c08d0));
    if (const auto* row =
            definitions(assets).find(2151, 0, gss::Value::ofInt(data.variable(place)))) {
        h.place = Definitions::text(*row, 2);
    }
    return h;
}

SaveRecord saveRecord(Assets& assets, const GameData& data) {
    SaveRecord r;
    r.header = saveHeader(assets, data);
    r.flags = data.flags;
    r.variables = data.variables;
    r.marks = data.marks;
    r.puzzles = data.puzzles;
    r.names = data.names;
    r.playFrames = data.playFrames;
    r.memo = data.memo;
    r.passedBy = data.passedBy;
    return r;
}

void restore(GameData& data, const SaveRecord& r) {
    data.resetStory();
    data.flags = r.flags;
    data.variables = r.variables;
    data.normalize(); // a save made before the layout (0.54)
    data.marks = r.marks;
    data.puzzles = r.puzzles;
    data.names = r.names;
    data.playFrames = r.playFrames;
    data.memo = r.memo;
    data.passedBy = r.passedBy;
}

void showSlots(GameData& data, const SaveSlots& slots) {
    for (int i = 0; i < SaveSlots::kCount; ++i) {
        GameData::Slot& s = data.slots[std::size_t(i)];
        s = {};
        if (const SaveRecord* r = slots.slot(i)) {
            const SaveHeader& h = r->header;
            s.used = true;
            s.first = h.name;
            s.second = h.place;
            s.numbers = {h.hours, h.minutes, h.found, h.solved, h.cleared};
        }
    }
}

} // namespace picarat::adventure4
