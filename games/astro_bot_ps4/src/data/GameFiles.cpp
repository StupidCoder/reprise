#include "data/GameFiles.hpp"

#include <algorithm>

namespace beacon::data {

bool GameFiles::open(const std::filesystem::path& root, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(root / "data", ec) || !fs::exists(root / "eboot.bin", ec)) {
        if (error) {
            *error = "not an extracted game folder (expected eboot.bin and data/): " + root.string();
        }
        return false;
    }
    root_ = root;
    byKey_.clear();
    for (const auto& entry : fs::recursive_directory_iterator(root / "data", ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const fs::path& p = entry.path();
        std::string key = p.stem().string() + p.extension().string();
        // Keep the first file per stem in path order, so lookups are deterministic.
        auto it = byKey_.find(key);
        if (it == byKey_.end() || p < it->second) {
            byKey_[key] = p;
        }
    }
    return true;
}

std::optional<std::filesystem::path> GameFiles::find(const std::string& stem, const std::string& extension) const {
    auto it = byKey_.find(stem + extension);
    if (it == byKey_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::filesystem::path> GameFiles::list(const std::string& extension) const {
    std::vector<std::filesystem::path> out;
    for (const auto& [key, path] : byKey_) {
        if (path.extension() == extension) {
            out.push_back(path);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}   // namespace data
