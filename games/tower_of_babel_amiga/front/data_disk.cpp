#include "tower_of_babel_amiga/front/data_disk.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <system_error>

namespace babel::front {
namespace {

const char* prefix(FileKind k) {
    switch (k) {
    case FileKind::tower: return "tower.";
    case FileKind::group: return "group.";
    case FileKind::position: return "position.";
    case FileKind::scores: return "scores.";
    }
    return "";
}

} // namespace

std::filesystem::path DataDisk::file(FileKind k, const std::string& name) const {
    return folder_ / (prefix(k) + name);
}

std::vector<std::uint8_t> DataDisk::answer(std::vector<std::uint8_t> now) const {
    if (!replay_) {
        answers_.push_back(now);
        return now;
    }
    if (next_ >= answers_.size()) throw std::runtime_error("The save data is invalid or incompatible.");
    return answers_[next_++];
}

std::vector<std::string> DataDisk::names(FileKind k) const {
    std::vector<std::uint8_t> bytes;
    if (!replay_)
        for (const auto& n : list(k)) {
            bytes.insert(bytes.end(), n.begin(), n.end());
            bytes.push_back(0);
        }
    bytes = answer(std::move(bytes));
    std::vector<std::string> result;
    std::string n;
    for (const auto b : bytes) {
        if (b != 0) {
            n += char(b);
            continue;
        }
        result.push_back(std::exchange(n, {}));
    }
    return result;
}

std::vector<std::string> DataDisk::list(FileKind k) const {
    std::vector<std::string> result;
    std::error_code error;
    for (const auto& e : std::filesystem::directory_iterator(folder_, error)) {
        const auto n = e.path().filename().string();
        const std::string p = prefix(k);
        if (n.rfind(p, 0) == 0 && n.size() > p.size()) result.push_back(n.substr(p.size()));
    }
    // $12AFA compares the names as 8 bytes, zero-filled.
    std::sort(result.begin(), result.end(), [](const std::string& a, const std::string& b) {
        std::string x = a, y = b;
        x.resize(name_length, '\0');
        y.resize(name_length, '\0');
        return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end(),
                                            [](char c, char d) { return std::uint8_t(c) < std::uint8_t(d); });
    });
    return result;
}

int DataDisk::files() const {
    int n = 0;
    if (replay_) return int(answer({}).size());
    std::error_code error;
    for (const auto& e : std::filesystem::directory_iterator(folder_, error)) {
        const auto f = e.path().filename().string();
        for (const auto k : {FileKind::tower, FileKind::group, FileKind::position, FileKind::scores})
            if (f.rfind(prefix(k), 0) == 0) ++n;
    }
    answer(std::vector<std::uint8_t>(std::size_t(n))); // (as many bytes as files)
    return n;
}

std::optional<std::vector<std::uint8_t>> DataDisk::read(FileKind k, const std::string& name) const {
    // The answer: a 0 (no file), or a 1 and the file.
    std::vector<std::uint8_t> bytes;
    if (!replay_) {
        const auto f = load(k, name);
        bytes.push_back(f ? 1 : 0);
        if (f) bytes.insert(bytes.end(), f->begin(), f->end());
    }
    bytes = answer(std::move(bytes));
    if (bytes.empty() || bytes[0] == 0) return std::nullopt;
    return std::vector<std::uint8_t>(bytes.begin() + 1, bytes.end());
}

std::optional<std::vector<std::uint8_t>> DataDisk::load(FileKind k, const std::string& name) const {
    std::ifstream in(file(k, name), std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

bool DataDisk::write(FileKind k, const std::string& name, const std::vector<std::uint8_t>& bytes) const {
    if (replay_) return !answer({}).empty();
    std::error_code error;
    std::filesystem::create_directories(folder_, error);
    std::ofstream out(file(k, name), std::ios::binary | std::ios::trunc);
    if (out) out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    const bool ok = bool(out);
    answer(std::vector<std::uint8_t>(ok ? 1 : 0)); // (one byte: written)
    return ok;
}

bool DataDisk::remove(FileKind k, const std::string& name) const {
    if (replay_) return !answer({}).empty();
    std::error_code error;
    const bool ok = std::filesystem::remove(file(k, name), error);
    answer(std::vector<std::uint8_t>(ok ? 1 : 0));
    return ok;
}

bool DataDisk::erase() const {
    if (replay_) return !answer({}).empty();
    bool all = true;
    std::error_code error;
    for (const auto k : {FileKind::tower, FileKind::group, FileKind::position, FileKind::scores})
        for (const auto& n : list(k)) all = std::filesystem::remove(file(k, n), error) && all;
    std::filesystem::create_directories(folder_, error);
    answer(std::vector<std::uint8_t>(all ? 1 : 0));
    return all;
}

} // namespace babel::front
