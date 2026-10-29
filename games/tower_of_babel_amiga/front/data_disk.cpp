#include "tower_of_babel_amiga/front/data_disk.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace babel::front {
namespace {

const char* prefix(FileKind k) {
    switch (k) {
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

std::vector<std::string> DataDisk::names(FileKind k) const {
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
    std::error_code error;
    for (const auto& e : std::filesystem::directory_iterator(folder_, error)) {
        const auto f = e.path().filename().string();
        for (const auto k : {FileKind::group, FileKind::position, FileKind::scores})
            if (f.rfind(prefix(k), 0) == 0) ++n;
    }
    return n;
}

std::optional<std::vector<std::uint8_t>> DataDisk::read(FileKind k, const std::string& name) const {
    std::ifstream in(file(k, name), std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

bool DataDisk::write(FileKind k, const std::string& name, const std::vector<std::uint8_t>& bytes) const {
    std::error_code error;
    std::filesystem::create_directories(folder_, error);
    std::ofstream out(file(k, name), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return bool(out);
}

} // namespace babel::front
