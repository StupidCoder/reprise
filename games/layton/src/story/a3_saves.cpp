// The third adventure's save slots. See a3_saves.hpp.
#include "story/a3_saves.hpp"
#include <fstream>

namespace picarat::adventure3 {

namespace {
constexpr char kMagic[8] = {'P', 'C', 'R', '3', 'S', 'A', 'V', '1'};

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

    void blob(const std::vector<std::uint8_t>& b) {
        u32(b.size());
        out.insert(out.end(), b.begin(), b.end());
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

    std::vector<std::uint8_t> blob() {
        const std::size_t n = u32();
        if (!ok || at + n > in.size()) {
            ok = false;
            return {};
        }
        std::vector<std::uint8_t> b(in.begin() + std::ptrdiff_t(at),
                                    in.begin() + std::ptrdiff_t(at + n));
        at += n;
        return b;
    }

    std::string text() {
        const auto b = blob();
        return {b.begin(), b.end()};
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
    w.blob(r.state);
    w.u32(r.puzzles.size());
    for (const auto& [number, p] : r.puzzles) {
        w.u32(std::uint32_t(number));
        w.u32(std::uint32_t(p.state));
        w.u32(std::uint32_t(p.failures));
        w.u32(std::uint32_t(p.hints));
        w.u32(unsigned(p.available) | unsigned(p.marked) << 1);
    }
    w.text(r.names[0]);
    w.text(r.names[1]);
    w.u32(r.playFrames, 8);
    return w.out;
}

std::optional<SaveRecord> decode(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < sizeof kMagic ||
        !std::equal(std::begin(kMagic), std::end(kMagic), bytes.begin())) {
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
    s.state = r.blob();
    const std::size_t count = r.u32();
    for (std::size_t i = 0; i < count && r.ok; ++i) {
        const int number = r.number();
        PuzzleRecord p;
        p.state = r.number();
        p.failures = r.number();
        p.hints = r.number();
        const auto bits = r.u32();
        p.available = bits & 1;
        p.marked = bits & 2;
        s.puzzles[number] = p;
    }
    s.names[0] = r.text();
    s.names[1] = r.text();
    s.playFrames = r.u32(8);
    if (!r.ok) {
        return std::nullopt;
    }
    return s;
}
} // namespace

SaveSlots::SaveSlots(std::filesystem::path folder) : folder_(std::move(folder)) {
    if (folder_.empty()) {
        return;
    }
    for (int i = 0; i <= kCheckpoint; ++i) {
        std::ifstream in(file(i), std::ios::binary);
        if (!in) {
            continue;
        }
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
        slots_[size_t(i)] = decode(bytes);
    }
}

std::filesystem::path SaveSlots::file(int index) const {
    return folder_ / (index == kCheckpoint ? std::string("checkpoint.sav")
                                           : "slot" + std::to_string(index) + ".sav");
}

const SaveRecord* SaveSlots::slot(int index) const {
    if (index < 0 || index > kCheckpoint || !slots_[size_t(index)]) {
        return nullptr;
    }
    return &*slots_[size_t(index)];
}

bool SaveSlots::write(int index, SaveRecord record) {
    if (index < 0 || index > kCheckpoint) {
        return false;
    }
    if (!folder_.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(folder_, ec);
        const auto bytes = encode(record);
        const auto temporary = file(index).string() + ".new";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            if (!out) {
                return false;
            }
        }
        std::filesystem::rename(temporary, file(index), ec);
        if (ec) {
            return false;
        }
    }
    slots_[size_t(index)] = std::move(record);
    return true;
}

bool SaveSlots::erase(int index) {
    if (index < 0 || index > kCheckpoint) {
        return false;
    }
    if (!folder_.empty()) {
        std::error_code ec;
        std::filesystem::remove(file(index), ec);
        if (ec) {
            return false;
        }
    }
    slots_[size_t(index)].reset();
    return true;
}

} // namespace picarat::adventure3
